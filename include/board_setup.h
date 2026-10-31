// TFT_eSPI setup for the Freenove ESP32 Display 2.8" (FNK0114B_2P8).
//
// Pulled in with -include from platformio.ini, ahead of TFT_eSPI's own
// User_Setup, so nothing in the library is edited. Values match Freenove's
// FNK0114B_2.8_240x320_ST7789.h and the freenove28 build of
// Moaske/SquachWatch-CYD-BATT, which runs on this exact board.
#pragma once

#define USER_SETUP_INFO "Freenove MultiRemote / FNK0114B 2.8 inch / ST7789"

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1   // no reset line: rely on power-on reset
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR

#define SPI_FREQUENCY      40000000
#define SPI_READ_FREQUENCY 20000000

// Fonts: the built-in small faces plus the Adafruit-GFX free fonts.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_GFXFF
#define SMOOTH_FONT
