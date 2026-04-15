# ESP32-S3 Development Project

## Hardware

**Board:** Freenove Development Kit for ESP32-S3 (with CAM)
- Dual-core 32-bit 240 MHz microcontroller
- Onboard camera, WiFi/Bluetooth
- Touch screen, stereo speakers, heart rate sensor

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

## Notes

- The board has two USB ports — the one in use is connected via the WCH/CH343 UART bridge
- If the port disappears, check the cable (must be data-capable, not charge-only) and re-plug
