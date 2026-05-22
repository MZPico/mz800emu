/*
 * io_history.c - Implementace ringu pro IORQ event history.
 *
 * Viz io_history.h pro datovy model a kontrakty.
 *
 * Licence: GPLv3
 */

#include "io_history.h"

#include <stdlib.h>
#include <string.h>


/**
 * Globalni instance ringu.
 */
st_IO_HISTORY_RING g_io_history = {
    .events    = NULL,
    .capacity  = 0,
    .head      = 0,
    .count     = 0,
    .overflow  = false,
};


/**
 * Per-port record_enabled mapa (V1.7+ 2.6).
 *
 * Default = vsechny 1 (= behavior beze zmeny). io_history_init() pripadne
 * cfg propagate callback ji prepise. Filter aplikovan v io_history_record().
 */
uint8_t g_io_history_record_enabled[ IO_HISTORY_RECORD_MAP_SIZE ] = {
    /* compile-time inicializace na 1 by vyzadovala 256 hodnot - misto toho
     * inicializujeme za behu v io_history_record_enable_all() volane z
     * io_history_init. Tady je sance nepouzit (kdyz nikdo init nezavola)
     * = vse zustane 0 = zadny port se nezaznamenava. Auto-init v
     * io_history_record() volajici io_history_init() to vyresi. */
    0
};


/**
 * Helper - clamp capacity do platneho rozsahu.
 */
static size_t clamp_capacity ( size_t cap )
{
    if ( cap < IO_HISTORY_MIN_CAPACITY ) cap = IO_HISTORY_MIN_CAPACITY;
    if ( cap > IO_HISTORY_MAX_CAPACITY ) cap = IO_HISTORY_MAX_CAPACITY;
    return cap;
}


void io_history_record_enable_all ( void )
{
    /* Nastavi vsech 256 portu na 1 = default capture. Byte writes
     * jsou atomicke - volat z UI vlakna je bezpecne i kdyz emu vlakno
     * pravě cte (= dostane mix starych a novych hodnot, ale obe valid).
     */
    for ( size_t i = 0; i < IO_HISTORY_RECORD_MAP_SIZE; i++ ) {
        g_io_history_record_enabled[ i ] = 1u;
    }
}


void io_history_init ( size_t capacity )
{
    /* Default record map = vse aktivni. Cfg propagate callback to muze
     * pozdeji prepsat na ulozenou hodnotu z [IO_PORTS_PANEL]. */
    io_history_record_enable_all ( );

    if ( capacity == 0 ) capacity = IO_HISTORY_DEFAULT_CAPACITY;
    capacity = clamp_capacity ( capacity );

    /* Pokud uz mame alokovano, jen vynulujeme stav. Realokace probiha
     * pres io_history_set_capacity(). */
    if ( g_io_history.events != NULL && g_io_history.capacity == capacity ) {
        g_io_history.head     = 0;
        g_io_history.count    = 0;
        g_io_history.overflow = false;
        return;
    }

    /* Realokace */
    free ( g_io_history.events );
    g_io_history.events = (st_IO_HISTORY_EVENT *)
        calloc ( capacity, sizeof ( st_IO_HISTORY_EVENT ) );
    g_io_history.capacity = capacity;
    g_io_history.head     = 0;
    g_io_history.count    = 0;
    g_io_history.overflow = false;
}


void io_history_destroy ( void )
{
    free ( g_io_history.events );
    g_io_history.events   = NULL;
    g_io_history.capacity = 0;
    g_io_history.head     = 0;
    g_io_history.count    = 0;
    g_io_history.overflow = false;
}


void io_history_set_capacity ( size_t new_capacity )
{
    new_capacity = clamp_capacity ( new_capacity );
    io_history_destroy ( );
    io_history_init ( new_capacity );
}


void io_history_record ( bool is_in, uint16_t port, uint8_t value,
                          uint16_t pc, uint32_t frame,
                          uint16_t scanline, uint16_t px,
                          uint32_t cpu_cycle )
{
    if ( g_io_history.events == NULL || g_io_history.capacity == 0 ) {
        /* Auto-init pri prvnim volani (= pripad kdyby debugger_init
         * nebyl zavolan v testovacim kontextu). */
        io_history_init ( IO_HISTORY_DEFAULT_CAPACITY );
        if ( g_io_history.events == NULL ) return;
    }

    /* V1.7+ 2.6 selective per-port filter. Low byte port adresy slouzi
     * jako 8-bit I/O index do record_enabled mapy. Atomic byte read -
     * UI vlakno muze tu hodnotu menit bez locku. Default vse = 1
     * (= zero-overhead beyond one branch). */
    if ( !g_io_history_record_enabled[ port & 0xFFu ] ) return;

    st_IO_HISTORY_EVENT *e = &g_io_history.events[ g_io_history.head ];
    e->frame     = frame;
    e->cpu_cycle = cpu_cycle;
    e->port      = port;
    e->pc        = pc;
    e->value     = value;
    /* IORQ event: bit 0 = is_read, bit 1 = 0 (= ne memory). */
    e->flags     = is_in ? IO_HISTORY_FLAG_READ : 0u;
    e->scanline  = scanline;
    e->px        = px;
    e->_pad      = 0;

    g_io_history.head = ( g_io_history.head + 1 ) % g_io_history.capacity;
    if ( g_io_history.count < g_io_history.capacity ) {
        g_io_history.count++;
    } else {
        g_io_history.overflow = true;
    }
}


void io_history_record_mem ( bool is_read, uint16_t addr, uint8_t value,
                              uint16_t pc, uint32_t frame,
                              uint16_t scanline, uint16_t px,
                              uint32_t cpu_cycle )
{
    if ( g_io_history.events == NULL || g_io_history.capacity == 0 ) {
        io_history_init ( IO_HISTORY_DEFAULT_CAPACITY );
        if ( g_io_history.events == NULL ) return;
    }

    st_IO_HISTORY_EVENT *e = &g_io_history.events[ g_io_history.head ];
    e->frame     = frame;
    e->cpu_cycle = cpu_cycle;
    e->port      = addr;     /* MMIO addr ulozeno do `port` field */
    e->pc        = pc;
    e->value     = value;
    /* Memory event: bit 0 = is_read, bit 1 = 1 (= memory). */
    e->flags     = ( is_read ? IO_HISTORY_FLAG_READ : 0u )
                   | IO_HISTORY_FLAG_MEMORY;
    e->scanline  = scanline;
    e->px        = px;
    e->_pad      = 0;

    g_io_history.head = ( g_io_history.head + 1 ) % g_io_history.capacity;
    if ( g_io_history.count < g_io_history.capacity ) {
        g_io_history.count++;
    } else {
        g_io_history.overflow = true;
    }
}


void io_history_clear ( void )
{
    g_io_history.head     = 0;
    g_io_history.count    = 0;
    g_io_history.overflow = false;
    /* Necleanujeme events[] obsah - count=0 zarucuje, ze se nepouzije. */
}


const st_IO_HISTORY_EVENT* io_history_get ( size_t idx )
{
    if ( idx >= g_io_history.count ) return NULL;
    if ( g_io_history.events == NULL ) return NULL;

    /* Pri non-overflow: data jsou na indexu 0..count-1 v fyzickem ringu.
     * Pri overflow: oldest je na head, fyzicky idx = (head + idx) % capacity. */
    size_t phys;
    if ( !g_io_history.overflow ) {
        phys = idx;
    } else {
        phys = ( g_io_history.head + idx ) % g_io_history.capacity;
    }
    return &g_io_history.events[ phys ];
}
