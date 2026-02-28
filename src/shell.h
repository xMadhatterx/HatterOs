#ifndef HATTEROS_SHELL_H
#define HATTEROS_SHELL_H

#include <efi.h>
#include "gfx.h"

typedef struct {
    EFI_HANDLE image_handle;
    EFI_SYSTEM_TABLE *st;
    GfxContext *gfx;
    UINTN cursor_col;
    UINTN cursor_row;
    UINTN cols;
    UINTN rows;
    UINTN margin_x;
    UINTN margin_y;
    UINT32 fg_color;
    UINT32 bg_color;
} Shell;

void shell_init(Shell *shell, EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st, GfxContext *gfx);
void shell_run(Shell *shell);
void shell_print(Shell *shell, const char *text);
void shell_println(Shell *shell, const char *text);
void shell_clear(Shell *shell);

#endif
