# HatterOS Shell Commands

Prompt defaults to `HATTEROS/...> ` (for example `HATTEROS/> ` or `HATTEROS/EFI/BOOT> `).
You can switch to `HATTEROS> ` via `theme prompt short`.

Line editor shortcuts:
- Left/Right arrows move cursor in the current line.
- Up/Down arrows browse command history.
- Backspace deletes left of cursor.
- A visible caret shows current insert position.

## `help [command]`

Lists available commands and short descriptions.

`help <command>` prints focused usage for a specific command.

## `clear`

Clears the framebuffer shell screen and resets cursor position.

## `echo <text>`

Prints `<text>` exactly as provided.

Examples:
- `echo hello`
- `echo HatterOS stage0`

## `pwd`

Prints current shell directory.

## `cd <path>`

Changes current shell directory.

Examples:
- `cd /EFI`
- `cd BOOT`
- `cd ..`

## `ls [-l] [path]`

Lists files/directories on the EFI System Partition (ESP). Supports absolute and relative paths.

Use `-l` for long view (type, size, modification time).

Examples:
- `ls`
- `ls -l`
- `ls /EFI/BOOT`
- `ls ..`

## `cat <path>`

Prints file contents from the ESP. Supports absolute and relative paths.

Examples:
- `cat /EFI/BOOT/startup.nsh`
- `cat /EFI/BOOT/readme.txt`

## `mkdir [-p] <path>`

Creates a directory on the ESP.

Use `-p` to create missing parent directories.

Examples:
- `mkdir logs`
- `mkdir -p /tmp/data`
- `mkdir /tmp/data`

## `touch <path>`

Creates an empty file (or opens an existing file).

Examples:
- `touch notes.txt`
- `touch /tmp/data/out.txt`

## `cp <src> <dst>`

Copies a file from `<src>` to `<dst>`.

Examples:
- `cp notes.txt notes.bak`
- `cp /docs/a.txt /docs/b.txt`

## `rm <path>`

Deletes a file on the ESP.

## `mv <src> <dst>`

Moves/renames a file on the ESP (implemented as copy + delete in stage 0).

## `hexdump <path>`

Prints file bytes as hex + ASCII rows.

## `history`

Prints command history currently kept in the shell buffer.

## `viewbmp <path>`

Displays an uncompressed 24-bit or 32-bit BMP full-screen.
Press any key to return to the shell.

## `initfs`

Creates the default `/HATTEROS` directory tree (idempotent).

## `theme [option]`

Updates shell colors or prompt style.

Options:
- `theme default`
- `theme light`
- `theme amber`
- `theme prompt full`
- `theme prompt short`

Examples:
- `theme amber`
- `theme prompt short`

Theme and prompt mode are persisted to `/HATTEROS/system/config/shell.cfg`.

## `time`

Reads current UTC time from UEFI runtime services.

## `memmap`

Prints a summary of the current UEFI memory map (descriptor count and pages by memory type).

## `info`

Shows system/runtime information:
- HatterOS version
- build timestamp
- GOP resolution
- framebuffer base address
- framebuffer size in bytes

## `reboot`

Invokes UEFI `ResetSystem` to reboot the virtual machine.

## Unknown Commands

Any unrecognized command prints:
- an error line showing the command
- suggestion to run `help`
