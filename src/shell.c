#include "shell.h"
#include "font.h"
#include "util.h"
#include <efilib.h>

#define INPUT_MAX 256
#define PATH16_MAX 260
#define FILE_IO_CHUNK 512

static void shell_newline(Shell *shell);
static void shell_putc(Shell *shell, char c);
static void shell_prompt(Shell *shell);
static void shell_execute(Shell *shell, char *line);
static EFI_STATUS shell_read_line(Shell *shell, char *line, UINTN max_len);
static void shell_scroll(Shell *shell);
static EFI_STATUS shell_open_root(Shell *shell, EFI_FILE_PROTOCOL **root);
static BOOLEAN shell_ascii_path_to_char16(const char *path, CHAR16 *out, UINTN out_len);
static void shell_print_file_name(Shell *shell, const CHAR16 *name);
static void shell_cmd_ls(Shell *shell, const char *arg);
static void shell_cmd_cat(Shell *shell, const char *arg);
static void *shell_alloc(Shell *shell, UINTN size);
static void shell_free(Shell *shell, void *ptr);

void shell_init(Shell *shell, EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st, GfxContext *gfx) {
    shell->image_handle = image_handle;
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
    if (shell->cols == 0) {
        shell->cols = 1;
    }
    if (shell->rows == 0) {
        shell->rows = 1;
    }
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
        EFI_STATUS status = uefi_call_wrapper(shell->st->BootServices->WaitForEvent, 3, 1, &event, &idx);
        if (EFI_ERROR(status)) {
            return status;
        }

        EFI_INPUT_KEY key;
        status = uefi_call_wrapper(shell->st->ConIn->ReadKeyStroke, 2, shell->st->ConIn, &key);
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

static BOOLEAN shell_ascii_path_to_char16(const char *path, CHAR16 *out, UINTN out_len) {
    if (out == NULL || out_len < 2) {
        return FALSE;
    }

    const char *p = path;
    if (p == NULL) {
        p = "";
    }
    p = u_trim_left((char *)p);

    if (*p == '\0' || (*p == '.' && p[1] == '\0')) {
        out[0] = '\\';
        out[1] = '\0';
        return TRUE;
    }

    UINTN i = 0;
    if (*p != '\\' && *p != '/') {
        out[i++] = '\\';
    }

    while (*p != '\0') {
        if (i + 1 >= out_len) {
            return FALSE;
        }
        char c = *p++;
        if (c == '/') {
            c = '\\';
        }
        out[i++] = (CHAR16)(UINT8)c;
    }

    out[i] = '\0';
    return TRUE;
}

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

static void shell_cmd_ls(Shell *shell, const char *arg) {
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *dir = NULL;
    CHAR16 path16[PATH16_MAX];

    if (!shell_ascii_path_to_char16(arg, path16, PATH16_MAX)) {
        shell_println(shell, "ls: path too long");
        return;
    }

    EFI_STATUS status = shell_open_root(shell, &root);
    if (EFI_ERROR(status) || root == NULL) {
        shell_println(shell, "ls: failed to open filesystem");
        return;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &dir, path16, EFI_FILE_MODE_READ, 0);
    uefi_call_wrapper(root->Close, 1, root);
    if (EFI_ERROR(status) || dir == NULL) {
        shell_println(shell, "ls: cannot open path");
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
                shell_print(shell, "      ");
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

        if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) {
            shell_print(shell, "[DIR] ");
        } else {
            shell_print(shell, "      ");
        }
        shell_print_file_name(shell, info->FileName);
    }

    shell_free(shell, info);
    uefi_call_wrapper(dir->Close, 1, dir);
}

static void shell_cmd_cat(Shell *shell, const char *arg) {
    if (arg == NULL || *arg == '\0') {
        shell_println(shell, "cat: usage: cat <path>");
        return;
    }

    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    CHAR16 path16[PATH16_MAX];

    if (!shell_ascii_path_to_char16(arg, path16, PATH16_MAX)) {
        shell_println(shell, "cat: path too long");
        return;
    }

    EFI_STATUS status = shell_open_root(shell, &root);
    if (EFI_ERROR(status) || root == NULL) {
        shell_println(shell, "cat: failed to open filesystem");
        return;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &file, path16, EFI_FILE_MODE_READ, 0);
    uefi_call_wrapper(root->Close, 1, root);
    if (EFI_ERROR(status) || file == NULL) {
        shell_println(shell, "cat: cannot open file");
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

    if (u_strcmp(cmd, "help") == 0) {
        shell_println(shell, "Commands:");
        shell_println(shell, "  help        - list commands");
        shell_println(shell, "  clear       - clear the screen");
        shell_println(shell, "  echo <text> - print text");
        shell_println(shell, "  ls [path]   - list files on ESP");
        shell_println(shell, "  cat <path>  - print file contents");
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
