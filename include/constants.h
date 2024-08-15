#ifndef CONSTANTS_H
#define CONSTANTS_H

// Color definitions
#define COLOR_BLUE    0x0000FF
#define COLOR_GREEN   0x00FF00
#define COLOR_CYAN    0x00FFFF
#define COLOR_YELLOW  0xFFFF00
#define COLOR_RED     0xFF0000
#define COLOR_ORANGE  0xFFA500

//LED Config
#define LED_PIN 8
#define NUM_LEDS 24
#define ROWS 4
#define COLS 6

// Pin definitions
const int rowPins[ROWS] = {21, 20, 3, 7};
const int colPins[COLS] = {0, 1, 10, 4, 5, 6};

#endif // CONSTANTS_H