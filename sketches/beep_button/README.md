# beep_button sketch

Draws a BEEP button on the 2.8" LCD. Tap it → speakers play 880 Hz for 200 ms.

## Setup — Arduino IDE

### 1. Board config
- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Enabled**
- Flash Size: **8MB**
- PSRAM: **OPI PSRAM**
- Port: `/dev/cu.wchusbserial5AB90133721`

### 2. Install libraries
Download the zip files from the [Freenove GitHub repo](https://github.com/Freenove/Freenove_Development_Kit_for_ESP32_S3/tree/main/Libraries) and install via **Sketch > Include Library > Add .ZIP Library**:

| Zip file | Purpose |
|----------|---------|
| `TFT_eSPI_v2.5.43.zip` | LCD driver |
| `Arduino-FT6336U_v1.0.2.zip` | Touch controller |

### 3. Configure TFT_eSPI
After installing TFT_eSPI, **replace** its `User_Setup.h` with the one in this folder:

```
cp User_Setup.h ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```

### 4. Upload
Open `beep_button.ino` in Arduino IDE and click Upload.

## Touch coordinate note
If the button doesn't register taps correctly, the X/Y axes may be swapped or mirrored
for your specific unit. Check Serial Monitor output and adjust the hit-test in `loop()`.
