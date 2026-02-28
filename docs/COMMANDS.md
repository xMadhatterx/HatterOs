# HatterOS Shell Commands

Prompt: `HATTEROS/...> ` (for example `HATTEROS/> ` or `HATTEROS/EFI/BOOT> `)

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
