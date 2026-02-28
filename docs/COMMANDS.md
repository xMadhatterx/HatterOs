# HatterOS Shell Commands

Prompt: `HATTEROS/...> ` (for example `HATTEROS/> ` or `HATTEROS/EFI/BOOT> `)

Line editor shortcuts:
- Left/Right arrows move cursor in the current line.
- Up/Down arrows browse command history.
- Backspace deletes left of cursor.

## `help`

Lists available commands and short descriptions.

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

## `ls [path]`

Lists files/directories on the EFI System Partition (ESP). Supports absolute and relative paths.

Examples:
- `ls`
- `ls /EFI/BOOT`
- `ls ..`

## `cat <path>`

Prints file contents from the ESP. Supports absolute and relative paths.

Examples:
- `cat /EFI/BOOT/startup.nsh`
- `cat /EFI/BOOT/readme.txt`

## `mkdir <path>`

Creates a directory on the ESP.

Examples:
- `mkdir logs`
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
