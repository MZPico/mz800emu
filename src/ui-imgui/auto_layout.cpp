/*
 * auto_layout.cpp - Master auto-layout pro ImGui okna při prvním otevření.
 *
 * Viz auto_layout.h pro kontrakty a popis algoritmu.
 *
 * Debug log: env DBG_AUTO_LAYOUT=1 zapne podrobný stdout výpis (rozměry
 * monitor / viewport, list occupied oken, výsledná pozice). Default vypnuto.
 *
 * Licence: GPLv3
 */

#include "libs/imgui/imgui.h"
#include "libs/imgui/imgui_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "auto_layout.h"


/* Debug log gated na env DBG_AUTO_LAYOUT. Inicializace lazy při prvním
 * volání - getenv() je C runtime, nevolat předčasně. */
static bool s_al_log_enabled = false;
static bool s_al_log_init    = false;

static void al_log_init ( void )
{
    if ( s_al_log_init ) return;
    s_al_log_init = true;
    const char *e = getenv ( "DBG_AUTO_LAYOUT" );
    s_al_log_enabled = ( e && e[ 0 ] && strcmp ( e, "0" ) != 0 );
}

#define AL_LOG(...) do { \
    if ( s_al_log_enabled ) { \
        printf ( __VA_ARGS__ ); \
        fflush ( stdout ); \
    } \
} while ( 0 )


/* Memo: jména oken, pro která jsme už auto-layout aplikovali v této
 * session. Druhé volání pro stejné okno (= další frame, nebo user-toggle
 * close/reopen) skip - ImGui FirstUseEver má saved settings, my bychom
 * stejně nic nepřepsali. Eliminuje per-frame log spam. */
static std::set<std::string> s_al_applied;

/* Rezervace slotů přidělených v current frame. Když se víc oken otevírá
 * ve stejném frame (např. dbg_workplace_apply zapíná Workplace okna naráz),
 * ImGui WasActive flag těchto oken je v current frame stále false (Begin
 * jim ho zaktivuje až po našem auto-layout call). Bez této rezervace by
 * každé okno vidělo "occupied = 0" a dostalo stejnou first-free pozici.
 * Resetujeme když ImGui::GetFrameCount() se posune. */
static std::vector<ImRect> s_al_frame_slots;
static int s_al_frame_slots_frame = -1;


/**
 * @brief Test zda název okna patří mezi vyloučená (= jejich pos/size se
 *        při hledání volného místa ignoruje).
 *
 * Sem patří hlavní emulátor okno (vyplňuje celý viewport, jinak by žádné
 * místo nebylo volné a algoritmus by skončil v cascade fallback), overlay
 * okno a interní ImGui debugovací okna.
 */
static bool al_is_excluded_name ( const char *name )
{
    if ( !name ) return true;
    if ( strstr ( name, "##MainEmulatorWindow" ) ) return true;
    if ( strstr ( name, "##OverlayWindow" ) ) return true;
    /* ImGui DockSpace pomocná okna - neměla by tu být, ale safety net. */
    if ( strstr ( name, "DockSpaceViewport" ) ) return true;
    return false;
}


/**
 * @brief Posbírá obdélníky aktuálně viditelných ImGui oken do `out`.
 *
 * Filtr: aktivní (WasActive && !Hidden), bez NoSavedSettings (= zahazuje
 * tooltipy), bez ChildWindow / Tooltip / Popup, jméno se neshoduje se
 * `self_name`, jméno není ve vyloučeném seznamu.
 */
static void al_collect_occupied ( const char *self_name,
                                   std::vector<ImRect> &out )
{
    ImGuiContext *ctx = ImGui::GetCurrentContext ();
    if ( !ctx ) return;

    for ( int i = 0; i < ctx->Windows.Size; i++ ) {
        ImGuiWindow *w = ctx->Windows[ i ];
        if ( !w ) continue;
        if ( !w->WasActive ) continue;
        if ( w->Hidden ) continue;

        ImGuiWindowFlags f = w->Flags;
        if ( f & ImGuiWindowFlags_NoSavedSettings ) continue;
        if ( f & ImGuiWindowFlags_ChildWindow ) continue;
        if ( f & ImGuiWindowFlags_Tooltip ) continue;
        if ( f & ImGuiWindowFlags_Popup ) continue;

        if ( al_is_excluded_name ( w->Name ) ) continue;
        if ( self_name && w->Name && strcmp ( w->Name, self_name ) == 0 ) continue;

        ImRect r ( w->Pos, ImVec2 ( w->Pos.x + w->Size.x, w->Pos.y + w->Size.y ) );
        out.push_back ( r );
        AL_LOG ( "[auto_layout]     [occupied] '%s' pos=(%.0f,%.0f) size=(%.0fx%.0f)\n",
                 w->Name, w->Pos.x, w->Pos.y, w->Size.x, w->Size.y );
    }
}


/**
 * @brief Test překryvu obdélníku `cand` s libovolným ze seznamu.
 */
static bool al_overlaps_any ( const ImRect &cand,
                               const std::vector<ImRect> &occupied )
{
    for ( size_t i = 0; i < occupied.size (); i++ ) {
        if ( cand.Overlaps ( occupied[ i ] ) ) return true;
    }
    return false;
}


/**
 * @brief Najde volnou pozici pro okno o velikosti (w,h).
 *
 * Reference area = primární fyzický monitor (ne main viewport - viewport
 * je velikost SDL hlavního okna, na 4K monitoru typicky výrazně menší).
 * Algoritmus využívá ImGui multi-viewport - okna mohou plout mimo SDL
 * main window do volné plochy monitoru jako samostatná platform okna.
 *
 * Algoritmus: **side-preferred grid scan with proximity score**.
 *  1. Body kandidátní pozice = grid přes celý monitor s krokem `step`.
 *  2. Vyloučí body kde okno by překrylo SDL emu viewport rect nebo existující
 *     okno (`occupied`).
 *  3. Z validních bodů zvolí ten s nejnižším score = vážená vzdálenost od
 *     hran viewportu. Vertikální offset (= okno NAD nebo POD emu) má 3x
 *     vyšší penalty než horizontální offset (= okno VLEVO/VPRAVO od emu).
 *     Důsledek: okna se preferenčně zarovnávají podél boků emu okna
 *     a využívají jeho výšku, místo zaplňování stripu nad/pod emu.
 *     Rovnost rozhodne deterministicky scan order (zleva doprava, shora dolů).
 *  4. Cascade fallback uvnitř main viewportu pokud žádný validní bod
 *     nenajde - okno přes emu screen, akceptovatelné když monitor
 *     je plný (vícero oken, fullscreen SDL na single monitor).
 *
 * Krok 30 px = ~128×69 bodů na 4K monitoru, ~70k overlap testů per
 * placement při ~8 existujících oknech. Auto-layout je volán jednou per
 * okno per session (memo skip), tj. amortized cost zanedbatelný.
 */
static ImVec2 al_find_free_slot ( float w, float h,
                                   const std::vector<ImRect> &occupied )
{
    ImGuiViewport *vp = ImGui::GetMainViewport ();

    /* Primární monitor - work area (mimo taskbar). Fallback na viewport
     * size pokud platform monitors nejsou k dispozici (nemělo by nastat
     * v ImGui multi-viewport módu, jen defenzivně). */
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO ();
    float ml, mt, mr, mb;
    if ( pio.Monitors.Size > 0 ) {
        ImGuiPlatformMonitor &mon = pio.Monitors[ 0 ];
        ml = mon.WorkPos.x;
        mt = mon.WorkPos.y;
        mr = ml + mon.WorkSize.x;
        mb = mt + mon.WorkSize.y;
    } else {
        ml = vp->WorkPos.x;
        mt = vp->WorkPos.y;
        mr = ml + vp->WorkSize.x;
        mb = mt + vp->WorkSize.y;
    }
    AL_LOG ( "[auto_layout]   monitor area: (%.0f,%.0f) - (%.0f,%.0f)\n",
             ml, mt, mr, mb );

    /* SDL emulator main window rect - okna se sem snaží neumísťovat. */
    ImRect vp_rect ( vp->WorkPos,
                     ImVec2 ( vp->WorkPos.x + vp->WorkSize.x,
                              vp->WorkPos.y + vp->WorkSize.y ) );
    AL_LOG ( "[auto_layout]   viewport rect: (%.0f,%.0f) - (%.0f,%.0f)\n",
             vp_rect.Min.x, vp_rect.Min.y, vp_rect.Max.x, vp_rect.Max.y );

    const float step = 30.0f;

    /* Score function: vážený součet horizontální a vertikální vzdálenosti
     * kandidátu od nejbližší hrany viewportu. Vertikální offset (= okno je
     * nad nebo pod emu) má 3x vyšší penalty, takže algoritmus preferenčně
     * zarovnává okna podél levého/pravého boku emu (využije plné výšky emu)
     * před stripy nad/pod (= malá výška, většinou neideální). */
    const float vert_penalty = 3.0f;

    /* Grid scan zleva doprava, shora dolů. Sledujeme best kandidát s
     * minimálním score. Rovnost: zachová se první nalezený (= leftmost,
     * topmost) díky striktnímu '<' compare. */
    float best_score = -1.0f;
    ImVec2 best_pos ( 0.0f, 0.0f );
    bool found = false;

    for ( float y = mt; y + h <= mb; y += step ) {
        for ( float x = ml; x + w <= mr; x += step ) {
            ImRect cand ( ImVec2 ( x, y ), ImVec2 ( x + w, y + h ) );

            /* Vyloučit pozice překrývající SDL emu viewport. */
            if ( cand.Overlaps ( vp_rect ) ) continue;

            /* Vyloučit pozice překrývající existující okna. */
            if ( al_overlaps_any ( cand, occupied ) ) continue;

            /* Horizontální offset - jak daleko vlevo/vpravo od emu. 0 pokud
             * cand X range překrývá emu X range (= cand je nad/pod emu). */
            float x_off = 0.0f;
            if ( cand.Max.x <= vp_rect.Min.x ) {
                x_off = vp_rect.Min.x - cand.Max.x;
            } else if ( cand.Min.x >= vp_rect.Max.x ) {
                x_off = cand.Min.x - vp_rect.Max.x;
            }

            /* Vertikální offset - jak daleko nad/pod emu. */
            float y_off = 0.0f;
            if ( cand.Max.y <= vp_rect.Min.y ) {
                y_off = vp_rect.Min.y - cand.Max.y;
            } else if ( cand.Min.y >= vp_rect.Max.y ) {
                y_off = cand.Min.y - vp_rect.Max.y;
            }

            float score = x_off + y_off * vert_penalty;

            if ( !found || score < best_score ) {
                found = true;
                best_score = score;
                best_pos = ImVec2 ( x, y );
            }
        }
    }

    if ( found ) {
        AL_LOG ( "[auto_layout]   => placed at (%.0f,%.0f) score=%.0f\n",
                 best_pos.x, best_pos.y, best_score );
        return best_pos;
    }

    /* Cascade fallback - middle uvnitř main viewportu (= okno přes emu
     * screen, akceptovatelné když monitor nemá volné místo kolem). */
    int n = (int) occupied.size ();
    ImVec2 cascade ( vp->WorkPos.x + 80.0f + (float) n * 30.0f,
                     vp->WorkPos.y + 80.0f + (float) n * 30.0f );
    AL_LOG ( "[auto_layout]   => CASCADE FALLBACK at (%.0f,%.0f)\n",
             cascade.x, cascade.y );
    return cascade;
}


extern "C" void auto_layout_first_use_portrait ( const char *window_name,
                                                  float pref_w, float pref_h )
{
    al_log_init ();

    /* Skip pokud jsme pro toto okno už auto-layout aplikovali (= další
     * frame, ImGui má our pos uloženou). Eliminuje per-frame logspam. */
    if ( !window_name ) return;
    if ( s_al_applied.count ( window_name ) > 0 ) return;
    s_al_applied.insert ( window_name );

    AL_LOG ( "[auto_layout] === request '%s' (pref %.0fx%.0f) ===\n",
             window_name, pref_w, pref_h );

    std::vector<ImRect> occupied;
    al_collect_occupied ( window_name, occupied );

    /* Reset frame-slots list pokud jsme v novém frame. */
    int curr_frame = ImGui::GetFrameCount ();
    if ( curr_frame != s_al_frame_slots_frame ) {
        s_al_frame_slots.clear ();
        s_al_frame_slots_frame = curr_frame;
    }
    /* Přidat sloty rezervované v tomto frame jinými okny (která ještě
     * nedoběhla Begin a tak nemají WasActive). */
    for ( size_t i = 0; i < s_al_frame_slots.size (); i++ ) {
        occupied.push_back ( s_al_frame_slots[ i ] );
        AL_LOG ( "[auto_layout]     [reserved frame-slot %zu] pos=(%.0f,%.0f) size=(%.0fx%.0f)\n",
                 i,
                 s_al_frame_slots[ i ].Min.x, s_al_frame_slots[ i ].Min.y,
                 s_al_frame_slots[ i ].Max.x - s_al_frame_slots[ i ].Min.x,
                 s_al_frame_slots[ i ].Max.y - s_al_frame_slots[ i ].Min.y );
    }

    ImVec2 pos = al_find_free_slot ( pref_w, pref_h, occupied );

    /* Rezervovat slot pro další volání ve stejném frame. */
    s_al_frame_slots.push_back ( ImRect ( pos,
        ImVec2 ( pos.x + pref_w, pos.y + pref_h ) ) );

    ImGui::SetNextWindowPos ( pos, ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize ( ImVec2 ( pref_w, pref_h ), ImGuiCond_FirstUseEver );
}


extern "C" void auto_layout_first_use_near_mouse ( const char *window_name,
                                                    float pref_w, float pref_h )
{
    al_log_init ();

    if ( !window_name ) return;
    if ( s_al_applied.count ( window_name ) > 0 ) return;
    s_al_applied.insert ( window_name );

    ImGuiViewport *vp = ImGui::GetMainViewport ();
    ImVec2 mp = ImGui::GetMousePos ();

    float vl = vp->WorkPos.x;
    float vt = vp->WorkPos.y;
    float vr = vl + vp->WorkSize.x;
    float vb = vt + vp->WorkSize.y;

    float x = mp.x - pref_w * 0.5f;
    float y = mp.y - pref_h * 0.5f;
    if ( x < vl ) x = vl;
    if ( y < vt ) y = vt;
    if ( x + pref_w > vr ) x = vr - pref_w;
    if ( y + pref_h > vb ) y = vb - pref_h;

    AL_LOG ( "[auto_layout] near-mouse '%s' mouse=(%.0f,%.0f) => placed at (%.0f,%.0f) size (%.0fx%.0f)\n",
             window_name, mp.x, mp.y, x, y, pref_w, pref_h );

    ImGui::SetNextWindowPos ( ImVec2 ( x, y ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize ( ImVec2 ( pref_w, pref_h ), ImGuiCond_FirstUseEver );
}
