#include <efi.h>

#include "gfx.h"
#include "font.h"
#include "shell.h"
#include "util.h"

static void uefi_text(EFI_SYSTEM_TABLE *st, CHAR16 *msg) {
    if (st && st->ConOut) {
        st->ConOut->OutputString(st->ConOut, msg);
    }
}

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

static void draw_splash(GfxContext *gfx) {
    gfx_draw_gradient(gfx, 0x0E1B2C, 0x253C59);

    const char *title = "HatterOS";
    UINTN title_scale = 8;
    UINTN title_w = font_text_width(title, title_scale);
    UINTN title_x = (gfx->width > title_w) ? (gfx->width - title_w) / 2 : 16;
    UINTN title_y = gfx->height / 2 - (FONT_CHAR_HEIGHT * title_scale);

    draw_hat_icon(gfx, gfx->width / 2, gfx->height / 2 - 40, 1);
    font_draw_text(gfx, title_x, title_y, title, 0xF3F7FF, 0, title_scale, TRUE);

    const char *hint = "Press any key to continue...";
    UINTN hint_scale = 2;
    UINTN hint_w = font_text_width(hint, hint_scale);
    UINTN hint_x = (gfx->width > hint_w) ? (gfx->width - hint_w) / 2 : 8;
    UINTN hint_y = gfx->height - 80;

    font_draw_text(gfx, hint_x, hint_y, hint, 0xDCE5F2, 0, hint_scale, TRUE);
}

static void wait_for_key_or_timeout(EFI_SYSTEM_TABLE *st, UINTN timeout_ms) {
    EFI_EVENT timer_event;
    EFI_STATUS status = st->BootServices->CreateEvent(EVT_TIMER, TPL_CALLBACK, NULL, NULL, &timer_event);
    if (EFI_ERROR(status)) {
        return;
    }

    UINT64 ticks_100ns = (UINT64)timeout_ms * 10000ULL;
    st->BootServices->SetTimer(timer_event, TimerRelative, ticks_100ns);

    EFI_EVENT events[2] = { st->ConIn->WaitForKey, timer_event };
    UINTN index = 0;
    st->BootServices->WaitForEvent(2, events, &index);

    if (index == 0) {
        EFI_INPUT_KEY key;
        st->ConIn->ReadKeyStroke(st->ConIn, &key);
    }

    st->BootServices->CloseEvent(timer_event);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    (void)image_handle;

    uefi_text(system_table, L"HatterOS: entry\r\n");

    serial_init();
    serial_writeln("[hatteros] Booting UEFI app");

    system_table->ConIn->Reset(system_table->ConIn, FALSE);

    GfxContext gfx;
    EFI_STATUS status = gfx_init(system_table, &gfx, 1024, 768);
    if (EFI_ERROR(status)) {
        uefi_text(system_table, L"HatterOS: GOP init failed, cannot start framebuffer shell.\r\n");
        serial_writeln("[hatteros] GOP init failed");
        return EFI_SUCCESS;
    }

    serial_writeln("[hatteros] GOP initialized");

    draw_splash(&gfx);
    wait_for_key_or_timeout(system_table, 2000);

    Shell shell;
    shell_init(&shell, system_table, &gfx);
    shell_run(&shell);

    return EFI_SUCCESS;
}
