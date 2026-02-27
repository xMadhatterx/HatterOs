#ifndef HATTEROS_FONT_H
#define HATTEROS_FONT_H

#include <efi.h>
#include "gfx.h"

#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16

void font_draw_char(GfxContext *ctx, UINTN x, UINTN y, char ch, UINT32 fg, UINT32 bg, UINTN scale, BOOLEAN transparent_bg);
void font_draw_text(GfxContext *ctx, UINTN x, UINTN y, const char *text, UINT32 fg, UINT32 bg, UINTN scale, BOOLEAN transparent_bg);
UINTN font_text_width(const char *text, UINTN scale);

#endif