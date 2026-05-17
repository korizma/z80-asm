#include "../common/glic80.h"

#define MIN_DIFFICULTY 1u
#define MAX_DIFFICULTY 64u
#define DEFAULT_DIFFICULTY 12u
#define FAST_DIFFICULTY_STEP 8u
#define HASH_BATCH 32u
#define LOOP_DELAY 900u

static unsigned char difficulty;
static unsigned char block_id;
static unsigned char best_zeroes;
static unsigned int mining_extra;
static unsigned int current_nonce_lo;
static unsigned int current_nonce_hi;
static unsigned int current_attempts_lo;
static unsigned int current_attempts_hi;
static unsigned int current_hash_a;
static unsigned int current_hash_b;
static unsigned int current_hash_c;
static unsigned int current_hash_d;
static unsigned int best_nonce_lo;
static unsigned int best_nonce_hi;
static unsigned int best_hash_a;
static unsigned int best_hash_b;
static unsigned int best_hash_c;
static unsigned int best_hash_d;
static unsigned int mined_nonce_lo;
static unsigned int mined_nonce_hi;
static unsigned int mined_attempts_lo;
static unsigned int mined_attempts_hi;
static unsigned int mined_hash_a;
static unsigned int mined_hash_b;
static unsigned int mined_hash_c;
static unsigned int mined_hash_d;

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

static void text_write_u8_2(unsigned char row,
                            unsigned char col,
                            unsigned char value) {
    unsigned char tens;

    if (value > 99u) {
        value = 99u;
    }

    tens = 0u;
    while (value >= 10u) {
        value = (unsigned char)(value - 10u);
        ++tens;
    }

    text_put(row, col, (unsigned char)('0' + tens));
    text_put(row, (unsigned char)(col + 1u), (unsigned char)('0' + value));
}

static unsigned char hex_digit(unsigned char value) {
    value &= 15u;
    if (value < 10u) {
        return (unsigned char)('0' + value);
    }
    return (unsigned char)('A' + (value - 10u));
}

static void text_write_hex2(unsigned char row,
                            unsigned char col,
                            unsigned char value) {
    text_put(row, col, hex_digit((unsigned char)(value >> 4)));
    text_put(row, (unsigned char)(col + 1u), hex_digit(value));
}

static void text_write_hex4(unsigned char row,
                            unsigned char col,
                            unsigned int value) {
    text_put(row, col, hex_digit((unsigned char)(value >> 12)));
    text_put(row, (unsigned char)(col + 1u),
             hex_digit((unsigned char)(value >> 8)));
    text_put(row, (unsigned char)(col + 2u),
             hex_digit((unsigned char)(value >> 4)));
    text_put(row, (unsigned char)(col + 3u), hex_digit((unsigned char)value));
}

static void text_write_hex8(unsigned char row,
                            unsigned char col,
                            unsigned int high,
                            unsigned int low) {
    text_write_hex4(row, col, high);
    text_write_hex4(row, (unsigned char)(col + 4u), low);
}

static void text_write_hash64(unsigned char row,
                              unsigned int a,
                              unsigned int b,
                              unsigned int c,
                              unsigned int d) {
    text_write(row, 0u, "HASH ");
    text_write_hex8(row, 5u, a, b);
    text_write_hex8((unsigned char)(row + 1u), 5u, c, d);
}

static unsigned char button_edge(unsigned char buttons,
                                 unsigned char last_buttons,
                                 unsigned char mask) {
    return (((last_buttons & mask) != 0u) && ((buttons & mask) == 0u));
}

static void wait_for_release(void) {
    while (glic_read_buttons() != GLIC_BUTTONS_NONE) {
        glic_delay(500u);
    }
}

static void clear_screen(void) {
    glic_prepare_screen(GLIC_BLACK);
}

static unsigned int rotl5(unsigned int value) {
    return (unsigned int)((value << 5) | (value >> 11));
}

static unsigned int rotl7(unsigned int value) {
    return (unsigned int)((value << 7) | (value >> 9));
}

static unsigned int swap16(unsigned int value) {
    return (unsigned int)((value << 8) | (value >> 8));
}

static unsigned int mix16(unsigned int hash, unsigned int value) {
    hash ^= value;
    hash = rotl5(hash);
    hash = (unsigned int)(hash + 0x9e37u);
    hash ^= (unsigned int)(hash >> 7);
    hash = (unsigned int)(hash + (hash << 3));
    hash = rotl7(hash);
    hash ^= (unsigned int)(hash >> 11);
    return hash;
}

static void mix_hash_word(unsigned int value) {
    current_hash_a = mix16(current_hash_a, (unsigned int)(value ^ current_hash_d));
    current_hash_b = mix16(current_hash_b, (unsigned int)(value + current_hash_a));
    current_hash_c = mix16(current_hash_c, (unsigned int)(value ^ current_hash_b));
    current_hash_d = mix16(current_hash_d, (unsigned int)(value + current_hash_c));
    current_hash_a ^= current_hash_c;
    current_hash_b = (unsigned int)(current_hash_b + current_hash_d);
}

static void pow_hash(void) {
    current_hash_a = 0x6a09u;
    current_hash_b = 0xbb67u;
    current_hash_c = 0x3c6eu;
    current_hash_d = 0xa54fu;

    mix_hash_word(0x474cu);
    mix_hash_word(0x4943u);
    mix_hash_word(0x3830u);
    mix_hash_word(0x4254u);
    mix_hash_word(0x4343u);
    mix_hash_word(((unsigned int)block_id << 8) | difficulty);
    mix_hash_word(mining_extra);
    mix_hash_word(current_nonce_hi);
    mix_hash_word(current_nonce_lo);
    mix_hash_word(swap16(current_nonce_hi));
    mix_hash_word(swap16(current_nonce_lo));
}

static unsigned char leading_zero_bits_word(unsigned int value) {
    unsigned char zeroes;
    unsigned int mask;

    zeroes = 0u;
    mask = 0x8000u;
    while ((mask != 0u) && ((value & mask) == 0u)) {
        ++zeroes;
        mask >>= 1;
    }
    return zeroes;
}

static unsigned char leading_zero_bits_hash(void) {
    unsigned char zeroes;

    zeroes = leading_zero_bits_word(current_hash_a);
    if (zeroes != 16u) {
        return zeroes;
    }

    zeroes = (unsigned char)(16u + leading_zero_bits_word(current_hash_b));
    if (zeroes != 32u) {
        return zeroes;
    }

    zeroes = (unsigned char)(32u + leading_zero_bits_word(current_hash_c));
    if (zeroes != 48u) {
        return zeroes;
    }

    return (unsigned char)(48u + leading_zero_bits_word(current_hash_d));
}

static unsigned char current_hash_less_best(void) {
    if (current_hash_a != best_hash_a) {
        return current_hash_a < best_hash_a;
    }
    if (current_hash_b != best_hash_b) {
        return current_hash_b < best_hash_b;
    }
    if (current_hash_c != best_hash_c) {
        return current_hash_c < best_hash_c;
    }
    return current_hash_d < best_hash_d;
}

static void copy_current_hash_to_best(void) {
    best_hash_a = current_hash_a;
    best_hash_b = current_hash_b;
    best_hash_c = current_hash_c;
    best_hash_d = current_hash_d;
    best_nonce_lo = current_nonce_lo;
    best_nonce_hi = current_nonce_hi;
}

static void copy_current_hash_to_mined(void) {
    mined_hash_a = current_hash_a;
    mined_hash_b = current_hash_b;
    mined_hash_c = current_hash_c;
    mined_hash_d = current_hash_d;
    mined_nonce_lo = current_nonce_lo;
    mined_nonce_hi = current_nonce_hi;
    mined_attempts_lo = current_attempts_lo;
    mined_attempts_hi = current_attempts_hi;
}

static void increment_nonce(void) {
    ++current_nonce_lo;
    if (current_nonce_lo == 0u) {
        ++current_nonce_hi;
        if (current_nonce_hi == 0u) {
            ++mining_extra;
        }
    }
}

static void increment_attempts(void) {
    ++current_attempts_lo;
    if (current_attempts_lo == 0u) {
        ++current_attempts_hi;
    }
}

static void draw_zero_bar(unsigned char row,
                          unsigned char zeroes,
                          unsigned char target) {
    unsigned char col;
    unsigned char filled;
    unsigned char acc;

    if (zeroes >= target) {
        filled = 16u;
    } else {
        filled = 0u;
        acc = 0u;
        for (col = 0u; col < 16u; ++col) {
            acc = (unsigned char)(acc + zeroes);
            if (acc >= target) {
                ++filled;
                acc = (unsigned char)(acc - target);
            }
        }
    }

    for (col = 0u; col < 16u; ++col) {
        text_put(row, col, (col < filled) ? '#' : '.');
    }
}

static void draw_setup(void) {
    clear_screen();
    text_write(0u, 0u, "BTC POW DEMO");
    text_write(1u, 0u, "64 BIT TOY HASH");
    text_write(3u, 0u, "DIFFICULTY");
    text_write(4u, 0u, "BITS ");
    text_write_u8_2(4u, 5u, difficulty);
    text_write(4u, 8u, "OF 64");
    text_write(5u, 0u, "BLOCK ");
    text_write_hex2(5u, 6u, block_id);
    text_write(7u, 0u, "UP/DN +/-1");
    text_write(8u, 0u, "LEFT/RIGHT +/-8");
    text_write(9u, 0u, "B NEW BLOCK");
    text_write(10u, 0u, "CENTER/A START");
    text_write(11u, 0u, "C WHY TOY");
}

static void show_help(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    clear_screen();
    text_write(0u, 0u, "WHY TOY?");
    text_write(2u, 0u, "BITCOIN NEEDS");
    text_write(3u, 0u, "NET+SHA256+ASIC");
    text_write(5u, 0u, "THIS USES A");
    text_write(6u, 0u, "64 BIT HASH");
    text_write(8u, 0u, "HIGH BITS CAN");
    text_write(9u, 0u, "RUN A LONG TIME");
    text_write(12u, 0u, "CENTER/C BACK");

    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if ((button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u)) {
            wait_for_release();
            return;
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void increase_difficulty(unsigned char amount) {
    while ((amount != 0u) && (difficulty < MAX_DIFFICULTY)) {
        ++difficulty;
        --amount;
    }
}

static void decrease_difficulty(unsigned char amount) {
    while ((amount != 0u) && (difficulty > MIN_DIFFICULTY)) {
        --difficulty;
        --amount;
    }
}

static void setup_loop(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    draw_setup();
    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            increase_difficulty(1u);
            draw_setup();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            decrease_difficulty(1u);
            draw_setup();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_RIGHT) != 0u) {
            increase_difficulty(FAST_DIFFICULTY_STEP);
            draw_setup();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_LEFT) != 0u) {
            decrease_difficulty(FAST_DIFFICULTY_STEP);
            draw_setup();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            ++block_id;
            draw_setup();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            show_help();
            draw_setup();
            last_buttons = GLIC_BUTTONS_NONE;
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) ||
                   (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u)) {
            wait_for_release();
            return;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void draw_mining_base(void) {
    clear_screen();
    text_write(0u, 0u, "MINING BTC SIM");
    text_write(8u, 0u, "BEST ZERO BITS");
    text_write(13u, 0u, "C STOP");
}

static void draw_mining_status(void) {
    text_clear_row(1u);
    text_write(1u, 0u, "DIFF ");
    text_write_u8_2(1u, 5u, difficulty);
    text_write(1u, 8u, "BEST ");
    text_write_u8_2(1u, 14u, best_zeroes);

    text_clear_row(2u);
    text_write(2u, 0u, "BLK ");
    text_write_hex2(2u, 4u, block_id);
    text_write(2u, 7u, "EX ");
    text_write_hex4(2u, 10u, mining_extra);

    text_clear_row(3u);
    text_write(3u, 0u, "NONCE ");
    text_write_hex8(3u, 6u, current_nonce_hi, current_nonce_lo);

    text_clear_row(4u);
    text_write(4u, 0u, "TRIED ");
    text_write_hex8(4u, 6u, current_attempts_hi, current_attempts_lo);

    text_clear_row(5u);
    text_clear_row(6u);
    text_write_hash64(5u,
                      current_hash_a,
                      current_hash_b,
                      current_hash_c,
                      current_hash_d);

    draw_zero_bar(9u, best_zeroes, difficulty);
}

static void reset_mining_state(void) {
    current_nonce_lo = 0u;
    current_nonce_hi = 0u;
    current_attempts_lo = 0u;
    current_attempts_hi = 0u;
    current_hash_a = 0u;
    current_hash_b = 0u;
    current_hash_c = 0u;
    current_hash_d = 0u;
    best_zeroes = 0u;
    best_nonce_lo = 0u;
    best_nonce_hi = 0u;
    best_hash_a = 0xffffu;
    best_hash_b = 0xffffu;
    best_hash_c = 0xffffu;
    best_hash_d = 0xffffu;
    mining_extra = 0u;
}

static unsigned char mine_block(void) {
    unsigned char i;
    unsigned char zeroes;

    reset_mining_state();
    draw_mining_base();
    draw_mining_status();

    while (1) {
        for (i = 0u; i < HASH_BATCH; ++i) {
            pow_hash();
            zeroes = leading_zero_bits_hash();
            increment_attempts();

            if ((zeroes > best_zeroes) ||
                ((zeroes == best_zeroes) && (current_hash_less_best() != 0u))) {
                best_zeroes = zeroes;
                copy_current_hash_to_best();
            }

            if (zeroes >= difficulty) {
                copy_current_hash_to_mined();
                draw_mining_status();
                return 1u;
            }

            increment_nonce();
        }

        draw_mining_status();
        if (glic_button_pressed(glic_read_buttons(), GLIC_BTN_C) != 0u) {
            wait_for_release();
            return 0u;
        }
    }
}

static void show_found(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    clear_screen();
    text_write(0u, 0u, "BLOCK MINED");
    text_write(2u, 0u, "DIFF ");
    text_write_u8_2(2u, 5u, difficulty);
    text_write(3u, 0u, "NONCE ");
    text_write_hex8(3u, 6u, mined_nonce_hi, mined_nonce_lo);
    text_write_hash64(4u,
                      mined_hash_a,
                      mined_hash_b,
                      mined_hash_c,
                      mined_hash_d);
    text_write(6u, 0u, "TRIES ");
    text_write_hex8(6u, 6u, mined_attempts_hi, mined_attempts_lo);
    text_write(8u, 0u, "REWARD 1 TOYBTC");
    text_write(11u, 0u, "CENTER MENU");
    text_write(12u, 0u, "A NEXT BLOCK");

    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            ++block_id;
            wait_for_release();
            return;
        }
        if ((button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u)) {
            wait_for_release();
            return;
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void show_stopped(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    clear_screen();
    text_write(0u, 0u, "MINING STOPPED");
    text_write(2u, 0u, "BEST ");
    text_write_u8_2(2u, 5u, best_zeroes);
    text_write(3u, 0u, "NONCE ");
    text_write_hex8(3u, 6u, best_nonce_hi, best_nonce_lo);
    text_write_hash64(4u, best_hash_a, best_hash_b, best_hash_c, best_hash_d);
    text_write(6u, 0u, "TRIED ");
    text_write_hex8(6u, 6u, current_attempts_hi, current_attempts_lo);
    text_write(10u, 0u, "CENTER MENU");

    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if ((button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u)) {
            wait_for_release();
            return;
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

void main(void) {
    difficulty = DEFAULT_DIFFICULTY;
    block_id = 1u;

    while (1) {
        setup_loop();
        if (mine_block() != 0u) {
            show_found();
        } else {
            show_stopped();
        }
    }
}
