#include <efi.h>
#include <efilib.h>

#include "gfx.h"
#include "font.h"
#include "shell.h"

#define SPLASH_BMP_PATH L"\\EFI\\BOOT\\SPLASH.BMP"

static UINT16 read_le16(const UINT8 *p) {
    return (UINT16)(p[0] | ((UINT16)p[1] << 8));
}

static UINT32 read_le32(const UINT8 *p) {
    return (UINT32)(p[0] |
                    ((UINT32)p[1] << 8) |
                    ((UINT32)p[2] << 16) |
                    ((UINT32)p[3] << 24));
}

// Small fallback path for text output when graphics setup fails.
static void uefi_text(EFI_SYSTEM_TABLE *st, CHAR16 *msg) {
    if (st && st->ConOut) {
        uefi_call_wrapper(st->ConOut->OutputString, 2, st->ConOut, msg);
    }
}

// Open the ESP root on the same device this EFI app was loaded from.
static EFI_STATUS open_esp_root(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st, EFI_FILE_PROTOCOL **root) {
    if (root == NULL || st == NULL || st->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *root = NULL;

    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;

    EFI_STATUS status = uefi_call_wrapper(
        st->BootServices->HandleProtocol,
        3,
        image_handle,
        &loaded_image_guid,
        (void **)&loaded
    );
    if (EFI_ERROR(status) || loaded == NULL) {
        return EFI_NOT_FOUND;
    }

    status = uefi_call_wrapper(
        st->BootServices->HandleProtocol,
        3,
        loaded->DeviceHandle,
        &sfs_guid,
        (void **)&fs
    );
    if (EFI_ERROR(status) || fs == NULL) {
        return EFI_NOT_FOUND;
    }

    return uefi_call_wrapper(fs->OpenVolume, 2, fs, root);
}

// Load a file from the ESP into boot-service pool memory.
static EFI_STATUS read_file_from_esp(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *st,
    const CHAR16 *path,
    UINT8 **out_data,
    UINTN *out_size
) {
    if (path == NULL || out_data == NULL || out_size == NULL || st == NULL || st->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    *out_data = NULL;
    *out_size = 0;

    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINT8 *data = NULL;
    EFI_STATUS status = open_esp_root(image_handle, st, &root);
    if (EFI_ERROR(status) || root == NULL) {
        goto out;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || file == NULL) {
        goto out;
    }

    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    UINTN info_size = 0;
    status = uefi_call_wrapper(file->GetInfo, 4, file, &file_info_guid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < SIZE_OF_EFI_FILE_INFO) {
        status = EFI_LOAD_ERROR;
        goto out;
    }

    status = uefi_call_wrapper(st->BootServices->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
    if (EFI_ERROR(status) || info == NULL) {
        status = EFI_OUT_OF_RESOURCES;
        goto out;
    }

    status = uefi_call_wrapper(file->GetInfo, 4, file, &file_info_guid, &info_size, info);
    if (EFI_ERROR(status)) {
        goto out;
    }
    if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) {
        status = EFI_INVALID_PARAMETER;
        goto out;
    }

    if (info->FileSize == 0 || info->FileSize > 32U * 1024U * 1024U) {
        status = EFI_BAD_BUFFER_SIZE;
        goto out;
    }

    UINTN file_size = (UINTN)info->FileSize;
    status = uefi_call_wrapper(st->BootServices->AllocatePool, 3, EfiLoaderData, file_size, (void **)&data);
    if (EFI_ERROR(status) || data == NULL) {
        status = EFI_OUT_OF_RESOURCES;
        goto out;
    }

    UINTN read_size = file_size;
    status = uefi_call_wrapper(file->Read, 3, file, &read_size, data);
    if (EFI_ERROR(status) || read_size != file_size) {
        status = EFI_LOAD_ERROR;
        goto out;
    }

    *out_data = data;
    *out_size = file_size;
    data = NULL;
    status = EFI_SUCCESS;

out:
    if (data != NULL) {
        uefi_call_wrapper(st->BootServices->FreePool, 1, data);
    }
    if (info != NULL) {
        uefi_call_wrapper(st->BootServices->FreePool, 1, info);
    }
    if (file != NULL) {
        uefi_call_wrapper(file->Close, 1, file);
    }
    if (root != NULL) {
        uefi_call_wrapper(root->Close, 1, root);
    }
    return status;
}

// Draw a basic uncompressed 24/32-bit BMP centered in the current framebuffer.
static BOOLEAN draw_bmp_centered(GfxContext *gfx, const UINT8 *bmp, UINTN bmp_size) {
    if (gfx == NULL || bmp == NULL || bmp_size < 54) {
        return FALSE;
    }
    if (bmp[0] != 'B' || bmp[1] != 'M') {
        return FALSE;
    }

    UINT32 pixel_offset = read_le32(bmp + 10);
    UINT32 dib_size = read_le32(bmp + 14);
    INT32 width = (INT32)read_le32(bmp + 18);
    INT32 height = (INT32)read_le32(bmp + 22);
    UINT16 planes = read_le16(bmp + 26);
    UINT16 bits_per_pixel = read_le16(bmp + 28);
    UINT32 compression = read_le32(bmp + 30);

    if (dib_size < 40 || width <= 0 || height == 0 || planes != 1) {
        return FALSE;
    }
    if (compression != 0 || (bits_per_pixel != 24 && bits_per_pixel != 32)) {
        return FALSE;
    }

    UINTN img_w = (UINTN)width;
    UINTN img_h = (height < 0) ? (UINTN)(-height) : (UINTN)height;
    UINTN bytes_per_pixel = bits_per_pixel / 8;
    if (img_w > (((UINTN)-1) - 3) / bytes_per_pixel) {
        return FALSE;
    }

    UINTN row_stride = (img_w * bytes_per_pixel + 3) & ~(UINTN)3;
    if (img_h > ((UINTN)-1) / row_stride) {
        return FALSE;
    }

    UINTN pixel_data_size = row_stride * img_h;
    if (pixel_offset > bmp_size || pixel_data_size > bmp_size - pixel_offset) {
        return FALSE;
    }

    UINTN src_x = 0;
    UINTN src_y = 0;
    UINTN draw_w = img_w;
    UINTN draw_h = img_h;
    UINTN dst_x = 0;
    UINTN dst_y = 0;

    if (draw_w > gfx->width) {
        src_x = (draw_w - gfx->width) / 2;
        draw_w = gfx->width;
    } else {
        dst_x = (gfx->width - draw_w) / 2;
    }

    if (draw_h > gfx->height) {
        src_y = (draw_h - gfx->height) / 2;
        draw_h = gfx->height;
    } else {
        dst_y = (gfx->height - draw_h) / 2;
    }

    const UINT8 *pixel_base = bmp + pixel_offset;
    BOOLEAN top_down = (height < 0);
    for (UINTN y = 0; y < draw_h; y++) {
        UINTN src_row = src_y + y;
        UINTN bmp_row = top_down ? src_row : (img_h - 1 - src_row);
        const UINT8 *row = pixel_base + bmp_row * row_stride;
        const UINT8 *src = row + src_x * bytes_per_pixel;

        for (UINTN x = 0; x < draw_w; x++) {
            UINT8 b = src[x * bytes_per_pixel + 0];
            UINT8 g = src[x * bytes_per_pixel + 1];
            UINT8 r = src[x * bytes_per_pixel + 2];
            UINT32 rgb = ((UINT32)r << 16) | ((UINT32)g << 8) | b;
            gfx_put_pixel(gfx, dst_x + x, dst_y + y, rgb);
        }
    }

    return TRUE;
}

// Try to draw a BMP splash from ESP. Returns FALSE if file is missing or invalid.
static BOOLEAN draw_external_splash(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st, GfxContext *gfx) {
    if (st == NULL || st->BootServices == NULL || gfx == NULL) {
        return FALSE;
    }

    UINT8 *bmp_data = NULL;
    UINTN bmp_size = 0;
    EFI_STATUS status = read_file_from_esp(image_handle, st, SPLASH_BMP_PATH, &bmp_data, &bmp_size);
    if (EFI_ERROR(status) || bmp_data == NULL) {
        return FALSE;
    }

    BOOLEAN ok = draw_bmp_centered(gfx, bmp_data, bmp_size);
    uefi_call_wrapper(st->BootServices->FreePool, 1, bmp_data);
    return ok;
}

// Draw a procedural top-hat icon so we do not need external image assets.
static void draw_hat_icon(GfxContext *gfx, UINTN center_x, UINTN center_y, UINTN scale) {
    UINTN brim_w = 120 * scale;
    UINTN brim_h = 18 * scale;
    UINTN crown_w = 70 * scale;
    UINTN crown_h = 70 * scale;

    UINT32 dark = 0x101010;
    UINT32 band = 0xB04A00;
    UINT32 highlight = 0x3A3A3A;

    UINTN brim_x = center_x - brim_w / 2;
    UINTN brim_y = center_y + 20 * scale;
    UINTN crown_x = center_x - crown_w / 2;
    UINTN crown_y = brim_y - crown_h + 6 * scale;

    gfx_fill_rect(gfx, brim_x, brim_y, brim_w, brim_h, dark);
    gfx_fill_rect(gfx, crown_x, crown_y, crown_w, crown_h, dark);
    gfx_fill_rect(gfx, crown_x, crown_y + crown_h / 2, crown_w, 8 * scale, band);

    // Small highlight to add shape and keep the icon readable on dark backgrounds.
    gfx_fill_rect(gfx, crown_x + 8 * scale, crown_y + 10 * scale, 8 * scale, crown_h - 20 * scale, highlight);
}

// Paint the splash scene. If /EFI/BOOT/SPLASH.BMP exists and parses, use it.
// Otherwise, fall back to the built-in procedural HatterOS splash.
static void draw_splash(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st, GfxContext *gfx) {
    gfx_draw_gradient(gfx, 0x0E1B2C, 0x253C59);

    if (!draw_external_splash(image_handle, st, gfx)) {
        const char *title = "HatterOS";
        UINTN title_scale = 8;
        UINTN title_w = font_text_width(title, title_scale);
        UINTN title_x = (gfx->width > title_w) ? (gfx->width - title_w) / 2 : 16;
        UINTN title_y = gfx->height / 2 - (FONT_CHAR_HEIGHT * title_scale);

        draw_hat_icon(gfx, gfx->width / 2, gfx->height / 2 - 40, 1);
        font_draw_text(gfx, title_x, title_y, title, 0xF3F7FF, 0, title_scale, TRUE);
    }

    const char *hint = "Press any key to continue...";
    UINTN hint_scale = 2;
    UINTN hint_w = font_text_width(hint, hint_scale);
    UINTN hint_x = (gfx->width > hint_w) ? (gfx->width - hint_w) / 2 : 8;
    UINTN hint_y = gfx->height - 80;

    font_draw_text(gfx, hint_x, hint_y, hint, 0xDCE5F2, 0, hint_scale, TRUE);
}

// Wait for either keyboard input or a timeout so splash can auto-advance.
static void wait_for_key_or_timeout(EFI_SYSTEM_TABLE *st, UINTN timeout_ms) {
    if (st == NULL || st->BootServices == NULL || st->ConIn == NULL) {
        return;
    }

    EFI_EVENT timer_event;
    EFI_STATUS status = uefi_call_wrapper(
        st->BootServices->CreateEvent,
        5,
        EVT_TIMER,
        TPL_CALLBACK,
        NULL,
        NULL,
        &timer_event
    );
    if (EFI_ERROR(status)) {
        return;
    }

    UINT64 ticks_100ns = (UINT64)timeout_ms * 10000ULL;
    uefi_call_wrapper(st->BootServices->SetTimer, 3, timer_event, TimerRelative, ticks_100ns);

    EFI_EVENT events[2] = { st->ConIn->WaitForKey, timer_event };
    UINTN index = 0;
    uefi_call_wrapper(st->BootServices->WaitForEvent, 3, 2, events, &index);

    if (index == 0) {
        EFI_INPUT_KEY key;
        uefi_call_wrapper(st->ConIn->ReadKeyStroke, 2, st->ConIn, &key);
    }

    uefi_call_wrapper(st->BootServices->CloseEvent, 1, timer_event);
}

// UEFI entrypoint: initialize graphics, show splash, then enter the shell.
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    if (system_table == NULL || system_table->BootServices == NULL) {
        return EFI_SUCCESS;
    }

    if (system_table->ConIn != NULL && system_table->ConIn->Reset != NULL) {
        uefi_call_wrapper(system_table->ConIn->Reset, 2, system_table->ConIn, FALSE);
    }

    GfxContext gfx;
    EFI_STATUS status = gfx_init(system_table, &gfx, 1024, 768);
    if (EFI_ERROR(status)) {
        // Keep failure mode user-friendly instead of returning cryptic firmware errors.
        uefi_text(system_table, L"HatterOS: GOP init failed, cannot start framebuffer shell.\r\n");
        return EFI_SUCCESS;
    }

    draw_splash(image_handle, system_table, &gfx);
    wait_for_key_or_timeout(system_table, 2000);

    Shell shell;
    shell_init(&shell, image_handle, system_table, &gfx);
    shell_run(&shell);

    return EFI_SUCCESS;
}
