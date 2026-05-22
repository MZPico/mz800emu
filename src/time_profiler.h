#ifndef TIME_PROFILER_H
#define TIME_PROFILER_H

#include <glib.h>

typedef struct TimeProfiler TimeProfiler;

#ifdef __cplusplus
extern "C"
{
#endif

    TimeProfiler *time_profiler_init(const gchar *name, guint report_interval_ms);
    void time_profiler_start(TimeProfiler *profiler);
    void time_profiler_stop(TimeProfiler *profiler);

#ifdef __cplusplus
}
#endif

#endif // TIME_PROFILER_H
