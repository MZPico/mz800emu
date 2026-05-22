#ifndef BASEUI_H
#define BASEUI_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

void baseui_show_message(bool is_error, char *format, ...);
void baseui_show_error_message(char *format, ...);

// TODO: Tohle makro by melo postupne nahradit vsechny fprintf(stderr, ...) volani
#define baseui_error(format, ...) \
    baseui_show_error_message("%s():%d ERROR - " format, __func__, __LINE__, ##__VA_ARGS__)

void baseui_cmt_check_mzf_filesize(const char *filename, int valid_size);

//char *baseui_filechhoser_open_file(char *last_file, const char *filter);
// char *baseui_filechhoser_save_file(char *last_file, const char *filter);
// char *baseui_filechhoser_open_directory(char *last_directory);

#include "baseui_tools.h"

#endif