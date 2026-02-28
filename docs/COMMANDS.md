# HatterOS Shell Commands

Prompt: `HatterOS> `

## `help`

Lists available commands and short descriptions.

## `clear`

Clears the framebuffer shell screen and resets cursor position.

## `echo <text>`

Prints `<text>` exactly as provided.

Examples:
- `echo hello`
- `echo HatterOS stage0`

## `ls [path]`

Lists files/directories on the EFI System Partition (ESP).

Examples:
- `ls`
- `ls /EFI/BOOT`

## `cat <path>`

Prints file contents from the ESP.

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
