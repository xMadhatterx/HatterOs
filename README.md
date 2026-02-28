# HatterOS (Stage 0 UEFI Prototype)

HatterOS is a minimal hobby OS prototype implemented as a single UEFI application (`BOOTX64.EFI`) in C.

Boot flow:
1. Initialize GOP and select mode closest to `1024x768`.
2. Draw graphical splash screen (gradient + hat icon + large `HatterOS` title).
3. Wait for keypress or 2-second timeout.
4. Enter framebuffer-rendered command shell.

## Repo Layout

- `src/main.c` - UEFI entrypoint, splash logic, transition into shell.
- `src/gfx.c`, `src/gfx.h` - GOP initialization and primitive drawing.
- `src/font.c`, `src/font.h` - tiny embedded bitmap font + text blitting.
- `src/shell.c`, `src/shell.h` - prompt, input loop, command handling.
- `src/util.c`, `src/util.h` - string helpers, number formatting, serial logging.
- `docs/ARCH.md` - architecture notes.
- `docs/COMMANDS.md` - shell command reference.
- `Makefile` - GNU-EFI build.
- `run_qemu.sh` - ESP image creation and QEMU launch with OVMF.

## Prerequisites

### Linux / WSL

Install packages (Debian/Ubuntu names):

```bash
sudo apt update
sudo apt install -y build-essential gnu-efi qemu-system-x86 qemu-utils ovmf dosfstools mtools
```

### macOS

Recommended: build and run under Linux VM or WSL-like environment. GNU-EFI + OVMF setup on macOS is possible but less standardized.

### Windows

Recommended: WSL2 (Ubuntu). Use the Linux instructions above in WSL.

## Build

```bash
make
```

Output files:
- `build/BOOTX64.EFI`
- `./BOOTX64.EFI` (copied convenience artifact)

## Run In QEMU

```bash
./run_qemu.sh
```

Script behavior:
1. Builds with `make`.
2. Creates `build/hatteros_esp.img` (FAT32).
3. Copies EFI app to `/EFI/BOOT/BOOTX64.EFI` inside image.
4. Copies optional extra files from `esp_files/` into the ESP image root.
5. Locates OVMF firmware from common paths.
6. Boots QEMU with `-serial stdio` enabled.

### Add Files For `ls` / `cat`

Put any files you want to test in `esp_files/` (subdirectories are preserved). Example:

```bash
mkdir -p esp_files/docs
cat > esp_files/docs/hello.txt <<'EOF'
Hello from ESP.
EOF
./run_qemu.sh
```

Then in HatterOS:

```text
ls /
ls /docs
cat /docs/hello.txt
```

Note: `esp_files/EFI/BOOT/STARTUP.NSH` is included as a smoke-test script template and copied into the image automatically.

After booting into the shell, quick smoke-test commands:
- `help`
- `pwd`
- `cd /EFI/BOOT`
- `ls`
- `cat /EFI/BOOT/STARTUP.NSH` (if present)
- `mkdir demo && touch demo/a.txt` (run as separate commands)
- `cp demo/a.txt demo/b.txt`
- `info`

### Minimal Diagnostic Boot

If the full app crashes with `#GP`, boot a minimal EFI app to isolate toolchain/firmware issues:

```bash
./run_qemu.sh --minimal
```

Expected result: a text screen saying `HatterOS minimal EFI test loaded.`

- If minimal also crashes: issue is likely in firmware pairing/toolchain output format.
- If minimal works: issue is in HatterOS app logic and we can debug it with symbol offsets.

### Override OVMF Paths (optional)

If your distro uses non-standard OVMF locations:

```bash
OVMF_CODE_PATH=/path/to/OVMF_CODE.fd OVMF_VARS_PATH=/path/to/OVMF_VARS.fd ./run_qemu.sh
```

## Screenshots (optional)

Inside another terminal while QEMU is running, you can use QEMU monitor commands to dump display buffers, or use host screenshot tooling.

## Troubleshooting

- `GNU-EFI headers not found`:
  - Install `gnu-efi` development package.
- `GNU-EFI linker files not found`:
  - Ensure `crt0-efi-x86_64.o` and `elf_x86_64_efi.lds` are installed.
- `Could not find OVMF firmware files`:
  - Install `ovmf` package and verify paths under `/usr/share/OVMF`.
- `Missing mtools` or `mkfs.fat`:
  - Install `mtools` and `dosfstools`.

## Alternate Build Path Note

If GNU-EFI is unavailable on your host, you can port this code to EDK2 (INF + DSC + UEFI app target). GNU-EFI is the default supported path in this repo.
