# HatterOS Architecture (Stage 0)

## Overview

HatterOS stage 0 is a single UEFI application (`BOOTX64.EFI`) with no kernel separation yet.

Subsystems:
- GOP graphics layer (`gfx.*`)
- Bitmap font renderer (`font.*`)
- Framebuffer shell (`shell.*`)
- Utility/helpers (`util.*`)

## Boot + Graphics Path

1. `efi_main` validates `SystemTable`, resets keyboard input, and starts graphics setup.
2. GOP protocol is located via `LocateHandleBuffer(ByProtocol)` + `HandleProtocol`.
3. Graphics mode is selected by nearest resolution distance to `1024x768`.
4. Framebuffer metadata is stored in `GfxContext`:
   - base address
   - size
   - width/height
   - pixels-per-scanline
5. Splash renderer draws:
   - vertical gradient base
   - optional external BMP (`\EFI\BOOT\SPLASH.BMP`, 24/32-bit uncompressed) centered on screen
   - fallback procedural top-hat icon + centered `HatterOS` title when BMP is missing/invalid
   - optional diagnostic text when external splash loading fails
   - continue hint

## Text Rendering

Font renderer stores compact 8x8 glyph patterns for common ASCII.

`font_draw_char` expands each 8x8 row to two scanlines, yielding an effective 8x16 character cell. This allows:
- higher readability in framebuffer text mode
- simple fixed-grid shell layout

Text drawing is done directly to GOP framebuffer, not `SimpleTextOutputProtocol`.

## Input + Shell Loop

Keyboard input uses `SimpleTextInputProtocol`:
- wait on `ConIn->WaitForKey`
- read keys via `ReadKeyStroke`
- handle Enter and Backspace
- append printable ASCII into line buffer

Shell tracks a fixed character grid based on framebuffer size and `8x16` cell dimensions.

When output reaches bottom row, it scrolls by copying framebuffer rows upward by one text line (`16` pixels) and clearing the last line.

## Command Dispatch

Commands are parsed by prefix/exact comparisons with custom string helpers:
- `help`
- `clear`
- `echo <text>`
- `pwd`
- `cd <path>`
- `ls [-l] [path]`
- `cat <path>`
- `mkdir [-p] <path>`
- `touch <path>`
- `cp <src> <dst>`
- `rm <path>`
- `mv <src> <dst>`
- `hexdump <path>`
- `history`
- `viewbmp <path>`
- `initfs`
- `theme [option]`
- `time`
- `memmap`
- `info`
- `reboot`

`info` reports runtime GOP details and build/version metadata.
`cd`/`pwd` maintain a shell-level current working directory.
`ls`/`cat` use `LoadedImage -> DeviceHandle -> SimpleFileSystem` to access files on the same ESP the EFI app was loaded from, with absolute or relative paths resolved against the current directory.
`mkdir`/`touch`/`cp`/`rm`/`mv` use the same path resolver and UEFI `EFI_FILE_PROTOCOL` operations for create/read/write/delete.
`mkdir -p` and `initfs` create the default `/HATTEROS` directory tree for future filesystem layering.
`viewbmp` reuses framebuffer rendering to preview BMP files from the ESP.
`theme` updates shell foreground/background colors and prompt style (full path vs short prompt).
Shell theme settings are persisted to `/HATTEROS/system/config/shell.cfg`.
`time` uses UEFI runtime service `GetTime`.
`memmap` uses UEFI boot service `GetMemoryMap` and prints a per-memory-type summary.

`reboot` delegates to UEFI runtime service `ResetSystem`.

## Line Editing

The shell input loop is a small single-line editor:
- insertion at cursor position
- left/right cursor movement
- backspace in the middle of the line
- up/down command history recall
- visible caret/cursor at the current insert column

## UEFI Call ABI Safety

Firmware and protocol method calls are routed through GNU-EFI `uefi_call_wrapper(...)` with `EFI_FUNCTION_WRAPPER` enabled in the build. This avoids x86_64 UEFI calling-convention mismatch issues that can otherwise trigger `#GP` faults on some firmware/QEMU combinations.

## Serial Debugging

`serial_*` helpers currently compile as no-ops. Direct COM port I/O from this UEFI app was disabled because it caused GP faults in some environments.

## Limits (Intentional for Stage 0)

- No tasking, memory manager, filesystem, or executable loader.
- No Unicode shell support beyond printable ASCII.
- Font table intentionally small and stylized.

This is a base for later stages (kernel handoff, memory map handling, proper terminal abstractions).
