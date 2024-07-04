#include <avr/pgmspace.h>
#include <uzebox.h>
#include <keyboard.h>

uint8_t x = 0;
uint8_t y = 0;
uint8_t color = 0xFF;
uint8_t state = 0;
uint8_t mode;
uint8_t param_0;

static void scroll() {
    if (y > (SCREEN_TILES_V - 1)) {
        // scroll all lines up
        for (u16 i = 0; i < (VRAM_TILES_H * (SCREEN_TILES_V - 1)); i++) {
            vram[i] = vram[(i + VRAM_TILES_H)];
            aram[i] = aram[(i + VRAM_TILES_H)];
        }

        // clear last line
        for (u8 i = 0; i < VRAM_TILES_H; i++){
            SetFont(i, SCREEN_TILES_V - 1, 0);
        }

        y = SCREEN_TILES_V - 1;
        x = 0;
    }
}

static void print_char(u8 c) {
    if (x > SCREEN_TILES_H) {
        x = 0;
        y++;
    }

    if (state == 1) {
        param_0 = c;
        state = 2;
    } else if (state == 2) {
        switch (mode) {
            case 0xF0:
                // fill
                for (int i = 0; i < VRAM_SIZE; i++) aram[i] = color;
                FontFill(0, 0, SCREEN_TILES_H - 1, SCREEN_TILES_V - 1, param_0);
                break;
            case 0xF1:
                // move cursor
                x = param_0;
                y = c;
                break;
            case 0xF2:
                // set color
                if (param_0 & 0xF0 == 0x70)
                    color = 0xFF;
                else
                    color = (param_0 & 0xF0) * 2;
                break;
            case 0xF3:
                // fill line
                for (int i = y * SCREEN_TILES_H + x; i < SCREEN_TILES_H; i++) aram[i] = color;
                FontFill(0, y, SCREEN_TILES_H - 1, 1, param_0);
                break;
        }
        state = 0;
    } else {
        switch (c) {
            case 0x08:
                if (x) x--;
                break;
            case 0x0D:
                x = 0;
                break;
            case 0x0A:
                x = 0;
                y++;
                break;
            case 0x8A:
                // cursor
                aram[y * SCREEN_TILES_H + x] = color;
                PrintChar(x++, y, 0xB1);
                break;
            case 0xF0:
            case 0xF1:
            case 0xF2:
            case 0xF3:
                mode = c;
                state = 1;
                break;
            case 0xFE:
            case 0xFF:
                // redraw
                break;

            default:
                aram[y * SCREEN_TILES_H + x] = color;
                PrintChar(x++, y, c);
                break;
        }
    }

    scroll();
}

int serial_get(void) {
    KeyboardPoll();
    u8 key = KeyboardGetKey(true);
    if (key == 0x0D) key = 0x0A;
    return key;
}

void serial_put(int value) {
    print_char(value);
}
