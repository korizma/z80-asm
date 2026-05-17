#include "../common/glic80.h"

#define OS_STORE ((volatile unsigned char *)0x6000)
#define OS_MAGIC0 0x4du
#define OS_MAGIC1 0x4fu
#define OS_MAGIC2 0x53u
#define OS_MAGIC3 0x31u

#define STORE_SELECTED 4u
#define STORE_COUNTER_VALUE 8u
#define STORE_COUNTER_RUNS 10u
#define STORE_MATH_INDEX 16u
#define STORE_MATH_CORRECT 17u
#define STORE_MATH_ANSWER 18u
#define STORE_MATH_ATTEMPTS 20u
#define STORE_MATH_LAST 21u
#define STORE_PAD_X 32u
#define STORE_PAD_Y 33u
#define STORE_PAD_ROWS 40u

#define PROGRAM_COUNT 3u
#define PROGRAM_COUNTER 0u
#define PROGRAM_MATH 1u
#define PROGRAM_PAD 2u

#define MATH_COUNT 6u
#define PAD_WIDTH 16u
#define PAD_HEIGHT 8u
#define LOOP_DELAY 1100u

static const signed char math_left[MATH_COUNT] = {
    3, 7, 12, 9, 8, 15
};

static const signed char math_right[MATH_COUNT] = {
    4, 5, 3, 6, 7, 2
};

static const unsigned char math_op[MATH_COUNT] = {
    '+', '+', '-', '+', '-', '+'
};

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

static void text_write_u16(unsigned char row,
                           unsigned char col,
                           unsigned int value) {
    unsigned int place;
    unsigned char digit;
    unsigned char started;

    place = 10000u;
    started = 0u;
    while ((place != 0u) && (col < 16u)) {
        digit = 0u;
        while (value >= place) {
            value = (unsigned int)(value - place);
            ++digit;
        }
        if ((started != 0u) || (digit != 0u) || (place == 1u)) {
            text_put(row, col, (unsigned char)('0' + digit));
            ++col;
            started = 1u;
        }

        if (place == 10000u) {
            place = 1000u;
        } else if (place == 1000u) {
            place = 100u;
        } else if (place == 100u) {
            place = 10u;
        } else if (place == 10u) {
            place = 1u;
        } else {
            place = 0u;
        }
    }
}

static void text_write_i16(unsigned char row, unsigned char col, int value) {
    if (value < 0) {
        text_put(row, col, '-');
        ++col;
        value = -value;
    }
    text_write_u16(row, col, (unsigned int)value);
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

static unsigned char store_read(unsigned char offset) {
    return OS_STORE[offset];
}

static void store_write(unsigned char offset, unsigned char value) {
    OS_STORE[offset] = value;
}

static unsigned int store_read_u16(unsigned char offset) {
    unsigned int value;

    value = store_read(offset);
    value |= ((unsigned int)store_read((unsigned char)(offset + 1u))) << 8;
    return value;
}

static void store_write_u16(unsigned char offset, unsigned int value) {
    store_write(offset, (unsigned char)(value & 0xffu));
    store_write((unsigned char)(offset + 1u), (unsigned char)(value >> 8));
}

static int store_read_i16(unsigned char offset) {
    return (int)store_read_u16(offset);
}

static void store_write_i16(unsigned char offset, int value) {
    store_write_u16(offset, (unsigned int)value);
}

static void set_magic(void) {
    store_write(0u, OS_MAGIC0);
    store_write(1u, OS_MAGIC1);
    store_write(2u, OS_MAGIC2);
    store_write(3u, OS_MAGIC3);
}

static void reset_counter_state(void) {
    store_write_i16(STORE_COUNTER_VALUE, 0);
    store_write_u16(STORE_COUNTER_RUNS, 0u);
}

static void reset_math_state(void) {
    store_write(STORE_MATH_INDEX, 0u);
    store_write(STORE_MATH_CORRECT, 0u);
    store_write_i16(STORE_MATH_ANSWER, 0);
    store_write(STORE_MATH_ATTEMPTS, 0u);
    store_write(STORE_MATH_LAST, 0u);
}

static void reset_pad_state(void) {
    unsigned char row;

    store_write(STORE_PAD_X, 0u);
    store_write(STORE_PAD_Y, 0u);
    for (row = 0u; row < PAD_HEIGHT; ++row) {
        store_write_u16((unsigned char)(STORE_PAD_ROWS + (row << 1)), 0u);
    }
}

static void reset_program_state(unsigned char program) {
    if (program == PROGRAM_COUNTER) {
        reset_counter_state();
    } else if (program == PROGRAM_MATH) {
        reset_math_state();
    } else {
        reset_pad_state();
    }
}

static void storage_init(void) {
    unsigned char selected;

    if ((store_read(0u) != OS_MAGIC0) ||
        (store_read(1u) != OS_MAGIC1) ||
        (store_read(2u) != OS_MAGIC2) ||
        (store_read(3u) != OS_MAGIC3)) {
        set_magic();
        store_write(STORE_SELECTED, 0u);
        reset_counter_state();
        reset_math_state();
        reset_pad_state();
        return;
    }

    selected = store_read(STORE_SELECTED);
    if (selected >= PROGRAM_COUNT) {
        store_write(STORE_SELECTED, 0u);
    }
    if (store_read(STORE_MATH_INDEX) >= MATH_COUNT) {
        store_write(STORE_MATH_INDEX, 0u);
    }
    if (store_read(STORE_PAD_X) >= PAD_WIDTH) {
        store_write(STORE_PAD_X, 0u);
    }
    if (store_read(STORE_PAD_Y) >= PAD_HEIGHT) {
        store_write(STORE_PAD_Y, 0u);
    }
}

static const char *program_name(unsigned char program) {
    if (program == PROGRAM_COUNTER) {
        return "COUNTER";
    }
    if (program == PROGRAM_MATH) {
        return "MATH DRILL";
    }
    return "PIXEL PAD";
}

static void clear_screen(void) {
    glic_prepare_screen(GLIC_BLACK);
}

static void draw_title(const char *title) {
    text_clear_row(0u);
    text_write(0u, 0u, "MINIOS ");
    text_write(0u, 7u, title);
}

static int math_answer_for(unsigned char index) {
    if (math_op[index] == '-') {
        return (int)(math_left[index] - math_right[index]);
    }
    return (int)(math_left[index] + math_right[index]);
}

static unsigned char pad_row_offset(unsigned char y) {
    return (unsigned char)(STORE_PAD_ROWS + (y << 1));
}

static unsigned int pad_row(unsigned char y) {
    return store_read_u16(pad_row_offset(y));
}

static void pad_write_row(unsigned char y, unsigned int value) {
    store_write_u16(pad_row_offset(y), value);
}

static unsigned char pad_get(unsigned char x, unsigned char y) {
    unsigned int mask;

    mask = (unsigned int)(1u << x);
    return (pad_row(y) & mask) != 0u;
}

static void pad_set(unsigned char x, unsigned char y, unsigned char on) {
    unsigned int row;
    unsigned int mask;

    row = pad_row(y);
    mask = (unsigned int)(1u << x);
    if (on != 0u) {
        row |= mask;
    } else {
        row &= (unsigned int)~mask;
    }
    pad_write_row(y, row);
}

static unsigned char pad_count_pixels(void) {
    unsigned char row;
    unsigned char col;
    unsigned char total;

    total = 0u;
    for (row = 0u; row < PAD_HEIGHT; ++row) {
        for (col = 0u; col < PAD_WIDTH; ++col) {
            if (pad_get(col, row) != 0u) {
                ++total;
            }
        }
    }
    return total;
}

static void draw_state_summary(unsigned char selected) {
    text_clear_row(12u);
    text_clear_row(13u);
    text_clear_row(14u);

    text_write(12u, 0u, "SAVED STATE");
    if (selected == PROGRAM_COUNTER) {
        text_write(13u, 0u, "VALUE ");
        text_write_i16(13u, 6u, store_read_i16(STORE_COUNTER_VALUE));
        text_write(14u, 0u, "RUNS ");
        text_write_u16(14u, 5u, store_read_u16(STORE_COUNTER_RUNS));
    } else if (selected == PROGRAM_MATH) {
        text_write(13u, 0u, "PROBLEM ");
        text_write_u16(13u, 8u, (unsigned int)(store_read(STORE_MATH_INDEX) + 1u));
        text_write(14u, 0u, "SCORE ");
        text_write_u16(14u, 6u, store_read(STORE_MATH_CORRECT));
    } else {
        text_write(13u, 0u, "PIXELS ");
        text_write_u16(13u, 7u, pad_count_pixels());
        text_write(14u, 0u, "CUR ");
        text_write_u16(14u, 4u, store_read(STORE_PAD_X));
        text_put(14u, 6u, ',');
        text_write_u16(14u, 7u, store_read(STORE_PAD_Y));
    }
}

static void draw_menu(unsigned char selected, const char *message) {
    unsigned char i;
    unsigned char row;

    clear_screen();
    draw_title("MENU");
    text_write(1u, 0u, "C PROGRAM SLOTS");
    for (i = 0u; i < PROGRAM_COUNT; ++i) {
        row = (unsigned char)(3u + (i << 1));
        text_put(row, 0u, (i == selected) ? '>' : ' ');
        text_write(row, 2u, program_name(i));
    }

    text_write(9u, 0u, "CENTER/A RUN");
    text_write(10u, 0u, "B RESET SLOT");
    text_write(11u, 0u, "C HELP");
    draw_state_summary(selected);

    if (message != 0) {
        text_clear_row(15u);
        text_write(15u, 0u, message);
    }
}

static void show_help(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    clear_screen();
    draw_title("HELP");
    text_write(2u, 0u, "APPS ARE C CODE");
    text_write(4u, 0u, "STATE AT 0x6000");
    text_write(6u, 0u, "C EXITS APP");
    text_write(8u, 0u, "EXIT SAVES SLOT");
    text_write(10u, 0u, "B RESETS SLOT");
    text_write(13u, 0u, "CENTER/C BACK");

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

static void draw_counter(void) {
    clear_screen();
    draw_title("COUNTER");
    text_write(3u, 0u, "VALUE");
    text_write_i16(4u, 0u, store_read_i16(STORE_COUNTER_VALUE));
    text_write(6u, 0u, "RUNS");
    text_write_u16(7u, 0u, store_read_u16(STORE_COUNTER_RUNS));
    text_write(10u, 0u, "UP/A +1");
    text_write(11u, 0u, "DOWN/B -1");
    text_write(12u, 0u, "CENTER +10");
    text_write(14u, 0u, "C SAVE EXIT");
}

static void run_counter(void) {
    unsigned char buttons;
    unsigned char last_buttons;
    int value;
    unsigned int runs;

    runs = (unsigned int)(store_read_u16(STORE_COUNTER_RUNS) + 1u);
    store_write_u16(STORE_COUNTER_RUNS, runs);
    draw_counter();
    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        value = store_read_i16(STORE_COUNTER_VALUE);
        if ((button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u)) {
            ++value;
            store_write_i16(STORE_COUNTER_VALUE, value);
            draw_counter();
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) ||
                   (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u)) {
            --value;
            store_write_i16(STORE_COUNTER_VALUE, value);
            draw_counter();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            value = (int)(value + 10);
            store_write_i16(STORE_COUNTER_VALUE, value);
            draw_counter();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            store_write_i16(STORE_COUNTER_VALUE, value);
            wait_for_release();
            return;
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void draw_math(void) {
    unsigned char index;
    unsigned char last;

    clear_screen();
    draw_title("MATH");
    index = store_read(STORE_MATH_INDEX);
    if (index >= MATH_COUNT) {
        index = 0u;
        store_write(STORE_MATH_INDEX, 0u);
    }

    text_write(2u, 0u, "PROBLEM ");
    text_write_u16(2u, 8u, (unsigned int)(index + 1u));
    text_put(2u, 9u, '/');
    text_write_u16(2u, 10u, MATH_COUNT);

    text_write_i16(4u, 0u, (int)math_left[index]);
    text_put(4u, 3u, math_op[index]);
    text_write_i16(4u, 5u, (int)math_right[index]);
    text_write(4u, 8u, "= ?");

    text_write(6u, 0u, "ANSWER ");
    text_write_i16(6u, 7u, store_read_i16(STORE_MATH_ANSWER));
    text_write(8u, 0u, "SCORE ");
    text_write_u16(8u, 6u, store_read(STORE_MATH_CORRECT));
    text_write(9u, 0u, "TRIES ");
    text_write_u16(9u, 6u, store_read(STORE_MATH_ATTEMPTS));

    text_write(11u, 0u, "UP/DN +/-1");
    text_write(12u, 0u, "A/B +/-10");
    text_write(13u, 0u, "CENTER CHECK");
    text_write(14u, 0u, "C SAVE EXIT");

    last = store_read(STORE_MATH_LAST);
    if (last == 1u) {
        text_write(15u, 0u, "CORRECT SAVED");
    } else if (last == 2u) {
        text_write(15u, 0u, "TRY AGAIN");
    }
}

static void math_advance_problem(void) {
    unsigned char index;

    index = (unsigned char)(store_read(STORE_MATH_INDEX) + 1u);
    if (index >= MATH_COUNT) {
        index = 0u;
    }
    store_write(STORE_MATH_INDEX, index);
    store_write_i16(STORE_MATH_ANSWER, 0);
}

static void run_math(void) {
    unsigned char buttons;
    unsigned char last_buttons;
    unsigned char attempts;
    unsigned char correct;
    unsigned char index;
    int answer;

    draw_math();
    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        answer = store_read_i16(STORE_MATH_ANSWER);
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            ++answer;
            store_write_i16(STORE_MATH_ANSWER, answer);
            store_write(STORE_MATH_LAST, 0u);
            draw_math();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            --answer;
            store_write_i16(STORE_MATH_ANSWER, answer);
            store_write(STORE_MATH_LAST, 0u);
            draw_math();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            answer = (int)(answer + 10);
            store_write_i16(STORE_MATH_ANSWER, answer);
            store_write(STORE_MATH_LAST, 0u);
            draw_math();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            answer = (int)(answer - 10);
            store_write_i16(STORE_MATH_ANSWER, answer);
            store_write(STORE_MATH_LAST, 0u);
            draw_math();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            attempts = (unsigned char)(store_read(STORE_MATH_ATTEMPTS) + 1u);
            store_write(STORE_MATH_ATTEMPTS, attempts);
            index = store_read(STORE_MATH_INDEX);
            if (answer == math_answer_for(index)) {
                correct = (unsigned char)(store_read(STORE_MATH_CORRECT) + 1u);
                store_write(STORE_MATH_CORRECT, correct);
                store_write(STORE_MATH_LAST, 1u);
                math_advance_problem();
            } else {
                store_write(STORE_MATH_LAST, 2u);
            }
            draw_math();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            store_write_i16(STORE_MATH_ANSWER, answer);
            wait_for_release();
            return;
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void draw_pad(void) {
    unsigned char row;
    unsigned char col;
    unsigned char cursor_x;
    unsigned char cursor_y;
    unsigned char on;
    unsigned char ch;

    clear_screen();
    draw_title("PAD");
    cursor_x = store_read(STORE_PAD_X);
    cursor_y = store_read(STORE_PAD_Y);
    if (cursor_x >= PAD_WIDTH) {
        cursor_x = 0u;
    }
    if (cursor_y >= PAD_HEIGHT) {
        cursor_y = 0u;
    }

    for (row = 0u; row < PAD_HEIGHT; ++row) {
        for (col = 0u; col < PAD_WIDTH; ++col) {
            on = pad_get(col, row);
            ch = on ? '#' : '.';
            if ((col == cursor_x) && (row == cursor_y)) {
                ch = on ? '@' : 'O';
            }
            text_put((unsigned char)(2u + row), col, ch);
        }
    }

    text_write(11u, 0u, "CENTER TOGGLE");
    text_write(12u, 0u, "A CLEAR B INVERT");
    text_write(13u, 0u, "ARROWS MOVE");
    text_write(14u, 0u, "C SAVE EXIT");
    text_write(15u, 0u, "PIXELS ");
    text_write_u16(15u, 7u, pad_count_pixels());
}

static void pad_invert(void) {
    unsigned char row;
    unsigned int value;

    for (row = 0u; row < PAD_HEIGHT; ++row) {
        value = (unsigned int)(pad_row(row) ^ 0xffffu);
        pad_write_row(row, value);
    }
}

static void run_pad(void) {
    unsigned char buttons;
    unsigned char last_buttons;
    unsigned char x;
    unsigned char y;

    draw_pad();
    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        x = store_read(STORE_PAD_X);
        y = store_read(STORE_PAD_Y);

        if ((button_edge(buttons, last_buttons, GLIC_BTN_LEFT) != 0u) &&
            (x != 0u)) {
            --x;
            store_write(STORE_PAD_X, x);
            draw_pad();
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_RIGHT) != 0u) &&
                   (x < (PAD_WIDTH - 1u))) {
            ++x;
            store_write(STORE_PAD_X, x);
            draw_pad();
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) &&
                   (y != 0u)) {
            --y;
            store_write(STORE_PAD_Y, y);
            draw_pad();
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) &&
                   (y < (PAD_HEIGHT - 1u))) {
            ++y;
            store_write(STORE_PAD_Y, y);
            draw_pad();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            pad_set(x, y, (unsigned char)(pad_get(x, y) == 0u));
            draw_pad();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            reset_pad_state();
            draw_pad();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            pad_invert();
            draw_pad();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            store_write(STORE_PAD_X, x);
            store_write(STORE_PAD_Y, y);
            wait_for_release();
            return;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void run_program(unsigned char program) {
    if (program == PROGRAM_COUNTER) {
        run_counter();
    } else if (program == PROGRAM_MATH) {
        run_math();
    } else {
        run_pad();
    }
}

void main(void) {
    unsigned char selected;
    unsigned char buttons;
    unsigned char last_buttons;
    const char *message;

    storage_init();
    selected = store_read(STORE_SELECTED);
    if (selected >= PROGRAM_COUNT) {
        selected = 0u;
    }

    message = 0;
    draw_menu(selected, message);
    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if ((button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) &&
            (selected != 0u)) {
            --selected;
            store_write(STORE_SELECTED, selected);
            message = 0;
            draw_menu(selected, message);
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) &&
                   (selected < (PROGRAM_COUNT - 1u))) {
            ++selected;
            store_write(STORE_SELECTED, selected);
            message = 0;
            draw_menu(selected, message);
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) ||
                   (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u)) {
            wait_for_release();
            run_program(selected);
            message = "STATE SAVED";
            draw_menu(selected, message);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            reset_program_state(selected);
            message = "SLOT RESET";
            draw_menu(selected, message);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            show_help();
            message = 0;
            draw_menu(selected, message);
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}
