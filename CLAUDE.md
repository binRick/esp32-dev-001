# ESP32-S3 Development Project

## Hardware

**Board:** Freenove Development Kit for ESP32-S3 — model FNK0086
- ESP32-S3 dual-core 32-bit 240 MHz microcontroller
- 8MB PSRAM, 8MB Flash
- Onboard OV2640 camera, WiFi, Bluetooth

### LCD Display
- **Driver:** ST7789, 2.8 inch, 240×320 portrait
- **Library:** TFT_eSPI (config: `FNK0086A_2.8_CFG1_240x320_ST7789.h`)
- MOSI=20, SCLK=21, DC=0, CS=-1 (tied GND), USE_HSPI_PORT, 80MHz SPI
- Color order: BGR, inversion OFF

### Touch Controller
- **Driver:** FT6336U (capacitive I2C)
- **Library:** Arduino-FT6336U
- SDA=2, SCL=1

### I2S Speakers (stereo)
- BCLK=42, DOUT=41, LRC=14
- **Library:** ESP32-audioI2S (for music); raw `driver/i2s.h` for tones

### Heart Rate Sensor
- **Driver:** MAX30102
- **Library:** SparkFun MAX3010x

### SD Card
- SDMMC protocol: CMD=38, CLK=39, D0=40

### Required Arduino Libraries (from Freenove repo)
All zips in `Libraries/` of https://github.com/Freenove/Freenove_Development_Kit_for_ESP32_S3
- `TFT_eSPI_v2.5.43.zip` — install via Arduino IDE, then replace `User_Setup.h` with FNK0086 config
- `TFT_eSPI_Setups_v1.3.zip` — pin config headers
- `Arduino-FT6336U_v1.0.2.zip` — touch controller
- `ESP32-audioI2S_v2.0.0.zip` — audio playback
- `SparkFun_MAX3010x_...zip` — heart rate sensor

## Serial Port

**Port:** `/dev/cu.wchusbserial5AB90133721`

**Driver:** WCH CH343 USB serial driver (installed manually, required reboot)

Use this port for flashing and serial monitor communication.

### Arduino IDE
Tools > Port > `/dev/cu.wchusbserial5AB90133721`

### esptool.py
Installed in the repo's virtual environment at `.venv/`.

```bash
# Activate venv first
source .venv/bin/activate

# Or call directly
.venv/bin/esptool.py --port /dev/cu.wchusbserial5AB90133721 ...
```

### ESP-IDF / VS Code
Set `PORT=/dev/cu.wchusbserial5AB90133721` in your project config.

## Python Environment

All Python tools must be installed into the repo's virtual environment — never globally.

```bash
# Install a new tool
.venv/bin/pip install <package>

# Run a tool
.venv/bin/<tool> ...

# Or activate first, then use normally
source .venv/bin/activate
```

Do not use `pip3 install` or `pip install` directly — always use `.venv/bin/pip`.

## Workflow — New Sketches

When asked to write a new sketch, always produce **both**:

1. **The sketch** — `sketches/<name>/<name>.ino` (plus any supporting files like `User_Setup.h`)
2. **A flash script** — `flash-sketch-<NNN>-<name>.sh` that compiles with arduino-cli and flashes via `.venv/bin/esptool.py`

Number scripts sequentially: `flash-sketch-001-...`, `flash-sketch-002-...`, etc.

After writing the sketch and flash script, **always run the flash script automatically** and iterate until it compiles and flashes successfully — fixing errors without being asked.

The flash script must:
- Install arduino-cli via Homebrew if missing
- Install required libraries (from registry or Freenove zip as needed)
- Patch `TFT_eSPI/User_Setup.h` if the sketch uses the display
- Compile with `arduino-cli` using FQBN `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi`
- Flash bootloader + partitions + app via `.venv/bin/esptool.py`
- Support `--compile` and `--flash` flags for partial runs

## Notes

- The board has two USB ports — the one in use is connected via the WCH/CH343 UART bridge
- If the port disappears, check the cable (must be data-capable, not charge-only) and re-plug
