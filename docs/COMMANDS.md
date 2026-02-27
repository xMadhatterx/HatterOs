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