#!/usr/bin/env bash
# flash.sh — compile beep_button sketch and flash to Freenove ESP32-S3 (FNK0086)
#
# Dependencies installed automatically:
#   arduino-cli  — compiles the .ino sketch
#   .venv/bin/esptool.py — flashes the compiled binary (already in this repo)
#
# Usage:
#   ./flash.sh            # compile + flash
#   ./flash.sh --compile  # compile only
#   ./flash.sh --flash    # flash last compiled binary only

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$SCRIPT_DIR/sketches/beep_button/beep_button.ino"
BUILD_DIR="$SCRIPT_DIR/build"
PORT="/dev/cu.wchusbserial5AB90133721"
ESPTOOL="$SCRIPT_DIR/.venv/bin/esptool.py"
FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi"

# ── Colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[flash]${NC} $*"; }
warn()  { echo -e "${YELLOW}[flash]${NC} $*"; }
error() { echo -e "${RED}[flash]${NC} $*"; exit 1; }

# ── Parse args ────────────────────────────────────────────────────────────────
DO_COMPILE=true
DO_FLASH=true
case "${1:-}" in
  --compile) DO_FLASH=false ;;
  --flash)   DO_COMPILE=false ;;
esac

# ── 1. Ensure arduino-cli is installed ───────────────────────────────────────
if ! command -v arduino-cli &>/dev/null; then
  info "arduino-cli not found — installing via Homebrew..."
  if ! command -v brew &>/dev/null; then
    error "Homebrew not found. Install it from https://brew.sh then re-run."
  fi
  brew install arduino-cli
fi
info "arduino-cli $(arduino-cli version | head -1)"

# ── 2. Ensure ESP32 core is installed ────────────────────────────────────────
CORE="esp32:esp32"
if ! arduino-cli core list 2>/dev/null | grep -q "$CORE"; then
  info "Adding Espressif board index..."
  arduino-cli config init --overwrite 2>/dev/null || true
  arduino-cli config add board_manager.additional_urls \
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  info "Updating board index..."
  arduino-cli core update-index
  info "Installing $CORE core (this takes a few minutes the first time)..."
  arduino-cli core install "$CORE"
else
  info "Core $CORE already installed"
fi

# ── 3. Ensure required libraries are installed ───────────────────────────────
ARDUINO_LIBS="$(arduino-cli config get directories.user 2>/dev/null || echo "$HOME/Documents/Arduino")/libraries"

install_lib_registry() {
  local name="$1"
  if ! arduino-cli lib list 2>/dev/null | grep -q "^$name"; then
    info "Installing library from registry: $name"
    arduino-cli lib install "$name"
  else
    info "Library already installed: $name"
  fi
}

# Install a library from a GitHub release zip if not already present
# Usage: install_lib_zip <dir-name> <zip-url>
install_lib_zip() {
  local dir_name="$1"
  local zip_url="$2"
  local dest="$ARDUINO_LIBS/$dir_name"
  if [ -d "$dest" ]; then
    info "Library already installed: $dir_name"
    return
  fi
  info "Downloading library: $dir_name"
  local tmp_zip
  tmp_zip="$(mktemp /tmp/lib_XXXXXX.zip)"
  curl -sL "$zip_url" -o "$tmp_zip"
  mkdir -p "$ARDUINO_LIBS"
  unzip -q "$tmp_zip" -d "$ARDUINO_LIBS"
  rm "$tmp_zip"
  # Rename extracted folder to expected name if needed
  local extracted
  extracted="$(unzip -Z -1 "$tmp_zip" 2>/dev/null | head -1 | cut -d/ -f1 || true)"
  if [ -n "$extracted" ] && [ "$extracted" != "$dir_name" ] && [ -d "$ARDUINO_LIBS/$extracted" ]; then
    mv "$ARDUINO_LIBS/$extracted" "$dest"
  fi
  info "Installed: $dir_name"
}

FREENOVE_RAW="https://raw.githubusercontent.com/Freenove/Freenove_Development_Kit_for_ESP32_S3/main/Libraries"

install_lib_registry "TFT_eSPI"
install_lib_zip "Arduino-FT6336U" "$FREENOVE_RAW/Arduino-FT6336U_v1.0.2.zip"

# Patch TFT_eSPI User_Setup.h with our board config
TFTESPI_DIR="$(arduino-cli config get directories.user 2>/dev/null || echo "$HOME/Documents/Arduino")/libraries/TFT_eSPI"
if [ -d "$TFTESPI_DIR" ]; then
  info "Patching TFT_eSPI User_Setup.h for FNK0086..."
  cp "$SCRIPT_DIR/sketches/beep_button/User_Setup.h" "$TFTESPI_DIR/User_Setup.h"
else
  warn "TFT_eSPI library directory not found at $TFTESPI_DIR — skipping patch"
fi

# ── 4. Compile ────────────────────────────────────────────────────────────────
if $DO_COMPILE; then
  info "Compiling sketch: $SKETCH"
  mkdir -p "$BUILD_DIR"
  arduino-cli compile \
    --fqbn "$FQBN" \
    --build-path "$BUILD_DIR" \
    --warnings default \
    "$SKETCH"
  info "Compile OK → $BUILD_DIR"
fi

# ── 5. Flash via esptool.py ───────────────────────────────────────────────────
if $DO_FLASH; then
  BIN="$BUILD_DIR/beep_button.ino.bin"
  BOOTLOADER="$BUILD_DIR/beep_button.ino.bootloader.bin"
  PARTITIONS="$BUILD_DIR/beep_button.ino.partitions.bin"
  BOOT_APP="$(find "$HOME/Library/Arduino15/packages/esp32" \
    "$HOME/.arduino15/packages/esp32" \
    -name "boot_app0.bin" 2>/dev/null | head -1)"

  [ -f "$BIN" ]         || error "Binary not found: $BIN  (run --compile first)"
  [ -f "$BOOTLOADER" ]  || error "Bootloader not found: $BOOTLOADER"
  [ -f "$PARTITIONS" ]  || error "Partitions not found: $PARTITIONS"
  [ -f "$BOOT_APP" ]    || error "boot_app0.bin not found in Arduino ESP32 core"

  info "Flashing to $PORT ..."
  "$ESPTOOL" \
    --chip esp32s3 \
    --port "$PORT" \
    --baud 460800 \
    --before default_reset \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size 8MB \
    0x0000  "$BOOTLOADER" \
    0x8000  "$PARTITIONS" \
    0xe000  "$BOOT_APP" \
    0x10000 "$BIN"

  info "Flash complete. Board is resetting..."
fi
