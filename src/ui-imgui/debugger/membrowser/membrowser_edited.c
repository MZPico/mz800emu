/**
 * @file membrowser_edited.c
 * @brief Recently-edited bytes storage - implementace.
 *
 * Datová struktura: per-instance pole záznamů (region_kind, region_sub_id,
 * addr). Ring overwrite stylem - při překročení limit nejstarší (index 0)
 * vypadne (shift down O(N), ale N max 1024 a jen 1x per write event).
 *
 * Lookup is_marked() je lineární scan - O(N) per byte v hex view render.
 * Pro typickou edit session (< 100 záznamů) zanedbatelné. Pro plnou
 * kapacitu (1024) a 16 B/row * 60 řádek = 960 scanů * 1024 = ~1M porovnání
 * per frame - na moderním CPU pod 1 ms.
 *
 * Threading: UI-thread only.
 *
 * Lifetime: globální storage. Pamět záznamů je file-static (žádný malloc).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#include "membrowser_edited.h"
#include "membrowser_state.h"

#include <string.h>


/**
 * @brief Jeden záznam o recently-edited byte.
 *
 * V1.5+: pridáno @c original_byte (hodnota PŘED prvním edit této addr
 * v aktuální "session"). Slouží pro auto-unmark detection - když undo
 * vrátí byte na original hodnotu, záznam se odebere a badge zmizí.
 */
typedef struct st_EDITED_ENTRY
{
    int region_kind;       /**< en_REGION_KIND. */
    int region_sub_id;     /**< Disambiguator. */
    uint32_t addr;         /**< Offset v rámci regionu. */
    uint8_t original_byte; /**< Hodnota PŘED prvním edit (session original). */
} st_EDITED_ENTRY;


/**
 * @brief Per-instance ring buffer recently-edited bytů.
 *
 * @c count = počet platných záznamů v entries[0..count-1]. Při overflow
 * se entries[0] vyhodí a zbytek se shiftuje dolů, nový se přidá na pozici
 * count-1.
 */
typedef struct st_EDITED_INSTANCE
{
    st_EDITED_ENTRY entries[MB_EDITED_MAX_PER_INSTANCE];
    int count;
} st_EDITED_INSTANCE;


/**
 * @brief Globální pole instancí (per Memory Browser window).
 *
 * Indexováno @c instance_idx z @ref st_MEMBROWSER_STATE (0..MB_INSTANCE_COUNT-1).
 * Inicializace na nuly = žádné záznamy.
 */
static st_EDITED_INSTANCE s_instances[MB_INSTANCE_COUNT];


/**
 * @brief Vrátí pointer na instanci pokud index je validní, jinak NULL.
 */
static st_EDITED_INSTANCE *get_instance ( int idx )
{
    if ( idx < 0 || idx >= MB_INSTANCE_COUNT ) return NULL;
    return &s_instances[idx];
}


/**
 * @brief Najde index existujícího záznamu (region, addr) v instance.
 *
 * @return Index 0..count-1 nebo -1.
 */
static int find_entry ( st_EDITED_INSTANCE *inst,
                         int region_kind, int region_sub_id,
                         uint32_t addr )
{
    for ( int i = 0; i < inst->count; i++ ) {
        const st_EDITED_ENTRY *e = &inst->entries[i];
        if ( e->region_kind == region_kind
             && e->region_sub_id == region_sub_id
             && e->addr == addr ) {
            return i;
        }
    }
    return -1;
}


/**
 * @brief Přidá nový záznam na konec, případně shift při overflow.
 *
 * Předpokládá, že (region, addr) ještě v ringu není - caller volá find_entry().
 */
static void push_entry ( st_EDITED_INSTANCE *inst,
                          int region_kind, int region_sub_id,
                          uint32_t addr, uint8_t original_byte )
{
    if ( inst->count >= MB_EDITED_MAX_PER_INSTANCE ) {
        /* Shift down - nejstarší (index 0) vypadne. */
        memmove ( &inst->entries[0], &inst->entries[1],
                  ( MB_EDITED_MAX_PER_INSTANCE - 1 ) * sizeof ( inst->entries[0] ) );
        inst->count = MB_EDITED_MAX_PER_INSTANCE - 1;
    }
    st_EDITED_ENTRY *e = &inst->entries[inst->count++];
    e->region_kind = region_kind;
    e->region_sub_id = region_sub_id;
    e->addr = addr;
    e->original_byte = original_byte;
}


/**
 * @brief Odebere záznam na indexu @p idx z instance (shift down zbylých).
 */
static void remove_entry_at ( st_EDITED_INSTANCE *inst, int idx )
{
    if ( idx < 0 || idx >= inst->count ) return;
    if ( idx < inst->count - 1 ) {
        memmove ( &inst->entries[idx], &inst->entries[idx + 1],
                  ( inst->count - idx - 1 ) * sizeof ( inst->entries[0] ) );
    }
    inst->count--;
}


void membrowser_edited_mark ( int instance_idx,
                               int region_kind, int region_sub_id,
                               uint32_t addr, uint8_t original_byte )
{
    st_EDITED_INSTANCE *inst = get_instance ( instance_idx );
    if ( !inst ) return;
    /* Pokud už existuje, no-op (= original_byte z prvního edit zůstává,
     * subsequent edits ho nepřepisují - "session original" je nejstarší). */
    if ( find_entry ( inst, region_kind, region_sub_id, addr ) >= 0 ) return;
    push_entry ( inst, region_kind, region_sub_id, addr, original_byte );
}


void membrowser_edited_mark_range ( int instance_idx,
                                     int region_kind, int region_sub_id,
                                     uint32_t start_addr, uint32_t length,
                                     const uint8_t *originals )
{
    if ( length == 0 ) return;
    st_EDITED_INSTANCE *inst = get_instance ( instance_idx );
    if ( !inst ) return;
    /* Optimalizace: pokud length >= kapacita, vyhodit všechny stávající a
     * naplnit ring posledními kapacita bytů range. */
    if ( length >= MB_EDITED_MAX_PER_INSTANCE ) {
        inst->count = 0;
        uint32_t skip = length - MB_EDITED_MAX_PER_INSTANCE;
        for ( uint32_t i = 0; i < MB_EDITED_MAX_PER_INSTANCE; i++ ) {
            st_EDITED_ENTRY *e = &inst->entries[i];
            e->region_kind = region_kind;
            e->region_sub_id = region_sub_id;
            e->addr = start_addr + skip + i;
            e->original_byte = originals ? originals[skip + i] : 0;
        }
        inst->count = MB_EDITED_MAX_PER_INSTANCE;
        return;
    }
    for ( uint32_t i = 0; i < length; i++ ) {
        uint8_t orig = originals ? originals[i] : 0;
        membrowser_edited_mark ( instance_idx, region_kind, region_sub_id,
                                  start_addr + i, orig );
    }
}


bool membrowser_edited_check_unmark ( int instance_idx,
                                       int region_kind, int region_sub_id,
                                       uint32_t addr, uint8_t current_byte )
{
    st_EDITED_INSTANCE *inst = get_instance ( instance_idx );
    if ( !inst ) return false;
    if ( inst->count == 0 ) return false;
    int idx = find_entry ( inst, region_kind, region_sub_id, addr );
    if ( idx < 0 ) return false;
    if ( inst->entries[idx].original_byte != current_byte ) return false;
    /* Match - byte se vrátil na session-original hodnotu, odebrat záznam. */
    remove_entry_at ( inst, idx );
    return true;
}


void membrowser_edited_check_unmark_range ( int instance_idx,
                                             int region_kind, int region_sub_id,
                                             uint32_t start_addr, uint32_t length,
                                             const uint8_t *current_bytes )
{
    if ( length == 0 || !current_bytes ) return;
    st_EDITED_INSTANCE *inst = get_instance ( instance_idx );
    if ( !inst || inst->count == 0 ) return;
    for ( uint32_t i = 0; i < length; i++ ) {
        membrowser_edited_check_unmark ( instance_idx, region_kind,
                                          region_sub_id,
                                          start_addr + i, current_bytes[i] );
    }
}


bool membrowser_edited_is_marked ( int instance_idx,
                                    int region_kind, int region_sub_id,
                                    uint32_t addr )
{
    st_EDITED_INSTANCE *inst = get_instance ( instance_idx );
    if ( !inst ) return false;
    /* Fast path - prázdná instance, nic neporovnávat. */
    if ( inst->count == 0 ) return false;
    return find_entry ( inst, region_kind, region_sub_id, addr ) >= 0;
}


int membrowser_edited_count_for_instance ( int instance_idx )
{
    st_EDITED_INSTANCE *inst = get_instance ( instance_idx );
    if ( !inst ) return 0;
    return inst->count;
}


void membrowser_edited_clear_for_instance ( int instance_idx )
{
    st_EDITED_INSTANCE *inst = get_instance ( instance_idx );
    if ( !inst ) return;
    inst->count = 0;
}


void membrowser_edited_clear_all ( void )
{
    for ( int i = 0; i < MB_INSTANCE_COUNT; i++ ) {
        s_instances[i].count = 0;
    }
}
