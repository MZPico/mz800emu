#ifndef IMGUI_CMT_H
#define IMGUI_CMT_H

#ifdef __cplusplus
extern "C"
{
#endif

    void imgui_cmt(bool *p_open);
    void imgui_cmt_fix_mzfsize_popup(bool *p_open);
    void imgui_cmt_fix_mzfsize_popup_activate(const char *filename, int valid_size, int file_size);
    void imgui_cmt_tape_index_window(bool *p_open);
    void imgui_cmt_tape_update_filelist(void);

#ifdef __cplusplus
}
#endif

#endif /* IMGUI_CMT_H */
