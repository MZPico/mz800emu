/* myimgui.h - definitions for My ImGui */
#ifndef IMGUI_IMAGES_H
#define IMGUI_IMAGES_H

#include <SDL3/SDL_opengl.h>
#include <stdbool.h>

typedef struct ui_glimage_t
{
    char *name;
    GLuint texture;
    int width;
    int height;
} ui_glimage_t;

#ifdef __cplusplus
extern "C"
{
#endif
    bool imgui_images_load_all(void);
    ui_glimage_t *imgui_images_append(const char *name, const char *fullpath);
    void imgui_images_destroy_all(void);
    ui_glimage_t *imgui_images_get_image_by_name(const char *name);
#ifdef __cplusplus
}
#endif

#endif /* IMGUI_IMAGES_H */
