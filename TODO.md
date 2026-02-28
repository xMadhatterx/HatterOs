# HatterOS TODO

Workflow rule:
- When an item is completed, move it into `Live Features`.

## Live Features
- [x] UEFI app boots in QEMU with OVMF.
- [x] GOP splash screen (gradient + logo text + icon).
- [x] Framebuffer shell with command input.
- [x] Commands: `help`, `clear`, `echo`, `info`, `reboot`.
- [x] ESP file commands: `ls`, `cat`.
- [x] Directory commands: `cd`, `pwd`.
- [x] `run_qemu.sh` supports extra files via `esp_files/`.
- [x] Add command history (up/down arrow support).
- [x] Add left/right cursor editing in input line.
- [x] Add a visible input cursor/caret in the shell line editor.
- [x] Add `mkdir` and `touch` commands (basic write support).
- [x] Add `cp` command for file copy inside ESP.
- [x] Add better error/status messages (EFI status to readable text).
- [x] Optional external splash asset loading (BMP from ESP).
- [x] Shell theme settings (colors, prompt style).
- [x] `memmap` command to inspect UEFI memory map.
- [x] `time` command via UEFI runtime services.
- [x] Small test script (`startup.nsh`) for automated smoke checks.
- [x] Host-side splash conversion: accept `splash.jpg` / `splash.png` in `esp_files/` and auto-convert to BMP in `run_qemu.sh`.
- [x] `ls -l` mode: show file size, type, and modified time.
- [x] `rm` and `mv` commands for basic file management on ESP.
- [x] `mkdir -p` support for nested directory creation.
- [x] `hexdump <path>` command for binary inspection.
- [x] `history` command to print previous commands.
- [x] `help <command>` for per-command usage/details.
- [x] `viewbmp <path>` command to show a BMP full-screen, then return to shell on keypress.
- [x] Save/load shell settings (`theme`, prompt mode) from an ESP config file.
- [x] Optional splash diagnostics line when external asset loading fails.
- [x] Define default HatterOS system folder structure under `/HATTEROS` for future FS/VFS work.
- [x] Add `initfs` command to create default `/HATTEROS` directories if missing (idempotent).
- [x] Pre-seed default `/HATTEROS` directory tree via `esp_files/` for first boot.

Implemented default tree:
```text
/HATTEROS/system/config
/HATTEROS/system/log
/HATTEROS/system/assets
/HATTEROS/system/tmp
/HATTEROS/user/home
/HATTEROS/user/docs
/HATTEROS/bin
```

## Active Quests
- [ ] Kernel handoff MVP scaffold (`stage0` -> `stage1`).
- [ ] Define `BootInfo` struct (GOP framebuffer, memory map, ACPI/RSDP, build/version metadata).
- [ ] Add `boot <path>` shell command to load stage-1 ELF from ESP.
- [ ] Implement minimal ELF64 loader validations (headers, segments, bounds, entrypoint).
- [ ] Wire `ExitBootServices` and jump to stage-1 entry.
- [ ] Create minimal `stage1.elf` that clears screen + prints banner + halts.
- [ ] Add docs for boot contract between stage-0 and stage-1.

## Bonus Quests
- No open items yet. Add quality-of-life ideas here.

## Cool Experiments (Pre-Long-Term)
- No open items yet. Add fun prototype ideas here.

## Long-Term
- [ ] Split stage-0 shell from future kernel handoff.
- [ ] Add ELF loader scaffold for stage-1 kernel.
- [ ] Introduce paging + physical memory manager.
- [ ] Add VFS abstraction beyond raw UEFI file protocol.
