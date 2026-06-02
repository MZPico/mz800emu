/*
 * File:   tcp_server.c
 *
 * Implementace TCP listener vrstvy MCP backendu (V0.A.5).
 *
 * Threading model (3 vlákna na běžící server, max):
 *  1. Listener thread - drží `listen_sock`, opakovaně volá `accept`.
 *     Při příchodu spojení:
 *      - pokud už existuje aktivní klient (V0/V1 limit = 1), pošle
 *        rejection řádek a close,
 *      - jinak spawnuje per-conn vlákno.
 *  2. Per-connection thread - vlastní `client_sock`, čte JSONL řádky,
 *     dispatchuje přes `mcp_dispatch_request`, posílá odpovědi.
 *  3. Main / GUI thread - volá `_create`, `_start`, `_stop`, čte status.
 *
 * Shutdown koordinace:
 *  - `_stop` nastaví `is_running = false` pod state mutexem.
 *  - `shutdown(listen_sock, SD_BOTH)` + `closesocket` unblokne `accept`.
 *  - `shutdown(client_sock, SD_BOTH)` + `closesocket` unblokne čtení
 *    v per-conn vlákně.
 *  - Hlavní thread joinne obě vlákna - deterministický cleanup,
 *    žádné detached threads.
 *
 * Přenositelnost (obojživelný kód Windows / POSIX):
 *  - Tělo je psané proti WinSock názvosloví (`SOCKET`, `INVALID_SOCKET`,
 *    `closesocket`, `WSAGetLastError`, `WSAE*`). Na POSIXu se tyto názvy
 *    mapují přes tenkou kompatibilní vrstvu (viz níže) na BSD socket API.
 *  - Windows: `WSAStartup` jednou per proces (guard `_winsock_initialized`),
 *    hardlink na `-lws2_32` přes `mz::platform` v `Dependencies.cmake`,
 *    `SOCKET` je `unsigned __int64`, chyba se čte přes `WSAGetLastError()`.
 *  - POSIX: `SOCKET` je `int`, žádná init/cleanup vrstva, chyba přes `errno`,
 *    `MSG_NOSIGNAL` potlačí `SIGPIPE` při zápisu na zavřený peer.
 *
 * Scope V0.A.5:
 *  - Žádný auth / TLS, žádný EVENT broadcast, žádný TRAP forward,
 *    žádný multi-client.
 *
 * Reference: `mcp-server/plans/INSTRUKCE-faze-V0.A.5-tcp-listener.md`,
 * `debugger/rozbory/budoucnost/mcp-server/README.md` sekce 6.2.
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ---------------------------------------------------------------------------
 */

#include "../mzarch/mzarch_config.h"

#ifdef MZ800EMU_CFG_MCP_TCP_ENABLED

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <glib.h>

/* Síťová vrstva - obojživelná (Windows WinSock 2 / POSIX BSD sockets).
 * Zbytek modulu je psaný proti WinSock názvosloví; na POSIXu ho na BSD
 * socket API mapuje tenká kompatibilní vrstva níže. */
#ifdef _WIN32
/* winsock2.h musí být *před* windows.h, aby se nedotahoval starý winsock.h. */
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

typedef int SOCKET;             /**< @brief POSIX socket deskriptor. */
#define INVALID_SOCKET (-1)     /**< @brief Sentinel neplatného socketu. */
#define SOCKET_ERROR   (-1)     /**< @brief Návratová hodnota chyby socket volání. */
#define closesocket(s) close(s) /**< @brief WinSock closesocket -> POSIX close. */

#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR       /**< @brief shutdown() obou směrů. */
#endif

/* WinSock chybové kódy -> POSIX errno. Chyba se čte hned po selhání
 * socket volání (mezi tím nesmí být errno clobbernuto). */
#define WSAGetLastError() (errno)
#define WSAEINTR        EINTR
#define WSAECONNRESET   ECONNRESET
#define WSAENOTSOCK     ENOTSOCK
#define WSAESHUTDOWN    ESHUTDOWN
#define WSAEINVAL       EINVAL
#define WSAECONNABORTED ECONNABORTED
#define WSAEADDRINUSE   EADDRINUSE
#endif /* _WIN32 */

/* send() na POSIXu může při zavřeném peeru vyvolat SIGPIPE a shodit
 * proces; MSG_NOSIGNAL to potlačí (send vrátí EPIPE jako na WinSocku).
 * Na Windows flag neexistuje a SIGPIPE tam není - fallback na 0. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include "tcp_server.h"
#include "jsonl_io.h"
#include "dispatch.h"
#include "cooperation.h"


/* ====================================================================== */
/* Interní stav serveru                                                    */
/* ====================================================================== */

/**
 * @brief Stav jedné TCP server instance.
 *
 * Drží listen socket, případně jeden aktivní klientský socket a handle
 * obou vláken. Synchronizace přes `state_mutex` - chrání proměnné
 * `is_running`, `client_count`, `client_sock`, `client_thread`.
 *
 * Invariant:
 *  - is_running == TRUE  =>  listener_thread != NULL && listen_sock != INVALID_SOCKET
 *  - is_running == FALSE =>  listener_thread == NULL && client_thread == NULL
 *  - client_count ∈ {0, 1} (V0/V1 single-client limit)
 *  - client_count == 1   =>  client_sock != INVALID_SOCKET && client_thread != NULL
 */
struct st_MCP_TCP_SERVER {
    SOCKET    listen_sock;       /**< @brief Listen socket nebo INVALID_SOCKET. */
    int       port;              /**< @brief Aktuální port (0 = stopped). */

    GThread  *listener_thread;   /**< @brief Listener vlákno nebo NULL. */
    GThread  *client_thread;     /**< @brief Per-conn vlákno nebo NULL. */

    SOCKET    client_sock;       /**< @brief Per-conn socket nebo INVALID_SOCKET. */
    gint      client_count;      /**< @brief Počet aktivních klientů (0 / 1). */

    gboolean  is_running;        /**< @brief Listener flag (pod state_mutex). */
    gboolean  stop_requested;    /**< @brief Stop signal pro vlákna. */

    GMutex    state_mutex;       /**< @brief Chrání is_running / client_* / port. */
    GMutex    client_write_mutex;/**< @brief Serializuje zápisy na client_sock. */
};


/* ====================================================================== */
/* Globální stav modulu                                                    */
/* ====================================================================== */

/**
 * @brief Globální handle TCP serveru pro UI integraci.
 *
 * Inicializován lazy z Tools menu nebo eagerly z `main` při
 * `--mcp-tcp-port=N`. Destroy v `mcp_tcp_server_shutdown_global`.
 */
st_MCP_TCP_SERVER *g_mcp_tcp_server = NULL;


/**
 * @brief Idempotentní WinSock init guard.
 *
 * `WSAStartup` musí proběhnout právě jednou za životnost procesu před
 * jakýmkoli socket voláním. `WSACleanup` neuklízíme - WinSock countuje
 * Startup/Cleanup volání, ale pokud je proces dlouhotrvající (GUI emu)
 * a uživatel Start/Stop opakuje, cleanup by zbytečně shazoval interní
 * struktury. Cleanup se provede až s `mcp_tcp_server_shutdown_global`.
 */
#ifdef _WIN32
static gboolean _winsock_initialized = FALSE;
static GMutex   _winsock_init_mutex;
static gboolean _winsock_init_mutex_ready = FALSE;
#endif


/* ====================================================================== */
/* Helpers                                                                  */
/* ====================================================================== */

/**
 * @brief Idempotentní inicializace socket vrstvy.
 *
 * Na Windows provede `WSAStartup` (právě jednou za proces, chráněno
 * mutexem, který se lazy inicializuje při prvním volání z hlavního
 * vlákna). Na POSIXu není socket vrstvu třeba inicializovat - funkce
 * je no-op vracející TRUE.
 *
 * @return TRUE pokud je socket vrstva připravena k použití
 */
static gboolean _ensure_winsock(void)
{
#ifndef _WIN32
    /* POSIX: BSD sockety nevyžadují žádnou inicializaci. */
    return TRUE;
#else
    if (!_winsock_init_mutex_ready)
    {
        /* Race window: pokud dva vlákna vstoupí současně při úplně prvním
         * volání, mohlo by dojít k dvojité inicializaci mutexu. V V0.A.5
         * je _ensure_winsock volán jen z hlavního (GUI) vlákna v rámci
         * _start, takže race v praxi nevzniká. Pro budoucí robustness
         * je třeba mutex inicializovat jednorázově v _module_init. */
        g_mutex_init(&_winsock_init_mutex);
        _winsock_init_mutex_ready = TRUE;
    }

    g_mutex_lock(&_winsock_init_mutex);
    if (!_winsock_initialized)
    {
        WSADATA wsa;
        int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (rc != 0)
        {
            fprintf(stderr, "[MCP] WSAStartup failed (rc=%d)\n", rc);
            g_mutex_unlock(&_winsock_init_mutex);
            return FALSE;
        }
        _winsock_initialized = TRUE;
    }
    g_mutex_unlock(&_winsock_init_mutex);
    return TRUE;
#endif /* _WIN32 */
}


/**
 * @brief Odešle celý buffer na socket (send-all smyčka).
 *
 * `send()` ze smlouvy nemusí odeslat celý buffer na jeden zátah - vrací
 * počet skutečně zařazených bajtů, který může být menší než požadovaný
 * (typicky u payloadů přesahujících `SO_SNDBUF`, default ~64 KB). Tato
 * funkce opakuje `send()` na nezaslaný zbytek, dokud se neodešle vše
 * nebo nenastane chyba. `EINTR` (přerušení signálem na POSIXu) se retryuje.
 *
 * Bez této smyčky se velké odpovědi (např. `get_frame_screenshot_raw`,
 * RGBA ~1 MB) odešlou nekompletní a klient dostane useknutý / rozbitý
 * JSONL řádek nebo abort spojení (mzdos request 0009).
 *
 * @param sock socket na který se píše
 * @param data buffer k odeslání
 * @param len  počet bajtů
 * @return TRUE pokud bylo odesláno všech `len` bajtů, jinak FALSE
 */
static gboolean _send_all(SOCKET sock, const char *data, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        /* Strop chunku kvůli `int` parametru send() na WinSocku
         * (zamezí přetečení u >2 GB bufferů; pro běžné payloady no-op). */
        size_t chunk = len - off;
        if (chunk > (1u << 20)) chunk = (1u << 20);
        int n = send(sock, data + off, (int)chunk, MSG_NOSIGNAL);
        if (n == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEINTR) continue; /* signál - retry */
            return FALSE;
        }
        if (n == 0)
        {
            /* Netypické pro blokující socket; defenzivně bereme jako
             * chybu, ať se nezacyklíme na nulovém progressu. */
            return FALSE;
        }
        off += (size_t)n;
    }
    return TRUE;
}


/**
 * @brief Pošle JSONL řádek na klientský socket (s newline).
 *
 * Chráněno `client_write_mutex` - serializace zápisů pro budoucí
 * EVENT broadcast (= V0.A.6+). V V0.A.5 je jediným producentem per-conn
 * vlákno samo, ale mutex je nutný kvůli rejection řádku posílanému z
 * listener vlákna na druhý klient socket (jiný socket, ale vzor stejný).
 *
 * Řádek i zakončovací newline se posílají přes `_send_all` (send-all
 * smyčka), takže velké odpovědi dorazí kompletní bez ohledu na SO_SNDBUF.
 *
 * @param srv  handle serveru
 * @param sock socket na který se píše
 * @param line JSONL řádek bez zakončovacího newline
 * @return TRUE pokud se celý řádek i newline odeslaly, jinak FALSE
 */
static gboolean _send_jsonl(st_MCP_TCP_SERVER *srv, SOCKET sock,
                            const char *line)
{
    if (!line) return FALSE;
    size_t len = strlen(line);
    g_mutex_lock(&srv->client_write_mutex);
    gboolean ok = _send_all(sock, line, len) && _send_all(sock, "\n", 1);
    g_mutex_unlock(&srv->client_write_mutex);
    return ok;
}


/**
 * @brief Sestaví minimální JSONL chybovou zprávu pro rejection scénáře.
 *
 * Používá se když listener odmítá druhého klienta (V0/V1 limit). Vrací
 * dynamicky alokovaný string (caller `g_free`).
 *
 * Formát:
 *   {"type":"error","reason":"max_clients_exceeded","detail":"..."}
 *
 * @param reason krátký error kód (anglicky, MCP wire convention)
 * @param detail detailní popis (anglicky)
 * @return alokovaný řádek nebo NULL při OOM
 */
static char *_build_error_line(const char *reason, const char *detail)
{
    return g_strdup_printf(
        "{\"type\":\"error\",\"reason\":\"%s\",\"detail\":\"%s\"}",
        reason ? reason : "unknown",
        detail ? detail : "");
}


/**
 * @brief Vrátí čitelnou textovou reprezentaci chyby socket vrstvy.
 *
 * Na Windows je `err` kód z `WSAGetLastError()`, na POSIXu hodnota
 * `errno`. Statický buffer (per-thread bezpečné NENÍ - V0.A.5 se to
 * volá jen v listener vlákně z _start error cesty, takže to nevadí).
 */
static const char *_wsa_strerror(int err)
{
    static char buf[128];
#ifdef _WIN32
    g_snprintf(buf, sizeof(buf), "WSA error %d", err);
#else
    g_snprintf(buf, sizeof(buf), "%s (errno %d)", g_strerror(err), err);
#endif
    return buf;
}


/* ====================================================================== */
/* Per-connection vlákno                                                   */
/* ====================================================================== */

/**
 * @brief Načte jeden JSONL řádek ze socketu.
 *
 * `jsonl_read_line` pracuje nad `FILE*`, ne `SOCKET`. Pro TCP používáme
 * raw `recv` s vlastním řádkovým bufferingem - jednoduché a vyhne se
 * nutnosti `fdopen(sock)` (které není přenositelné mezi WinSock a
 * POSIX sockety).
 *
 * @param sock     klientský socket
 * @param buf      pracovní buffer (alokovaný v caller)
 * @param buf_size velikost bufferu (= max délka jednoho JSONL řádku)
 * @param out_line výstupní nově alokovaný řádek bez '\n' (caller g_free)
 * @return 0 OK, -1 EOF, -2 chyba (overflow nebo recv)
 */
static int _recv_line(SOCKET sock, char *buf, size_t buf_size,
                      char **out_line)
{
    size_t used = 0;
    while (used + 1 < buf_size)
    {
        char c;
        int rc = recv(sock, &c, 1, 0);
        if (rc == 0)
        {
            /* Klient zavřel spojení. */
            if (used == 0) return -1; /* žádný data = clean EOF */
            buf[used] = '\0';
            *out_line = g_strdup(buf);
            return 0;
        }
        if (rc == SOCKET_ERROR)
        {
            return -2;
        }
        if (c == '\n')
        {
            buf[used] = '\0';
            *out_line = g_strdup(buf);
            return 0;
        }
        if (c == '\r')
        {
            /* Přeskočíme CR (pro \r\n line endings) */
            continue;
        }
        buf[used++] = c;
    }
    /* Overflow - řádek delší než buffer. */
    return -2;
}


/**
 * @brief Hlavní smyčka per-connection vlákna.
 *
 * `data` je `st_MCP_TCP_SERVER *`. Klientský socket je v
 * `srv->client_sock`. Smyčka čte JSONL řádky, dispatchuje a posílá
 * odpovědi. Při EOF / recv chybě / stop signálu vyskočí, ukončí spojení
 * a sníží `client_count` na 0.
 *
 * @param data handle TCP serveru
 * @return NULL
 */
static gpointer _client_thread_func(gpointer data)
{
    st_MCP_TCP_SERVER *srv = (st_MCP_TCP_SERVER *)data;
    SOCKET sock = srv->client_sock;

    /* Buffer pro jeden JSONL řádek. 64 KB je vědomě nadsazené - největší
     * očekávaná zpráva v V0.A.3 (mem_read max 4096 B) má < 16 KB raw JSON. */
    enum { LINE_BUF_SIZE = 65536 };
    char *buf = g_new(char, LINE_BUF_SIZE);

    /* Pošli HELLO zprávu (sdílí builder s pipe transportem). */
    char *hello = NULL;
    if (mcp_dispatch_build_hello(&hello) == MCP_DISPATCH_OK && hello)
    {
        _send_jsonl(srv, sock, hello);
        free(hello);
    }
    else
    {
        fprintf(stderr, "[MCP] failed to build hello for TCP client\n");
    }

    /* Hlavní smyčka */
    while (TRUE)
    {
        /* Předčasná kontrola stop_requested - pokud _stop nastavil flag,
         * recv níže buď padne na shutdown socketu, nebo by se zacyklil. */
        g_mutex_lock(&srv->state_mutex);
        gboolean stop = srv->stop_requested;
        g_mutex_unlock(&srv->state_mutex);
        if (stop) break;

        char *line = NULL;
        int rr = _recv_line(sock, buf, LINE_BUF_SIZE, &line);
        if (rr == -1)
        {
            /* Clean EOF - klient zavřel. */
            break;
        }
        if (rr == -2)
        {
            /* Chyba - log a vyskočení. */
            int err = WSAGetLastError();
            /* WSAEINTR / WSAECONNRESET při shutdownu jsou očekávané. */
            if (err != WSAEINTR && err != WSAECONNRESET &&
                err != WSAENOTSOCK && err != WSAESHUTDOWN)
            {
                fprintf(stderr, "[MCP] TCP recv error: %s\n",
                        _wsa_strerror(err));
            }
            break;
        }

        /* Prázdné řádky ignorovat. */
        if (!line || line[0] == '\0')
        {
            if (line) g_free(line);
            continue;
        }

        st_JSONL_MESSAGE *msg = NULL;
        en_JSONL_RESULT pr = jsonl_parse_line(line, &msg);
        if (pr != JSONL_OK || !msg)
        {
            fprintf(stderr,
                    "[MCP] TCP JSONL parse error (pr=%d) - line ignored\n",
                    pr);
            if (msg) jsonl_msg_free(msg);
            g_free(line);
            continue;
        }

        char *response = NULL;
        en_MCP_DISPATCH_RESULT dr = mcp_dispatch_request(msg, &response);

        if (response)
        {
            if (!_send_jsonl(srv, sock, response))
            {
                /* Klient pravděpodobně zavřel během dispatche. */
                free(response);
                jsonl_msg_free(msg);
                g_free(line);
                break;
            }
            free(response);
        }
        else
        {
            if (dr != MCP_DISPATCH_NOT_A_REQUEST)
            {
                fprintf(stderr,
                        "[MCP] TCP dispatch produced no response (dr=%d)\n",
                        dr);
            }
        }

        jsonl_msg_free(msg);
        g_free(line);
    }

    /* Cleanup spojení. Logujeme peer info, pokud lze získat. */
    struct sockaddr_in peer;
    socklen_t peerlen = sizeof(peer);
    char ip[INET_ADDRSTRLEN] = "?.?.?.?";
    int peer_port = 0;
    if (getpeername(sock, (struct sockaddr *)&peer, &peerlen) == 0)
    {
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        peer_port = ntohs(peer.sin_port);
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);

    g_mutex_lock(&srv->state_mutex);
    srv->client_sock = INVALID_SOCKET;
    /* client_thread NULL nastavíme až v _stop nebo když vlákno končí
     * spontánně - jinak by listener neměl odkud zjistit handle pro join. */
    g_atomic_int_set(&srv->client_count, 0);
    g_mutex_unlock(&srv->state_mutex);

    g_print("[MCP] Client %s:%d disconnected (clean)\n", ip, peer_port);

    g_free(buf);
    return NULL;
}


/* ====================================================================== */
/* Listener vlákno                                                          */
/* ====================================================================== */

/**
 * @brief Spawnuje per-conn vlákno pro nově přijaté spojení.
 *
 * Předpokládá, že volající už ověřil, že `client_count == 0`. Po
 * úspěšném spawnu inkrementuje `client_count` a uloží socket / thread
 * handle do stavu serveru.
 *
 * Pokud joinitelné staré per-conn vlákno existuje (= předchozí klient
 * se odpojil, ale vlákno ještě nebyl joinnuté), joinneme ho synchronně
 * - V0/V1 single-client model umožní joinem reklamovat resources.
 *
 * @param srv  handle serveru
 * @param sock socket nového klienta (vlastnictví přechází na srv)
 * @param ip   peer IP string (pro log)
 * @param port peer port (pro log)
 */
static void _accept_client(st_MCP_TCP_SERVER *srv, SOCKET sock,
                           const char *ip, int port)
{
    /* Pokud existuje "zombie" client_thread handle (= odpojil se sám, ale
     * ještě jsme ho nejoinnuli), reklamujeme ho. */
    GThread *zombie = NULL;
    g_mutex_lock(&srv->state_mutex);
    if (srv->client_thread && srv->client_sock == INVALID_SOCKET)
    {
        zombie = srv->client_thread;
        srv->client_thread = NULL;
    }
    g_mutex_unlock(&srv->state_mutex);
    if (zombie) g_thread_join(zombie);

    g_mutex_lock(&srv->state_mutex);
    srv->client_sock = sock;
    g_atomic_int_set(&srv->client_count, 1);
    g_mutex_unlock(&srv->state_mutex);

    g_print("[MCP] Client connected from %s:%d\n", ip, port);

    GThread *t = g_thread_new("mcp-tcp-client", _client_thread_func, srv);
    if (!t)
    {
        /* Spawn fail - rollback. */
        fprintf(stderr, "[MCP] failed to spawn per-conn thread\n");
        shutdown(sock, SD_BOTH);
        closesocket(sock);

        g_mutex_lock(&srv->state_mutex);
        srv->client_sock = INVALID_SOCKET;
        g_atomic_int_set(&srv->client_count, 0);
        g_mutex_unlock(&srv->state_mutex);
        return;
    }

    g_mutex_lock(&srv->state_mutex);
    srv->client_thread = t;
    g_mutex_unlock(&srv->state_mutex);
}


/**
 * @brief Odmítne příchozího klienta (V0/V1 max clients = 1).
 *
 * Pošle krátkou error zprávu a uzavře socket. Nelogujeme jako warning,
 * jen info - reject je legitimní operace.
 */
static void _reject_client(st_MCP_TCP_SERVER *srv, SOCKET sock,
                           const char *ip, int port)
{
    (void)srv; /* mutex není potřeba, sock je čerstvě accepted */
    char *err = _build_error_line("max_clients_exceeded",
        "V0.A.5 server accepts only 1 concurrent client");
    if (err)
    {
        /* Přímý _send_all (ne _send_jsonl) - jiný socket než
         * srv->client_sock, mutex by byl zbytečný. Řádek je malý, ale
         * send-all smyčku použijeme kvůli konzistenci a robustnosti. */
        _send_all(sock, err, strlen(err));
        _send_all(sock, "\n", 1);
        g_free(err);
    }
    shutdown(sock, SD_BOTH);
    closesocket(sock);
    g_print("[MCP] Rejected client %s:%d (max clients=1)\n", ip, port);
}


/**
 * @brief Hlavní smyčka listener vlákna.
 *
 * Opakovaně volá `accept` na `listen_sock`. Při příchozím spojení
 * rozhodne dle `client_count`:
 *  - 0 -> spawnuje per-conn vlákno (`_accept_client`),
 *  - 1 -> reject (`_reject_client`).
 *
 * Smyčka se ukončí když `accept` vrátí chybu (typicky `WSAEINTR` /
 * `WSAENOTSOCK` po `shutdown(listen_sock)` z `_stop`).
 *
 * @param data handle TCP serveru
 * @return NULL
 */
static gpointer _listener_thread_func(gpointer data)
{
    st_MCP_TCP_SERVER *srv = (st_MCP_TCP_SERVER *)data;

    while (TRUE)
    {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);

        SOCKET conn = accept(srv->listen_sock,
                             (struct sockaddr *)&peer, &peerlen);
        if (conn == INVALID_SOCKET)
        {
            int err = WSAGetLastError();
            /* Očekávané chyby při _stop: WSAEINTR (interrupted),
             * WSAENOTSOCK (closesocket), WSAEINVAL (shutdown). */
            if (err != WSAEINTR && err != WSAENOTSOCK &&
                err != WSAEINVAL && err != WSAECONNABORTED)
            {
                fprintf(stderr, "[MCP] TCP accept error: %s\n",
                        _wsa_strerror(err));
            }
            break;
        }

        char ip[INET_ADDRSTRLEN] = "?.?.?.?";
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        int peer_port = ntohs(peer.sin_port);

        gint cc = g_atomic_int_get(&srv->client_count);
        if (cc >= 1)
        {
            _reject_client(srv, conn, ip, peer_port);
        }
        else
        {
            _accept_client(srv, conn, ip, peer_port);
        }
    }

    return NULL;
}


/* ====================================================================== */
/* Veřejné API                                                              */
/* ====================================================================== */

st_MCP_TCP_SERVER *mcp_tcp_server_create(void)
{
    st_MCP_TCP_SERVER *srv = g_new0(st_MCP_TCP_SERVER, 1);
    if (!srv) return NULL;

    srv->listen_sock = INVALID_SOCKET;
    srv->client_sock = INVALID_SOCKET;
    srv->port = 0;
    srv->listener_thread = NULL;
    srv->client_thread = NULL;
    srv->is_running = FALSE;
    srv->stop_requested = FALSE;
    srv->client_count = 0;

    g_mutex_init(&srv->state_mutex);
    g_mutex_init(&srv->client_write_mutex);

    return srv;
}


en_MCP_TCP_START_RESULT mcp_tcp_server_start(st_MCP_TCP_SERVER *srv,
                                             int port,
                                             const char *bind_addr)
{
    if (!srv) return MCP_TCP_START_ERROR;
    if (port < 1 || port > 65535) return MCP_TCP_START_ERROR;

    /* V1.A.1: reset cooperation hintu před startem TCP listeneru. Hint
     * je per-session, mezi restartem serveru se nedědí. */
    cooperation_hint_init();

    /* V1.B.3: inicializuj dispatch supported_commands a označ
     * transport jako TCP (= emu_stop handler vrátí error, hot-swap
     * nesmí zabít živou GUI session uživatele). */
    mcp_dispatch_init();
    mcp_dispatch_set_transport_kind(MCP_DISPATCH_TRANSPORT_TCP);

    /* V0.B.4: bind_addr může být NULL -> default 127.0.0.1 (loopback).
     * "0.0.0.0" = wildcard pro všechna rozhraní (vyžaduje vědomé
     * rozhodnutí uživatele, V0.B.4 GUI dialog na to upozorní). */
    const char *eff_bind = bind_addr ? bind_addr : "127.0.0.1";

    g_mutex_lock(&srv->state_mutex);
    if (srv->is_running)
    {
        g_mutex_unlock(&srv->state_mutex);
        return MCP_TCP_START_ALREADY_RUNNING;
    }
    g_mutex_unlock(&srv->state_mutex);

    if (!_ensure_winsock())
    {
        return MCP_TCP_START_ERROR;
    }

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET)
    {
        fprintf(stderr, "[MCP] socket() failed: %s\n",
                _wsa_strerror(WSAGetLastError()));
        return MCP_TCP_START_ERROR;
    }

    /* SO_REUSEADDR usnadňuje rychlý restart po crash / stop. */
    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    /* Konverze bind_addr (textová IPv4) na binární formu. inet_pton
     * vrací 1 při úspěchu, 0 při neplatném formátu, -1 při systémové
     * chybě (af jiný než AF_INET / AF_INET6). */
    int pton_rc = inet_pton(AF_INET, eff_bind, &addr.sin_addr);
    if (pton_rc != 1)
    {
        fprintf(stderr,
            "[MCP] invalid bind address '%s' (expected IPv4 like 127.0.0.1 or 0.0.0.0)\n",
            eff_bind);
        closesocket(ls);
        return MCP_TCP_START_ERROR;
    }

    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        closesocket(ls);
        if (err == WSAEADDRINUSE)
        {
            fprintf(stderr,
                "[MCP] TCP bind failed: port %d already in use\n", port);
            return MCP_TCP_START_PORT_IN_USE;
        }
        fprintf(stderr, "[MCP] TCP bind failed: %s\n", _wsa_strerror(err));
        return MCP_TCP_START_ERROR;
    }

    if (listen(ls, 1) == SOCKET_ERROR)
    {
        fprintf(stderr, "[MCP] listen() failed: %s\n",
                _wsa_strerror(WSAGetLastError()));
        closesocket(ls);
        return MCP_TCP_START_ERROR;
    }

    g_mutex_lock(&srv->state_mutex);
    srv->listen_sock = ls;
    srv->port = port;
    srv->stop_requested = FALSE;
    srv->is_running = TRUE;
    g_mutex_unlock(&srv->state_mutex);

    GThread *t = g_thread_new("mcp-tcp-listener", _listener_thread_func, srv);
    if (!t)
    {
        fprintf(stderr, "[MCP] failed to spawn listener thread\n");
        g_mutex_lock(&srv->state_mutex);
        srv->is_running = FALSE;
        closesocket(srv->listen_sock);
        srv->listen_sock = INVALID_SOCKET;
        srv->port = 0;
        g_mutex_unlock(&srv->state_mutex);
        return MCP_TCP_START_ERROR;
    }

    g_mutex_lock(&srv->state_mutex);
    srv->listener_thread = t;
    g_mutex_unlock(&srv->state_mutex);

    g_print("[MCP] TCP server started on %s:%d\n", eff_bind, port);
    return MCP_TCP_START_OK;
}


void mcp_tcp_server_stop(st_MCP_TCP_SERVER *srv)
{
    if (!srv) return;

    GThread *listener = NULL;
    GThread *client = NULL;
    SOCKET ls = INVALID_SOCKET;
    SOCKET cs = INVALID_SOCKET;

    g_mutex_lock(&srv->state_mutex);
    if (!srv->is_running)
    {
        g_mutex_unlock(&srv->state_mutex);
        return;
    }
    srv->stop_requested = TRUE;
    srv->is_running = FALSE;
    ls = srv->listen_sock;
    cs = srv->client_sock;
    listener = srv->listener_thread;
    client = srv->client_thread;
    srv->listen_sock = INVALID_SOCKET;
    srv->client_sock = INVALID_SOCKET;
    srv->listener_thread = NULL;
    srv->client_thread = NULL;
    g_mutex_unlock(&srv->state_mutex);

    /* Unblokneme accept v listener vlákně. */
    if (ls != INVALID_SOCKET)
    {
        shutdown(ls, SD_BOTH);
        closesocket(ls);
    }

    /* Unblokneme recv v per-conn vlákně. */
    if (cs != INVALID_SOCKET)
    {
        shutdown(cs, SD_BOTH);
        closesocket(cs);
    }

    if (listener) g_thread_join(listener);
    if (client)   g_thread_join(client);

    g_atomic_int_set(&srv->client_count, 0);
    srv->port = 0;

    /* V1.B.3: cleanup dispatch state. Po stop už žádný request
     * nepřijde, takže dynamický supported_commands seznam můžeme
     * uvolnit a transport kind shodit na NONE. */
    mcp_dispatch_set_transport_kind(MCP_DISPATCH_TRANSPORT_NONE);
    mcp_dispatch_shutdown();

    g_print("[MCP] TCP server stopped\n");
}


void mcp_tcp_server_destroy(st_MCP_TCP_SERVER *srv)
{
    if (!srv) return;
    mcp_tcp_server_stop(srv);
    g_mutex_clear(&srv->state_mutex);
    g_mutex_clear(&srv->client_write_mutex);
    g_free(srv);
}


bool mcp_tcp_server_is_running(const st_MCP_TCP_SERVER *srv)
{
    if (!srv) return false;
    /* Cast: const_cast pro mutex lock - g_mutex_lock nemůže být const,
     * ale read-only přístup k flagu chráníme zámkem kvůli memory ordering. */
    st_MCP_TCP_SERVER *m = (st_MCP_TCP_SERVER *)srv;
    g_mutex_lock(&m->state_mutex);
    gboolean r = m->is_running;
    g_mutex_unlock(&m->state_mutex);
    return r ? true : false;
}


int mcp_tcp_server_get_port(const st_MCP_TCP_SERVER *srv)
{
    if (!srv) return 0;
    st_MCP_TCP_SERVER *m = (st_MCP_TCP_SERVER *)srv;
    g_mutex_lock(&m->state_mutex);
    int p = m->port;
    g_mutex_unlock(&m->state_mutex);
    return p;
}


int mcp_tcp_server_get_client_count(const st_MCP_TCP_SERVER *srv)
{
    if (!srv) return 0;
    return g_atomic_int_get(&((st_MCP_TCP_SERVER *)srv)->client_count);
}


void mcp_tcp_server_shutdown_global(void)
{
    if (!g_mcp_tcp_server) return;
    mcp_tcp_server_destroy(g_mcp_tcp_server);
    g_mcp_tcp_server = NULL;
}


#endif /* MZ800EMU_CFG_MCP_TCP_ENABLED */
