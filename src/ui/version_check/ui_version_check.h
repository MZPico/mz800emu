#ifndef UI_VERSION_CHECK_H
#define UI_VERSION_CHECK_H

#ifdef __cplusplus
extern "C"
{
#endif

    const char *ui_version_check_get_url_by_tag(const char *tag);
    char *ui_version_check_create_version_string(guint32 version);

#ifdef __cplusplus
}
#endif

#endif /* UI_VERSION_CHECK_H */
