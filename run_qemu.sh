#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
EFI_BIN="$BUILD_DIR/BOOTX64.EFI"
ESP_IMG="$BUILD_DIR/hatteros_esp.img"
OVMF_VARS_WORK="$BUILD_DIR/OVMF_VARS.fd"
ESP_FILES_DIR="$ROOT_DIR/esp_files"
MAKE_TARGET="all"

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

seed_default_hatteros_tree() {
  local dirs=(
    "/HATTEROS"
    "/HATTEROS/system"
    "/HATTEROS/system/config"
    "/HATTEROS/system/log"
    "/HATTEROS/system/assets"
    "/HATTEROS/system/tmp"
    "/HATTEROS/user"
    "/HATTEROS/user/home"
    "/HATTEROS/user/docs"
    "/HATTEROS/bin"
  )

  local dir
  for dir in "${dirs[@]}"; do
    mmd -i "$ESP_IMG" "::$dir" </dev/null >/dev/null 2>&1 || true
  done
}

prepare_auto_splash() {
  # If a BMP already exists in common locations, keep it.
  local bmp_candidates=(
    "$ESP_FILES_DIR/EFI/BOOT/SPLASH.BMP"
    "$ESP_FILES_DIR/EFI/BOOT/splash.bmp"
    "$ESP_FILES_DIR/SPLASH.BMP"
    "$ESP_FILES_DIR/splash.bmp"
  )
  local f
  for f in "${bmp_candidates[@]}"; do
    if [[ -f "$f" ]]; then
      return
    fi
  done

  # Otherwise accept png/jpg and convert host-side to BOOT splash BMP.
  local src_candidates=(
    "$ESP_FILES_DIR/EFI/BOOT/splash.png"
    "$ESP_FILES_DIR/EFI/BOOT/SPLASH.PNG"
    "$ESP_FILES_DIR/EFI/BOOT/splash.jpg"
    "$ESP_FILES_DIR/EFI/BOOT/SPLASH.JPG"
    "$ESP_FILES_DIR/EFI/BOOT/splash.jpeg"
    "$ESP_FILES_DIR/EFI/BOOT/SPLASH.JPEG"
    "$ESP_FILES_DIR/splash.png"
    "$ESP_FILES_DIR/SPLASH.PNG"
    "$ESP_FILES_DIR/splash.jpg"
    "$ESP_FILES_DIR/SPLASH.JPG"
    "$ESP_FILES_DIR/splash.jpeg"
    "$ESP_FILES_DIR/SPLASH.JPEG"
  )

  local src=""
  for f in "${src_candidates[@]}"; do
    if [[ -f "$f" ]]; then
      src="$f"
      break
    fi
  done
  if [[ -z "$src" ]]; then
    return
  fi

  local out="$BUILD_DIR/auto_splash.bmp"
  if command -v magick >/dev/null 2>&1; then
    magick "$src" -type TrueColor -alpha off BMP3:"$out"
  elif command -v convert >/dev/null 2>&1; then
    convert "$src" -type TrueColor -alpha off BMP3:"$out"
  else
    echo "Found splash source ($src) but no ImageMagick tool (magick/convert). Skipping auto-convert." >&2
    return
  fi

  echo "Auto-converted splash asset: ${src#$ROOT_DIR/} -> EFI/BOOT/SPLASH.BMP"
  mcopy -i "$ESP_IMG" -D o "$out" ::/EFI/BOOT/SPLASH.BMP >/dev/null
}

copy_extra_esp_files() {
  if [[ ! -d "$ESP_FILES_DIR" ]]; then
    return
  fi

  local first_entry
  first_entry="$(find "$ESP_FILES_DIR" -mindepth 1 ! -name .gitkeep -print -quit)"
  if [[ -z "$first_entry" ]]; then
    return
  fi

  echo "Copying extra files from esp_files/ into ESP image..."

  local dir_count=0
  local file_count=0

  while IFS= read -r -d '' dir; do
    local rel="${dir#"$ESP_FILES_DIR"/}"
    if [[ "$rel" == "EFI" || "$rel" == "EFI/BOOT" ]]; then
      continue
    fi
    mmd -i "$ESP_IMG" "::/$rel" </dev/null >/dev/null 2>&1 || true
    dir_count=$((dir_count + 1))
  done < <(find "$ESP_FILES_DIR" -mindepth 1 -type d -print0)

  while IFS= read -r -d '' file; do
    local rel="${file#"$ESP_FILES_DIR"/}"
    echo "  -> $rel"
    # -D o makes clashes non-interactive (overwrite) so script never blocks on prompts.
    mcopy -i "$ESP_IMG" -D o "$file" "::/$rel" >/dev/null
    file_count=$((file_count + 1))
  done < <(find "$ESP_FILES_DIR" -type f ! -name .gitkeep -print0)

  echo "Copied $file_count files across $dir_count directories from esp_files/."
}

main() {
  if [[ "${1:-}" == "--minimal" ]]; then
    EFI_BIN="$BUILD_DIR/BOOTX64_MIN.EFI"
    MAKE_TARGET="minimal"
  fi

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

  local ovmf_code ovmf_vars_src ovmf_pair
  if [[ -n "${OVMF_CODE_PATH:-}" || -n "${OVMF_VARS_PATH:-}" ]]; then
    if [[ -z "${OVMF_CODE_PATH:-}" || -z "${OVMF_VARS_PATH:-}" ]]; then
      echo "Set both OVMF_CODE_PATH and OVMF_VARS_PATH together." >&2
      exit 1
    fi
    ovmf_code="$OVMF_CODE_PATH"
    ovmf_vars_src="$OVMF_VARS_PATH"
    if [[ ! -f "$ovmf_code" || ! -f "$ovmf_vars_src" ]]; then
      echo "OVMF_CODE_PATH/OVMF_VARS_PATH file not found." >&2
      exit 1
    fi
  else
    if ! ovmf_pair="$(find_ovmf_pair)"; then
      echo "Could not find OVMF firmware files." >&2
      echo "Checked common paths under /usr/share/OVMF and /usr/share/edk2*." >&2
      exit 1
    fi
    ovmf_code="${ovmf_pair%%|*}"
    ovmf_vars_src="${ovmf_pair##*|}"
  fi

  make -C "$ROOT_DIR" "$MAKE_TARGET"

  if [[ ! -f "$EFI_BIN" ]]; then
    echo "Expected EFI binary at $EFI_BIN but it was not produced." >&2
    exit 1
  fi

  rm -f "$ESP_IMG"
  qemu-img create -f raw "$ESP_IMG" 64M >/dev/null

  if command -v mkfs.fat >/dev/null 2>&1; then
    mkfs.fat -F 32 "$ESP_IMG" >/dev/null
  else
    mkfs.vfat -F 32 "$ESP_IMG" >/dev/null
  fi

  mmd -i "$ESP_IMG" ::/EFI ::/EFI/BOOT >/dev/null
  mcopy -i "$ESP_IMG" "$EFI_BIN" ::/EFI/BOOT/BOOTX64.EFI >/dev/null
  copy_extra_esp_files
  seed_default_hatteros_tree
  prepare_auto_splash

  cp "$ovmf_vars_src" "$OVMF_VARS_WORK"

  echo "Using OVMF CODE: $ovmf_code"
  echo "Using OVMF VARS: $ovmf_vars_src"
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
