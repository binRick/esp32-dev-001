// TFT_eSPI User_Setup.h for Freenove ESP32-S3 Dev Kit (FNK0086)
// 2.8" ST7789, 240x320
//
// Copy this file to:
//   <Arduino libraries folder>/TFT_eSPI/User_Setup.h
// (overwrite the existing one)

#define USER_SETUP_INFO "FNK0086_ST7789_240x320"

// Driver
#define ST7789_DRIVER
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_OFF

// Pins (HSPI)
#define TFT_MOSI  20
#define TFT_SCLK  21
#define TFT_CS    -1   // CS tied to GND on board
#define TFT_DC     0
// TFT_RST not connected — comment out or leave undefined

// Use ESP32 HSPI bus
#define USE_HSPI_PORT

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

// Speed
#define SPI_FREQUENCY  80000000
