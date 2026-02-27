#ifndef HATTEROS_UTIL_H
#define HATTEROS_UTIL_H

#include <efi.h>

#define HATTEROS_VERSION "0.1.0-stage0"
#define HATTEROS_BUILD_DATE __DATE__ " " __TIME__

UINTN u_strlen(const char *s);
INTN u_strcmp(const char *a, const char *b);
INTN u_strncmp(const char *a, const char *b, UINTN n);
BOOLEAN u_startswith(const char *str, const char *prefix);
char *u_trim_left(char *s);
void u_u64_to_dec(UINT64 value, char *out, UINTN out_size);
void u_u64_to_hex(UINT64 value, char *out, UINTN out_size);

void serial_init(void);
void serial_write(const char *text);
void serial_writeln(const char *text);

#endif