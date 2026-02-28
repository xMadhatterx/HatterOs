#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
EFI_BIN="$BUILD_DIR/BOOTX64.EFI"
ESP_IMG="$BUILD_DIR/hatteros_esp.img"
OVMF_VARS_WORK="$BUILD_DIR/OVMF_VARS.fd"

find_ovmf_pair() {
  local pairs=(
    "/usr/share/OVMF/OVMF_CODE.fd|/usr/share/OVMF/OVMF_VARS.fd"
    "/usr/share/OVMF/OVMF_CODE_4M.fd|/usr/share/OVMF/OVMF_VARS_4M.fd"
    "/usr/share/edk2/x64/OVMF_CODE.fd|/usr/share/edk2/x64/OVMF_VARS.fd"
    "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd|/usr/share/edk2-ovmf/x64/OVMF_VARS.fd"
  )

  local pair code vars
  for pair in "${pairs[@]}"; do
    code="${pair%%|*}"
    vars="${pair##*|}"
    if [[ -f "$code" && -f "$vars" ]]; then
      echo "$code|$vars"
      return 0
    fi
  done

  return 1
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

main() {
  mkdir -p "$BUILD_DIR"

  require_cmd make
  require_cmd qemu-system-x86_64
  require_cmd qemu-img

  if ! command -v mkfs.fat >/dev/null 2>&1 && ! command -v mkfs.vfat >/dev/null 2>&1; then
    echo "Missing mkfs.fat/mkfs.vfat. Install dosfstools." >&2
    exit 1
  fi

  if ! command -v mcopy >/dev/null 2>&1 || ! command -v mmd >/dev/null 2>&1; then
    echo "Missing mtools (mcopy/mmd). Install mtools to populate FAT image." >&2
    exit 1
  fi

  local ovmf_pair
  if ! ovmf_pair="$(find_ovmf_pair)"; then
    echo "Could not find OVMF firmware files." >&2
    echo "Checked common paths under /usr/share/OVMF and /usr/share/edk2*." >&2
    exit 1
  fi

  local ovmf_code="${ovmf_pair%%|*}"
  local ovmf_vars_src="${ovmf_pair##*|}"

  make -C "$ROOT_DIR"

  if [[ ! -f "$EFI_BIN" ]]; then
    echo "Expected EFI binary at $EFI_BIN but it was not produced." >&2
    exit 1
  fi

  qemu-img create -f raw "$ESP_IMG" 64M >/dev/null

  if command -v mkfs.fat >/dev/null 2>&1; then
    mkfs.fat -F 32 "$ESP_IMG" >/dev/null
  else
    mkfs.vfat -F 32 "$ESP_IMG" >/dev/null
  fi

  mmd -i "$ESP_IMG" ::/EFI ::/EFI/BOOT >/dev/null
  mcopy -i "$ESP_IMG" "$EFI_BIN" ::/EFI/BOOT/BOOTX64.EFI >/dev/null

  cp "$ovmf_vars_src" "$OVMF_VARS_WORK"

  echo "Launching QEMU with serial on stdio..."
  qemu-system-x86_64 \
    -machine q35,accel=tcg \
    -m 512M \
    -serial stdio \
    -display default \
    -drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
    -drive if=pflash,format=raw,file="$OVMF_VARS_WORK" \
    -drive format=raw,file="$ESP_IMG"
}

main "$@"
