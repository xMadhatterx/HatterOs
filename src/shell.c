#include "shell.h"
#include "font.h"
#include "util.h"

#define INPUT_MAX 256

static void shell_newline(Shell *shell);
static void shell_putc(Shell *shell, char c);
static void shell_prompt(Shell *shell);
static void shell_execute(Shell *shell, char *line);
static EFI_STATUS shell_read_line(Shell *shell, char *line, UINTN max_len);
static void shell_scroll(Shell *shell);

void shell_init(Shell *shell, EFI_SYSTEM_TABLE *st, GfxContext *gfx) {
    shell->st = st;
    shell->gfx = gfx;
    shell->margin_x = 8;
    shell->margin_y = 8;
    shell->fg_color = 0xE8E8E8;
    shell->bg_color = 0x10161E;

    UINTN usable_w = (gfx->width > shell->margin_x * 2) ? (gfx->width - shell->margin_x * 2) : gfx->width;
    UINTN usable_h = (gfx->height > shell->margin_y * 2) ? (gfx->height - shell->margin_y * 2) : gfx->height;
    shell->cols = usable_w / FONT_CHAR_WIDTH;
    shell->rows = usable_h / FONT_CHAR_HEIGHT;
    shell->cursor_col = 0;
    shell->cursor_row = 0;

    shell_clear(shell);
}

void shell_clear(Shell *shell) {
    gfx_clear(shell->gfx, shell->bg_color);
    shell->cursor_col = 0;
    shell->cursor_row = 0;
}

static void shell_scroll(Shell *shell) {
    GfxContext *gfx = shell->gfx;
    UINTN line_px = FONT_CHAR_HEIGHT;

    for (UINTN y = shell->margin_y; y + line_px < gfx->height - shell->margin_y; y++) {
        UINTN dst_row = y * gfx->pixels_per_scanline;
        UINTN src_row = (y + line_px) * gfx->pixels_per_scanline;
        for (UINTN x = shell->margin_x; x < gfx->width - shell->margin_x; x++) {
            gfx->framebuffer[dst_row + x] = gfx->framebuffer[src_row + x];
        }
    }

    UINTN clear_start = gfx->height - shell->margin_y - line_px;
    gfx_fill_rect(gfx, shell->margin_x, clear_start, gfx->width - shell->margin_x * 2, line_px, shell->bg_color);
}

static void shell_newline(Shell *shell) {
    shell->cursor_col = 0;
    shell->cursor_row++;
    if (shell->cursor_row >= shell->rows) {
        shell_scroll(shell);
        shell->cursor_row = shell->rows - 1;
    }
}

static void shell_putc(Shell *shell, char c) {
    if (c == '\n') {
        shell_newline(shell);
        return;
    }

    UINTN px = shell->margin_x + shell->cursor_col * FONT_CHAR_WIDTH;
    UINTN py = shell->margin_y + shell->cursor_row * FONT_CHAR_HEIGHT;
    font_draw_char(shell->gfx, px, py, c, shell->fg_color, shell->bg_color, 1, FALSE);

    shell->cursor_col++;
    if (shell->cursor_col >= shell->cols) {
        shell_newline(shell);
    }
}

void shell_print(Shell *shell, const char *text) {
    while (*text) {
        shell_putc(shell, *text++);
    }
}

void shell_println(Shell *shell, const char *text) {
    shell_print(shell, text);
    shell_putc(shell, '\n');
}

static void shell_prompt(Shell *shell) {
    shell_print(shell, "HatterOS> ");
}

static void erase_last_char(Shell *shell) {
    if (shell->cursor_col == 0 && shell->cursor_row == 0) {
        return;
    }

    if (shell->cursor_col == 0) {
        shell->cursor_row--;
        shell->cursor_col = shell->cols - 1;
    } else {
        shell->cursor_col--;
    }

    UINTN px = shell->margin_x + shell->cursor_col * FONT_CHAR_WIDTH;
    UINTN py = shell->margin_y + shell->cursor_row * FONT_CHAR_HEIGHT;
    gfx_fill_rect(shell->gfx, px, py, FONT_CHAR_WIDTH, FONT_CHAR_HEIGHT, shell->bg_color);
}

static EFI_STATUS shell_read_line(Shell *shell, char *line, UINTN max_len) {
    if (shell == NULL || shell->st == NULL || shell->st->BootServices == NULL || shell->st->ConIn == NULL) {
        return EFI_UNSUPPORTED;
    }

    UINTN len = 0;

    while (1) {
        UINTN idx;
        EFI_EVENT event = shell->st->ConIn->WaitForKey;
        EFI_STATUS status = shell->st->BootServices->WaitForEvent(1, &event, &idx);
        if (EFI_ERROR(status)) {
            return status;
        }

        EFI_INPUT_KEY key;
        status = shell->st->ConIn->ReadKeyStroke(shell->st->ConIn, &key);
        if (EFI_ERROR(status)) {
            continue;
        }

        CHAR16 uc = key.UnicodeChar;

        if (uc == (CHAR16)'\r') {
            line[len] = '\0';
            shell_putc(shell, '\n');
            return EFI_SUCCESS;
        }

        if (uc == (CHAR16)'\b') {
            if (len > 0) {
                len--;
                erase_last_char(shell);
            }
            continue;
        }

        if (uc >= 32 && uc <= 126 && len + 1 < max_len) {
            char c = (char)uc;
            line[len++] = c;
            shell_putc(shell, c);
        }
    }
}

static void print_info(Shell *shell) {
    char w[32], h[32], fb_addr[32], fb_size[32];
    u_u64_to_dec(shell->gfx->width, w, sizeof(w));
    u_u64_to_dec(shell->gfx->height, h, sizeof(h));
    u_u64_to_hex((UINT64)shell->gfx->framebuffer_base, fb_addr, sizeof(fb_addr));
    u_u64_to_dec(shell->gfx->framebuffer_size, fb_size, sizeof(fb_size));

    shell_println(shell, "HatterOS stage-0 shell");
    shell_print(shell, "Version: ");
    shell_println(shell, HATTEROS_VERSION);
    shell_print(shell, "Build: ");
    shell_println(shell, HATTEROS_BUILD_DATE);
    shell_print(shell, "Resolution: ");
    shell_print(shell, w);
    shell_print(shell, "x");
    shell_println(shell, h);
    shell_print(shell, "Framebuffer: ");
    shell_println(shell, fb_addr);
    shell_print(shell, "Framebuffer size: ");
    shell_print(shell, fb_size);
    shell_println(shell, " bytes");
}

static void shell_execute(Shell *shell, char *line) {
    char *cmd = u_trim_left(line);
    if (*cmd == '\0') {
        return;
    }

    serial_write("[shell] ");
    serial_writeln(cmd);

    if (u_strcmp(cmd, "help") == 0) {
        shell_println(shell, "Commands:");
        shell_println(shell, "  help        - list commands");
        shell_println(shell, "  clear       - clear the screen");
        shell_println(shell, "  echo <text> - print text");
        shell_println(shell, "  info        - show system info");
        shell_println(shell, "  reboot      - reboot machine");
        return;
    }

    if (u_strcmp(cmd, "clear") == 0) {
        shell_clear(shell);
        return;
    }

    if (u_strcmp(cmd, "info") == 0) {
        print_info(shell);
        return;
    }

    if (u_strcmp(cmd, "reboot") == 0) {
        shell_println(shell, "Rebooting...");
        shell->st->RuntimeServices->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
        return;
    }

    if (u_strcmp(cmd, "echo") == 0) {
        shell_putc(shell, '\n');
        return;
    }

    if (u_startswith(cmd, "echo ")) {
        shell_println(shell, cmd + 5);
        return;
    }

    shell_print(shell, "Unknown command: ");
    shell_println(shell, cmd);
    shell_println(shell, "Type 'help' for available commands.");
}

void shell_run(Shell *shell) {
    if (shell == NULL || shell->st == NULL || shell->st->ConIn == NULL) {
        return;
    }

    shell_println(shell, "HatterOS shell ready. Type 'help'.");

    while (1) {
        char input[INPUT_MAX];
        shell_prompt(shell);

        EFI_STATUS status = shell_read_line(shell, input, sizeof(input));
        if (EFI_ERROR(status)) {
            shell_println(shell, "Input error.");
            continue;
        }

        shell_execute(shell, input);
    }
}
