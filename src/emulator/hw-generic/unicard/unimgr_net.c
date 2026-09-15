/*
 * File:   unimgr_net.c
 *
 * MZPico NET extension of the Unicard repository device. See unimgr_net.h
 * and BomberNet/docs/net-protocol.md.
 *
 * Threads: the emulation thread runs every command and drains the inbound
 * line queue; a transport (socket reader thread, or the browser page)
 * pushes lines. The queues are single-producer / single-consumer rings on
 * atomics, so no mutex is needed and the page side never blocks.
 *
 * ---------------------------------------------------------------------------
 * GPLv3 - see the project licence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "unimgr_net.h"

#ifndef __EMSCRIPTEN__
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#else
#include <emscripten.h>
#endif

int g_unicard_mzpico_mode = 0;

/* vendor command codes (mirrors the Z80 client and the firmware plan) */
#define cmdX_INFO    0x95
#define cmdN_STATUS  0xa0
#define cmdN_CREATE  0xa1
#define cmdN_JOIN    0xa2
#define cmdN_LEAVE   0xa3
#define cmdN_READY   0xa4
#define cmdN_SEND    0xa5
#define cmdN_POLL    0xa6
#define cmdN_HASH    0xa7
#define cmdN_MSG     0xa8
#define cmdN_RECV    0xa9

#define NETST_NOLINK 0
#define NETST_READY  1
#define NETST_INROOM 2
#define NETST_RUNNING 3
#define NETST_DESYNC 4
#define NETST_DROPPED 5
#define NETST_SPECTATOR 6

#define E_BUILD 6
#define E_ROOM  7
#define E_NOROOM 8
#define E_NOLINK 9
#define E_PARAM 10
#define E_FULL  11
#define E_NOTIMPL 1

#define FRAMES 256
#define MAX_SLOTS 4
#define MAX_BYTES 4
#define SETTINGS_LEN 16
#define MSG_LEN 32
#define LINE_LEN 320
#define QUEUE_LEN 64

/* ---------------- SPSC line queues ---------------- */

typedef struct {
    char lines[QUEUE_LEN][LINE_LEN];
    volatile unsigned head, tail;       /* head: producer writes, tail: consumer reads */
} st_LINEQ;

static st_LINEQ g_inq, g_outq;

static int lineq_push ( st_LINEQ *q, const char *line ) {
    unsigned h = __atomic_load_n ( &q->head, __ATOMIC_ACQUIRE );
    unsigned t = __atomic_load_n ( &q->tail, __ATOMIC_ACQUIRE );
    if ( h - t >= QUEUE_LEN ) return -1;
    strncpy ( q->lines[h % QUEUE_LEN], line, LINE_LEN - 1 );
    q->lines[h % QUEUE_LEN][LINE_LEN - 1] = 0;
    __atomic_store_n ( &q->head, h + 1, __ATOMIC_RELEASE );
    return 0;
}

static const char *lineq_pop ( st_LINEQ *q ) {
    unsigned h = __atomic_load_n ( &q->head, __ATOMIC_ACQUIRE );
    unsigned t = __atomic_load_n ( &q->tail, __ATOMIC_ACQUIRE );
    if ( h == t ) return NULL;
    const char *l = q->lines[t % QUEUE_LEN];
    __atomic_store_n ( &q->tail, t + 1, __ATOMIC_RELEASE );
    return l;
}

/* ---------------- tiny JSON helpers (flat objects only) ---------------- */

static const char *json_find ( const char *s, const char *key ) {
    char pat[40];
    snprintf ( pat, sizeof ( pat ), "\"%s\"", key );
    const char *p = strstr ( s, pat );
    if ( !p ) return NULL;
    p += strlen ( pat );
    while ( *p == ' ' || *p == ':' ) p++;
    return p;
}

static long json_int ( const char *s, const char *key, long def ) {
    const char *p = json_find ( s, key );
    if ( !p ) return def;
    return strtol ( p, NULL, 10 );
}

static int json_str ( const char *s, const char *key, char *out, int n ) {
    const char *p = json_find ( s, key );
    int i = 0;
    if ( !p || *p != '"' ) { out[0] = 0; return 0; }
    p++;
    while ( *p && *p != '"' && i < n - 1 ) out[i++] = *p++;
    out[i] = 0;
    return i;
}

static int json_bool ( const char *s, const char *key ) {
    const char *p = json_find ( s, key );
    return p && !strncmp ( p, "true", 4 );
}

static int hexval ( char c ) {
    if ( c >= '0' && c <= '9' ) return c - '0';
    if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
    return 0;
}

static int hex_decode ( const char *hex, uint8_t *out, int max ) {
    int n = 0;
    while ( hex[0] && hex[1] && n < max ) {
        out[n++] = ( uint8_t ) ( ( hexval ( hex[0] ) << 4 ) | hexval ( hex[1] ) );
        hex += 2;
    }
    return n;
}

static void hex_encode ( const uint8_t *in, int n, char *out ) {
    static const char d[] = "0123456789abcdef";
    while ( n-- ) { *out++ = d[*in >> 4]; *out++ = d[*in & 15]; in++; }
    *out = 0;
}

/* ---------------- device state ---------------- */

typedef struct {
    int linked;
    uint8_t state, slot, members, ready_mask, rtt, last_error;
    uint8_t slots, nbytes;
    uint8_t full_mask;                  /* slots taking part (from start), a frame needs them all */
    uint8_t settings[SETTINGS_LEN];
    uint8_t settings_len;
    uint16_t seed, start_frame;
    int started;                        /* start received */
    char code[8];
    /* frame ring: window [base, base+FRAMES) */
    uint32_t base;                      /* lowest frame kept = next incomplete frame */
    uint8_t have[FRAMES];               /* slot mask present */
    uint8_t data[FRAMES][MAX_SLOTS][MAX_BYTES];
    /* messages */
    struct { uint8_t from, len, data[MSG_LEN]; } msgs[8];
    int msg_head, msg_tail;
    /* async command */
    uint8_t pending_cmd;
    int pending_done, pending_err;
    uint8_t pending_out[32];
    int pending_len;
} st_NET;

static st_NET g;

static void frames_clear ( void ) {
    g.base = 0;
    memset ( g.have, 0, sizeof ( g.have ) );
    memset ( g.data, 0, sizeof ( g.data ) );
}

void unimgr_net_reset ( void ) {
    int linked = g.linked;
    memset ( &g, 0, sizeof ( g ) );
    g.linked = linked;
    g.state = linked ? NETST_READY : NETST_NOLINK;
    g.slots = MAX_SLOTS;
    g.nbytes = 1;
}

void unimgr_net_set_link ( int linked ) {
    g.linked = linked;
    if ( !linked ) { g.state = NETST_NOLINK; g.started = 0; }
    else if ( g.state == NETST_NOLINK ) g.state = NETST_READY;
}

/* ---------------- transport ---------------- */

#ifndef __EMSCRIPTEN__
static int g_sock = -1;
static pthread_t g_reader;

static void *reader_thread ( void *arg ) {
    char buf[LINE_LEN];
    int n = 0;
    ( void ) arg;
    for ( ;; ) {
        char c;
        ssize_t r = recv ( g_sock, &c, 1, 0 );
        if ( r <= 0 ) break;
        if ( c == '\n' ) {
            buf[n] = 0;
            if ( n ) lineq_push ( &g_inq, buf );
            n = 0;
        } else if ( n < LINE_LEN - 1 ) {
            buf[n++] = c;
        }
    }
    lineq_push ( &g_inq, "{\"op\":\"link\",\"linked\":0}" );
    return NULL;
}

static int transport_connect ( void ) {
    char host[100], *colon;
    int port;
    struct addrinfo hints, *res;
    char portstr[8];
    if ( g_sock >= 0 ) return 0;
    strncpy ( host, unicard_get_net_relay ( ), sizeof ( host ) - 1 );
    host[sizeof ( host ) - 1] = 0;
    colon = strrchr ( host, ':' );
    port = colon ? atoi ( colon + 1 ) : 8766;
    if ( colon ) *colon = 0;
    memset ( &hints, 0, sizeof ( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf ( portstr, sizeof ( portstr ), "%d", port );
    if ( getaddrinfo ( host, portstr, &hints, &res ) != 0 ) return -1;
    int s = socket ( res->ai_family, res->ai_socktype, res->ai_protocol );
    if ( s < 0 || connect ( s, res->ai_addr, res->ai_addrlen ) < 0 ) {
        if ( s >= 0 ) close ( s );
        freeaddrinfo ( res );
        return -1;
    }
    freeaddrinfo ( res );
    g_sock = s;
    pthread_create ( &g_reader, NULL, reader_thread, NULL );
    unimgr_net_set_link ( 1 );
    fprintf ( stderr, "UNICARD NET: connected to relay %s\n", unicard_get_net_relay ( ) );
    return 0;
}

static int transport_send ( const char *line ) {
    if ( g_sock < 0 && transport_connect ( ) != 0 ) return -1;
    size_t n = strlen ( line );
    if ( send ( g_sock, line, n, 0 ) != ( ssize_t ) n ) return -1;
    return send ( g_sock, "\n", 1, 0 ) == 1 ? 0 : -1;
}
#else
static int transport_connect ( void ) { return g.linked ? 0 : -1; }
static int transport_send ( const char *line ) {
    if ( !g.linked ) return -1;
    return lineq_push ( &g_outq, line );
}
#endif

void unimgr_net_push_line ( const char *line ) {
    lineq_push ( &g_inq, line );
}

const char *unimgr_net_pop_out ( void ) {
    return lineq_pop ( &g_outq );
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void mz_wasm_net_push ( const char *line ) { unimgr_net_push_line ( line ); }
EMSCRIPTEN_KEEPALIVE const char *mz_wasm_net_pop ( void ) { return unimgr_net_pop_out ( ); }
EMSCRIPTEN_KEEPALIVE void mz_wasm_net_link ( int linked ) { unimgr_net_set_link ( linked ); }
EMSCRIPTEN_KEEPALIVE int mz_wasm_net_enabled ( void ) { return g_unicard_mzpico_mode; }
#endif

/* ---------------- inbound processing ---------------- */

static void store_input ( uint32_t frame, int slot, const char *hex ) {
    uint32_t idx;
    if ( slot < 0 || slot >= MAX_SLOTS ) return;
    if ( frame < g.base || frame >= g.base + FRAMES ) return;
    idx = frame % FRAMES;
    hex_decode ( hex, g.data[idx][slot], MAX_BYTES );
    g.have[idx] |= ( uint8_t ) ( 1 << slot );
}

static void handle_line ( const char *l ) {
    char op[16];
    json_str ( l, "op", op, sizeof ( op ) );
    if ( !strcmp ( op, "room" ) ) {
        int slot = ( int ) json_int ( l, "slot", -1 );
        char hex[2 * SETTINGS_LEN + 1];
        json_str ( l, "code", g.code, sizeof ( g.code ) );
        g.slots = ( uint8_t ) json_int ( l, "slots", g.slots );
        g.nbytes = ( uint8_t ) json_int ( l, "bytes", g.nbytes );
        if ( json_str ( l, "settings", hex, sizeof ( hex ) ) )
            g.settings_len = ( uint8_t ) hex_decode ( hex, g.settings, SETTINGS_LEN );
        if ( json_bool ( l, "spectator" ) || slot < 0 ) { g.slot = 0xff; g.state = NETST_SPECTATOR; }
        else { g.slot = ( uint8_t ) slot; g.state = NETST_INROOM; }
        g.members = 1; g.ready_mask = 0; g.started = 0; g.full_mask = 0;
        frames_clear ( );
        if ( g.pending_cmd == cmdN_CREATE ) {
            memcpy ( g.pending_out, g.code, 4 );
            g.pending_out[4] = 0x0d;
            g.pending_out[5] = g.slot;
            g.pending_len = 6;
            g.pending_done = 1;
        } else if ( g.pending_cmd == cmdN_JOIN ) {
            g.pending_out[0] = g.slot;
            g.pending_out[1] = g.slots;
            g.pending_out[2] = g.nbytes;
            g.pending_out[3] = g.settings_len;
            memcpy ( g.pending_out + 4, g.settings, SETTINGS_LEN );
            g.pending_len = 4 + SETTINGS_LEN;
            g.pending_done = 1;
        }
    } else if ( !strcmp ( op, "members" ) ) {
        g.members = ( uint8_t ) json_int ( l, "count", g.members );
        g.ready_mask = ( uint8_t ) json_int ( l, "ready", g.ready_mask );
    } else if ( !strcmp ( op, "start" ) ) {
        g.seed = ( uint16_t ) json_int ( l, "seed", 0 );
        g.start_frame = ( uint16_t ) json_int ( l, "frame", 0 );
        g.started = 1;
        g.full_mask = ( uint8_t ) json_int ( l, "mask", ( 1 << g.slots ) - 1 );
        if ( g.state != NETST_SPECTATOR ) g.state = NETST_RUNNING;
        frames_clear ( );
        g.base = g.start_frame;
    } else if ( !strcmp ( op, "input" ) ) {
        char hex[2 * MAX_BYTES + 1];
        json_str ( l, "data", hex, sizeof ( hex ) );
        store_input ( ( uint32_t ) json_int ( l, "frame", 0 ), ( int ) json_int ( l, "slot", -1 ), hex );
    } else if ( !strcmp ( op, "desync" ) ) {
        g.state = NETST_DESYNC;
    } else if ( !strcmp ( op, "dropped" ) ) {
        g.state = NETST_DROPPED;
        g.started = 0;
    } else if ( !strcmp ( op, "msg" ) ) {
        if ( g.msg_head - g.msg_tail < 8 ) {
            char hex[2 * MSG_LEN + 1];
            int i = g.msg_head % 8;
            json_str ( l, "data", hex, sizeof ( hex ) );
            g.msgs[i].from = ( uint8_t ) json_int ( l, "from", 0xff );
            g.msgs[i].len = ( uint8_t ) hex_decode ( hex, g.msgs[i].data, MSG_LEN );
            g.msg_head++;
        }
    } else if ( !strcmp ( op, "error" ) ) {
        g.last_error = ( uint8_t ) json_int ( l, "code", E_PARAM );
        if ( g.pending_cmd ) { g.pending_err = g.last_error; g.pending_done = 1; }
    } else if ( !strcmp ( op, "link" ) ) {
        unimgr_net_set_link ( ( int ) json_int ( l, "linked", 0 ) );
#ifndef __EMSCRIPTEN__
        if ( !g.linked && g_sock >= 0 ) { close ( g_sock ); g_sock = -1; }
#endif
        if ( g.pending_cmd ) { g.pending_err = E_NOLINK; g.pending_done = 1; }
    }
}

static void pump ( void ) {
    const char *l;
    while ( ( l = lineq_pop ( &g_inq ) ) != NULL ) handle_line ( l );
}

/* highest frame such that all frames from base to it are complete */
static uint32_t avail_frame ( void ) {
    uint8_t full = g.full_mask ? g.full_mask : ( uint8_t ) ( ( 1 << g.slots ) - 1 );
    while ( g.base < 0xffff && ( g.have[g.base % FRAMES] & full ) == full ) {
        /* frame base is complete: keep it (window) but advance the marker */
        uint32_t next = g.base + 1;
        /* clear the slot that falls out of the window */
        if ( next + FRAMES - 1 < 0x10000 ) {
            g.have[( next + FRAMES - 1 ) % FRAMES] = 0;
        }
        g.base = next;
    }
    return g.base ? g.base - 1 : 0xffff;
}

/* ---------------- command interface ---------------- */

/* MZPico management extensions used by the MZPico menu and explorer (the
 * firmware's unicard.cpp is the reference): enough of them to run those two
 * programs in the emulator - volume list, [menu] config records, WiFi state,
 * listing options, current mounts. */
int g_unimgr_mzpico_stream = 0;   /* GETCONFIG output is a record STREAM (status bit 2) */
int g_unimgr_mzpico_sort = 0;     /* SETSORT flags: bit 1 = launchable files only */
static uint8_t g_mzpico_wifi = 3; /* CONNECTED */

int unimgr_net_is_cmd ( uint8_t cmd ) {
    if ( getenv ( "MZPICO_DEBUG" ) && cmd >= 0x90 ) fprintf ( stderr, "MZPICO is_cmd %02x mode=%d\n", cmd, g_unicard_mzpico_mode );
    if ( !g_unicard_mzpico_mode ) return 0;
    if ( cmd == cmdX_INFO || ( cmd >= cmdN_STATUS && cmd <= cmdN_RECV ) ) return 1;
    return cmd == 0x90 || cmd == 0x92 || cmd == 0x93 || cmd == 0x96 || cmd == 0x98;
}

const char *unimgr_net_param_format ( uint8_t cmd ) {
    if ( !unimgr_net_is_cmd ( cmd ) ) return NULL;
    switch ( cmd ) {
        case 0x92:        return "S";                            /* GETCONFIG section */
        case 0x96:        return "B";                            /* SETSORT flags */
        case cmdN_CREATE: return "BBBBBBBBBBBBBBBBBBBBBBB";     /* game, build, slots, bytes, len, 16 settings */
        case cmdN_JOIN:   return "BBBBS";                       /* game, build, code */
        case cmdN_READY:  return "B";
        case cmdN_SEND:   return "BBBBBB";                      /* frame, up to 4 bytes (nbytes used) */
        case cmdN_POLL:   return "BB";
        case cmdN_HASH:   return "BBBB";
        case cmdN_MSG:    return "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";   /* to, len, 32 bytes */
        default:          return "";
    }
}

static void fill_status ( uint8_t *out ) {
    uint32_t av;
    pump ( );
    av = ( g.state == NETST_RUNNING || g.state == NETST_SPECTATOR ) ? avail_frame ( ) : 0xffff;
    out[0] = g.state;
    out[1] = g.slot;
    out[2] = g.members;
    out[3] = g.ready_mask;
    out[4] = g.rtt;
    out[5] = ( av == 0xffff ) ? 0 : ( uint8_t ) ( ( av + 1 > g.start_frame ) ? ( av + 1 - g.start_frame ) & 0xff : 0 );
    out[6] = ( uint8_t ) ( g.msg_head - g.msg_tail );
    out[7] = g.last_error;
}

int unimgr_net_exec ( uint8_t cmd, const uint8_t *p, uint8_t *out, int *out_len ) {
    char line[LINE_LEN];
    *out_len = 0;
    pump ( );
    switch ( cmd ) {
        case 0x90: {                                             /* LISTVOL */
            const char *v = "sd:\r";
            memcpy ( out, v, 4 ); *out_len = 4; return 0;
        }
        case 0x92: {                                             /* GETCONFIG: [menu] records key[16] value[64] */
            if ( getenv ( "MZPICO_DEBUG" ) ) fprintf ( stderr, "MZPICO GETCONFIG '%s'\n", (const char*) p );
            static const char *cfg[][2] = { { "key_b", "Basic|@basic" }, { "key_e", "Explorer|@explorer" },
                                            { "key_c", "CP/M|sd:/cpm/CPM.dsk" } };
            int n = 0;
            if ( !strcmp ( (const char*) p, "menu" ) ) {
                for ( int i = 0; i < 3; i++ ) {
                    memset ( out + n, 0, 80 );
                    strncpy ( (char*) out + n, cfg[i][0], 15 );
                    strncpy ( (char*) out + n + 16, cfg[i][1], 63 );
                    n += 80;
                }
            }
            g_unimgr_mzpico_stream = n ? 1 : 0;
            *out_len = n; return 0;
        }
        case 0x93:                                               /* WIFISTATUS */
            out[0] = g_mzpico_wifi; *out_len = 1; return 0;
        case 0x96:                                               /* SETSORT: order left to the Z80, bit 1 filters */
            g_unimgr_mzpico_sort = p[0]; *out_len = 0; return 0;
        case 0x98: {                                             /* MOUNTS */
            const char *m = "1:sd:/cpm/CPM.dsk\r2:\r3:\r4:\rQ:sd:/games/QDisk-5Z001.mzq\r";
            int l = strlen ( m ); memcpy ( out, m, l ); *out_len = l; return 0;
        }
        case cmdX_INFO:
            memset ( out, 0, 16 );
            out[0] = 1;                  /* extensions protocol revision */
            out[1] = 0x81;               /* board: Deluxe | W */
            out[2] = 2;
            out[3] = 0x01 | 0x02 | 0x08; /* wifi, sound, NET */
            *out_len = 16;
            return 0;

        case cmdN_STATUS:
            fill_status ( out );
            *out_len = 8;
            return 0;

        case cmdN_CREATE: {
            char hex[2 * SETTINGS_LEN + 1];
            int len = p[6] > SETTINGS_LEN ? SETTINGS_LEN : p[6];
            if ( transport_connect ( ) != 0 ) { g.last_error = E_NOLINK; return E_NOLINK; }
            if ( p[4] < 1 || p[4] > MAX_SLOTS || p[5] < 1 || p[5] > MAX_BYTES ) return E_PARAM;
            hex_encode ( p + 7, len, hex );
            snprintf ( line, sizeof ( line ), "{\"op\":\"create\",\"game\":%u,\"build\":%u,\"slots\":%u,\"bytes\":%u,\"settings\":\"%s\"}",
                       p[0] | ( p[1] << 8 ), p[2] | ( p[3] << 8 ), p[4], p[5], hex );
            if ( transport_send ( line ) != 0 ) return E_NOLINK;
            g.pending_cmd = cmd; g.pending_done = 0; g.pending_err = 0; g.pending_len = 0;
            return -1;
        }

        case cmdN_JOIN:
            if ( transport_connect ( ) != 0 ) { g.last_error = E_NOLINK; return E_NOLINK; }
            snprintf ( line, sizeof ( line ), "{\"op\":\"join\",\"game\":%u,\"build\":%u,\"code\":\"%s\"}",
                       p[0] | ( p[1] << 8 ), p[2] | ( p[3] << 8 ), ( const char * ) ( p + 4 ) );
            if ( transport_send ( line ) != 0 ) return E_NOLINK;
            g.pending_cmd = cmd; g.pending_done = 0; g.pending_err = 0; g.pending_len = 0;
            return -1;

        case cmdN_LEAVE:
            if ( g.state == NETST_NOLINK || g.state == NETST_READY ) return E_NOROOM;
            transport_send ( "{\"op\":\"leave\"}" );
            g.state = NETST_READY; g.started = 0; g.members = 0; g.ready_mask = 0; g.full_mask = 0;
            return 0;

        case cmdN_READY:
            if ( g.state != NETST_INROOM && g.state != NETST_RUNNING ) return E_NOROOM;
            snprintf ( line, sizeof ( line ), "{\"op\":\"ready\",\"ready\":%u}", p[0] ? 1 : 0 );
            transport_send ( line );
            if ( g.started ) {
                out[0] = ( uint8_t ) g.seed; out[1] = ( uint8_t ) ( g.seed >> 8 );
                out[2] = ( uint8_t ) g.start_frame; out[3] = ( uint8_t ) ( g.start_frame >> 8 );
            } else {
                memset ( out, 0xff, 4 );
            }
            *out_len = 4;
            return 0;

        case cmdN_SEND: {
            uint32_t frame = p[0] | ( p[1] << 8 );
            char hex[2 * MAX_BYTES + 1];
            if ( g.state != NETST_RUNNING ) return E_NOROOM;
            hex_encode ( p + 2, g.nbytes, hex );
            store_input ( frame, g.slot, hex );
            snprintf ( line, sizeof ( line ), "{\"op\":\"input\",\"frame\":%u,\"data\":\"%s\"}", frame, hex );
            return transport_send ( line ) == 0 ? 0 : E_NOLINK;
        }

        case cmdN_POLL: {
            uint32_t frame = p[0] | ( p[1] << 8 ), av;
            if ( g.state != NETST_RUNNING && g.state != NETST_SPECTATOR ) return E_NOROOM;
            av = avail_frame ( );
            out[0] = ( uint8_t ) av; out[1] = ( uint8_t ) ( av >> 8 );
            memset ( out + 2, 0, MAX_SLOTS * MAX_BYTES );
            if ( av != 0xffff && frame <= av && frame + FRAMES > g.base ) {
                int s;
                for ( s = 0; s < g.slots; s++ )
                    memcpy ( out + 2 + s * g.nbytes, g.data[frame % FRAMES][s], g.nbytes );
            }
            *out_len = 2 + g.slots * g.nbytes;
            return 0;
        }

        case cmdN_HASH:
            if ( g.state != NETST_RUNNING ) return E_NOROOM;
            snprintf ( line, sizeof ( line ), "{\"op\":\"hash\",\"frame\":%u,\"hash\":%u}",
                       p[0] | ( p[1] << 8 ), p[2] | ( p[3] << 8 ) );
            transport_send ( line );
            return 0;

        case cmdN_MSG: {
            char hex[2 * MSG_LEN + 1];
            int len = p[1] > MSG_LEN ? MSG_LEN : p[1];
            if ( g.state < NETST_INROOM ) return E_NOROOM;
            hex_encode ( p + 2, len, hex );
            snprintf ( line, sizeof ( line ), "{\"op\":\"msg\",\"to\":%d,\"data\":\"%s\"}", p[0] == 0xff ? -1 : p[0], hex );
            transport_send ( line );
            return 0;
        }

        case cmdN_RECV:
            memset ( out, 0, 2 + MSG_LEN );
            if ( g.msg_head != g.msg_tail ) {
                int i = g.msg_tail % 8;
                out[0] = g.msgs[i].from;
                out[1] = g.msgs[i].len;
                memcpy ( out + 2, g.msgs[i].data, MSG_LEN );
                g.msg_tail++;
            } else {
                out[0] = 0xff;
            }
            *out_len = 2 + MSG_LEN;
            return 0;
    }
    return E_NOTIMPL;
}

int unimgr_net_async_poll ( uint8_t *out, int *out_len ) {
    pump ( );
    if ( !g.pending_cmd ) return E_NOTIMPL;
    if ( !g.pending_done ) return 0;
    g.pending_cmd = 0;
    if ( g.pending_err ) return g.pending_err;
    memcpy ( out, g.pending_out, g.pending_len );
    *out_len = g.pending_len;
    return 1;
}
