#include <avr/pgmspace.h>
#include <uzebox.h>
#include <keyboard.h>

uint8_t x = 0;
uint8_t y = 0;

static void scroll() {
    if (y > (SCREEN_TILES_V - 1)) {
        // scroll all lines up
        for (u16 i = 0; i < (VRAM_TILES_H * (SCREEN_TILES_V - 1)); i++) {
            vram[i] = vram[(i + VRAM_TILES_H)];
        }

        // clear last line
        for(u8 i = 0; i < VRAM_TILES_H; i++){
            SetFont(i, SCREEN_TILES_V - 1, 0);
        }

        y = SCREEN_TILES_V - 1;
        x = 0;
    }
}

static void print_char(char c) {
    if (c == 0x0D) {
        x = 0;
    } else if (c == 0x0A) {
        x = 0;
        y++;
    } else if (c == 0x8A) {
        // cursor
        PrintChar(x++, y, '_');
    } else if (c == 0xFE || c == 0xFF) {
        // redraw, do nothing
    } else {
        PrintChar(x++, y, c);
    }
    if (x > SCREEN_TILES_H) {
        x = 0;
        y++;
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
