#ifndef IMGUI_MESSAGE_WINDOW_H
#define IMGUI_MESSAGE_WINDOW_H

#include <stdarg.h>

typedef enum ui_message_type_t
{
    MESSAGE_TYPE_INFO = 0,
    MESSAGE_TYPE_ERROR,
    MESSAGE_TYPE_WARNING,
} ui_message_type_t;

#ifdef __cplusplus
extern "C"
{
#endif
    void imgui_message_create(ui_message_type_t type, const char *message);
    void imgui_message_vprintf(ui_message_type_t type, const char *format, va_list args);
    void imgui_message_window(void);
#ifdef __cplusplus
}
#endif

#endif /* IMGUI_MESSAGE_WINDOW_H */
