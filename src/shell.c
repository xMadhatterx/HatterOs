#include "shell.h"
#include "font.h"
#include "util.h"
#include <efilib.h>

#define FILE_IO_CHUNK 512
#define SHELL_CFG_PATH "\\HATTEROS\\system\\config\\shell.cfg"
#define HEXDUMP_COLS 16
#define SHELL_CFG_MAGIC 0x53434647U
#define SHELL_CFG_VERSION 1U

typedef struct {
    UINT32 magic;
    UINT32 version;
    UINT32 fg_color;
    UINT32 bg_color;
    UINT8 prompt_show_path;
    UINT8 reserved[3];
} ShellConfigFile;

static void shell_newline(Shell *shell);
static void shell_putc(Shell *shell, char c);
static void shell_set_cursor(Shell *shell, UINTN row, UINTN col);
static UINT16 shell_read_le16(const UINT8 *p);
static UINT32 shell_read_le32(const UINT8 *p);
static void shell_prompt(Shell *shell);
static UINTN shell_build_prompt(Shell *shell, char *out, UINTN out_len);
static void shell_execute(Shell *shell, char *line);
static EFI_STATUS shell_read_line(Shell *shell, char *line, UINTN max_len);
static void shell_scroll(Shell *shell);
static void shell_draw_cursor(Shell *shell, UINTN row, UINTN col);
static EFI_STATUS shell_open_root(Shell *shell, EFI_FILE_PROTOCOL **root);
static EFI_STATUS shell_open_path(Shell *shell, const char *path, UINT64 mode, UINT64 attrs, EFI_FILE_PROTOCOL **out);
static EFI_FILE_INFO *shell_get_file_info(Shell *shell, EFI_FILE_PROTOCOL *file, EFI_STATUS *out_status);
static EFI_STATUS shell_ensure_dir(Shell *shell, const char *path);
static EFI_STATUS shell_ensure_dir_tree(Shell *shell, const char *path);
static BOOLEAN shell_parse_ls_args(const char *arg, BOOLEAN *long_mode, char *path_out, UINTN path_out_len);
static void shell_print_history(Shell *shell);
static void shell_print_help(Shell *shell, const char *topic);
static BOOLEAN shell_normalize_path(const char *cwd, const char *input, char *out, UINTN out_len);
static BOOLEAN shell_path_to_char16(const char *path, CHAR16 *out, UINTN out_len);
static void shell_print_file_name(Shell *shell, const CHAR16 *name);
static const char *shell_status_str(EFI_STATUS status);
static void shell_print_error_status(Shell *shell, const char *prefix, EFI_STATUS status);
static void shell_cmd_ls(Shell *shell, const char *arg);
static void shell_cmd_cat(Shell *shell, const char *arg);
static void shell_cmd_cd(Shell *shell, const char *arg);
static void shell_cmd_pwd(Shell *shell);
static void shell_cmd_mkdir(Shell *shell, const char *arg);
static void shell_cmd_touch(Shell *shell, const char *arg);
static void shell_cmd_cp(Shell *shell, const char *src_arg, const char *dst_arg);
static EFI_STATUS shell_copy_file(Shell *shell, const char *src_raw, const char *dst_raw);
static void shell_cmd_rm(Shell *shell, const char *arg);
static void shell_cmd_mv(Shell *shell, const char *src_arg, const char *dst_arg);
static void shell_cmd_hexdump(Shell *shell, const char *arg);
static void shell_cmd_history(Shell *shell);
static void shell_cmd_viewbmp(Shell *shell, const char *arg);
static void shell_cmd_initfs(Shell *shell);
static void shell_cmd_theme(Shell *shell, const char *arg);
static void shell_cmd_time(Shell *shell);
static void shell_cmd_memmap(Shell *shell);
static void shell_print_u64(Shell *shell, UINT64 value);
static void shell_print_padded_u64(Shell *shell, UINT64 value, UINTN width);
static void shell_print_padded_hex8(Shell *shell, UINT8 value);
static const char *shell_mem_type_name(UINT32 type);
static void shell_apply_theme(Shell *shell, UINT32 fg, UINT32 bg, BOOLEAN clear_screen);
static void shell_save_settings(Shell *shell);
static void shell_load_settings(Shell *shell);
static BOOLEAN shell_draw_bmp_centered(Shell *shell, const UINT8 *bmp, UINTN bmp_size);
static void shell_history_add(Shell *shell, const char *line);
static void *shell_alloc(Shell *shell, UINTN size);
static void shell_free(Shell *shell, void *ptr);

// Initialize shell state and compute text-grid size from framebuffer dimensions.
void shell_init(Shell *shell, EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st, GfxContext *gfx) {
    shell->image_handle = image_handle;
    shell->st = st;
    shell->gfx = gfx;
    shell->margin_x = 8;
    shell->margin_y = 8;
    shell->fg_color = 0xE8E8E8;
    shell->bg_color = 0x10161E;
    shell->prompt_show_path = TRUE;

    UINTN usable_w = (gfx->width > shell->margin_x * 2) ? (gfx->width - shell->margin_x * 2) : gfx->width;
    UINTN usable_h = (gfx->height > shell->margin_y * 2) ? (gfx->height - shell->margin_y * 2) : gfx->height;
    shell->cols = usable_w / FONT_CHAR_WIDTH;
    shell->rows = usable_h / FONT_CHAR_HEIGHT;
    if (shell->cols == 0) {
        shell->cols = 1;
    }
    if (shell->rows == 0) {
        shell->rows = 1;
    }
    shell->cursor_col = 0;
    shell->cursor_row = 0;
    shell->cwd[0] = '\\';
    shell->cwd[1] = '\0';
    shell->history_count = 0;
    shell_load_settings(shell);

    shell_clear(shell);
}

// Clear the shell viewport and reset cursor to top-left.
void shell_clear(Shell *shell) {
    gfx_clear(shell->gfx, shell->bg_color);
    shell->cursor_col = 0;
    shell->cursor_row = 0;
}

// Scroll one text row upward by moving framebuffer pixels directly.
static void shell_scroll(Shell *shell) {
    GfxContext *gfx = shell->gfx;
    UINTN line_px = FONT_CHAR_HEIGHT;
    if (gfx->height <= shell->margin_y * 2 + line_px || gfx->width <= shell->margin_x * 2) {
        shell_clear(shell);
        return;
    }

    UINTN right = gfx->width - shell->margin_x;
    UINTN bottom = gfx->height - shell->margin_y;

    for (UINTN y = shell->margin_y; y + line_px < bottom; y++) {
        UINTN dst_row = y * gfx->pixels_per_scanline;
        UINTN src_row = (y + line_px) * gfx->pixels_per_scanline;
        for (UINTN x = shell->margin_x; x < right; x++) {
            gfx->framebuffer[dst_row + x] = gfx->framebuffer[src_row + x];
        }
    }

    UINTN clear_start = bottom - line_px;
    gfx_fill_rect(gfx, shell->margin_x, clear_start, right - shell->margin_x, line_px, shell->bg_color);
}

// Move to the next line and scroll if we reached the bottom row.
static void shell_newline(Shell *shell) {
    shell->cursor_col = 0;
    shell->cursor_row++;
    if (shell->cursor_row >= shell->rows) {
        shell_scroll(shell);
        shell->cursor_row = shell->rows - 1;
    }
}

static void shell_set_cursor(Shell *shell, UINTN row, UINTN col) {
    if (shell->rows == 0 || shell->cols == 0) {
        shell->cursor_row = 0;
        shell->cursor_col = 0;
        return;
    }
    if (row >= shell->rows) {
        row = shell->rows - 1;
    }
    if (col >= shell->cols) {
        col = shell->cols - 1;
    }
    shell->cursor_row = row;
    shell->cursor_col = col;
}

static UINT16 shell_read_le16(const UINT8 *p) {
    return (UINT16)(p[0] | ((UINT16)p[1] << 8));
}

static UINT32 shell_read_le32(const UINT8 *p) {
    return (UINT32)(p[0] |
                    ((UINT32)p[1] << 8) |
                    ((UINT32)p[2] << 16) |
                    ((UINT32)p[3] << 24));
}

// Render one printable character into the shell grid.
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

// Print a string without implicit newline.
void shell_print(Shell *shell, const char *text) {
    while (*text) {
        shell_putc(shell, *text++);
    }
}

// Print a string followed by newline.
void shell_println(Shell *shell, const char *text) {
    shell_print(shell, text);
    shell_putc(shell, '\n');
}

static UINTN shell_build_prompt(Shell *shell, char *prompt, UINTN out_len) {
    if (prompt == NULL || out_len == 0) {
        return 0;
    }

    UINTN i = 0;
    const char *base = "HATTEROS";

    while (base[i] != '\0' && i + 1 < out_len) {
        prompt[i] = base[i];
        i++;
    }

    if (shell->prompt_show_path) {
        if (shell->cwd[0] == '\\' && shell->cwd[1] == '\0') {
            if (i + 1 < out_len) {
                prompt[i++] = '/';
            }
        } else {
            UINTN j = 0;
            while (shell->cwd[j] != '\0' && i + 1 < out_len) {
                prompt[i++] = (shell->cwd[j] == '\\') ? '/' : shell->cwd[j];
                j++;
            }
        }
    }

    if (i + 2 < out_len) {
        prompt[i++] = '>';
        prompt[i++] = ' ';
    }
    prompt[i] = '\0';
    return i;
}

// Draw the interactive prompt.
static void shell_prompt(Shell *shell) {
    char prompt[SHELL_PATH_MAX + 16];
    shell_build_prompt(shell, prompt, sizeof(prompt));
    shell_print(shell, prompt);
}

static void shell_redraw_input(
    Shell *shell,
    UINTN row,
    UINTN col,
    UINTN field_len,
    const char *line,
    UINTN len,
    UINTN cursor,
    BOOLEAN show_cursor
) {
    UINTN px = shell->margin_x + col * FONT_CHAR_WIDTH;
    UINTN py = shell->margin_y + row * FONT_CHAR_HEIGHT;
    gfx_fill_rect(shell->gfx, px, py, field_len * FONT_CHAR_WIDTH, FONT_CHAR_HEIGHT, shell->bg_color);

    shell_set_cursor(shell, row, col);
    for (UINTN i = 0; i < len; i++) {
        shell_putc(shell, line[i]);
    }
    shell_set_cursor(shell, row, col + cursor);

    if (show_cursor) {
        UINTN draw_col = col + cursor;
        if (draw_col >= shell->cols) {
            draw_col = shell->cols - 1;
        }
        shell_draw_cursor(shell, row, draw_col);
    }
}

// Draw a simple visible caret at the current input position.
static void shell_draw_cursor(Shell *shell, UINTN row, UINTN col) {
    if (shell == NULL || shell->gfx == NULL) {
        return;
    }

    UINTN px = shell->margin_x + col * FONT_CHAR_WIDTH;
    UINTN py = shell->margin_y + row * FONT_CHAR_HEIGHT + (FONT_CHAR_HEIGHT - 2);
    gfx_fill_rect(shell->gfx, px, py, FONT_CHAR_WIDTH, 2, shell->fg_color);
}

// Blocking line editor using UEFI keyboard input.
// Supports printable ASCII insertion, left/right movement, history (up/down), and backspace.
static EFI_STATUS shell_read_line(Shell *shell, char *line, UINTN max_len) {
    if (shell == NULL || shell->st == NULL || shell->st->BootServices == NULL || shell->st->ConIn == NULL) {
        return EFI_UNSUPPORTED;
    }
    if (line == NULL || max_len < 2) {
        return EFI_INVALID_PARAMETER;
    }

    UINTN start_row = shell->cursor_row;
    UINTN start_col = shell->cursor_col;
    UINTN field_len = (shell->cols > start_col) ? (shell->cols - start_col) : 1;
    UINTN max_chars = max_len - 1;
    if (max_chars > field_len) {
        max_chars = field_len;
    }

    UINTN len = 0;
    UINTN cursor = 0;
    INTN history_nav = -1;
    line[0] = '\0';
    shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, TRUE);

    while (1) {
        UINTN idx;
        EFI_EVENT event = shell->st->ConIn->WaitForKey;
        EFI_STATUS status = uefi_call_wrapper(shell->st->BootServices->WaitForEvent, 3, 1, &event, &idx);
        if (EFI_ERROR(status)) {
            return status;
        }

        EFI_INPUT_KEY key;
        status = uefi_call_wrapper(shell->st->ConIn->ReadKeyStroke, 2, shell->st->ConIn, &key);
        if (EFI_ERROR(status)) {
            continue;
        }

        if (key.ScanCode == SCAN_UP || key.ScanCode == SCAN_DOWN || key.ScanCode == SCAN_LEFT || key.ScanCode == SCAN_RIGHT) {
            if (key.ScanCode == SCAN_LEFT) {
                if (cursor > 0) {
                    cursor--;
                    shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, TRUE);
                }
            } else if (key.ScanCode == SCAN_RIGHT) {
                if (cursor < len) {
                    cursor++;
                    shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, TRUE);
                }
            } else if (key.ScanCode == SCAN_UP) {
                if (shell->history_count > 0) {
                    if (history_nav < 0) {
                        history_nav = (INTN)shell->history_count - 1;
                    } else if (history_nav > 0) {
                        history_nav--;
                    }

                    UINTN i = 0;
                    while (shell->history[history_nav][i] != '\0' && i < max_chars) {
                        line[i] = shell->history[history_nav][i];
                        i++;
                    }
                    len = i;
                    cursor = len;
                    line[len] = '\0';
                    shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, TRUE);
                }
            } else if (key.ScanCode == SCAN_DOWN) {
                if (history_nav >= 0) {
                    if ((UINTN)history_nav + 1 < shell->history_count) {
                        history_nav++;
                        UINTN i = 0;
                        while (shell->history[history_nav][i] != '\0' && i < max_chars) {
                            line[i] = shell->history[history_nav][i];
                            i++;
                        }
                        len = i;
                        cursor = len;
                        line[len] = '\0';
                    } else {
                        history_nav = -1;
                        len = 0;
                        cursor = 0;
                        line[0] = '\0';
                    }
                    shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, TRUE);
                }
            }
            continue;
        }

        CHAR16 uc = key.UnicodeChar;

        if (uc == (CHAR16)'\r') {
            line[len] = '\0';
            // Remove caret before committing the line so printed text remains clean.
            shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, FALSE);
            shell_set_cursor(shell, start_row, start_col + len);
            shell_putc(shell, '\n');
            return EFI_SUCCESS;
        }

        if (uc == (CHAR16)'\b') {
            if (cursor > 0) {
                for (UINTN i = cursor - 1; i < len; i++) {
                    line[i] = line[i + 1];
                }
                len--;
                cursor--;
                history_nav = -1;
                shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, TRUE);
            }
            continue;
        }

        if (uc >= 32 && uc <= 126 && len < max_chars) {
            char c = (char)uc;
            for (UINTN i = len; i > cursor; i--) {
                line[i] = line[i - 1];
            }
            line[cursor] = c;
            len++;
            cursor++;
            line[len] = '\0';
            history_nav = -1;
            shell_redraw_input(shell, start_row, start_col, field_len, line, len, cursor, TRUE);
        }
    }
}

// Small wrappers over BootServices AllocatePool/FreePool for convenience.
static void *shell_alloc(Shell *shell, UINTN size) {
    if (shell == NULL || shell->st == NULL || shell->st->BootServices == NULL) {
        return NULL;
    }
    void *ptr = NULL;
    EFI_STATUS status = uefi_call_wrapper(shell->st->BootServices->AllocatePool, 3, EfiLoaderData, size, &ptr);
    if (EFI_ERROR(status)) {
        return NULL;
    }
    return ptr;
}

static void shell_free(Shell *shell, void *ptr) {
    if (ptr == NULL || shell == NULL || shell->st == NULL || shell->st->BootServices == NULL) {
        return;
    }
    uefi_call_wrapper(shell->st->BootServices->FreePool, 1, ptr);
}

static const char *shell_status_str(EFI_STATUS status) {
    switch (status) {
    case EFI_SUCCESS: return "SUCCESS";
    case EFI_LOAD_ERROR: return "LOAD_ERROR";
    case EFI_INVALID_PARAMETER: return "INVALID_PARAMETER";
    case EFI_UNSUPPORTED: return "UNSUPPORTED";
    case EFI_BAD_BUFFER_SIZE: return "BAD_BUFFER_SIZE";
    case EFI_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
    case EFI_NOT_READY: return "NOT_READY";
    case EFI_DEVICE_ERROR: return "DEVICE_ERROR";
    case EFI_WRITE_PROTECTED: return "WRITE_PROTECTED";
    case EFI_OUT_OF_RESOURCES: return "OUT_OF_RESOURCES";
    case EFI_VOLUME_CORRUPTED: return "VOLUME_CORRUPTED";
    case EFI_VOLUME_FULL: return "VOLUME_FULL";
    case EFI_NO_MEDIA: return "NO_MEDIA";
    case EFI_MEDIA_CHANGED: return "MEDIA_CHANGED";
    case EFI_NOT_FOUND: return "NOT_FOUND";
    case EFI_ACCESS_DENIED: return "ACCESS_DENIED";
    case EFI_NO_RESPONSE: return "NO_RESPONSE";
    case EFI_NO_MAPPING: return "NO_MAPPING";
    case EFI_TIMEOUT: return "TIMEOUT";
    case EFI_ABORTED: return "ABORTED";
    case EFI_ALREADY_STARTED: return "ALREADY_STARTED";
    default: return "UNKNOWN";
    }
}

static void shell_print_error_status(Shell *shell, const char *prefix, EFI_STATUS status) {
    char code[32];
    u_u64_to_hex((UINT64)status, code, sizeof(code));
    shell_print(shell, prefix);
    shell_print(shell, ": ");
    shell_print(shell, shell_status_str(status));
    shell_print(shell, " (");
    shell_print(shell, code);
    shell_println(shell, ")");
}

static void shell_print_u64(Shell *shell, UINT64 value) {
    char buf[32];
    u_u64_to_dec(value, buf, sizeof(buf));
    shell_print(shell, buf);
}

static void shell_print_padded_u64(Shell *shell, UINT64 value, UINTN width) {
    char buf[32];
    u_u64_to_dec(value, buf, sizeof(buf));
    UINTN len = u_strlen(buf);
    while (len < width) {
        shell_putc(shell, '0');
        len++;
    }
    shell_print(shell, buf);
}

static void shell_print_padded_hex8(Shell *shell, UINT8 value) {
    char hex[4];
    static const char digits[] = "0123456789ABCDEF";
    hex[0] = digits[(value >> 4) & 0xF];
    hex[1] = digits[value & 0xF];
    hex[2] = '\0';
    shell_print(shell, hex);
}

static const char *shell_mem_type_name(UINT32 type) {
    switch (type) {
    case EfiReservedMemoryType: return "Reserved";
    case EfiLoaderCode: return "LoaderCode";
    case EfiLoaderData: return "LoaderData";
    case EfiBootServicesCode: return "BS_Code";
    case EfiBootServicesData: return "BS_Data";
    case EfiRuntimeServicesCode: return "RT_Code";
    case EfiRuntimeServicesData: return "RT_Data";
    case EfiConventionalMemory: return "Conventional";
    case EfiUnusableMemory: return "Unusable";
    case EfiACPIReclaimMemory: return "ACPI_Reclaim";
    case EfiACPIMemoryNVS: return "ACPI_NVS";
    case EfiMemoryMappedIO: return "MMIO";
    case EfiMemoryMappedIOPortSpace: return "MMIO_Port";
    case EfiPalCode: return "PalCode";
    default: return "Unknown";
    }
}

static void shell_apply_theme(Shell *shell, UINT32 fg, UINT32 bg, BOOLEAN clear_screen) {
    shell->fg_color = fg;
    shell->bg_color = bg;
    if (clear_screen) {
        shell_clear(shell);
    }
}

static void shell_save_settings(Shell *shell) {
    if (shell == NULL) {
        return;
    }

    EFI_STATUS status = shell_ensure_dir_tree(shell, "\\HATTEROS\\system\\config");
    if (EFI_ERROR(status)) {
        return;
    }

    EFI_FILE_PROTOCOL *cfg = NULL;
    status = shell_open_path(
        shell,
        SHELL_CFG_PATH,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        0,
        &cfg
    );
    if (EFI_ERROR(status) || cfg == NULL) {
        return;
    }

    EFI_STATUS info_status = EFI_SUCCESS;
    EFI_FILE_INFO *info = shell_get_file_info(shell, cfg, &info_status);
    if (info != NULL) {
        EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
        info->FileSize = 0;
        info->PhysicalSize = 0;
        uefi_call_wrapper(cfg->SetInfo, 4, cfg, &file_info_guid, info->Size, info);
        shell_free(shell, info);
    }

    ShellConfigFile data;
    data.magic = SHELL_CFG_MAGIC;
    data.version = SHELL_CFG_VERSION;
    data.fg_color = shell->fg_color;
    data.bg_color = shell->bg_color;
    data.prompt_show_path = shell->prompt_show_path ? 1 : 0;
    data.reserved[0] = 0;
    data.reserved[1] = 0;
    data.reserved[2] = 0;

    uefi_call_wrapper(cfg->SetPosition, 2, cfg, 0);
    UINTN write_size = sizeof(data);
    uefi_call_wrapper(cfg->Write, 3, cfg, &write_size, &data);
    uefi_call_wrapper(cfg->Close, 1, cfg);
}

static void shell_load_settings(Shell *shell) {
    if (shell == NULL) {
        return;
    }

    EFI_FILE_PROTOCOL *cfg = NULL;
    EFI_STATUS status = shell_open_path(shell, SHELL_CFG_PATH, EFI_FILE_MODE_READ, 0, &cfg);
    if (EFI_ERROR(status) || cfg == NULL) {
        return;
    }

    ShellConfigFile data;
    UINTN read_size = sizeof(data);
    status = uefi_call_wrapper(cfg->Read, 3, cfg, &read_size, &data);
    uefi_call_wrapper(cfg->Close, 1, cfg);
    if (EFI_ERROR(status) || read_size < sizeof(data)) {
        return;
    }

    if (data.magic != SHELL_CFG_MAGIC || data.version != SHELL_CFG_VERSION) {
        return;
    }

    shell->fg_color = data.fg_color;
    shell->bg_color = data.bg_color;
    shell->prompt_show_path = (data.prompt_show_path != 0);
}

static BOOLEAN shell_draw_bmp_centered(Shell *shell, const UINT8 *bmp, UINTN bmp_size) {
    if (shell == NULL || shell->gfx == NULL || bmp == NULL || bmp_size < 54) {
        return FALSE;
    }
    if (bmp[0] != 'B' || bmp[1] != 'M') {
        return FALSE;
    }

    UINT32 pixel_offset = shell_read_le32(bmp + 10);
    UINT32 dib_size = shell_read_le32(bmp + 14);
    INT32 width = (INT32)shell_read_le32(bmp + 18);
    INT32 height = (INT32)shell_read_le32(bmp + 22);
    UINT16 planes = shell_read_le16(bmp + 26);
    UINT16 bits_per_pixel = shell_read_le16(bmp + 28);
    UINT32 compression = shell_read_le32(bmp + 30);

    if (dib_size < 40 || width <= 0 || height == 0 || planes != 1) {
        return FALSE;
    }
    if (compression != 0 || (bits_per_pixel != 24 && bits_per_pixel != 32)) {
        return FALSE;
    }

    UINTN img_w = (UINTN)width;
    UINTN img_h = (height < 0) ? (UINTN)(-height) : (UINTN)height;
    UINTN bpp = bits_per_pixel / 8;
    UINTN row_stride = (img_w * bpp + 3) & ~(UINTN)3;
    if (pixel_offset > bmp_size || img_h > ((UINTN)-1) / row_stride) {
        return FALSE;
    }
    if ((row_stride * img_h) > (bmp_size - pixel_offset)) {
        return FALSE;
    }

    UINTN src_x = 0;
    UINTN src_y = 0;
    UINTN draw_w = img_w;
    UINTN draw_h = img_h;
    UINTN dst_x = 0;
    UINTN dst_y = 0;

    if (draw_w > shell->gfx->width) {
        src_x = (draw_w - shell->gfx->width) / 2;
        draw_w = shell->gfx->width;
    } else {
        dst_x = (shell->gfx->width - draw_w) / 2;
    }
    if (draw_h > shell->gfx->height) {
        src_y = (draw_h - shell->gfx->height) / 2;
        draw_h = shell->gfx->height;
    } else {
        dst_y = (shell->gfx->height - draw_h) / 2;
    }

    const UINT8 *pixel_base = bmp + pixel_offset;
    BOOLEAN top_down = (height < 0);
    for (UINTN y = 0; y < draw_h; y++) {
        UINTN src_row = src_y + y;
        UINTN bmp_row = top_down ? src_row : (img_h - 1 - src_row);
        const UINT8 *row = pixel_base + bmp_row * row_stride;
        const UINT8 *src = row + src_x * bpp;
        for (UINTN x = 0; x < draw_w; x++) {
            UINT8 b = src[x * bpp + 0];
            UINT8 g = src[x * bpp + 1];
            UINT8 r = src[x * bpp + 2];
            gfx_put_pixel(shell->gfx, dst_x + x, dst_y + y, ((UINT32)r << 16) | ((UINT32)g << 8) | b);
        }
    }

    return TRUE;
}

static EFI_STATUS shell_ensure_dir(Shell *shell, const char *path) {
    EFI_FILE_PROTOCOL *dir = NULL;
    EFI_STATUS status = shell_open_path(
        shell,
        path,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        EFI_FILE_DIRECTORY,
        &dir
    );
    if (!EFI_ERROR(status) && dir != NULL) {
        uefi_call_wrapper(dir->Close, 1, dir);
    }
    return status;
}

static EFI_STATUS shell_ensure_dir_tree(Shell *shell, const char *path) {
    char abs[SHELL_PATH_MAX];
    if (!shell_normalize_path(shell->cwd, path, abs, sizeof(abs))) {
        return EFI_INVALID_PARAMETER;
    }
    if (abs[0] == '\\' && abs[1] == '\0') {
        return EFI_SUCCESS;
    }

    char partial[SHELL_PATH_MAX];
    partial[0] = '\\';
    partial[1] = '\0';
    UINTN p = 1;

    UINTN i = 1;
    while (abs[i] != '\0') {
        while (abs[i] == '\\') {
            i++;
        }
        if (abs[i] == '\0') {
            break;
        }

        char token[SHELL_PATH_MAX];
        UINTN t = 0;
        while (abs[i] != '\0' && abs[i] != '\\' && t + 1 < sizeof(token)) {
            token[t++] = abs[i++];
        }
        token[t] = '\0';
        if (token[0] == '\0') {
            continue;
        }

        if (p > 1) {
            if (p + 1 >= sizeof(partial)) {
                return EFI_INVALID_PARAMETER;
            }
            partial[p++] = '\\';
        }
        for (UINTN k = 0; token[k] != '\0'; k++) {
            if (p + 1 >= sizeof(partial)) {
                return EFI_INVALID_PARAMETER;
            }
            partial[p++] = token[k];
        }
        partial[p] = '\0';

        EFI_STATUS status = shell_ensure_dir(shell, partial);
        if (EFI_ERROR(status)) {
            return status;
        }
    }

    return EFI_SUCCESS;
}

static BOOLEAN shell_parse_ls_args(const char *arg, BOOLEAN *long_mode, char *path_out, UINTN path_out_len) {
    if (long_mode == NULL || path_out == NULL || path_out_len < 2) {
        return FALSE;
    }

    *long_mode = FALSE;
    path_out[0] = '.';
    path_out[1] = '\0';

    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        return TRUE;
    }

    if (u_strcmp(raw, "-l") == 0) {
        *long_mode = TRUE;
        return TRUE;
    }

    if (u_startswith(raw, "-l ")) {
        *long_mode = TRUE;
        raw = u_trim_left((char *)(raw + 2));
    }

    UINTN i = 0;
    while (raw[i] != '\0' && i + 1 < path_out_len) {
        path_out[i] = raw[i];
        i++;
    }
    path_out[i] = '\0';
    return TRUE;
}

static void shell_print_history(Shell *shell) {
    if (shell->history_count == 0) {
        shell_println(shell, "history: empty");
        return;
    }
    for (UINTN i = 0; i < shell->history_count; i++) {
        shell_print_u64(shell, i + 1);
        shell_print(shell, "  ");
        shell_println(shell, shell->history[i]);
    }
}

static void shell_print_help(Shell *shell, const char *topic) {
    if (topic == NULL || topic[0] == '\0') {
        shell_println(shell, "Commands:");
        shell_println(shell, "  help [cmd]      - list commands or command help");
        shell_println(shell, "  clear           - clear the screen");
        shell_println(shell, "  echo <text>     - print text");
        shell_println(shell, "  pwd             - print current directory");
        shell_println(shell, "  cd <path>       - change current directory");
        shell_println(shell, "  ls [-l] [path]  - list files");
        shell_println(shell, "  cat <path>      - print file contents");
        shell_println(shell, "  mkdir [-p] <p>  - create directory");
        shell_println(shell, "  touch <p>       - create empty file");
        shell_println(shell, "  cp <s> <d>      - copy file");
        shell_println(shell, "  rm <path>       - delete file");
        shell_println(shell, "  mv <s> <d>      - move/rename file");
        shell_println(shell, "  hexdump <path>  - hex view of file");
        shell_println(shell, "  history         - show command history");
        shell_println(shell, "  viewbmp <path>  - full-screen BMP preview");
        shell_println(shell, "  initfs          - create /HATTEROS tree");
        shell_println(shell, "  theme ...       - shell colors/prompt");
        shell_println(shell, "  time            - read UEFI clock");
        shell_println(shell, "  memmap          - summarize memory map");
        shell_println(shell, "  info            - show system info");
        shell_println(shell, "  reboot          - reboot machine");
        return;
    }

    if (u_strcmp(topic, "ls") == 0) {
        shell_println(shell, "ls [-l] [path]");
        shell_println(shell, "  -l shows type, size, and modified timestamp.");
        return;
    }
    if (u_strcmp(topic, "mkdir") == 0) {
        shell_println(shell, "mkdir [-p] <path>");
        shell_println(shell, "  -p creates missing parent directories.");
        return;
    }
    if (u_strcmp(topic, "theme") == 0) {
        shell_println(shell, "theme default|light|amber|prompt <full|short>");
        shell_println(shell, "  Changes are saved to /HATTEROS/system/config/shell.cfg.");
        return;
    }
    if (u_strcmp(topic, "initfs") == 0) {
        shell_println(shell, "initfs");
        shell_println(shell, "  Creates /HATTEROS/system/*, /HATTEROS/user/*, /HATTEROS/bin.");
        return;
    }
    if (u_strcmp(topic, "viewbmp") == 0) {
        shell_println(shell, "viewbmp <path>");
        shell_println(shell, "  Supports uncompressed 24-bit or 32-bit BMP.");
        return;
    }

    shell_print(shell, "No detailed help for: ");
    shell_println(shell, topic);
}

// Open ESP root directory where this EFI app was loaded from.
static EFI_STATUS shell_open_root(Shell *shell, EFI_FILE_PROTOCOL **root) {
    if (root == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    *root = NULL;

    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;

    EFI_STATUS status = uefi_call_wrapper(
        shell->st->BootServices->HandleProtocol,
        3,
        shell->image_handle,
        &loaded_image_guid,
        (void **)&loaded
    );
    if (EFI_ERROR(status) || loaded == NULL) {
        return EFI_NOT_FOUND;
    }

    status = uefi_call_wrapper(
        shell->st->BootServices->HandleProtocol,
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

static EFI_STATUS shell_open_path(Shell *shell, const char *path, UINT64 mode, UINT64 attrs, EFI_FILE_PROTOCOL **out) {
    if (out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *out = NULL;

    char resolved[SHELL_PATH_MAX];
    CHAR16 path16[SHELL_PATH_MAX];
    if (!shell_normalize_path(shell->cwd, path, resolved, sizeof(resolved)) ||
        !shell_path_to_char16(resolved, path16, SHELL_PATH_MAX)) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_FILE_PROTOCOL *root = NULL;
    EFI_STATUS status = shell_open_root(shell, &root);
    if (EFI_ERROR(status) || root == NULL) {
        return status;
    }

    status = uefi_call_wrapper(root->Open, 5, root, out, path16, mode, attrs);
    uefi_call_wrapper(root->Close, 1, root);
    return status;
}

static EFI_FILE_INFO *shell_get_file_info(Shell *shell, EFI_FILE_PROTOCOL *file, EFI_STATUS *out_status) {
    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    UINTN info_size = 0;
    EFI_STATUS status = uefi_call_wrapper(file->GetInfo, 4, file, &file_info_guid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size == 0) {
        if (out_status != NULL) {
            *out_status = status;
        }
        return NULL;
    }

    EFI_FILE_INFO *info = (EFI_FILE_INFO *)shell_alloc(shell, info_size);
    if (info == NULL) {
        if (out_status != NULL) {
            *out_status = EFI_OUT_OF_RESOURCES;
        }
        return NULL;
    }

    status = uefi_call_wrapper(file->GetInfo, 4, file, &file_info_guid, &info_size, info);
    if (EFI_ERROR(status)) {
        shell_free(shell, info);
        if (out_status != NULL) {
            *out_status = status;
        }
        return NULL;
    }

    if (out_status != NULL) {
        *out_status = EFI_SUCCESS;
    }
    return info;
}

// Normalize relative/absolute shell paths into canonical absolute form.
// Examples (cwd="\\docs"): "a.txt" -> "\\docs\\a.txt", "../x" -> "\\x".
static BOOLEAN shell_normalize_path(const char *cwd, const char *input, char *out, UINTN out_len) {
    if (cwd == NULL || input == NULL || out == NULL || out_len < 2) {
        return FALSE;
    }

    char full[SHELL_PATH_MAX];
    UINTN f = 0;
    const char *p = u_trim_left((char *)input);
    BOOLEAN absolute = (*p == '\\' || *p == '/');

    if (!absolute) {
        UINTN i = 0;
        while (cwd[i] != '\0' && f + 1 < sizeof(full)) {
            full[f++] = cwd[i++];
        }
        if (f == 0) {
            full[f++] = '\\';
        }
        if (f > 1 && full[f - 1] != '\\') {
            if (f + 1 >= sizeof(full)) {
                return FALSE;
            }
            full[f++] = '\\';
        }
    } else {
        full[f++] = '\\';
        p++;
    }

    while (*p != '\0') {
        if (f + 1 >= sizeof(full)) {
            return FALSE;
        }
        char c = *p++;
        full[f++] = (c == '/') ? '\\' : c;
    }
    full[f] = '\0';

    if (full[0] == '\0') {
        full[0] = '\\';
        full[1] = '\0';
    }

    UINTN seg_start[64];
    UINTN depth = 0;
    UINTN o = 1;
    out[0] = '\\';
    out[1] = '\0';

    UINTN i = 0;
    while (full[i] != '\0') {
        while (full[i] == '\\') {
            i++;
        }
        if (full[i] == '\0') {
            break;
        }

        char token[SHELL_PATH_MAX];
        UINTN t = 0;
        while (full[i] != '\0' && full[i] != '\\' && t + 1 < sizeof(token)) {
            token[t++] = full[i++];
        }
        token[t] = '\0';

        if (u_strcmp(token, ".") == 0 || token[0] == '\0') {
            continue;
        }

        if (u_strcmp(token, "..") == 0) {
            if (depth > 0) {
                o = seg_start[depth - 1];
                if (o > 1) {
                    o--;
                }
                depth--;
                out[o] = '\0';
            }
            continue;
        }

        if (depth >= 64) {
            return FALSE;
        }

        if (o > 1) {
            if (o + 1 >= out_len) {
                return FALSE;
            }
            out[o++] = '\\';
        }
        seg_start[depth++] = o;

        for (UINTN k = 0; token[k] != '\0'; k++) {
            if (o + 1 >= out_len) {
                return FALSE;
            }
            out[o++] = token[k];
        }
        out[o] = '\0';
    }

    if (o == 0) {
        out[0] = '\\';
        out[1] = '\0';
    }
    return TRUE;
}

// Convert normalized ASCII path into UEFI CHAR16 path.
static BOOLEAN shell_path_to_char16(const char *path, CHAR16 *out, UINTN out_len) {
    if (path == NULL || out == NULL || out_len < 2) {
        return FALSE;
    }

    UINTN i = 0;
    while (path[i] != '\0') {
        if (i + 1 >= out_len) {
            return FALSE;
        }
        out[i] = (CHAR16)(UINT8)path[i];
        i++;
    }
    out[i] = '\0';
    return TRUE;
}

// Render a CHAR16 filename as best-effort ASCII.
static void shell_print_file_name(Shell *shell, const CHAR16 *name) {
    char line[256];
    UINTN i = 0;
    while (name != NULL && name[i] != 0 && i + 1 < sizeof(line)) {
        CHAR16 wc = name[i];
        line[i] = (wc >= 32 && wc <= 126) ? (char)wc : '?';
        i++;
    }
    line[i] = '\0';
    shell_println(shell, line);
}

// `ls [path]` implementation.
// If path is a file, print that entry; if path is a directory, iterate entries.
static void shell_cmd_ls(Shell *shell, const char *arg) {
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *dir = NULL;
    CHAR16 path16[SHELL_PATH_MAX];
    char resolved[SHELL_PATH_MAX];
    char path_arg[SHELL_PATH_MAX];
    BOOLEAN long_mode = FALSE;
    shell_parse_ls_args(arg, &long_mode, path_arg, sizeof(path_arg));

    if (!shell_normalize_path(shell->cwd, path_arg, resolved, sizeof(resolved)) ||
        !shell_path_to_char16(resolved, path16, SHELL_PATH_MAX)) {
        shell_println(shell, "ls: path too long");
        return;
    }

    EFI_STATUS status = shell_open_root(shell, &root);
    if (EFI_ERROR(status) || root == NULL) {
        shell_print_error_status(shell, "ls filesystem open failed", status);
        return;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &dir, path16, EFI_FILE_MODE_READ, 0);
    uefi_call_wrapper(root->Close, 1, root);
    if (EFI_ERROR(status) || dir == NULL) {
        shell_print_error_status(shell, "ls open path failed", status);
        return;
    }

    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    UINTN meta_size = 0;
    status = uefi_call_wrapper(dir->GetInfo, 4, dir, &file_info_guid, &meta_size, NULL);
    if (status == EFI_BUFFER_TOO_SMALL && meta_size > 0) {
        EFI_FILE_INFO *meta = (EFI_FILE_INFO *)shell_alloc(shell, meta_size);
        if (meta != NULL) {
            status = uefi_call_wrapper(dir->GetInfo, 4, dir, &file_info_guid, &meta_size, meta);
            if (!EFI_ERROR(status) && (meta->Attribute & EFI_FILE_DIRECTORY) == 0) {
                if (long_mode) {
                    char size_buf[32];
                    u_u64_to_dec(meta->FileSize, size_buf, sizeof(size_buf));
                    shell_print(shell, "[FIL] ");
                    shell_print(shell, size_buf);
                    shell_print(shell, "  ");
                    if (meta->ModificationTime.Year > 0) {
                        shell_print_u64(shell, meta->ModificationTime.Year);
                        shell_putc(shell, '-');
                        shell_print_padded_u64(shell, meta->ModificationTime.Month, 2);
                        shell_putc(shell, '-');
                        shell_print_padded_u64(shell, meta->ModificationTime.Day, 2);
                        shell_putc(shell, ' ');
                        shell_print_padded_u64(shell, meta->ModificationTime.Hour, 2);
                        shell_putc(shell, ':');
                        shell_print_padded_u64(shell, meta->ModificationTime.Minute, 2);
                    } else {
                        shell_print(shell, "---- -- -- --:--");
                    }
                    shell_print(shell, "  ");
                } else {
                    shell_print(shell, "      ");
                }
                shell_print_file_name(shell, meta->FileName);
                shell_free(shell, meta);
                uefi_call_wrapper(dir->Close, 1, dir);
                return;
            }
            shell_free(shell, meta);
        }
    }

    UINTN info_buf_size = SIZE_OF_EFI_FILE_INFO + 512;
    EFI_FILE_INFO *info = (EFI_FILE_INFO *)shell_alloc(shell, info_buf_size);
    if (info == NULL) {
        shell_println(shell, "ls: out of memory");
        uefi_call_wrapper(dir->Close, 1, dir);
        return;
    }

    uefi_call_wrapper(dir->SetPosition, 2, dir, 0);
    while (1) {
        UINTN read_size = info_buf_size;
        status = uefi_call_wrapper(dir->Read, 3, dir, &read_size, info);
        if (status == EFI_BUFFER_TOO_SMALL && read_size > info_buf_size) {
            // Some filesystems return variable-sized file info records.
            shell_free(shell, info);
            info_buf_size = read_size;
            info = (EFI_FILE_INFO *)shell_alloc(shell, info_buf_size);
            if (info == NULL) {
                shell_println(shell, "ls: out of memory");
                break;
            }
            continue;
        }
        if (EFI_ERROR(status) || read_size == 0) {
            break;
        }

        if (info->FileName[0] == 0) {
            continue;
        }

        if (long_mode) {
            char size_buf[32];
            u_u64_to_dec(info->FileSize, size_buf, sizeof(size_buf));
            shell_print(shell, (info->Attribute & EFI_FILE_DIRECTORY) ? "[DIR] " : "[FIL] ");
            shell_print(shell, size_buf);
            shell_print(shell, "  ");
            if (info->ModificationTime.Year > 0) {
                shell_print_u64(shell, info->ModificationTime.Year);
                shell_putc(shell, '-');
                shell_print_padded_u64(shell, info->ModificationTime.Month, 2);
                shell_putc(shell, '-');
                shell_print_padded_u64(shell, info->ModificationTime.Day, 2);
                shell_putc(shell, ' ');
                shell_print_padded_u64(shell, info->ModificationTime.Hour, 2);
                shell_putc(shell, ':');
                shell_print_padded_u64(shell, info->ModificationTime.Minute, 2);
            } else {
                shell_print(shell, "---- -- -- --:--");
            }
            shell_print(shell, "  ");
        } else if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) {
            shell_print(shell, "[DIR] ");
        } else {
            shell_print(shell, "      ");
        }
        shell_print_file_name(shell, info->FileName);
    }

    shell_free(shell, info);
    uefi_call_wrapper(dir->Close, 1, dir);
}

// `cat <path>` implementation (text-oriented viewer).
static void shell_cmd_cat(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        shell_println(shell, "cat: usage: cat <path>");
        return;
    }

    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    CHAR16 path16[SHELL_PATH_MAX];
    char resolved[SHELL_PATH_MAX];

    if (!shell_normalize_path(shell->cwd, raw, resolved, sizeof(resolved)) ||
        !shell_path_to_char16(resolved, path16, SHELL_PATH_MAX)) {
        shell_println(shell, "cat: path too long");
        return;
    }

    EFI_STATUS status = shell_open_root(shell, &root);
    if (EFI_ERROR(status) || root == NULL) {
        shell_print_error_status(shell, "cat filesystem open failed", status);
        return;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &file, path16, EFI_FILE_MODE_READ, 0);
    uefi_call_wrapper(root->Close, 1, root);
    if (EFI_ERROR(status) || file == NULL) {
        shell_print_error_status(shell, "cat open failed", status);
        return;
    }

    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    UINTN info_size = 0;
    status = uefi_call_wrapper(file->GetInfo, 4, file, &file_info_guid, &info_size, NULL);
    if (status == EFI_BUFFER_TOO_SMALL && info_size > 0) {
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)shell_alloc(shell, info_size);
        if (info != NULL) {
            status = uefi_call_wrapper(file->GetInfo, 4, file, &file_info_guid, &info_size, info);
            if (!EFI_ERROR(status) && (info->Attribute & EFI_FILE_DIRECTORY) != 0) {
                shell_println(shell, "cat: path is a directory");
                shell_free(shell, info);
                uefi_call_wrapper(file->Close, 1, file);
                return;
            }
            shell_free(shell, info);
        }
    }

    UINT8 *buf = (UINT8 *)shell_alloc(shell, FILE_IO_CHUNK);
    if (buf == NULL) {
        shell_println(shell, "cat: out of memory");
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }

    while (1) {
        UINTN read_size = FILE_IO_CHUNK;
        status = uefi_call_wrapper(file->Read, 3, file, &read_size, buf);
        if (EFI_ERROR(status) || read_size == 0) {
            break;
        }

        for (UINTN i = 0; i < read_size; i++) {
            char c = (char)buf[i];
            if (c == '\r') {
                continue;
            }
            // Keep display stable for non-printable bytes.
            if (c == '\n' || c == '\t' || (c >= 32 && c <= 126)) {
                shell_putc(shell, c);
            } else {
                shell_putc(shell, '.');
            }
        }
    }

    shell_putc(shell, '\n');
    shell_free(shell, buf);
    uefi_call_wrapper(file->Close, 1, file);
}

// `cd <path>` implementation.
static void shell_cmd_cd(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        shell_println(shell, "cd: usage: cd <path>");
        return;
    }

    char resolved[SHELL_PATH_MAX];
    CHAR16 path16[SHELL_PATH_MAX];
    if (!shell_normalize_path(shell->cwd, raw, resolved, sizeof(resolved)) ||
        !shell_path_to_char16(resolved, path16, SHELL_PATH_MAX)) {
        shell_println(shell, "cd: path too long");
        return;
    }

    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *node = NULL;
    EFI_STATUS status = shell_open_root(shell, &root);
    if (EFI_ERROR(status) || root == NULL) {
        shell_print_error_status(shell, "cd filesystem open failed", status);
        return;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &node, path16, EFI_FILE_MODE_READ, 0);
    uefi_call_wrapper(root->Close, 1, root);
    if (EFI_ERROR(status) || node == NULL) {
        shell_print_error_status(shell, "cd open failed", status);
        return;
    }

    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    UINTN info_size = 0;
    status = uefi_call_wrapper(node->GetInfo, 4, node, &file_info_guid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size == 0) {
        shell_println(shell, "cd: cannot query path");
        uefi_call_wrapper(node->Close, 1, node);
        return;
    }

    EFI_FILE_INFO *info = (EFI_FILE_INFO *)shell_alloc(shell, info_size);
    if (info == NULL) {
        shell_println(shell, "cd: out of memory");
        uefi_call_wrapper(node->Close, 1, node);
        return;
    }

    status = uefi_call_wrapper(node->GetInfo, 4, node, &file_info_guid, &info_size, info);
    if (EFI_ERROR(status)) {
        shell_println(shell, "cd: cannot query path");
        shell_free(shell, info);
        uefi_call_wrapper(node->Close, 1, node);
        return;
    }

    if ((info->Attribute & EFI_FILE_DIRECTORY) == 0) {
        shell_println(shell, "cd: target is not a directory");
        shell_free(shell, info);
        uefi_call_wrapper(node->Close, 1, node);
        return;
    }

    UINTN i = 0;
    while (resolved[i] != '\0' && i + 1 < sizeof(shell->cwd)) {
        shell->cwd[i] = resolved[i];
        i++;
    }
    shell->cwd[i] = '\0';

    shell_free(shell, info);
    uefi_call_wrapper(node->Close, 1, node);
}

// `pwd` implementation.
static void shell_cmd_pwd(Shell *shell) {
    if (shell->cwd[0] == '\\' && shell->cwd[1] == '\0') {
        shell_println(shell, "/");
        return;
    }

    char out[SHELL_PATH_MAX];
    UINTN i = 0;
    while (shell->cwd[i] != '\0' && i + 1 < sizeof(out)) {
        out[i] = (shell->cwd[i] == '\\') ? '/' : shell->cwd[i];
        i++;
    }
    out[i] = '\0';
    shell_println(shell, out);
}

static void shell_history_add(Shell *shell, const char *line) {
    if (shell == NULL || line == NULL || line[0] == '\0') {
        return;
    }

    if (shell->history_count > 0 &&
        u_strcmp(shell->history[shell->history_count - 1], line) == 0) {
        return;
    }

    if (shell->history_count == SHELL_HISTORY_MAX) {
        for (UINTN i = 1; i < SHELL_HISTORY_MAX; i++) {
            UINTN j = 0;
            while (shell->history[i][j] != '\0' && j + 1 < SHELL_INPUT_MAX) {
                shell->history[i - 1][j] = shell->history[i][j];
                j++;
            }
            shell->history[i - 1][j] = '\0';
        }
        shell->history_count--;
    }

    UINTN idx = shell->history_count++;
    UINTN i = 0;
    while (line[i] != '\0' && i + 1 < SHELL_INPUT_MAX) {
        shell->history[idx][i] = line[i];
        i++;
    }
    shell->history[idx][i] = '\0';
}

static void shell_cmd_mkdir(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        shell_println(shell, "mkdir: usage: mkdir [-p] <path>");
        return;
    }

    BOOLEAN parents = FALSE;
    if (u_strcmp(raw, "-p") == 0) {
        shell_println(shell, "mkdir: usage: mkdir [-p] <path>");
        return;
    }
    if (u_startswith(raw, "-p ")) {
        parents = TRUE;
        raw = u_trim_left((char *)(raw + 2));
        if (*raw == '\0') {
            shell_println(shell, "mkdir: usage: mkdir [-p] <path>");
            return;
        }
    }

    if (parents) {
        EFI_STATUS st = shell_ensure_dir_tree(shell, raw);
        if (EFI_ERROR(st)) {
            shell_print_error_status(shell, "mkdir -p failed", st);
        }
        return;
    }

    EFI_FILE_PROTOCOL *dir = NULL;
    EFI_STATUS status = shell_open_path(
        shell,
        raw,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        EFI_FILE_DIRECTORY,
        &dir
    );
    if (EFI_ERROR(status) || dir == NULL) {
        shell_print_error_status(shell, "mkdir failed", status);
        return;
    }

    EFI_STATUS info_status = EFI_SUCCESS;
    EFI_FILE_INFO *info = shell_get_file_info(shell, dir, &info_status);
    if (info == NULL) {
        shell_print_error_status(shell, "mkdir info failed", info_status);
        uefi_call_wrapper(dir->Close, 1, dir);
        return;
    }

    if ((info->Attribute & EFI_FILE_DIRECTORY) == 0) {
        shell_println(shell, "mkdir: path exists and is not a directory");
    }

    shell_free(shell, info);
    uefi_call_wrapper(dir->Close, 1, dir);
}

static void shell_cmd_touch(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        shell_println(shell, "touch: usage: touch <path>");
        return;
    }

    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status = shell_open_path(
        shell,
        raw,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        0,
        &file
    );
    if (EFI_ERROR(status) || file == NULL) {
        shell_print_error_status(shell, "touch failed", status);
        return;
    }

    EFI_STATUS info_status = EFI_SUCCESS;
    EFI_FILE_INFO *info = shell_get_file_info(shell, file, &info_status);
    if (info == NULL) {
        shell_print_error_status(shell, "touch info failed", info_status);
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }

    if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) {
        shell_println(shell, "touch: path is a directory");
    }

    shell_free(shell, info);
    uefi_call_wrapper(file->Close, 1, file);
}

static EFI_STATUS shell_copy_file(Shell *shell, const char *src_raw, const char *dst_raw) {
    char src_abs[SHELL_PATH_MAX];
    char dst_abs[SHELL_PATH_MAX];
    if (!shell_normalize_path(shell->cwd, src_raw, src_abs, sizeof(src_abs)) ||
        !shell_normalize_path(shell->cwd, dst_raw, dst_abs, sizeof(dst_abs))) {
        return EFI_INVALID_PARAMETER;
    }
    if (u_strcmp(src_abs, dst_abs) == 0) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_FILE_PROTOCOL *src = NULL;
    EFI_FILE_PROTOCOL *dst = NULL;
    UINT8 *buf = NULL;
    EFI_FILE_INFO *src_info = NULL;
    EFI_FILE_INFO *dst_info = NULL;
    EFI_STATUS status = shell_open_path(shell, src_raw, EFI_FILE_MODE_READ, 0, &src);
    if (EFI_ERROR(status) || src == NULL) {
        goto out;
    }

    EFI_STATUS info_status = EFI_SUCCESS;
    src_info = shell_get_file_info(shell, src, &info_status);
    if (src_info == NULL) {
        status = info_status;
        goto out;
    }
    if ((src_info->Attribute & EFI_FILE_DIRECTORY) != 0) {
        status = EFI_ACCESS_DENIED;
        goto out;
    }

    status = shell_open_path(shell, dst_raw, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0, &dst);
    if (EFI_ERROR(status) || dst == NULL) {
        goto out;
    }

    dst_info = shell_get_file_info(shell, dst, &info_status);
    if (dst_info == NULL) {
        status = info_status;
        goto out;
    }
    if ((dst_info->Attribute & EFI_FILE_DIRECTORY) != 0) {
        status = EFI_ACCESS_DENIED;
        goto out;
    }

    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    dst_info->FileSize = 0;
    dst_info->PhysicalSize = 0;
    status = uefi_call_wrapper(dst->SetInfo, 4, dst, &file_info_guid, dst_info->Size, dst_info);
    if (EFI_ERROR(status)) {
        goto out;
    }

    uefi_call_wrapper(src->SetPosition, 2, src, 0);
    uefi_call_wrapper(dst->SetPosition, 2, dst, 0);

    buf = (UINT8 *)shell_alloc(shell, FILE_IO_CHUNK);
    if (buf == NULL) {
        status = EFI_OUT_OF_RESOURCES;
        goto out;
    }

    while (1) {
        UINTN read_size = FILE_IO_CHUNK;
        status = uefi_call_wrapper(src->Read, 3, src, &read_size, buf);
        if (EFI_ERROR(status)) {
            goto out;
        }
        if (read_size == 0) {
            break;
        }

        UINTN write_size = read_size;
        status = uefi_call_wrapper(dst->Write, 3, dst, &write_size, buf);
        if (EFI_ERROR(status) || write_size != read_size) {
            status = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
            goto out;
        }
    }
    status = EFI_SUCCESS;

out:
    if (src_info != NULL) {
        shell_free(shell, src_info);
    }
    if (dst_info != NULL) {
        shell_free(shell, dst_info);
    }
    if (buf != NULL) {
        shell_free(shell, buf);
    }
    if (src != NULL) {
        uefi_call_wrapper(src->Close, 1, src);
    }
    if (dst != NULL) {
        uefi_call_wrapper(dst->Close, 1, dst);
    }
    return status;
}

static void shell_cmd_cp(Shell *shell, const char *src_arg, const char *dst_arg) {
    const char *src_raw = (src_arg != NULL) ? src_arg : "";
    const char *dst_raw = (dst_arg != NULL) ? dst_arg : "";
    src_raw = u_trim_left((char *)src_raw);
    dst_raw = u_trim_left((char *)dst_raw);
    if (*src_raw == '\0' || *dst_raw == '\0') {
        shell_println(shell, "cp: usage: cp <src> <dst>");
        return;
    }

    EFI_STATUS st = shell_copy_file(shell, src_raw, dst_raw);
    if (EFI_ERROR(st)) {
        shell_print_error_status(shell, "cp failed", st);
    }
}

static void shell_cmd_rm(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        shell_println(shell, "rm: usage: rm <path>");
        return;
    }

    EFI_FILE_PROTOCOL *node = NULL;
    EFI_STATUS status = shell_open_path(shell, raw, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &node);
    if (EFI_ERROR(status) || node == NULL) {
        shell_print_error_status(shell, "rm open failed", status);
        return;
    }

    EFI_STATUS info_status = EFI_SUCCESS;
    EFI_FILE_INFO *info = shell_get_file_info(shell, node, &info_status);
    if (info == NULL) {
        shell_print_error_status(shell, "rm info failed", info_status);
        uefi_call_wrapper(node->Close, 1, node);
        return;
    }
    if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) {
        shell_println(shell, "rm: refusing to remove a directory");
        shell_free(shell, info);
        uefi_call_wrapper(node->Close, 1, node);
        return;
    }
    shell_free(shell, info);

    status = uefi_call_wrapper(node->Delete, 1, node);
    if (EFI_ERROR(status)) {
        shell_print_error_status(shell, "rm delete failed", status);
    }
}

static void shell_cmd_mv(Shell *shell, const char *src_arg, const char *dst_arg) {
    const char *src_raw = (src_arg != NULL) ? src_arg : "";
    const char *dst_raw = (dst_arg != NULL) ? dst_arg : "";
    src_raw = u_trim_left((char *)src_raw);
    dst_raw = u_trim_left((char *)dst_raw);
    if (*src_raw == '\0' || *dst_raw == '\0') {
        shell_println(shell, "mv: usage: mv <src> <dst>");
        return;
    }

    EFI_STATUS st = shell_copy_file(shell, src_raw, dst_raw);
    if (EFI_ERROR(st)) {
        shell_print_error_status(shell, "mv copy failed", st);
        return;
    }

    EFI_FILE_PROTOCOL *node = NULL;
    st = shell_open_path(shell, src_raw, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &node);
    if (EFI_ERROR(st) || node == NULL) {
        shell_print_error_status(shell, "mv cleanup open failed", st);
        return;
    }
    st = uefi_call_wrapper(node->Delete, 1, node);
    if (EFI_ERROR(st)) {
        shell_print_error_status(shell, "mv cleanup delete failed", st);
    }
}

static void shell_cmd_hexdump(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        shell_println(shell, "hexdump: usage: hexdump <path>");
        return;
    }

    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status = shell_open_path(shell, raw, EFI_FILE_MODE_READ, 0, &file);
    if (EFI_ERROR(status) || file == NULL) {
        shell_print_error_status(shell, "hexdump open failed", status);
        return;
    }

    EFI_STATUS info_status = EFI_SUCCESS;
    EFI_FILE_INFO *info = shell_get_file_info(shell, file, &info_status);
    if (info != NULL) {
        if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) {
            shell_println(shell, "hexdump: path is a directory");
            shell_free(shell, info);
            uefi_call_wrapper(file->Close, 1, file);
            return;
        }
        shell_free(shell, info);
    }

    UINT8 *buf = (UINT8 *)shell_alloc(shell, FILE_IO_CHUNK);
    if (buf == NULL) {
        shell_println(shell, "hexdump: out of memory");
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }

    UINT64 offset = 0;
    while (1) {
        UINTN read_size = FILE_IO_CHUNK;
        status = uefi_call_wrapper(file->Read, 3, file, &read_size, buf);
        if (EFI_ERROR(status) || read_size == 0) {
            break;
        }

        for (UINTN row = 0; row < read_size; row += HEXDUMP_COLS) {
            UINTN row_len = ((read_size - row) > HEXDUMP_COLS) ? HEXDUMP_COLS : (read_size - row);
            char off[32];
            u_u64_to_hex(offset + row, off, sizeof(off));
            shell_print(shell, off);
            shell_print(shell, ": ");

            for (UINTN i = 0; i < HEXDUMP_COLS; i++) {
                if (i < row_len) {
                    shell_print_padded_hex8(shell, buf[row + i]);
                    shell_putc(shell, ' ');
                } else {
                    shell_print(shell, "   ");
                }
            }

            shell_print(shell, " |");
            for (UINTN i = 0; i < row_len; i++) {
                char c = (char)buf[row + i];
                if (c < 32 || c > 126) {
                    c = '.';
                }
                shell_putc(shell, c);
            }
            shell_println(shell, "|");
        }

        offset += read_size;
    }

    shell_free(shell, buf);
    uefi_call_wrapper(file->Close, 1, file);
}

static void shell_cmd_history(Shell *shell) {
    shell_print_history(shell);
}

static void shell_cmd_viewbmp(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);
    if (*raw == '\0') {
        shell_println(shell, "viewbmp: usage: viewbmp <path>");
        return;
    }

    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status = shell_open_path(shell, raw, EFI_FILE_MODE_READ, 0, &file);
    if (EFI_ERROR(status) || file == NULL) {
        shell_print_error_status(shell, "viewbmp open failed", status);
        return;
    }

    EFI_STATUS info_status = EFI_SUCCESS;
    EFI_FILE_INFO *info = shell_get_file_info(shell, file, &info_status);
    if (info == NULL) {
        shell_print_error_status(shell, "viewbmp info failed", info_status);
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }
    if ((info->Attribute & EFI_FILE_DIRECTORY) != 0 || info->FileSize == 0 || info->FileSize > 32U * 1024U * 1024U) {
        shell_println(shell, "viewbmp: invalid file");
        shell_free(shell, info);
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }

    UINTN size = (UINTN)info->FileSize;
    shell_free(shell, info);
    UINT8 *data = (UINT8 *)shell_alloc(shell, size);
    if (data == NULL) {
        shell_println(shell, "viewbmp: out of memory");
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }

    UINTN read_size = size;
    status = uefi_call_wrapper(file->Read, 3, file, &read_size, data);
    uefi_call_wrapper(file->Close, 1, file);
    if (EFI_ERROR(status) || read_size != size) {
        shell_print_error_status(shell, "viewbmp read failed", status);
        shell_free(shell, data);
        return;
    }

    if (!shell_draw_bmp_centered(shell, data, size)) {
        shell_println(shell, "viewbmp: unsupported BMP (need uncompressed 24/32-bit)");
        shell_free(shell, data);
        return;
    }
    shell_free(shell, data);

    const char *hint = "Press any key to return...";
    UINTN hint_w = font_text_width(hint, 2);
    UINTN hint_x = (shell->gfx->width > hint_w) ? (shell->gfx->width - hint_w) / 2 : 8;
    UINTN hint_y = shell->gfx->height - 48;
    font_draw_text(shell->gfx, hint_x, hint_y, hint, 0xF0F0F0, 0x000000, 2, TRUE);

    if (shell->st != NULL && shell->st->BootServices != NULL && shell->st->ConIn != NULL) {
        UINTN idx;
        EFI_EVENT event = shell->st->ConIn->WaitForKey;
        uefi_call_wrapper(shell->st->BootServices->WaitForEvent, 3, 1, &event, &idx);
        EFI_INPUT_KEY key;
        uefi_call_wrapper(shell->st->ConIn->ReadKeyStroke, 2, shell->st->ConIn, &key);
    }

    shell_clear(shell);
}

static void shell_cmd_initfs(Shell *shell) {
    static const char *paths[] = {
        "\\HATTEROS",
        "\\HATTEROS\\system",
        "\\HATTEROS\\system\\config",
        "\\HATTEROS\\system\\log",
        "\\HATTEROS\\system\\assets",
        "\\HATTEROS\\system\\tmp",
        "\\HATTEROS\\user",
        "\\HATTEROS\\user\\home",
        "\\HATTEROS\\user\\docs",
        "\\HATTEROS\\bin",
    };

    for (UINTN i = 0; i < (sizeof(paths) / sizeof(paths[0])); i++) {
        EFI_STATUS st = shell_ensure_dir_tree(shell, paths[i]);
        if (EFI_ERROR(st)) {
            shell_print(shell, "initfs failed at ");
            shell_println(shell, paths[i]);
            shell_print_error_status(shell, "initfs", st);
            return;
        }
    }

    shell_println(shell, "initfs: /HATTEROS directory tree ready");
}

static void shell_cmd_theme(Shell *shell, const char *arg) {
    const char *raw = (arg != NULL) ? arg : "";
    raw = u_trim_left((char *)raw);

    if (*raw == '\0') {
        shell_println(shell, "theme usage:");
        shell_println(shell, "  theme default");
        shell_println(shell, "  theme light");
        shell_println(shell, "  theme amber");
        shell_println(shell, "  theme prompt full");
        shell_println(shell, "  theme prompt short");
        shell_print(shell, "  current prompt mode: ");
        shell_println(shell, shell->prompt_show_path ? "full" : "short");
        return;
    }

    if (u_strcmp(raw, "default") == 0) {
        shell_apply_theme(shell, 0xE8E8E8, 0x10161E, TRUE);
        shell_save_settings(shell);
        shell_println(shell, "theme: default");
        return;
    }

    if (u_strcmp(raw, "light") == 0) {
        shell_apply_theme(shell, 0x101820, 0xE7EDF4, TRUE);
        shell_save_settings(shell);
        shell_println(shell, "theme: light");
        return;
    }

    if (u_strcmp(raw, "amber") == 0) {
        shell_apply_theme(shell, 0xFFBF3A, 0x14100A, TRUE);
        shell_save_settings(shell);
        shell_println(shell, "theme: amber");
        return;
    }

    if (u_startswith(raw, "prompt ")) {
        const char *mode = u_trim_left((char *)(raw + 7));
        if (u_strcmp(mode, "full") == 0) {
            shell->prompt_show_path = TRUE;
            shell_save_settings(shell);
            shell_println(shell, "theme: prompt full");
            return;
        }
        if (u_strcmp(mode, "short") == 0) {
            shell->prompt_show_path = FALSE;
            shell_save_settings(shell);
            shell_println(shell, "theme: prompt short");
            return;
        }
        shell_println(shell, "theme: usage: theme prompt <full|short>");
        return;
    }

    shell_println(shell, "theme: unknown option");
}

static void shell_cmd_time(Shell *shell) {
    if (shell == NULL || shell->st == NULL || shell->st->RuntimeServices == NULL) {
        shell_println(shell, "time: runtime services unavailable");
        return;
    }

    EFI_TIME now;
    EFI_STATUS status = uefi_call_wrapper(shell->st->RuntimeServices->GetTime, 2, &now, NULL);
    if (EFI_ERROR(status)) {
        shell_print_error_status(shell, "time failed", status);
        return;
    }

    shell_print(shell, "UTC ");
    shell_print_u64(shell, now.Year);
    shell_putc(shell, '-');
    shell_print_padded_u64(shell, now.Month, 2);
    shell_putc(shell, '-');
    shell_print_padded_u64(shell, now.Day, 2);
    shell_putc(shell, ' ');
    shell_print_padded_u64(shell, now.Hour, 2);
    shell_putc(shell, ':');
    shell_print_padded_u64(shell, now.Minute, 2);
    shell_putc(shell, ':');
    shell_print_padded_u64(shell, now.Second, 2);
    shell_putc(shell, '\n');
}

static void shell_cmd_memmap(Shell *shell) {
    if (shell == NULL || shell->st == NULL || shell->st->BootServices == NULL) {
        shell_println(shell, "memmap: boot services unavailable");
        return;
    }

    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;
    EFI_STATUS status = uefi_call_wrapper(
        shell->st->BootServices->GetMemoryMap,
        5,
        &map_size,
        NULL,
        &map_key,
        &desc_size,
        &desc_version
    );
    if (status != EFI_BUFFER_TOO_SMALL || desc_size == 0) {
        shell_print_error_status(shell, "memmap failed", status);
        return;
    }

    map_size += desc_size * 8;
    EFI_MEMORY_DESCRIPTOR *map = (EFI_MEMORY_DESCRIPTOR *)shell_alloc(shell, map_size);
    if (map == NULL) {
        shell_println(shell, "memmap: out of memory");
        return;
    }

    status = uefi_call_wrapper(
        shell->st->BootServices->GetMemoryMap,
        5,
        &map_size,
        map,
        &map_key,
        &desc_size,
        &desc_version
    );
    if (EFI_ERROR(status)) {
        shell_print_error_status(shell, "memmap failed", status);
        shell_free(shell, map);
        return;
    }

    UINTN desc_count = map_size / desc_size;
    UINT64 pages_by_type[EfiMaxMemoryType + 1];
    for (UINTN i = 0; i <= EfiMaxMemoryType; i++) {
        pages_by_type[i] = 0;
    }

    for (UINTN i = 0; i < desc_count; i++) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
        if (d->Type <= EfiMaxMemoryType) {
            pages_by_type[d->Type] += d->NumberOfPages;
        }
    }

    shell_print(shell, "Descriptors: ");
    shell_print_u64(shell, desc_count);
    shell_print(shell, ", descriptor size: ");
    shell_print_u64(shell, desc_size);
    shell_putc(shell, '\n');

    char map_key_hex[32];
    u_u64_to_hex((UINT64)map_key, map_key_hex, sizeof(map_key_hex));
    shell_print(shell, "Map key: ");
    shell_print(shell, map_key_hex);
    shell_print(shell, ", desc version: ");
    shell_print_u64(shell, desc_version);
    shell_putc(shell, '\n');

    UINT64 total_pages = 0;
    for (UINTN type = 0; type <= EfiMaxMemoryType; type++) {
        if (pages_by_type[type] == 0) {
            continue;
        }
        total_pages += pages_by_type[type];

        shell_print(shell, "  ");
        shell_print(shell, shell_mem_type_name((UINT32)type));
        shell_print(shell, ": ");
        shell_print_u64(shell, pages_by_type[type]);
        shell_print(shell, " pages (");
        shell_print_u64(shell, pages_by_type[type] / 256);
        shell_println(shell, " MiB)");
    }

    shell_print(shell, "Total pages: ");
    shell_print_u64(shell, total_pages);
    shell_print(shell, " (");
    shell_print_u64(shell, total_pages / 256);
    shell_println(shell, " MiB)");

    shell_free(shell, map);
}

// Print runtime/system metadata for debugging.
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

// Parse and dispatch one command line.
static void shell_execute(Shell *shell, char *line) {
    char *cmd = u_trim_left(line);
    if (*cmd == '\0') {
        return;
    }

    if (u_strcmp(cmd, "help") == 0) {
        shell_print_help(shell, "");
        return;
    }

    if (u_startswith(cmd, "help ")) {
        shell_print_help(shell, u_trim_left(cmd + 5));
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
        uefi_call_wrapper(shell->st->RuntimeServices->ResetSystem, 4, EfiResetWarm, EFI_SUCCESS, 0, NULL);
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

    if (u_strcmp(cmd, "pwd") == 0) {
        shell_cmd_pwd(shell);
        return;
    }

    if (u_startswith(cmd, "cd ")) {
        shell_cmd_cd(shell, u_trim_left(cmd + 3));
        return;
    }

    if (u_strcmp(cmd, "cd") == 0) {
        shell_cmd_cd(shell, "");
        return;
    }

    if (u_strcmp(cmd, "ls") == 0) {
        shell_cmd_ls(shell, "");
        return;
    }

    if (u_startswith(cmd, "ls ")) {
        shell_cmd_ls(shell, u_trim_left(cmd + 3));
        return;
    }

    if (u_startswith(cmd, "cat ")) {
        shell_cmd_cat(shell, u_trim_left(cmd + 4));
        return;
    }

    if (u_strcmp(cmd, "cat") == 0) {
        shell_cmd_cat(shell, "");
        return;
    }

    if (u_startswith(cmd, "mkdir ")) {
        shell_cmd_mkdir(shell, u_trim_left(cmd + 6));
        return;
    }

    if (u_strcmp(cmd, "mkdir") == 0) {
        shell_cmd_mkdir(shell, "");
        return;
    }

    if (u_startswith(cmd, "touch ")) {
        shell_cmd_touch(shell, u_trim_left(cmd + 6));
        return;
    }

    if (u_strcmp(cmd, "touch") == 0) {
        shell_cmd_touch(shell, "");
        return;
    }

    if (u_startswith(cmd, "cp ")) {
        char *args = u_trim_left(cmd + 3);
        char *src = args;
        while (*args != '\0' && *args != ' ' && *args != '\t') {
            args++;
        }
        if (*args == '\0') {
            shell_cmd_cp(shell, "", "");
            return;
        }
        *args++ = '\0';
        char *dst = u_trim_left(args);
        if (*dst == '\0') {
            shell_cmd_cp(shell, "", "");
            return;
        }
        shell_cmd_cp(shell, src, dst);
        return;
    }

    if (u_strcmp(cmd, "cp") == 0) {
        shell_cmd_cp(shell, "", "");
        return;
    }

    if (u_startswith(cmd, "rm ")) {
        shell_cmd_rm(shell, u_trim_left(cmd + 3));
        return;
    }

    if (u_strcmp(cmd, "rm") == 0) {
        shell_cmd_rm(shell, "");
        return;
    }

    if (u_startswith(cmd, "mv ")) {
        char *args = u_trim_left(cmd + 3);
        char *src = args;
        while (*args != '\0' && *args != ' ' && *args != '\t') {
            args++;
        }
        if (*args == '\0') {
            shell_cmd_mv(shell, "", "");
            return;
        }
        *args++ = '\0';
        char *dst = u_trim_left(args);
        if (*dst == '\0') {
            shell_cmd_mv(shell, "", "");
            return;
        }
        shell_cmd_mv(shell, src, dst);
        return;
    }

    if (u_strcmp(cmd, "mv") == 0) {
        shell_cmd_mv(shell, "", "");
        return;
    }

    if (u_startswith(cmd, "hexdump ")) {
        shell_cmd_hexdump(shell, u_trim_left(cmd + 8));
        return;
    }

    if (u_strcmp(cmd, "hexdump") == 0) {
        shell_cmd_hexdump(shell, "");
        return;
    }

    if (u_startswith(cmd, "viewbmp ")) {
        shell_cmd_viewbmp(shell, u_trim_left(cmd + 8));
        return;
    }

    if (u_strcmp(cmd, "viewbmp") == 0) {
        shell_cmd_viewbmp(shell, "");
        return;
    }

    if (u_strcmp(cmd, "history") == 0) {
        shell_cmd_history(shell);
        return;
    }

    if (u_strcmp(cmd, "initfs") == 0) {
        shell_cmd_initfs(shell);
        return;
    }

    if (u_startswith(cmd, "theme ")) {
        shell_cmd_theme(shell, u_trim_left(cmd + 6));
        return;
    }

    if (u_strcmp(cmd, "theme") == 0) {
        shell_cmd_theme(shell, "");
        return;
    }

    if (u_strcmp(cmd, "time") == 0) {
        shell_cmd_time(shell);
        return;
    }

    if (u_strcmp(cmd, "memmap") == 0) {
        shell_cmd_memmap(shell);
        return;
    }

    shell_print(shell, "Unknown command: ");
    shell_println(shell, cmd);
    shell_println(shell, "Type 'help' for available commands.");
}

// Main REPL loop.
void shell_run(Shell *shell) {
    if (shell == NULL || shell->st == NULL || shell->st->ConIn == NULL) {
        return;
    }

    shell_println(shell, "HatterOS shell ready. Type 'help'.");

    while (1) {
        char input[SHELL_INPUT_MAX];
        shell_prompt(shell);

        EFI_STATUS status = shell_read_line(shell, input, sizeof(input));
        if (EFI_ERROR(status)) {
            shell_println(shell, "Input error.");
            continue;
        }

        shell_history_add(shell, u_trim_left(input));
        shell_execute(shell, input);
    }
}
