#ifndef IFACE_H
#define IFACE_H

#include "main.h"
#include <stdbool.h>
#include "app/app_thread.h"

#ifdef __cplusplus
extern "C"
{
#endif
    extern bool iface_init(void);
    extern void iface_exit(void);
#ifdef __cplusplus
}
#endif

#endif // IFACE_H
