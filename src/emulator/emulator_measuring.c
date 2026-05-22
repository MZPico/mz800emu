#include "main.h"
#ifdef WINDOWS
#include <windows.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <glib.h>
#include <time.h>
#include <math.h>

#include "emulator_measuring.h"
#include "hw-generic/gdg/gdgclk.h"
#include "hw-generic/gdg/video.h"
#include "hw-generic/gdg/gdg.h"

st_EMULATOR_MEASURING g_emulator_measuring;

// Funkce pro porovnání čísel (qsort potřebuje tuto funkci)
static int compare(const void *a, const void *b)
{
    double diff = (*(double *)a - *(double *)b);
    return (diff > 0) - (diff < 0);
}

void emulator_measuring_frame_timing_reset(void)
{
    st_EMULATOR_MEASURING_FRAME_TIMING *ms20 = &g_emulator_measuring.frame_timing;
    ms20->head = 0;
    ms20->tail = 0;
    struct timespec *last_time = &ms20->last_time;
    last_time->tv_sec = 0;
    last_time->tv_nsec = 0;
    ms20->stats_cntr = ms20->stats_predef;
}

static void emulator_measuring_stats(double *samples, uint32_t head, uint32_t tail, uint32_t measuring_width)
{
    uint32_t samples_count = head - tail;
    if (measuring_width < samples_count)
    {
        samples_count = measuring_width;
        tail = head - samples_count;
    };

    // Vyhodnocení měření
    double sum = 0.0, min = samples[tail % MAX_MEASURING_FRAME_TIMING_SAMPLES], max = samples[tail % MAX_MEASURING_FRAME_TIMING_SAMPLES];
    double sum_sq = 0.0; // Pro výpočet směrodatné odchylky

    for (uint32_t i = 0; i < samples_count; i++)
    {
        double val = samples[(tail + i) % MAX_MEASURING_FRAME_TIMING_SAMPLES];
        sum += val;
        sum_sq += val * val;
        if (val < min)
            min = val;
        if (val > max)
            max = val;
    }

    double mean = sum / samples_count;
    double variance = (sum_sq / samples_count) - (mean * mean);
    double stddev = sqrt(variance);

    // Výpočet mediánu
    double sorted[MAX_MEASURING_FRAME_TIMING_SAMPLES];
    for (uint32_t i = 0; i < samples_count; i++)
    {
        sorted[i] = samples[i];
    }
    qsort(sorted, samples_count, sizeof(double), compare);

    double median;
    if (samples_count % 2 == 0)
    {
        median = (sorted[samples_count / 2 - 1] + sorted[samples_count / 2]) / 2.0;
    }
    else
    {
        median = sorted[samples_count / 2];
    };

    st_EMULATOR_MEASURING_FRAME_TIMING_STATS *stats = &g_emulator_measuring.frame_timing.stats;

    stats->mean = mean / 1000;
    stats->min = min / 1000;
    stats->max = max / 1000;
    stats->median = median / 1000;
    stats->stddev = stddev / 1000;
    stats->samples_count = samples_count;
    stats->total_width = (float)samples_count / VIDEO_SCREENS_PER_SEC;
    stats->real_total_width = sum / 1e6;

    if (g_emulator_measuring.frame_timing.console_output_enabled)
    {
        g_print("\n---- Frame timing statistics ----\n");
        g_print(" Average time:       %7.4f ms\n", stats->mean);
        g_print(" Max time:           %7.4f ms\n", stats->max);
        g_print(" Min time:           %7.4f ms\n", stats->min);
        g_print(" Median:             %7.4f ms\n", stats->median);
        g_print(" Standard deviation: %7.4f ms\n", stats->stddev);
        g_print("--------------------------------\n");
        g_print(" Frames count:       %7d\n", stats->samples_count);
        g_print(" Total width:        %7.2f s\n", stats->total_width);
        g_print(" Real total width:   %7.4f s\n\n", stats->real_total_width);
    };
}

void emulator_measuring_frame_timing_event(void)
{
    st_EMULATOR_MEASURING_FRAME_TIMING *ms20 = &g_emulator_measuring.frame_timing;
    struct timespec *last_time = &ms20->last_time;
    double *samples = ms20->samples;
    uint32_t samples_count = ms20->head - ms20->tail;

    struct timespec curr_time;
    clock_gettime(CLOCK_MONOTONIC, &curr_time);

    if (last_time->tv_sec != 0 && last_time->tv_nsec != 0)
    {
        // Výpočet rozdílu času mezi voláními v mikrosekundách
        double delta = (curr_time.tv_sec - last_time->tv_sec) * 1e6 +
                       (curr_time.tv_nsec - last_time->tv_nsec) / 1e3;

        samples[ms20->head % MAX_MEASURING_FRAME_TIMING_SAMPLES] = delta;

        ms20->head++;
        if (samples_count >= MAX_MEASURING_FRAME_TIMING_SAMPLES)
            ms20->tail++;
        ms20->stats_cntr--;
    };

    ms20->last_time.tv_sec = curr_time.tv_sec;
    ms20->last_time.tv_nsec = curr_time.tv_nsec;

    if (ms20->stats_cntr == 0)
    {
        emulator_measuring_stats(samples, ms20->head, ms20->tail, ms20->measuring_width);
        ms20->stats_cntr = ms20->stats_predef; // Resetujeme čítač
    };
}

static char *format_uint32_with_thousands(uint32_t number)
{
    GString *str0 = g_string_new(NULL);
    GString *str1 = g_string_new(NULL);
    g_string_printf(str0, "%u", number);
    g_string_printf(str1, "%'u", number);

    if (!g_string_equal(str0, str1))
    {
        // Porovnejme znak po znaku a zjistíme oddělovač
        for (size_t i = 0; i < str1->len; i++)
        {
            // Znak, který není číslem, je pravděpodobně oddělovač
            if (!g_ascii_isdigit(str1->str[i]))
            {
                char separator = str1->str[i]; // Oddělovač nalezen

                // Nahrazení oddělovače mezerou
                for (size_t j = 0; j < str1->len; j++)
                {
                    if (str1->str[j] == separator)
                    {
                        str1->str[j] = ' ';
                    }
                }
                break;
            };
        };
    };

    g_string_free(str0, TRUE);
    return g_string_free(str1, FALSE);
}

void emulator_measuring_gdg_event(st_EMULATOR_MEASURING_GDG *msgdg)
{
    if (msgdg->enabled)
    {
        g_rw_lock_writer_lock(&msgdg->rwlock);
    };

    if (!msgdg->timer)
    {
        msgdg->timer = g_timer_new();
        g_timer_start(msgdg->timer);
    };

    g_timer_stop(msgdg->timer);
    // cas v sekundach od posledniho zastaveni
    msgdg->elapsed_time = (float)g_timer_elapsed(msgdg->timer, NULL);
    g_timer_start(msgdg->timer);

    uint64_t now_gdg_ticks = gdg_compute_total_ticks();
    uint64_t elapsed_gdg_ticks = now_gdg_ticks - msgdg->last_gdg_ticks;
    msgdg->last_gdg_ticks = now_gdg_ticks;

    msgdg->current_gdg_frequency = elapsed_gdg_ticks / msgdg->elapsed_time;
    msgdg->current_fps = (elapsed_gdg_ticks / VIDEO_SCREEN_TICKS) / msgdg->elapsed_time;
    msgdg->current_gdg_frequency_percent = (msgdg->current_gdg_frequency / GDGCLK_REAL_BASE) * 100;

    if (msgdg->enabled)
    {
        g_rw_lock_writer_unlock(&msgdg->rwlock);
    };

    if (msgdg->console_output_enabled)
    {
        float native_fps = (float)GDGCLK_REAL_BASE / VIDEO_SCREEN_TICKS;
        float simulated_fps = (float)GDGCLK_BASE / VIDEO_SCREEN_TICKS;

        char *formatted_num = NULL;
        printf("\n\nGDG meassuring:\n");
        printf(" - Measured time: %f s\n", msgdg->elapsed_time);

        formatted_num = format_uint32_with_thousands(GDGCLK_REAL_BASE);
        printf(" - Native GDG freq.: %s Hz\n", formatted_num);
        g_free(formatted_num);

        printf(" - Native FPS: %.4f\n", native_fps);

        formatted_num = format_uint32_with_thousands(GDGCLK_BASE);
        printf(" - Simulated GDG freq.: %s Hz\n", formatted_num);
        g_free(formatted_num);

        printf(" - Simulated FPS: %.4f\n", simulated_fps);

        formatted_num = format_uint32_with_thousands(msgdg->current_gdg_frequency);
        printf(" - Current GDG freq.: %s Hz\n", formatted_num);
        g_free(formatted_num);

        printf(" - Current FPS: %.4f\n", msgdg->current_fps);
        printf(" - Current GDG freq. in %%: %.4f %%\n", msgdg->current_gdg_frequency_percent);
    };
}

void emulator_measuring_gdg_init(st_EMULATOR_MEASURING_GDG *msgdg)
{
    msgdg->enabled = false;
    msgdg->console_output_enabled = false;
    msgdg->timer = NULL;
    msgdg->last_gdg_ticks = 0;
    msgdg->elapsed_time = 0.0;
    msgdg->current_gdg_frequency = 0.0;
    msgdg->current_fps = 0.0;
    msgdg->current_gdg_frequency_percent = 0.0;
    msgdg->thread = NULL;
    g_rw_lock_init(&msgdg->rwlock);
    g_mutex_init(&msgdg->mutex);
    g_cond_init(&msgdg->cond);
    msgdg->thread_usleep_time = EMULATOR_MEASURING_DEFAULT_GDG_UPDATE_SEC * 1000000;
}

void emulator_measuring_gdg_exit(st_EMULATOR_MEASURING_GDG *msgdg)
{
    msgdg->enabled = false;

    if (msgdg->thread)
    {
        g_print("Stopping GDG measuring thread...\n");

        g_mutex_lock(&msgdg->mutex);
        msgdg->enabled = false;
        g_cond_signal(&msgdg->cond);
        g_mutex_unlock(&msgdg->mutex);

        g_thread_join(msgdg->thread);
        msgdg->thread = NULL;
    };

    if (msgdg->timer)
    {
        g_timer_stop(msgdg->timer);
        g_timer_destroy(msgdg->timer);
        msgdg->timer = NULL;
    };

    g_rw_lock_clear(&msgdg->rwlock);
    g_mutex_clear(&msgdg->mutex);
    g_cond_clear(&msgdg->cond);
}

void emulator_measuring_init(void)
{
    st_EMULATOR_MEASURING_FRAME_TIMING *ms20 = &g_emulator_measuring.frame_timing;
    ms20->enabled = false;
    ms20->console_output_enabled = false;
    ms20->stats_predef = PREDEF_MEASURING_FRAME_TIMING;
    ms20->measuring_width = MAX_MEASURING_FRAME_TIMING_SAMPLES;
    emulator_measuring_frame_timing_reset();

    st_EMULATOR_MEASURING_GDG *msgdg = &g_emulator_measuring.gdg;
    emulator_measuring_gdg_init(msgdg);
}

void emulator_measuring_exit(void)
{
    st_EMULATOR_MEASURING_GDG *msgdg = &g_emulator_measuring.gdg;
    emulator_measuring_gdg_exit(msgdg);
}

gpointer emulator_measuring_gdg_thread(st_EMULATOR_MEASURING_GDG *msgdg)
{
    while (msgdg->enabled)
    {
        emulator_measuring_gdg_event(msgdg);
        g_mutex_lock(&msgdg->mutex);
        // cekame na signal s timeoutem thread_usleep_time
        g_cond_wait_until(&msgdg->cond, &msgdg->mutex, g_get_monotonic_time() + msgdg->thread_usleep_time);
        g_mutex_unlock(&msgdg->mutex);
    };
    return NULL;
}

bool emulator_measuring_gdg_set_enabled(st_EMULATOR_MEASURING_GDG *msgdg, bool enabled, int update_time_sec)
{
    if ((enabled == true) && ((update_time_sec < 1) || (update_time_sec > 10)))
    {
        SDLAPP_ERROR("Invalid parameters for GDG measuring!");
        return false;
    };

    if (enabled == false)
    {
        if (!msgdg->thread)
            return true;

        g_print("Stopping GDG measuring thread...\n");

        g_mutex_lock(&msgdg->mutex);
        msgdg->enabled = false;
        g_cond_signal(&msgdg->cond);
        g_mutex_unlock(&msgdg->mutex);

        g_thread_join(msgdg->thread);
        msgdg->thread = NULL;
        return true;
    };

    if ((msgdg->enabled == false) && (msgdg->thread))
    {
        g_print("Stopping previous GDG measuring thread...\n");

        g_mutex_lock(&msgdg->mutex);
        msgdg->enabled = false;
        g_cond_signal(&msgdg->cond);
        g_mutex_unlock(&msgdg->mutex);

        g_thread_join(msgdg->thread);
        msgdg->thread = NULL;
    };

    g_mutex_lock(&msgdg->mutex);
    msgdg->enabled = true;
    msgdg->thread_usleep_time = update_time_sec * 1000000;
    g_cond_signal(&msgdg->cond);
    g_mutex_unlock(&msgdg->mutex);

    if (!msgdg->thread)
    {
        msgdg->thread = g_thread_new("emulator_measuring_gdg_thread", (GThreadFunc)emulator_measuring_gdg_thread, msgdg);
        if (!msgdg->thread)
        {
            g_print("Failed to start GDG measuring thread!\n");
            msgdg->enabled = false;
            return false;
        };
    };
    return true;
}
