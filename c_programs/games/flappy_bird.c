#include "../common/glic80.h"

#define BIRD_X 24u
#define PIPE_WIDTH 14u
#define PIPE_GAP_PAGES 5u
#define FLAPPY_DELAY 2500u
#define FLAP_MASK (GLIC_BTN_CENTER | GLIC_BTN_UP | GLIC_BTN_A)

static unsigned char rng_state;
static unsigned char score;

static const unsigned char bird_wing_up[8] = {
    0x18u, 0x3cu, 0x7eu, 0xffu, 0x7eu, 0x5au, 0x24u, 0x00u
};

static const unsigned char bird_wing_down[8] = {
    0x18u, 0x3cu, 0x7eu, 0xffu, 0x7eu, 0x24u, 0x5au, 0x00u
};

static const unsigned char bird_crash[8] = {
    0x81u, 0x42u, 0x24u, 0x18u, 0x18u, 0x24u, 0x42u, 0x81u
};

static const unsigned char pipe_texture[4] = {
    0xffu, 0xe7u, 0xdbu, 0xe7u
};

static unsigned char random8(void) {
    unsigned char bit;

    bit = (unsigned char)(((rng_state >> 7) ^
                           (rng_state >> 5) ^
                           (rng_state >> 4) ^
                           (rng_state >> 3)) & 1u);
    rng_state = (unsigned char)((rng_state << 1) | bit);
    return rng_state;
}

static unsigned char flap_pressed(unsigned char buttons) {
    return (buttons & FLAP_MASK) != FLAP_MASK;
}

static void wait_for_flap_press(void) {
    while (flap_pressed(glic_read_buttons()) != 0u) {
        random8();
    }
    while (flap_pressed(glic_read_buttons()) == 0u) {
        random8();
    }
    while (flap_pressed(glic_read_buttons()) != 0u) {
        random8();
    }
}

static volatile unsigned char *text_cell(unsigned char row,
                                         unsigned char col) {
    volatile unsigned char *p;

    p = GLIC_CVRAM;
    while (row != 0u) {
        p += 16u;
        --row;
    }
    p += col;
    return p;
}

static void text_put(unsigned char row, unsigned char col, unsigned char ch) {
    if ((row >= 16u) || (col >= 16u)) {
        return;
    }
    *text_cell(row, col) = ch;
}

static void text_clear_row(unsigned char row) {
    unsigned char col;

    for (col = 0u; col < 16u; ++col) {
        text_put(row, col, 0u);
    }
}

static void text_write(unsigned char row, unsigned char col, const char *text) {
    while ((*text != 0) && (col < 16u)) {
        text_put(row, col, (unsigned char)*text);
        ++text;
        ++col;
    }
}

static void text_write_u8_3(unsigned char row,
                            unsigned char col,
                            unsigned char value) {
    unsigned char hundreds;
    unsigned char tens;

    hundreds = 0u;
    while (value >= 100u) {
        value = (unsigned char)(value - 100u);
        ++hundreds;
    }
    tens = 0u;
    while (value >= 10u) {
        value = (unsigned char)(value - 10u);
        ++tens;
    }
    text_put(row, col, (unsigned char)('0' + hundreds));
    text_put(row, (unsigned char)(col + 1u), (unsigned char)('0' + tens));
    text_put(row,
             (unsigned char)(col + 2u),
             (unsigned char)('0' + value));
}

static void draw_score(void) {
    text_clear_row(0u);
    text_write(0u, 0u, "SCORE");
    text_write_u8_3(0u, 6u, score);
}

static void clear_scene(void) {
    glic_fill_graphics(GLIC_BLACK);
}

static unsigned char pipe_byte(unsigned char pipe_x,
                               unsigned char gap_page,
                               unsigned char x,
                               unsigned char page) {
    unsigned char local_x;
    unsigned char gap_end;

    if ((page >= 15u) || (x < pipe_x)) {
        return GLIC_BLACK;
    }

    local_x = (unsigned char)(x - pipe_x);
    if (local_x >= PIPE_WIDTH) {
        return GLIC_BLACK;
    }

    gap_end = (unsigned char)(gap_page + PIPE_GAP_PAGES);
    if ((page >= gap_page) && (page < gap_end)) {
        return GLIC_BLACK;
    }

    return pipe_texture[page & 3u];
}

static void restore_scene_region(unsigned char x,
                                 unsigned char width,
                                 unsigned char page,
                                 unsigned char page_count,
                                 unsigned char pipe_x,
                                 unsigned char gap_page) {
    unsigned char p;
    unsigned int offset;

    while (width != 0u) {
        if (x < GLIC_SCREEN_WIDTH) {
            p = page;
            while (p < (unsigned char)(page + page_count)) {
                offset = (((unsigned int)p) << 7) + x;
                GLIC_GVRAM[offset] = pipe_byte(pipe_x, gap_page, x, p);
                ++p;
            }
        }
        ++x;
        --width;
    }
}

static void erase_pipe(unsigned char pipe_x) {
    unsigned char i;
    unsigned char col;
    unsigned char page;

    for (i = 0u; i < PIPE_WIDTH; ++i) {
        col = (unsigned char)(pipe_x + i);
        if (col >= GLIC_SCREEN_WIDTH) {
            continue;
        }
        for (page = 0u; page < 15u; ++page) {
            GLIC_GVRAM[(((unsigned int)page) << 7) + col] = GLIC_BLACK;
        }
    }
}

static void erase_pipe_column(unsigned char x) {
    unsigned char page;

    if (x >= GLIC_SCREEN_WIDTH) {
        return;
    }
    for (page = 0u; page < 15u; ++page) {
        GLIC_GVRAM[(((unsigned int)page) << 7) + x] = GLIC_BLACK;
    }
}

static void draw_pipe_column(unsigned char pipe_x,
                             unsigned char gap_page,
                             unsigned char x) {
    unsigned char page;

    if (x >= GLIC_SCREEN_WIDTH) {
        return;
    }
    for (page = 0u; page < 15u; ++page) {
        GLIC_GVRAM[(((unsigned int)page) << 7) + x] =
            pipe_byte(pipe_x, gap_page, x, page);
    }
}

static void draw_pipe(unsigned char pipe_x, unsigned char gap_page) {
    unsigned char i;
    unsigned char page;
    unsigned char col;

    for (i = 0u; i < PIPE_WIDTH; ++i) {
        col = (unsigned char)(pipe_x + i);
        if (col >= GLIC_SCREEN_WIDTH) {
            continue;
        }
        for (page = 0u; page < 15u; ++page) {
            GLIC_GVRAM[(((unsigned int)page) << 7) + col] =
                pipe_byte(pipe_x, gap_page, col, page);
        }
    }
}

static void move_pipe_left(unsigned char old_pipe_x,
                           unsigned char pipe_x,
                           unsigned char gap_page) {
    unsigned char old_right;

    old_right = (unsigned char)(old_pipe_x + PIPE_WIDTH - 1u);
    if (old_right < GLIC_SCREEN_WIDTH) {
        erase_pipe_column(old_right);
    }
    draw_pipe_column(pipe_x, gap_page, pipe_x);
}

static void erase_bird(unsigned char bird_y,
                       unsigned char pipe_x,
                       unsigned char gap_page) {
    unsigned char page;
    unsigned char last_page;

    page = bird_y >> 3;
    last_page = (unsigned char)((bird_y + 7u) >> 3);
    restore_scene_region(BIRD_X,
                         8u,
                         page,
                         (unsigned char)(last_page - page + 1u),
                         pipe_x,
                         gap_page);
}

static void draw_bird(unsigned char bird_y, unsigned char frame) {
    if ((frame & 4u) == 0u) {
        glic_draw_bitmap8(BIRD_X, bird_y, bird_wing_up);
    } else {
        glic_draw_bitmap8(BIRD_X, bird_y, bird_wing_down);
    }
}

static unsigned char bird_hits_pipe(unsigned char pipe_x,
                                    unsigned char gap_page,
                                    unsigned char bird_y) {
    unsigned char pipe_right;
    unsigned char bird_top_page;
    unsigned char bird_bottom_page;

    pipe_right = (unsigned char)(pipe_x + PIPE_WIDTH - 1u);
    if (((unsigned char)(BIRD_X + 7u) < pipe_x) || (BIRD_X > pipe_right)) {
        return 0u;
    }

    bird_top_page = bird_y >> 3;
    bird_bottom_page = (unsigned char)((bird_y + 7u) >> 3);
    if ((bird_top_page < gap_page) ||
        (bird_bottom_page >= (unsigned char)(gap_page + PIPE_GAP_PAGES))) {
        return 1u;
    }

    return 0u;
}

static void draw_crash(unsigned char bird_y) {
    unsigned char i;

    glic_draw_bitmap8(BIRD_X, bird_y, bird_crash);
    for (i = 0u; i < GLIC_SCREEN_WIDTH; ++i) {
        glic_plot(i, i, 1u);
        glic_plot((unsigned char)(127u - i), i, 1u);
    }
}

static void draw_title_screen(void) {
    glic_prepare_screen(GLIC_BLACK);
    draw_pipe(96u, 5u);
    draw_bird(56u, 0u);
    text_write(2u, 2u, "FLAPPY BIRD");
    text_write(5u, 1u, "FLAP TO START");
    text_write(7u, 2u, "UP/A FLAP");
    text_write(11u, 2u, "DODGE PIPES");
}

static void draw_game_over_text(void) {
    text_clear_row(5u);
    text_clear_row(6u);
    text_clear_row(7u);
    text_clear_row(8u);
    text_clear_row(10u);
    text_write(5u, 3u, "GAME OVER");
    text_write(7u, 4u, "SCORE");
    text_write_u8_3(7u, 10u, score);
    text_write(10u, 3u, "FLAP MENU");
}

void main(void) {
    unsigned char buttons;
    unsigned char last_buttons;
    unsigned char pipe_x;
    unsigned char gap_page;
    unsigned char pipe_right;
    unsigned char pipe_scored;
    unsigned char bird_y;
    unsigned char frame;
    unsigned char game_over;
    signed char velocity;
    signed int next_y;
    unsigned char old_bird_y;
    unsigned char old_pipe_x;
    unsigned char old_gap_page;

    glic_clear_text();
    rng_state = 0x5au;

    while (1) {
        draw_title_screen();
        wait_for_flap_press();
        clear_scene();
        glic_clear_text();
        bird_y = 56u;
        velocity = 0;
        pipe_x = 112u;
        gap_page = 5u;
        pipe_scored = 0u;
        score = 0u;
        frame = 0u;
        last_buttons = GLIC_BUTTONS_NONE;
        game_over = 0u;
        draw_pipe(pipe_x, gap_page);
        draw_bird(bird_y, frame);
        draw_score();

        while (game_over == 0u) {
            old_bird_y = bird_y;
            old_pipe_x = pipe_x;
            old_gap_page = gap_page;
            buttons = glic_read_buttons();
            if ((flap_pressed(last_buttons) == 0u) &&
                (flap_pressed(buttons) != 0u)) {
                velocity = -5;
            } else if (velocity < 4) {
                ++velocity;
            }
            last_buttons = buttons;

            next_y = (signed int)bird_y + (signed int)velocity;
            if (next_y < 0) {
                bird_y = 0u;
                game_over = 1u;
            } else if (next_y > 119) {
                bird_y = 119u;
                game_over = 1u;
            } else {
                bird_y = (unsigned char)next_y;
            }

            if (pipe_x == 0u) {
                pipe_x = 127u;
                gap_page = (unsigned char)(2u + (random8() & 7u));
                if (gap_page > 9u) {
                    gap_page = 9u;
                }
                pipe_scored = 0u;
            } else {
                --pipe_x;
            }

            pipe_right = (unsigned char)(pipe_x + PIPE_WIDTH - 1u);
            if ((pipe_scored == 0u) && (pipe_right < BIRD_X)) {
                if (score < 255u) {
                    ++score;
                }
                draw_score();
                pipe_scored = 1u;
            }

            if (bird_hits_pipe(pipe_x, gap_page, bird_y) != 0u) {
                game_over = 1u;
            }

            erase_bird(old_bird_y, old_pipe_x, old_gap_page);
            if (pipe_x > old_pipe_x) {
                erase_pipe(old_pipe_x);
                draw_pipe(pipe_x, gap_page);
            } else {
                move_pipe_left(old_pipe_x, pipe_x, gap_page);
            }
            draw_bird(bird_y, frame);
            if (game_over != 0u) {
                draw_crash(bird_y);
                draw_game_over_text();
            }
            ++frame;
            glic_delay(FLAPPY_DELAY);
        }

        wait_for_flap_press();
    }
}
