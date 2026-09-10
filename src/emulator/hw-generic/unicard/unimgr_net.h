/*
 * File:   unimgr_net.h
 *
 * MZPico NET extension of the Unicard repository device (vendor commands
 * 0xA0-0xA9 plus INFO 0x95 and the MZPico REVD identity), as specified in
 * BomberNet/docs/net-protocol.md. Rooms, per-frame input broadcast, hashes
 * and messages for lockstep multiplayer; the relay is reached through a
 * pluggable transport:
 *   - native build: a TCP JSON-lines socket to the relay ([UNICARD]
 *     net_relay = host:port), reader thread pushes lines into a queue;
 *   - Emscripten: the embedding page owns a WebSocket and pumps lines with
 *     mz_wasm_net_push() / mz_wasm_net_pop().
 * Enabled by [UNICARD] mzpico_mode = 1; then REVD answers with subtype 'M'.
 *
 * ---------------------------------------------------------------------------
 * GPLv3 - see the project licence.
 */
#ifndef UNIMGR_NET_H
#define UNIMGR_NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* configuration (set by unicard.c from the INI) */
    extern int g_unicard_mzpico_mode;
    extern const char *unicard_get_net_relay ( void );   /* [UNICARD] net_relay, host:port */

    extern void unimgr_net_reset ( void );

    /* Command interface used by unimgr.c.
     *   param_format: NULL = not a NET command; "" = no parameters (execute
     *                 at once); else unimgr's 'B'/'S' format string.
     *   exec: 0 = done, out/out_len filled (out_len 0 = no output);
     *         1..255 = error code (status byte 2); -1 = pending (IN_PROGRESS),
     *         poll with unimgr_net_async_poll.
     *   async_poll: 0 = still pending, 1 = done (out filled), >1 = error code. */
    extern const char *unimgr_net_param_format ( uint8_t cmd );
    extern int unimgr_net_exec ( uint8_t cmd, const uint8_t *params, uint8_t *out, int *out_len );
    extern int unimgr_net_async_poll ( uint8_t *out, int *out_len );
    extern int unimgr_net_is_cmd ( uint8_t cmd );

    /* transport side (any thread): inbound JSON line from the relay */
    extern void unimgr_net_push_line ( const char *line );
    /* outbound line for a page-owned transport (Emscripten); NULL = none */
    extern const char *unimgr_net_pop_out ( void );
    extern void unimgr_net_set_link ( int linked );

#ifdef __cplusplus
}
#endif

#endif /* UNIMGR_NET_H */
