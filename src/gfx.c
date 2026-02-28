#include "gfx.h"

static UINTN abs_diff(UINTN a, UINTN b) {
    return (a > b) ? (a - b) : (b - a);
}

static UINT32 to_native_color(const GfxContext *ctx, UINT32 rgb) {
    UINT32 r = (rgb >> 16) & 0xFF;
    UINT32 g = (rgb >> 8) & 0xFF;
    UINT32 b = rgb & 0xFF;

    if (ctx->pixel_format == PixelRedGreenBlueReserved8BitPerColor) {
        return (b << 16) | (g << 8) | r;
    }

    return (r << 16) | (g << 8) | b;
}

EFI_STATUS gfx_init(EFI_SYSTEM_TABLE *st, GfxContext *ctx, UINTN target_w, UINTN target_h) {
    EFI_STATUS status;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN handle_count = 0;

    status = st->BootServices->LocateHandleBuffer(ByProtocol, &gop_guid, NULL, &handle_count, &handles);
    if (EFI_ERROR(status) || handle_count == 0) {
        return status;
    }

    status = st->BootServices->HandleProtocol(handles[0], &gop_guid, (void **)&ctx->gop);
    st->BootServices->FreePool(handles);
    if (EFI_ERROR(status)) {
        return status;
    }

    UINT32 best_mode = ctx->gop->Mode->Mode;
    UINTN best_score = (UINTN)-1;

    for (UINT32 mode = 0; mode < ctx->gop->Mode->MaxMode; mode++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        UINTN info_size = 0;
        status = ctx->gop->QueryMode(ctx->gop, mode, &info_size, &info);
        if (EFI_ERROR(status)) {
            continue;
        }

        if (info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor &&
            info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor) {
            st->BootServices->FreePool(info);
            continue;
        }

        UINTN score = abs_diff(info->HorizontalResolution, target_w) +
                      abs_diff(info->VerticalResolution, target_h);

        if (score < best_score) {
            best_score = score;
            best_mode = mode;
        }

        st->BootServices->FreePool(info);
    }

    if (best_mode != ctx->gop->Mode->Mode) {
        status = ctx->gop->SetMode(ctx->gop, best_mode);
        if (EFI_ERROR(status)) {
            // Keep running in current mode if preferred mode switch fails.
            status = EFI_SUCCESS;
        }
    }

    ctx->framebuffer_base = ctx->gop->Mode->FrameBufferBase;
    ctx->framebuffer_size = ctx->gop->Mode->FrameBufferSize;
    ctx->framebuffer = (UINT32 *)(UINTN)ctx->framebuffer_base;
    ctx->width = ctx->gop->Mode->Info->HorizontalResolution;
    ctx->height = ctx->gop->Mode->Info->VerticalResolution;
    ctx->pixels_per_scanline = ctx->gop->Mode->Info->PixelsPerScanLine;
    ctx->pixel_format = ctx->gop->Mode->Info->PixelFormat;
    return EFI_SUCCESS;
}

void gfx_put_pixel(GfxContext *ctx, UINTN x, UINTN y, UINT32 color) {
    if (x >= ctx->width || y >= ctx->height) {
        return;
    }
    ctx->framebuffer[y * ctx->pixels_per_scanline + x] = to_native_color(ctx, color);
}

void gfx_clear(GfxContext *ctx, UINT32 color) {
    UINT32 native = to_native_color(ctx, color);
    for (UINTN y = 0; y < ctx->height; y++) {
        UINTN row = y * ctx->pixels_per_scanline;
        for (UINTN x = 0; x < ctx->width; x++) {
            ctx->framebuffer[row + x] = native;
        }
    }
}

void gfx_fill_rect(GfxContext *ctx, UINTN x, UINTN y, UINTN w, UINTN h, UINT32 color) {
    UINTN x_end = x + w;
    UINTN y_end = y + h;
    if (x_end > ctx->width) {
        x_end = ctx->width;
    }
    if (y_end > ctx->height) {
        y_end = ctx->height;
    }

    UINT32 native = to_native_color(ctx, color);
    for (UINTN yy = y; yy < y_end; yy++) {
        UINTN row = yy * ctx->pixels_per_scanline;
        for (UINTN xx = x; xx < x_end; xx++) {
            ctx->framebuffer[row + xx] = native;
        }
    }
}

void gfx_draw_gradient(GfxContext *ctx, UINT32 top_color, UINT32 bottom_color) {
    UINT8 tr = (UINT8)((top_color >> 16) & 0xFF);
    UINT8 tg = (UINT8)((top_color >> 8) & 0xFF);
    UINT8 tb = (UINT8)(top_color & 0xFF);

    UINT8 br = (UINT8)((bottom_color >> 16) & 0xFF);
    UINT8 bg = (UINT8)((bottom_color >> 8) & 0xFF);
    UINT8 bb = (UINT8)(bottom_color & 0xFF);

    UINTN denom = (ctx->height > 1) ? (ctx->height - 1) : 1;

    for (UINTN y = 0; y < ctx->height; y++) {
        UINT8 r = (UINT8)(tr + ((INTN)(br - tr) * (INTN)y) / (INTN)denom);
        UINT8 g = (UINT8)(tg + ((INTN)(bg - tg) * (INTN)y) / (INTN)denom);
        UINT8 b = (UINT8)(tb + ((INTN)(bb - tb) * (INTN)y) / (INTN)denom);
        UINT32 color = ((UINT32)r << 16) | ((UINT32)g << 8) | b;
        UINT32 native = to_native_color(ctx, color);

        UINTN row = y * ctx->pixels_per_scanline;
        for (UINTN x = 0; x < ctx->width; x++) {
            ctx->framebuffer[row + x] = native;
        }
    }
}
