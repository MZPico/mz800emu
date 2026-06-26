/*
 * dbgapi_helpers.cpp - implementace UI helperů nad dbgapi CMDRQ
 *
 * Každý helper je tenký wrapper nad dbgapi_ui_submit_cmd_sync() s
 * default timeoutem DBG_UI_DEFAULT_TIMEOUT_MS. Parametrické struktury
 * jsou alokované na stacku - submit_cmd_sync je synchronní, takže do
 * návratu helperu jsou data v platnosti pro EMU stranu.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */

#include "main.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include "debugger/dbgapi_ui.h"
#include "debugger/dbgapi_cmdrq.h"
#include "dbgapi_helpers.h"


bool dbg_ui_pause(void)
{
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_PAUSE,
                                     NULL, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_run(void)
{
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_RUN,
                                     NULL, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_pause_toggle(void)
{
    /* Aktuální stav zjistíme přes CMD_IS_RUNNING. Alternativa: číst
     * přímo EMULATOR_TEST_PAUSED z UI vlákna - to je atomické čtení
     * jednoho bitu, ale obchází to abstrakci dbgapi. Pro V1 použijeme
     * dvě CMDRQ aby UI vůbec nemuselo sahat do g_emulator stavu. */
    bool is_running = false;
    bool ok = dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                        DBGAPI_CMD_IS_RUNNING,
                                        NULL, &is_running,
                                        DBG_UI_DEFAULT_TIMEOUT_MS);
    if (!ok)
        return false;

    if (is_running)
        return dbg_ui_pause();
    return dbg_ui_run();
}


bool dbg_ui_reset(void)
{
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_RESET,
                                     NULL, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_debugger_state_recompute(void)
{
    /* Deleguje mzarch_platform_fn_debugger_state_changed na EMU vlákno
     * (DBGAPI_CMD_DEBUGGER_STATE_RECOMPUTE) místo přímého volání z UI vlákna.
     * Nutné pro trace-suite: stop kanálu uvolní writer buffer (tlog_writer_close)
     * - kdyby to běželo na UI vlákně souběžně s emu-thread tlog_writer_append,
     * vznikne use-after-free race. Volající si PŘEDEM nastaví mode/flag
     * (cfg.mode = ... apod., atomický int zápis), pak zavolá tuto funkci. */
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_DEBUGGER_STATE_RECOMPUTE,
                                     NULL, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_step_into(void)
{
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_STEP_INTO,
                                     NULL, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_step_over(void)
{
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_STEP_OVER,
                                     NULL, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_run_to(uint16_t addr)
{
    /* data_ptr ukazuje na lokální uint16_t. Po návratu submit_cmd_sync()
     * EMU strana data už nečte - synchronní kontrakt CMDRQ. */
    uint16_t target = addr;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_RUN_TO,
                                     &target, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_set_reg(uint8_t reg_id, uint16_t value)
{
    st_DBGAPI_REG_PARAM p;
    p.reg_id = reg_id;
    p.value = value;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_SET_REG,
                                     &p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_mem_write(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    if (!buf || len == 0)
        return false;

    st_DBGAPI_MEM_PARAM p;
    p.addr = addr;
    p.len = len;
    /* Cast const-away: dispatch v dbgapi.c jen čte z p.buf, neměnit. */
    p.buf = (uint8_t *)buf;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_MEM_WRITE,
                                     &p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_bp_add(uint16_t addr, int *out_id)
{
    st_DBGAPI_BP_PARAM p;
    p.addr = addr;
    p.id = -1;
    bool ok = dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                        DBGAPI_CMD_BP_ADD,
                                        &p, NULL,
                                        DBG_UI_DEFAULT_TIMEOUT_MS);
    if (ok && out_id)
        *out_id = p.id;
    return ok;
}


bool dbg_ui_bp_remove(int id)
{
    st_DBGAPI_BP_PARAM p;
    p.addr = 0;
    p.id = id;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_BP_REMOVE,
                                     &p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_bp_update(const st_DBGAPI_BP_UPDATE_PARAM *p)
{
    if (!p) return false;
    /* Const-cast: dispatch handler v UPDATE cestě (allow_create=false)
     * payload jen čte. CREATE cesta (allow_create=true) zapisuje do
     * p->id - tam volá caller dbg_ui_bp_create_with_init(), který
     * dostává non-const pointer. */
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_BP_UPDATE,
                                     (void *)p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_bp_set_enabled(int id, bool enabled)
{
    st_DBGAPI_BP_SET_ENABLED_PARAM p;
    p.id = id;
    p.enabled = enabled;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_BP_SET_ENABLED,
                                     &p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_bp_set_parent(int id, int parent_id)
{
    st_DBGAPI_BP_SET_PARENT_PARAM p;
    p.id = id;
    p.parent_id = parent_id;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_BP_SET_PARENT,
                                     &p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_bp_create_with_init(st_DBGAPI_BP_UPDATE_PARAM *p, int *out_id)
{
    if (!p) return false;
    p->id = -1;  /* CREATE invariant - handler odmítne jinou hodnotu */
    bool ok = dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                        DBGAPI_CMD_BP_CREATE_WITH_INIT,
                                        p, NULL,
                                        DBG_UI_DEFAULT_TIMEOUT_MS);
    if (ok && out_id)
        *out_id = p->id;
    return ok;
}


bool dbg_ui_bpgrp_add(const char *name, int parent, int *out_id)
{
    st_DBGAPI_BPGRP_ADD_PARAM p;
    p.name = name;
    p.parent = parent;
    p.id = -1;
    bool ok = dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                        DBGAPI_CMD_BPGRP_ADD,
                                        &p, NULL,
                                        DBG_UI_DEFAULT_TIMEOUT_MS);
    if (ok && out_id)
        *out_id = p.id;
    return ok;
}


bool dbg_ui_bpgrp_remove(int id)
{
    st_DBGAPI_BPGRP_REMOVE_PARAM p;
    p.id = id;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_BPGRP_REMOVE,
                                     &p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}


bool dbg_ui_bpgrp_update(const st_DBGAPI_BPGRP_UPDATE_PARAM *p)
{
    if (!p) return false;
    return dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
                                     DBGAPI_CMD_BPGRP_UPDATE,
                                     (void *)p, NULL,
                                     DBG_UI_DEFAULT_TIMEOUT_MS);
}

#else /* !MZ800EMU_CFG_DEBUGGER_ENABLED */

/* No-debugger build: pause/run helpery se volají z non-debug UI
 * (snapshot dialogy, topmenu Alt+P). CMDRQ fronta v tomto buildu
 * neexistuje, takže jdeme přímo přes emulator_pause() z UI vlákna -
 * stejný zavedený vzor jako emulator_max_speed() volaný z menu Speed /
 * Alt+M. EMU vlákno respektuje g_emulator.paused i bez debuggeru
 * (mzarch.c má pause wait-loop ve větvi #else). */
#include "dbgapi_helpers.h"
#include "emulator.h"
bool dbg_ui_pause(void)        { emulator_pause(true);  return true; }
bool dbg_ui_run(void)          { emulator_pause(false); return true; }
bool dbg_ui_pause_toggle(void) { emulator_pause(!EMULATOR_TEST_PAUSED); return true; }

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
