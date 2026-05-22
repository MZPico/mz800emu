#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "app_thread.h"
#include <stdint.h>
#include <stdbool.h>
#include <glib.h>

#if 0
typedef struct app_main_thread_status_t
{
    app_mutex_t *mutex;
    bool running;
    int error;
} app_main_thread_status_t;

// Struktura pro každé vlákno
typedef struct app_main_thread_t
{
    GThread *thread;
    bool thread_exit_request;
    void *data;
    app_main_thread_status_t *status;
} app_main_thread_t;

// Struktura pro synchronizaci threadu
typedef struct app_main_t
{
    bool app_exit_request;
    app_rwlock_t *running_rwlock;
    app_main_thread_t **tharray;
    int tharray_size;
} app_main_t;

extern app_main_t *g_main_app;

#define main_thread_set_startup_status(status) \
    do                                     \
    {                                      \
        APP_MUTEX_LOCK(status->mutex);      \
        status->running = true;            \
        status->error = 0;                 \
        APP_MUTEX_UNLOCK(status->mutex);    \
    } while (0)

#define main_thread_set_shutdown_status(status, errcode) \
    do                                               \
    {                                                \
        APP_MUTEX_LOCK(status->mutex);                \
        status->error = errcode;                     \
        status->running = false;                     \
        APP_MUTEX_UNLOCK(status->mutex);              \
    } while (0)


#define main_app_set_exit_request(app, value) RWLOCK_WRITE_VAR(app->running_rwlock, app->app_exit_request, value)
#define main_app_get_exit_request(app) RWLOCK_READ_VAR(app->running_rwlock, app->app_exit_request)

#define main_thread_get_exit_request(t) MUTEX_READ_VAR(t->status->mutex, t->thread_exit_request)
#define main_thread_set_exit_request(t, value) MUTEX_WRITE_VAR(t->status->mutex, t->thread_exit_request, value)

#define main_thread_get_running(t) MUTEX_READ_VAR(t->status->mutex, t->status->running)


#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __cplusplus
}
#endif
#endif

#endif // APP_MAIN_H