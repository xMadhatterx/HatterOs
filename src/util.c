#include "util.h"

UINTN u_strlen(const char *s) {
    UINTN len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

INTN u_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (INTN)((UINT8)*a - (UINT8)*b);
}

INTN u_strncmp(const char *a, const char *b, UINTN n) {
    for (UINTN i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return (INTN)((UINT8)a[i] - (UINT8)b[i]);
        }
    }
    return 0;
}

BOOLEAN u_startswith(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) {
            return FALSE;
        }
        str++;
        prefix++;
    }
    return TRUE;
}

char *u_trim_left(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

void u_u64_to_dec(UINT64 value, char *out, UINTN out_size) {
    if (out_size == 0) {
        return;
    }

    if (value == 0) {
        out[0] = '0';
        if (out_size > 1) {
            out[1] = '\0';
        }
        return;
    }

    char tmp[32];
    UINTN i = 0;
    while (value != 0 && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    UINTN j = 0;
    while (i > 0 && j + 1 < out_size) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

void u_u64_to_hex(UINT64 value, char *out, UINTN out_size) {
    static const char hex[] = "0123456789ABCDEF";
    if (out_size < 3) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return;
    }

    out[0] = '0';
    out[1] = 'x';

    BOOLEAN started = FALSE;
    UINTN idx = 2;

    for (INTN i = 15; i >= 0 && idx + 1 < out_size; i--) {
        UINT8 nibble = (UINT8)((value >> (i * 4)) & 0xF);
        if (nibble != 0 || started || i == 0) {
            started = TRUE;
            out[idx++] = hex[nibble];
        }
    }

    out[idx] = '\0';
}

void serial_init(void) {
    // Intentionally left as a no-op.
    // Direct x86 port I/O (COM1 outb) can trigger a GP fault in some UEFI
    // execution environments depending on firmware/privilege configuration.
}

static void serial_write_char(char c) {
    (void)c;
}

void serial_write(const char *text) {
    while (*text) {
        if (*text == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*text++);
    }
}

void serial_writeln(const char *text) {
    serial_write(text);
    serial_write("\n");
}
