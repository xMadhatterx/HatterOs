#ifndef HATTEROS_GFX_H
#define HATTEROS_GFX_H

#include <efi.h>

typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    UINT32 *framebuffer;
    EFI_PHYSICAL_ADDRESS framebuffer_base;
    UINTN framebuffer_size;
    UINTN width;
    UINTN height;
    UINTN pixels_per_scanline;
    EFI_GRAPHICS_PIXEL_FORMAT pixel_format;
} GfxContext;

EFI_STATUS gfx_init(EFI_SYSTEM_TABLE *st, GfxContext *ctx, UINTN target_w, UINTN target_h);
void gfx_clear(GfxContext *ctx, UINT32 color);
void gfx_draw_gradient(GfxContext *ctx, UINT32 top_color, UINT32 bottom_color);
void gfx_put_pixel(GfxContext *ctx, UINTN x, UINTN y, UINT32 color);
void gfx_fill_rect(GfxContext *ctx, UINTN x, UINTN y, UINTN w, UINTN h, UINT32 color);

#endif
