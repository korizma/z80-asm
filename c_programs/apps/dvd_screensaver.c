#include "../common/glic80.h"

#define LOGO_WIDTH 44u
#define LOGO_HEIGHT 24u
#define LOGO_PAGES 3u
#define LOGO_MAX_X (GLIC_SCREEN_WIDTH - LOGO_WIDTH)
#define LOGO_MAX_Y (128u - LOGO_HEIGHT)
#define FRAME_DELAY 1800u

static const unsigned char logo_shape[LOGO_PAGES][LOGO_WIDTH] = {
    {
        0x00u, 0x00u, 0x00u, 0x00u, 0xfcu, 0xfcu, 0x0cu, 0x0cu,
        0x0cu, 0x0cu, 0x0cu, 0x0cu, 0xf0u, 0xf0u, 0x00u, 0x00u,
        0x00u, 0xfcu, 0xfcu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0xfcu, 0xfcu, 0x00u, 0x00u, 0x00u, 0xfcu, 0xfcu,
        0x0cu, 0x0cu, 0x0cu, 0x0cu, 0x0cu, 0x0cu, 0xf0u, 0xf0u,
        0x00u, 0x00u, 0x00u, 0x00u
    },
    {
        0x00u, 0x00u, 0x00u, 0x00u, 0xffu, 0xffu, 0xc0u, 0xc0u,
        0xc0u, 0xc0u, 0xc0u, 0xc0u, 0x3fu, 0x3fu, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x0fu, 0x0fu, 0xf0u, 0xf0u, 0x0fu,
        0x0fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xffu, 0xffu,
        0xc0u, 0xc0u, 0xc0u, 0xc0u, 0xc0u, 0xc0u, 0x3fu, 0x3fu,
        0x00u, 0x00u, 0x00u, 0x00u
    },
    {
        0x08u, 0x1cu, 0x1cu, 0x1cu, 0x1cu, 0x3eu, 0x3eu, 0x3eu,
        0x3eu, 0x3eu, 0x3eu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu,
        0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu,
        0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu,
        0x7fu, 0x3eu, 0x3eu, 0x3eu, 0x3eu, 0x3eu, 0x3eu, 0x1cu,
        0x1cu, 0x1cu, 0x1cu, 0x08u
    }
};

static void clear_logo_area(unsigned char x, unsigned char y) {
    unsigned char first_page;
    unsigned char last_page;
    unsigned char page;
    unsigned char col;
    unsigned int offset;

    first_page = (unsigned char)(y >> 3);
    last_page = (unsigned char)((y + LOGO_HEIGHT - 1u) >> 3);

    for (page = first_page; page <= last_page; ++page) {
        offset = (((unsigned int)page) << 7) + x;
        for (col = 0u; col < LOGO_WIDTH; ++col) {
            GLIC_GVRAM[offset + col] = GLIC_BLACK;
        }
    }
}

static void draw_logo_aligned(unsigned char x, unsigned char page) {
    unsigned char logo_page;
    unsigned char col;
    unsigned char bits;
    unsigned int offset;

    for (logo_page = 0u; logo_page < LOGO_PAGES; ++logo_page) {
        offset = (((unsigned int)(page + logo_page)) << 7) + x;
        for (col = 0u; col < LOGO_WIDTH; ++col) {
            bits = logo_shape[logo_page][col];
            GLIC_GVRAM[offset + col] |= bits;
        }
    }
}

static void draw_logo_shifted(unsigned char x,
                              unsigned char page,
                              unsigned char shift) {
    unsigned char logo_page;
    unsigned char col;
    unsigned char bits;
    unsigned char spill_shift;
    unsigned int offset;

    spill_shift = (unsigned char)(8u - shift);

    for (logo_page = 0u; logo_page < LOGO_PAGES; ++logo_page) {
        offset = (((unsigned int)(page + logo_page)) << 7) + x;
        for (col = 0u; col < LOGO_WIDTH; ++col) {
            bits = logo_shape[logo_page][col];
            if (bits != 0u) {
                GLIC_GVRAM[offset + col] |= (unsigned char)(bits << shift);
                GLIC_GVRAM[offset + 128u + col] |=
                    (unsigned char)(bits >> spill_shift);
            }
        }
    }
}

static void draw_logo(unsigned char x, unsigned char y) {
    unsigned char page;
    unsigned char shift;

    page = (unsigned char)(y >> 3);
    shift = (unsigned char)(y & 7u);

    if (shift == 0u) {
        draw_logo_aligned(x, page);
    } else {
        draw_logo_shifted(x, page, shift);
    }
}

void main(void) {
    unsigned char x;
    unsigned char y;
    unsigned char move_right;
    unsigned char move_down;

    glic_prepare_screen(GLIC_BLACK);

    x = 9u;
    y = 16u;
    move_right = 1u;
    move_down = 1u;

    draw_logo(x, y);

    while (1) {
        glic_delay(FRAME_DELAY);
        clear_logo_area(x, y);

        if (move_right != 0u) {
            if (x < LOGO_MAX_X) {
                ++x;
            } else {
                move_right = 0u;
                --x;
            }
        } else if (x != 0u) {
            --x;
        } else {
            move_right = 1u;
            ++x;
        }

        if (move_down != 0u) {
            if (y < LOGO_MAX_Y) {
                ++y;
            } else {
                move_down = 0u;
                --y;
            }
        } else if (y != 0u) {
            --y;
        } else {
            move_down = 1u;
            ++y;
        }

        draw_logo(x, y);
    }
}
