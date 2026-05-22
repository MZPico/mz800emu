/**
 * @file time_profiler.c
 * @brief Modul pro přesné měření času v aplikaci.
 *
 * Tento kód umožňuje vytvářet nezávislé časovače a měřit délku vybraných úseků.
 * Používá monotónní čas z glib2 (`g_get_monotonic_time()`).
 *
 * @author Michal Hučík
 * @date 2025
 */

#include <glib.h>
#include <stdlib.h>
#include <math.h>

typedef struct
{
    gchar *name;             ///< Název profileru
    gint64 *durations;       ///< Pole zaznamenaných časů
    gsize count;             ///< Počet měření
    gsize capacity;          ///< Velikost pole
    gint64 report_interval;  ///< Interval reportu (v mikrosekundách)
    gint64 start_time;       ///< Čas začátku měření
    gint64 last_report_time; ///< Čas posledního reportu
} TimeProfiler;

/**
 * @brief Inicializuje nový profiler se jménem.
 *
 * @param name Jméno profileru (kopírováno)
 * @param report_interval_ms Interval reportu (v ms)
 * @return Ukazatel na nový profiler
 */
TimeProfiler *time_profiler_init(const gchar *name, guint report_interval_ms)
{
    TimeProfiler *profiler = g_new0(TimeProfiler, 1);
    if (name)
    {
        profiler->name = g_strdup(name);
    }
    else
    {
        profiler->name = g_strdup("TimeProfiler");
    };
    profiler->capacity = 1000;
    profiler->durations = g_new0(gint64, profiler->capacity);
    profiler->count = 0;
    profiler->report_interval = report_interval_ms * 1000; // ms → us
    profiler->start_time = 0;
    profiler->last_report_time = g_get_monotonic_time(); // Nastavíme začátek měření
    g_print("Time Profiler '%s' initialized, report interval: %u ms\n", profiler->name, report_interval_ms);
    return profiler;
}

/**
 * @brief Začne měření časového úseku.
 *
 * @param profiler Ukazatel na profiler
 */
void time_profiler_start(TimeProfiler *profiler)
{
    if (!profiler)
        return;
    profiler->start_time = g_get_monotonic_time();
}

/**
 * @brief Funkce pro porovnání dvou `gint64` pro `g_sort_array()`
 */
static gint int64_compare(gconstpointer a, gconstpointer b, gpointer user_data)
{
    (void)user_data;
    gint64 va = *(gint64 *)a;
    gint64 vb = *(gint64 *)b;
    return (va > vb) - (va < vb);
}

/**
 * @brief Ukončí měření úseku a uloží výsledek.
 *
 * @param profiler Ukazatel na profiler
 */
void time_profiler_stop(TimeProfiler *profiler)
{
    if (!profiler || profiler->start_time == 0)
        return;

    gint64 now = g_get_monotonic_time();
    gint64 duration = now - profiler->start_time;
    profiler->start_time = 0; // Reset

    // Uložení do pole
    if (profiler->count >= profiler->capacity)
    {
        profiler->capacity *= 2;
        profiler->durations = g_renew(gint64, profiler->durations, profiler->capacity);
    }
    profiler->durations[profiler->count++] = duration;

    // Ověříme, jestli uplynul interval od posledního reportu
    if (now - profiler->last_report_time >= profiler->report_interval)
    {
        gsize i;
        gint64 min = G_MAXINT64, max = 0, sum = 0;
        gdouble mean, variance = 0, stddev, median;

        // Kopie dat pro medián
        gint64 *sorted = g_memdup2(profiler->durations, profiler->count * sizeof(gint64));

#if GLIB_CHECK_VERSION(2, 82, 0)
        // V MSYS2 máme verzi 2.82.4
        g_sort_array(sorted, profiler->count, sizeof(gint64), int64_compare, NULL);
#else
        // v Linuxu mam verzi 2.80.0
        g_qsort_with_data(sorted, profiler->count, sizeof(gint64), int64_compare, NULL);
#endif

        // Výpočet sumy, min, max
        for (i = 0; i < profiler->count; i++)
        {
            sum += profiler->durations[i];
            if (profiler->durations[i] < min)
                min = profiler->durations[i];
            if (profiler->durations[i] > max)
                max = profiler->durations[i];
        }

        // Průměr
        mean = (gdouble)sum / profiler->count;

        // Medián
        if (profiler->count % 2 == 0)
            median = (sorted[profiler->count / 2 - 1] + sorted[profiler->count / 2]) / 2.0;
        else
            median = sorted[profiler->count / 2];

        // Směrodatná odchylka
        for (i = 0; i < profiler->count; i++)
        {
            variance += pow(profiler->durations[i] - mean, 2);
        }
        variance /= profiler->count;
        stddev = sqrt(variance);

        // Výpis výsledků (milisekundy)
        g_print("\n--- Time Profiler Report: %s ---\n", profiler->name);
        g_print("  Total time: %10.3f ms\n", sum / 1000.0);
        g_print("  Mean:       %10.3f ms\n", mean / 1000.0);
        g_print("  Median:     %10.3f ms\n", median / 1000.0);
        g_print("  Stddev:     %10.3f ms\n", stddev / 1000.0);
        g_print("  Min:        %10.3f ms\n", min / 1000.0);
        g_print("  Max:        %10.3f ms\n", max / 1000.0);
        g_print("  Samples:    %10zu\n", profiler->count);
        g_print("---------------------------\n\n");

        // Uvolnění paměti a reset
        g_free(sorted);
        profiler->count = 0;
        profiler->last_report_time = now; // Resetujeme čas posledního reportu
    }
}

/**
 * @brief Uvolní paměť alokovanou profilerem.
 *
 * @param profiler Ukazatel na profiler
 */
void time_profiler_free(TimeProfiler *profiler)
{
    if (!profiler)
        return;
    g_free(profiler->durations);
    g_free(profiler);
}
