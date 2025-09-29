#pragma once

// -----------------------------------------------------------------------------
// University logo bitmap placeholder for the thermal printer.
//
// How to use:
// 1. Convert your university emblem to a monochrome (black & white) image with
//    a maximum width of 384 pixels (the printer's printable width).
// 2. Export the bitmap as 1-bit depth and generate the byte array using tools
//    such as:
//      - https://github.com/adafruit/Adafruit_Thermal_Printer/tree/master/examples
//        (ThermalImageConversion sketch),
//      - https://javl.github.io/image2cpp/ (select Monochrome, vertical bytes), or
//      - the Python helper in this repo's tmp/ folder (see README notes below).
// 3. Replace the width, height, and data array below with the values generated
//    by the conversion tool.
// 4. Set UNIVERSITY_LOGO_WIDTH/HEIGHT to match your bitmap and update the
//    contents of UNIVERSITY_LOGO_BITMAP.
// 5. The firmware will automatically detect non-zero dimensions and print the
//    logo before the receipt text.
//
// Notes:
// - Black pixels correspond to value bit=1, white pixels bit=0 (MSB on the left).
// - Keep the data in PROGMEM to avoid consuming precious RAM on the ESP32.
// - If you don't want to print a logo, leave width/height as 0.
// -----------------------------------------------------------------------------

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif

#define UNIVERSITY_LOGO_WIDTH  0
#define UNIVERSITY_LOGO_HEIGHT 0

static const uint8_t UNIVERSITY_LOGO_BITMAP[] PROGMEM = {
    // TODO: replace with your logo data
};
