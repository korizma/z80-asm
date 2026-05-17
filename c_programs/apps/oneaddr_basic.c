#include "../common/glic80.h"

#define STORE_BASE ((volatile unsigned char *)0x6000)
#define STORE_MAGIC0 0x4fu
#define STORE_MAGIC1 0x41u
#define STORE_MAGIC2 0x42u
#define STORE_MAGIC3 0x31u
#define STORE_LEN_LO 4u
#define STORE_LEN_HI 5u
#define PROGRAM_TEXT (STORE_BASE + 6u)
#define PROGRAM_CAPACITY 512u

#define KEY_ROWS 6u
#define KEY_COLS 8u
#define KEY_TOP 9u
#define EDIT_TEXT_FIRST_ROW 1u
#define EDIT_TEXT_ROWS 7u
#define EDIT_VIEW_COLS 15u
#define EDIT_VISIBLE_CHARS (EDIT_TEXT_ROWS * EDIT_VIEW_COLS)
#define LOOP_DELAY 900u
#define MAX_LOOP_STACK 4u
#define INSTRUCTION_COUNT 12u

#define ACTION_EDIT 1u
#define ACTION_RUN 2u

typedef struct {
    unsigned char var;
    int limit;
    int step;
    unsigned int body_pc;
} LoopFrame;

static const unsigned char keyboard_keys[KEY_ROWS * KEY_COLS] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '0', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '_', '=', '+', '-',
    '*', '/', '<', '>', '(', ')', '"', '\n'
};

static const char demo_source[] =
    "PRINT \"ONEADDR\"\n"
    "LET A=0\n"
    "FOR I=1 TO 5\n"
    "LET A=A+I\n"
    "PRINT A\n"
    "NEXT\n"
    "IF A=15 THEN PRINT \"OK\"\n"
    "END\n";

static int variables[26];
static LoopFrame loop_stack[MAX_LOOP_STACK];
static unsigned char loop_depth;
static unsigned int run_pc;
static unsigned int run_len;
static unsigned char run_error;
static unsigned char run_stop;
static unsigned char run_break;
static unsigned char run_done;
static unsigned char output_row;
static unsigned char output_col;
static unsigned int editor_view_start;

static void run_program(void);

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

static void text_write_u16_3(unsigned char row,
                             unsigned char col,
                             unsigned int value) {
    unsigned char hundreds;
    unsigned char tens;

    if (value > 999u) {
        value = 999u;
    }

    hundreds = 0u;
    while (value >= 100u) {
        value = (unsigned int)(value - 100u);
        ++hundreds;
    }
    tens = 0u;
    while (value >= 10u) {
        value = (unsigned int)(value - 10u);
        ++tens;
    }

    text_put(row, col, (unsigned char)('0' + hundreds));
    text_put(row, (unsigned char)(col + 1u), (unsigned char)('0' + tens));
    text_put(row, (unsigned char)(col + 2u), (unsigned char)('0' + value));
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

static unsigned int program_len(void) {
    unsigned int len;

    len = STORE_BASE[STORE_LEN_LO];
    len |= ((unsigned int)STORE_BASE[STORE_LEN_HI]) << 8;
    if (len > PROGRAM_CAPACITY) {
        return 0u;
    }
    return len;
}

static void set_program_len(unsigned int len) {
    if (len > PROGRAM_CAPACITY) {
        len = PROGRAM_CAPACITY;
    }
    STORE_BASE[STORE_LEN_LO] = (unsigned char)(len & 0xffu);
    STORE_BASE[STORE_LEN_HI] = (unsigned char)(len >> 8);
}

static void mark_storage_valid(void) {
    STORE_BASE[0] = STORE_MAGIC0;
    STORE_BASE[1] = STORE_MAGIC1;
    STORE_BASE[2] = STORE_MAGIC2;
    STORE_BASE[3] = STORE_MAGIC3;
}

static void load_demo_program(void) {
    const char *src;
    unsigned int len;

    mark_storage_valid();
    src = demo_source;
    len = 0u;
    while ((*src != 0) && (len < PROGRAM_CAPACITY)) {
        PROGRAM_TEXT[len] = (unsigned char)*src;
        ++src;
        ++len;
    }
    set_program_len(len);
    if (len < PROGRAM_CAPACITY) {
        PROGRAM_TEXT[len] = 0u;
    }
}

static void storage_init(void) {
    unsigned int len;

    if ((STORE_BASE[0] != STORE_MAGIC0) ||
        (STORE_BASE[1] != STORE_MAGIC1) ||
        (STORE_BASE[2] != STORE_MAGIC2) ||
        (STORE_BASE[3] != STORE_MAGIC3)) {
        load_demo_program();
        return;
    }

    len = program_len();
    if (len > PROGRAM_CAPACITY) {
        load_demo_program();
        return;
    }
    if (len < PROGRAM_CAPACITY) {
        PROGRAM_TEXT[len] = 0u;
    }
}

static unsigned char key_at(unsigned char row, unsigned char col) {
    return keyboard_keys[((unsigned char)(row << 3)) + col];
}

static unsigned char typed_key(unsigned char row, unsigned char col) {
    unsigned char ch;

    ch = key_at(row, col);
    if (ch == '_') {
        return ' ';
    }
    return ch;
}

static void key_label(unsigned char key,
                      unsigned char *first,
                      unsigned char *second) {
    if (key == '_') {
        *first = 'S';
        *second = 'P';
        return;
    }
    if (key == '\n') {
        *first = 'N';
        *second = 'L';
        return;
    }
    *first = key;
    *second = ' ';
}

static void draw_key_label(unsigned char row,
                           unsigned char col,
                           unsigned char key,
                           unsigned char selected) {
    unsigned char first;
    unsigned char second;

    key_label(key, &first, &second);
    text_put(row, col, first);
    text_put(row, (unsigned char)(col + 1u),
             (selected != 0u) ? '<' : second);
}

static void draw_keyboard(unsigned char key_row, unsigned char key_col) {
    unsigned char row;
    unsigned char col;
    unsigned char screen_col;

    for (row = 0u; row < KEY_ROWS; ++row) {
        text_clear_row((unsigned char)(KEY_TOP + row));
        for (col = 0u; col < KEY_COLS; ++col) {
            screen_col = (unsigned char)(col << 1);
            draw_key_label((unsigned char)(KEY_TOP + row),
                           screen_col,
                           key_at(row, col),
                           ((row == key_row) && (col == key_col)) ? 1u : 0u);
        }
    }
}

static unsigned char source_starts_new_row(unsigned int pos,
                                           unsigned int len,
                                           unsigned char ch,
                                           unsigned char *col) {
    if (ch == '\n') {
        *col = 0u;
        return 1u;
    }

    ++(*col);
    if ((*col >= EDIT_VIEW_COLS) && (pos < len)) {
        *col = 0u;
        return 1u;
    }
    return 0u;
}

static unsigned int source_max_window_start(unsigned int len) {
    unsigned int row_starts[8];
    unsigned int pos;
    unsigned int row_no;
    unsigned int target_row;
    unsigned char slot;
    unsigned char col;
    unsigned char ch;

    row_starts[0] = 0u;
    pos = 0u;
    row_no = 0u;
    col = 0u;

    while (pos < len) {
        ch = PROGRAM_TEXT[pos];
        ++pos;
        if (source_starts_new_row(pos, len, ch, &col) != 0u) {
            ++row_no;
            slot = (unsigned char)(row_no & 7u);
            row_starts[slot] = pos;
        }
    }

    if ((unsigned int)(row_no + 1u) <= EDIT_TEXT_ROWS) {
        return 0u;
    }

    target_row = (unsigned int)(row_no + 1u - EDIT_TEXT_ROWS);
    slot = (unsigned char)(target_row & 7u);
    return row_starts[slot];
}

static unsigned int source_prev_window_start(unsigned int start) {
    unsigned int len;
    unsigned int pos;
    unsigned int prev;
    unsigned char col;
    unsigned char ch;

    len = program_len();
    pos = 0u;
    prev = 0u;
    col = 0u;

    while ((pos < len) && (pos < start)) {
        ch = PROGRAM_TEXT[pos];
        ++pos;
        if (source_starts_new_row(pos, len, ch, &col) != 0u) {
            if (pos >= start) {
                break;
            }
            prev = pos;
        }
    }
    return prev;
}

static unsigned int source_next_window_start(unsigned int start) {
    unsigned int len;
    unsigned int max_start;
    unsigned int pos;
    unsigned char col;
    unsigned char ch;

    len = program_len();
    max_start = source_max_window_start(len);
    pos = 0u;
    col = 0u;

    while (pos < len) {
        ch = PROGRAM_TEXT[pos];
        ++pos;
        if (source_starts_new_row(pos, len, ch, &col) != 0u) {
            if (pos > start) {
                if (pos > max_start) {
                    return max_start;
                }
                return pos;
            }
        }
    }
    return max_start;
}

static void clamp_editor_view(void) {
    unsigned int max_start;

    max_start = source_max_window_start(program_len());
    if (editor_view_start > max_start) {
        editor_view_start = max_start;
    }
}

static void editor_view_to_end(void) {
    editor_view_start = source_max_window_start(program_len());
}

static void editor_scroll_up(void) {
    editor_view_start = source_prev_window_start(editor_view_start);
}

static void editor_scroll_down(void) {
    unsigned int max_start;

    max_start = source_max_window_start(program_len());
    if (editor_view_start < max_start) {
        editor_view_start = source_next_window_start(editor_view_start);
    }
}

static void draw_editor_scrollbar(unsigned int len) {
    unsigned int max_start;
    unsigned int scaled;
    unsigned char row;
    unsigned char i;
    unsigned char knob_index;
    unsigned char knob_row;

    max_start = source_max_window_start(len);
    for (row = 0u; row < EDIT_TEXT_ROWS; ++row) {
        text_put((unsigned char)(EDIT_TEXT_FIRST_ROW + row), 15u, '.');
    }

    if (max_start == 0u) {
        for (row = 0u; row < EDIT_TEXT_ROWS; ++row) {
            text_put((unsigned char)(EDIT_TEXT_FIRST_ROW + row), 15u, '#');
        }
        return;
    }

    if (editor_view_start == 0u) {
        knob_row = EDIT_TEXT_FIRST_ROW;
    } else if (editor_view_start >= max_start) {
        knob_row = (unsigned char)(EDIT_TEXT_FIRST_ROW + EDIT_TEXT_ROWS - 1u);
    } else {
        scaled = 0u;
        for (i = 0u; i < (unsigned char)(EDIT_TEXT_ROWS - 1u); ++i) {
            scaled = (unsigned int)(scaled + editor_view_start);
        }
        knob_index = 0u;
        while ((scaled >= max_start) &&
               (knob_index < (unsigned char)(EDIT_TEXT_ROWS - 1u))) {
            scaled = (unsigned int)(scaled - max_start);
            ++knob_index;
        }
        knob_row = (unsigned char)(EDIT_TEXT_FIRST_ROW + knob_index);
    }
    text_put(knob_row, 15u, '#');
}

static void draw_source_preview(void) {
    unsigned int len;
    unsigned int pos;
    unsigned char row;
    unsigned char col;
    unsigned char ch;

    for (row = 0u; row < EDIT_TEXT_ROWS; ++row) {
        text_clear_row((unsigned char)(EDIT_TEXT_FIRST_ROW + row));
    }

    len = program_len();
    clamp_editor_view();
    if (len == 0u) {
        text_write(4u, 5u, "EMPTY");
        draw_editor_scrollbar(len);
        return;
    }

    pos = editor_view_start;
    row = EDIT_TEXT_FIRST_ROW;
    col = 0u;

    while ((pos < len) &&
           (row < (unsigned char)(EDIT_TEXT_FIRST_ROW + EDIT_TEXT_ROWS))) {
        ch = PROGRAM_TEXT[pos];
        ++pos;

        if (ch == '\n') {
            if (col < EDIT_VIEW_COLS) {
                text_put(row, col, '\\');
            }
            ++row;
            col = 0u;
            continue;
        }
        if ((ch < 32u) || (ch > 126u)) {
            ch = '?';
        }

        text_put(row, col, ch);
        ++col;
        if (col >= EDIT_VIEW_COLS) {
            col = 0u;
            ++row;
        }
    }
    draw_editor_scrollbar(len);
}

static void draw_editor_header(unsigned char key_row,
                               unsigned char key_col,
                               const char *status) {
    unsigned int len;

    len = program_len();
    text_clear_row(0u);
    text_write_u16_3(0u, 0u, len);
    text_write(0u, 3u, "/512 KEY ");
    draw_key_label(0u, 12u, key_at(key_row, key_col), 0u);
    text_write(0u, 14u, status);
}

static void draw_editor_key(unsigned char key_row,
                            unsigned char key_col,
                            unsigned char selected) {
    draw_key_label((unsigned char)(KEY_TOP + key_row),
                   (unsigned char)(key_col << 1),
                   key_at(key_row, key_col),
                   selected);
}

static void move_editor_key(unsigned char old_row,
                            unsigned char old_col,
                            unsigned char new_row,
                            unsigned char new_col) {
    if ((old_row == new_row) && (old_col == new_col)) {
        return;
    }
    draw_editor_key(old_row, old_col, 0u);
    draw_editor_key(new_row, new_col, 1u);
}

static void draw_editor_shell(unsigned char key_row,
                              unsigned char key_col,
                              const char *status) {
    glic_prepare_screen(GLIC_BLACK);
    draw_editor_header(key_row, key_col, status);
    draw_source_preview();
    text_write(8u, 0u, "B DEL C MENU");
    draw_keyboard(key_row, key_col);
    text_write(15u, 0u, "AA RUN A UD SCR");
}

static void insert_char(unsigned char key_row, unsigned char key_col) {
    unsigned int len;

    len = program_len();
    if (len >= PROGRAM_CAPACITY) {
        return;
    }

    PROGRAM_TEXT[len] = typed_key(key_row, key_col);
    ++len;
    set_program_len(len);
    if (len < PROGRAM_CAPACITY) {
        PROGRAM_TEXT[len] = 0u;
    }
}

static void delete_char(void) {
    unsigned int len;

    len = program_len();
    if (len == 0u) {
        return;
    }
    --len;
    set_program_len(len);
    PROGRAM_TEXT[len] = 0u;
}

static void run_editor(void) {
    unsigned char buttons;
    unsigned char last_buttons;
    unsigned char key_row;
    unsigned char key_col;
    unsigned char old_row;
    unsigned char old_col;
    unsigned char a_prefix;
    const char *status;

    key_row = 0u;
    key_col = 0u;
    a_prefix = 0u;
    editor_view_to_end();
    status = "";
    draw_editor_shell(key_row, key_col, status);
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            if (a_prefix != 0u) {
                editor_scroll_up();
                a_prefix = 0u;
                status = "SC";
                draw_editor_header(key_row, key_col, status);
                draw_source_preview();
            } else {
                old_row = key_row;
                old_col = key_col;
                if (key_row == 0u) {
                    key_row = (unsigned char)(KEY_ROWS - 1u);
                } else {
                    --key_row;
                }
                status = "";
                draw_editor_header(key_row, key_col, status);
                move_editor_key(old_row, old_col, key_row, key_col);
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            if (a_prefix != 0u) {
                editor_scroll_down();
                a_prefix = 0u;
                status = "SC";
                draw_editor_header(key_row, key_col, status);
                draw_source_preview();
            } else {
                old_row = key_row;
                old_col = key_col;
                ++key_row;
                if (key_row >= KEY_ROWS) {
                    key_row = 0u;
                }
                status = "";
                draw_editor_header(key_row, key_col, status);
                move_editor_key(old_row, old_col, key_row, key_col);
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_LEFT) != 0u) {
            a_prefix = 0u;
            old_row = key_row;
            old_col = key_col;
            if (key_col == 0u) {
                key_col = (unsigned char)(KEY_COLS - 1u);
            } else {
                --key_col;
            }
            status = "";
            draw_editor_header(key_row, key_col, status);
            move_editor_key(old_row, old_col, key_row, key_col);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_RIGHT) != 0u) {
            a_prefix = 0u;
            old_row = key_row;
            old_col = key_col;
            ++key_col;
            if (key_col >= KEY_COLS) {
                key_col = 0u;
            }
            status = "";
            draw_editor_header(key_row, key_col, status);
            move_editor_key(old_row, old_col, key_row, key_col);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            a_prefix = 0u;
            if (program_len() < PROGRAM_CAPACITY) {
                insert_char(key_row, key_col);
                editor_view_to_end();
                status = "";
            } else {
                status = "FL";
            }
            draw_editor_header(key_row, key_col, status);
            draw_source_preview();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            a_prefix = 0u;
            delete_char();
            editor_view_to_end();
            status = "";
            draw_editor_header(key_row, key_col, status);
            draw_source_preview();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            if (a_prefix != 0u) {
                wait_for_release();
                a_prefix = 0u;
                run_program();
                status = "";
                draw_editor_shell(key_row, key_col, status);
                last_buttons = GLIC_BUTTONS_NONE;
            } else {
                a_prefix = 1u;
                status = "A?";
                draw_editor_header(key_row, key_col, status);
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            wait_for_release();
            return;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void copy_text_row(unsigned char from_row, unsigned char to_row) {
    unsigned char col;

    for (col = 0u; col < 16u; ++col) {
        text_put(to_row, col, *text_cell(from_row, col));
    }
}

static void output_scroll(void) {
    unsigned char row;

    for (row = 1u; row < 14u; ++row) {
        copy_text_row((unsigned char)(row + 1u), row);
    }
    text_clear_row(14u);
    output_row = 14u;
    output_col = 0u;
}

static void output_newline(void) {
    output_col = 0u;
    if (output_row < 14u) {
        ++output_row;
    } else {
        output_scroll();
    }
}

static void output_char(unsigned char ch) {
    if (ch == '\n') {
        output_newline();
        return;
    }
    if ((ch < 32u) || (ch > 126u)) {
        ch = ' ';
    }
    if (output_col >= 16u) {
        output_newline();
    }
    text_put(output_row, output_col, ch);
    ++output_col;
}

static void output_text(const char *text) {
    while (*text != 0) {
        output_char((unsigned char)*text);
        ++text;
    }
}

static unsigned int abs_to_unsigned(int value) {
    if (value < 0) {
        return (unsigned int)(0u - (unsigned int)value);
    }
    return (unsigned int)value;
}

static int math_mul(int left, int right) {
    unsigned int a;
    unsigned int b;
    unsigned int result;
    unsigned char negative;

    negative = 0u;
    if (left < 0) {
        negative = 1u;
        a = (unsigned int)(0u - (unsigned int)left);
    } else {
        a = (unsigned int)left;
    }
    if (right < 0) {
        negative ^= 1u;
        b = (unsigned int)(0u - (unsigned int)right);
    } else {
        b = (unsigned int)right;
    }

    result = 0u;
    while (b != 0u) {
        if ((b & 1u) != 0u) {
            result = (unsigned int)(result + a);
        }
        a = (unsigned int)(a << 1);
        b = (unsigned int)(b >> 1);
    }

    if (negative != 0u) {
        return (int)(0u - result);
    }
    return (int)result;
}

static unsigned int math_udiv(unsigned int numerator,
                              unsigned int denominator) {
    unsigned int quotient;
    unsigned int shifted;
    unsigned int bit;
    unsigned int next_shifted;

    if (denominator == 0u) {
        return 0u;
    }

    quotient = 0u;
    shifted = denominator;
    bit = 1u;

    while (shifted <= numerator) {
        next_shifted = (unsigned int)(shifted << 1);
        if ((next_shifted <= shifted) || (next_shifted > numerator)) {
            break;
        }
        shifted = next_shifted;
        bit = (unsigned int)(bit << 1);
    }

    while (bit != 0u) {
        if (numerator >= shifted) {
            numerator = (unsigned int)(numerator - shifted);
            quotient = (unsigned int)(quotient | bit);
        }
        shifted = (unsigned int)(shifted >> 1);
        bit = (unsigned int)(bit >> 1);
    }

    return quotient;
}

static int math_div(int left, int right) {
    unsigned int quotient;
    unsigned char negative;

    negative = 0u;
    if (left < 0) {
        negative = 1u;
    }
    if (right < 0) {
        negative ^= 1u;
    }

    quotient = math_udiv(abs_to_unsigned(left), abs_to_unsigned(right));
    if (negative != 0u) {
        return (int)(0u - quotient);
    }
    return (int)quotient;
}

static void output_number(int value) {
    unsigned int n;
    static const unsigned int places[5] = {
        10000u, 1000u, 100u, 10u, 1u
    };
    unsigned char i;
    unsigned char digit;
    unsigned char started;

    if (value < 0) {
        output_char('-');
        n = abs_to_unsigned(value);
    } else {
        n = (unsigned int)value;
    }

    started = 0u;
    for (i = 0u; i < 5u; ++i) {
        digit = 0u;
        while (n >= places[i]) {
            n = (unsigned int)(n - places[i]);
            ++digit;
        }
        if ((digit != 0u) || (started != 0u) || (i == 4u)) {
            output_char((unsigned char)('0' + digit));
            started = 1u;
        }
    }
}

static void output_clear(void) {
    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 0u, "RUN ONEADDR");
    text_write(15u, 0u, "C STOP");
    output_row = 1u;
    output_col = 0u;
}

static unsigned char read_buttons_value(void) {
    return (unsigned char)(~glic_read_buttons());
}

static unsigned char source_at(unsigned int pos) {
    if (pos >= run_len) {
        return 0u;
    }
    return PROGRAM_TEXT[pos];
}

static unsigned char upper_char(unsigned char ch) {
    if ((ch >= 'a') && (ch <= 'z')) {
        return (unsigned char)(ch - ('a' - 'A'));
    }
    return ch;
}

static unsigned char is_digit_char(unsigned char ch) {
    return ((ch >= '0') && (ch <= '9'));
}

static unsigned char is_name_char(unsigned char ch) {
    ch = upper_char(ch);
    return (((ch >= 'A') && (ch <= 'Z')) ||
            ((ch >= '0') && (ch <= '9')) ||
            (ch == '_'));
}

static void skip_spaces(unsigned int *pos) {
    unsigned char ch;

    while (*pos < run_len) {
        ch = source_at(*pos);
        if ((ch != ' ') && (ch != '\t') && (ch != '\r')) {
            return;
        }
        ++(*pos);
    }
}

static void skip_statement_gaps(unsigned int *pos) {
    unsigned char ch;

    while (*pos < run_len) {
        ch = source_at(*pos);
        if ((ch != ' ') && (ch != '\t') &&
            (ch != '\r') && (ch != '\n')) {
            return;
        }
        ++(*pos);
    }
}

static void skip_to_eol(unsigned int *pos) {
    unsigned char ch;

    while (*pos < run_len) {
        ch = source_at(*pos);
        if (ch == '\n') {
            return;
        }
        ++(*pos);
    }
}

static void skip_past_eol(unsigned int *pos) {
    skip_to_eol(pos);
    if (source_at(*pos) == '\n') {
        ++(*pos);
    }
}

static unsigned char keyword_at(unsigned int pos,
                                const char *keyword,
                                unsigned int *end_pos) {
    unsigned char ch;
    unsigned char want;

    skip_spaces(&pos);
    while (*keyword != 0) {
        ch = upper_char(source_at(pos));
        want = (unsigned char)*keyword;
        if (ch != want) {
            return 0u;
        }
        ++keyword;
        ++pos;
    }
    if (is_name_char(source_at(pos)) != 0u) {
        return 0u;
    }
    *end_pos = pos;
    return 1u;
}

static unsigned char match_keyword(unsigned int *pos, const char *keyword) {
    unsigned int end_pos;

    if (keyword_at(*pos, keyword, &end_pos) == 0u) {
        return 0u;
    }
    *pos = end_pos;
    return 1u;
}

static unsigned char match_char(unsigned int *pos, unsigned char want) {
    skip_spaces(pos);
    if (source_at(*pos) != want) {
        return 0u;
    }
    ++(*pos);
    return 1u;
}

static void fail_run(const char *message) {
    if (run_error == 0u) {
        output_newline();
        output_text("ERR ");
        output_text(message);
        run_error = 1u;
        run_stop = 1u;
    }
}

static unsigned char parse_variable(unsigned int *pos, unsigned char *var) {
    unsigned char ch;

    skip_spaces(pos);
    ch = upper_char(source_at(*pos));
    if ((ch < 'A') || (ch > 'Z')) {
        fail_run("VAR");
        return 0u;
    }
    *var = (unsigned char)(ch - 'A');
    ++(*pos);
    return 1u;
}

static int parse_expr(unsigned int *pos);

static int parse_number(unsigned int *pos) {
    int value;
    unsigned char ch;

    skip_spaces(pos);
    value = 0;
    ch = source_at(*pos);
    if (is_digit_char(ch) == 0u) {
        fail_run("NUMBER");
        return 0;
    }
    while (is_digit_char(ch) != 0u) {
        value = (int)(((int)(value << 3)) +
                      ((int)(value << 1)) +
                      (ch - '0'));
        ++(*pos);
        ch = source_at(*pos);
    }
    return value;
}

static int parse_factor(unsigned int *pos) {
    int value;
    unsigned char var;

    skip_spaces(pos);
    if (match_char(pos, '+') != 0u) {
        return parse_factor(pos);
    }
    if (match_char(pos, '-') != 0u) {
        return (int)(0 - parse_factor(pos));
    }
    if (match_char(pos, '(') != 0u) {
        value = parse_expr(pos);
        if (match_char(pos, ')') == 0u) {
            fail_run(")");
        }
        return value;
    }
    if (match_keyword(pos, "BTN") != 0u) {
        return (int)read_buttons_value();
    }
    if (is_digit_char(source_at(*pos)) != 0u) {
        return parse_number(pos);
    }
    if (parse_variable(pos, &var) == 0u) {
        return 0;
    }
    return variables[var];
}

static int parse_term(unsigned int *pos) {
    int value;
    int rhs;
    unsigned char op;

    value = parse_factor(pos);
    while (run_error == 0u) {
        skip_spaces(pos);
        op = source_at(*pos);
        if ((op != '*') && (op != '/')) {
            break;
        }
        ++(*pos);
        rhs = parse_factor(pos);
        if (op == '*') {
            value = math_mul(value, rhs);
        } else {
            if (rhs == 0) {
                fail_run("DIV0");
                return 0;
            }
            value = math_div(value, rhs);
        }
    }
    return value;
}

static int parse_sum(unsigned int *pos) {
    int value;
    int rhs;
    unsigned char op;

    value = parse_term(pos);
    while (run_error == 0u) {
        skip_spaces(pos);
        op = source_at(*pos);
        if ((op != '+') && (op != '-')) {
            break;
        }
        ++(*pos);
        rhs = parse_term(pos);
        if (op == '+') {
            value = (int)(value + rhs);
        } else {
            value = (int)(value - rhs);
        }
    }
    return value;
}

static int parse_expr(unsigned int *pos) {
    int left;
    int right;
    unsigned char op;

    left = parse_sum(pos);
    if (run_error != 0u) {
        return 0;
    }

    skip_spaces(pos);
    op = source_at(*pos);
    if ((op != '=') && (op != '<') && (op != '>')) {
        return left;
    }

    ++(*pos);
    if (op == '<') {
        if (source_at(*pos) == '=') {
            ++(*pos);
            right = parse_sum(pos);
            return (left <= right) ? 1 : 0;
        }
        if (source_at(*pos) == '>') {
            ++(*pos);
            right = parse_sum(pos);
            return (left != right) ? 1 : 0;
        }
        right = parse_sum(pos);
        return (left < right) ? 1 : 0;
    }
    if (op == '>') {
        if (source_at(*pos) == '=') {
            ++(*pos);
            right = parse_sum(pos);
            return (left >= right) ? 1 : 0;
        }
        right = parse_sum(pos);
        return (left > right) ? 1 : 0;
    }

    right = parse_sum(pos);
    return (left == right) ? 1 : 0;
}

static unsigned char loop_in_range(int value, int limit, int step) {
    if (step > 0) {
        return (value <= limit) ? 1u : 0u;
    }
    return (value >= limit) ? 1u : 0u;
}

static unsigned char find_matching_next(unsigned int start,
                                        unsigned int *after_next) {
    unsigned int pos;
    unsigned int check;
    unsigned char depth;

    pos = start;
    depth = 0u;
    while (pos < run_len) {
        skip_statement_gaps(&pos);
        if (pos >= run_len) {
            break;
        }

        check = pos;
        if (match_keyword(&check, "FOR") != 0u) {
            ++depth;
        } else {
            check = pos;
            if (match_keyword(&check, "NEXT") != 0u) {
                if (depth == 0u) {
                    skip_past_eol(&check);
                    *after_next = check;
                    return 1u;
                }
                --depth;
            }
        }
        skip_past_eol(&pos);
    }
    return 0u;
}

static void exec_statement(void);

static void exec_print(void) {
    int value;
    unsigned char ch;

    skip_spaces(&run_pc);
    if (source_at(run_pc) == '"') {
        ++run_pc;
        while ((run_pc < run_len) && (source_at(run_pc) != '"') &&
               (source_at(run_pc) != '\n')) {
            ch = source_at(run_pc);
            output_char(ch);
            ++run_pc;
        }
        if (source_at(run_pc) != '"') {
            fail_run("QUOTE");
            return;
        }
        ++run_pc;
    } else {
        value = parse_expr(&run_pc);
        if (run_error != 0u) {
            return;
        }
        output_number(value);
    }
    output_newline();
    skip_to_eol(&run_pc);
}

static void exec_assignment(unsigned char var) {
    int value;

    if (match_char(&run_pc, '=') == 0u) {
        fail_run("=");
        return;
    }
    value = parse_expr(&run_pc);
    if (run_error != 0u) {
        return;
    }
    variables[var] = value;
    skip_to_eol(&run_pc);
}

static void exec_let(void) {
    unsigned char var;

    if (parse_variable(&run_pc, &var) == 0u) {
        return;
    }
    exec_assignment(var);
}

static void exec_if(void) {
    int condition;

    condition = parse_expr(&run_pc);
    if (run_error != 0u) {
        return;
    }
    if (match_keyword(&run_pc, "THEN") == 0u) {
        fail_run("THEN");
        return;
    }
    if (condition != 0) {
        exec_statement();
    } else {
        skip_to_eol(&run_pc);
    }
}

static void exec_for(void) {
    unsigned char var;
    int start_value;
    int limit_value;
    int step_value;
    unsigned int body_pc;
    unsigned int after_next;

    if (parse_variable(&run_pc, &var) == 0u) {
        return;
    }
    if (match_char(&run_pc, '=') == 0u) {
        fail_run("=");
        return;
    }
    start_value = parse_expr(&run_pc);
    if (run_error != 0u) {
        return;
    }
    if (match_keyword(&run_pc, "TO") == 0u) {
        fail_run("TO");
        return;
    }
    limit_value = parse_expr(&run_pc);
    if (run_error != 0u) {
        return;
    }
    if (match_keyword(&run_pc, "STEP") != 0u) {
        step_value = parse_expr(&run_pc);
        if (run_error != 0u) {
            return;
        }
    } else {
        step_value = 1;
    }
    if (step_value == 0) {
        fail_run("STEP");
        return;
    }

    variables[var] = start_value;
    body_pc = run_pc;
    skip_past_eol(&body_pc);
    skip_statement_gaps(&body_pc);

    if (loop_in_range(start_value, limit_value, step_value) == 0u) {
        if (find_matching_next(body_pc, &after_next) == 0u) {
            fail_run("NEXT");
            return;
        }
        run_pc = after_next;
        return;
    }

    if (loop_depth >= MAX_LOOP_STACK) {
        fail_run("LOOP");
        return;
    }
    loop_stack[loop_depth].var = var;
    loop_stack[loop_depth].limit = limit_value;
    loop_stack[loop_depth].step = step_value;
    loop_stack[loop_depth].body_pc = body_pc;
    ++loop_depth;
    run_pc = body_pc;
}

static void exec_next(void) {
    LoopFrame *frame;
    unsigned char var;
    unsigned char have_var;
    int value;
    unsigned int after_next;

    if (loop_depth == 0u) {
        fail_run("FOR");
        return;
    }

    var = 0u;
    have_var = 0u;
    skip_spaces(&run_pc);
    if (is_name_char(source_at(run_pc)) != 0u) {
        if (parse_variable(&run_pc, &var) == 0u) {
            return;
        }
        have_var = 1u;
    }

    frame = &loop_stack[(unsigned char)(loop_depth - 1u)];
    if ((have_var != 0u) && (var != frame->var)) {
        fail_run("VAR");
        return;
    }

    after_next = run_pc;
    skip_past_eol(&after_next);

    value = (int)(variables[frame->var] + frame->step);
    variables[frame->var] = value;
    if (loop_in_range(value, frame->limit, frame->step) != 0u) {
        run_pc = frame->body_pc;
    } else {
        --loop_depth;
        run_pc = after_next;
    }
}

static void exec_input(void) {
    unsigned char var;

    if (parse_variable(&run_pc, &var) == 0u) {
        return;
    }
    variables[var] = (int)read_buttons_value();
    skip_to_eol(&run_pc);
}

static void exec_wait(void) {
    int count;

    count = parse_expr(&run_pc);
    if (run_error != 0u) {
        return;
    }
    if (count < 0) {
        count = 0;
    }
    while ((count != 0) && (run_stop == 0u)) {
        if (glic_button_pressed(glic_read_buttons(), GLIC_BTN_C) != 0u) {
            run_break = 1u;
            run_stop = 1u;
            return;
        }
        glic_delay(400u);
        --count;
    }
    skip_to_eol(&run_pc);
}

static void exec_statement(void) {
    unsigned char var;

    skip_spaces(&run_pc);
    if ((run_pc >= run_len) || (source_at(run_pc) == '\n')) {
        return;
    }

    if (match_keyword(&run_pc, "REM") != 0u) {
        skip_to_eol(&run_pc);
    } else if (match_keyword(&run_pc, "PRINT") != 0u) {
        exec_print();
    } else if (match_keyword(&run_pc, "LET") != 0u) {
        exec_let();
    } else if (match_keyword(&run_pc, "IF") != 0u) {
        exec_if();
    } else if (match_keyword(&run_pc, "FOR") != 0u) {
        exec_for();
    } else if (match_keyword(&run_pc, "NEXT") != 0u) {
        exec_next();
    } else if (match_keyword(&run_pc, "INPUT") != 0u) {
        exec_input();
    } else if (match_keyword(&run_pc, "WAIT") != 0u) {
        exec_wait();
    } else if (match_keyword(&run_pc, "CLS") != 0u) {
        output_clear();
        skip_to_eol(&run_pc);
    } else if (match_keyword(&run_pc, "END") != 0u) {
        run_done = 1u;
        run_stop = 1u;
        skip_to_eol(&run_pc);
    } else if (parse_variable(&run_pc, &var) != 0u) {
        exec_assignment(var);
    }
}

static void poll_run_stop(void) {
    if ((run_stop == 0u) &&
        (glic_button_pressed(glic_read_buttons(), GLIC_BTN_C) != 0u)) {
        run_break = 1u;
        run_stop = 1u;
    }
}

static void run_program(void) {
    unsigned char i;
    unsigned char buttons;
    unsigned char last_buttons;

    wait_for_release();
    run_len = program_len();
    run_pc = 0u;
    run_error = 0u;
    run_stop = 0u;
    run_break = 0u;
    run_done = 0u;
    loop_depth = 0u;

    for (i = 0u; i < 26u; ++i) {
        variables[i] = 0;
    }

    output_clear();
    if (run_len == 0u) {
        output_text("EMPTY");
        output_newline();
        run_done = 1u;
    }

    while ((run_stop == 0u) && (run_done == 0u)) {
        skip_statement_gaps(&run_pc);
        if (run_pc >= run_len) {
            run_done = 1u;
            break;
        }
        exec_statement();
        if (run_error != 0u) {
            break;
        }
        poll_run_stop();
    }

    if (run_error == 0u) {
        output_newline();
        if (run_break != 0u) {
            output_text("STOP");
        } else {
            output_text("DONE");
        }
    }

    text_clear_row(15u);
    text_write(15u, 0u, "C MENU CTR EXIT");
    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if ((button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u)) {
            wait_for_release();
            return;
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void draw_menu(void) {
    unsigned int len;

    len = program_len();
    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 1u, "ONEADDR BASIC");
    text_write(2u, 0u, "SRC 000/512");
    text_write_u16_3(2u, 4u, len);
    text_write(5u, 1u, "CENTER EDIT");
    text_write(7u, 1u, "A RUN");
    text_write(9u, 1u, "B LOAD DEMO");
    text_write(11u, 1u, "C INSTRUCTIONS");
    text_write(15u, 0u, "TEXT AT 0x6006");
}

static const char *instruction_name(unsigned char index) {
    switch (index) {
    case 0u:
        return "PRINT";
    case 1u:
        return "LET";
    case 2u:
        return "IF";
    case 3u:
        return "FOR";
    case 4u:
        return "NEXT";
    case 5u:
        return "INPUT";
    case 6u:
        return "BTN";
    case 7u:
        return "WAIT";
    case 8u:
        return "CLS";
    case 9u:
        return "REM";
    case 10u:
        return "END";
    default:
        return "EXPR";
    }
}

static const char *instruction_detail(unsigned char index,
                                      unsigned char line) {
    switch (index) {
    case 0u:
        switch (line) {
        case 0u:
            return "PRINT X";
        case 1u:
            return "SHOWS A NUMBER";
        case 2u:
            return "PRINT \"HI\"";
        case 3u:
            return "SHOWS TEXT";
        default:
            return "";
        }
    case 1u:
        switch (line) {
        case 0u:
            return "LET A=1";
        case 1u:
            return "A=A+1 ALSO OK";
        case 2u:
            return "VARS ARE A-Z";
        default:
            return "";
        }
    case 2u:
        switch (line) {
        case 0u:
            return "IF X THEN CMD";
        case 1u:
            return "RUNS CMD WHEN";
        case 2u:
            return "X IS NOT ZERO";
        case 3u:
            return "COMPARE = < >";
        default:
            return "";
        }
    case 3u:
        switch (line) {
        case 0u:
            return "FOR I=1 TO 5";
        case 1u:
            return "REPEATS LINES";
        case 2u:
            return "UNTIL NEXT";
        case 3u:
            return "STEP OPTIONAL";
        default:
            return "";
        }
    case 4u:
        switch (line) {
        case 0u:
            return "NEXT";
        case 1u:
            return "ENDS FOR LOOP";
        case 2u:
            return "NEXT I CHECKS";
        case 3u:
            return "THE VARIABLE";
        default:
            return "";
        }
    case 5u:
        switch (line) {
        case 0u:
            return "INPUT A";
        case 1u:
            return "READS BUTTONS";
        case 2u:
            return "A GETS A MASK";
        case 3u:
            return "0 MEANS NONE";
        default:
            return "";
        }
    case 6u:
        switch (line) {
        case 0u:
            return "BTN";
        case 1u:
            return "CURRENT BUTTON";
        case 2u:
            return "MASK IN EXPR";
        case 3u:
            return "IF BTN THEN...";
        default:
            return "";
        }
    case 7u:
        switch (line) {
        case 0u:
            return "WAIT 50";
        case 1u:
            return "PAUSES A BIT";
        case 2u:
            return "C CAN STOP IT";
        default:
            return "";
        }
    case 8u:
        switch (line) {
        case 0u:
            return "CLS";
        case 1u:
            return "CLEARS OUTPUT";
        case 2u:
            return "PROGRAM KEEPS";
        case 3u:
            return "RUNNING";
        default:
            return "";
        }
    case 9u:
        switch (line) {
        case 0u:
            return "REM COMMENT";
        case 1u:
            return "IGNORED LINE";
        case 2u:
            return "USE FOR NOTES";
        default:
            return "";
        }
    case 10u:
        switch (line) {
        case 0u:
            return "END";
        case 1u:
            return "STOPS PROGRAM";
        case 2u:
            return "DONE IS SHOWN";
        default:
            return "";
        }
    default:
        switch (line) {
        case 0u:
            return "A+2*(B-1)";
        case 1u:
            return "+ - * / ( )";
        case 2u:
            return "COMPARES MAKE";
        case 3u:
            return "0 FALSE 1 TRUE";
        default:
            return "";
        }
    }
}

static void draw_instruction_list(unsigned char selected) {
    unsigned char i;
    unsigned char row;

    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 1u, "INSTRUCTIONS");
    text_write(1u, 0u, "CTR OPENS ITEM");

    for (i = 0u; i < INSTRUCTION_COUNT; ++i) {
        row = (unsigned char)(2u + i);
        text_put(row, 0u, (i == selected) ? '>' : ' ');
        text_write(row, 2u, instruction_name(i));
    }

    text_write(14u, 0u, "UP/DN PICK");
    text_write(15u, 0u, "CTR VIEW C BACK");
}

static void draw_instruction_detail(unsigned char selected) {
    unsigned char line;

    glic_prepare_screen(GLIC_BLACK);
    text_put(0u, 0u, '>');
    text_write(0u, 2u, instruction_name(selected));

    for (line = 0u; line < 9u; ++line) {
        text_write((unsigned char)(2u + line),
                   0u,
                   instruction_detail(selected, line));
    }

    text_write(13u, 0u, "< > NEXT ITEM");
    text_write(14u, 0u, "UP/DN ALSO");
    text_write(15u, 0u, "C LIST CTR LIST");
}

static void show_instruction_detail(unsigned char *selected) {
    unsigned char buttons;
    unsigned char last_buttons;

    draw_instruction_detail(*selected);
    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if ((button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u)) {
            wait_for_release();
            return;
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_RIGHT) != 0u) ||
                   (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u)) {
            ++(*selected);
            if (*selected >= INSTRUCTION_COUNT) {
                *selected = 0u;
            }
            draw_instruction_detail(*selected);
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_LEFT) != 0u) ||
                   (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u)) {
            if (*selected == 0u) {
                *selected = (unsigned char)(INSTRUCTION_COUNT - 1u);
            } else {
                --(*selected);
            }
            draw_instruction_detail(*selected);
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void show_instructions(void) {
    unsigned char buttons;
    unsigned char last_buttons;
    unsigned char selected;

    selected = 0u;
    draw_instruction_list(selected);
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            if (selected == 0u) {
                selected = (unsigned char)(INSTRUCTION_COUNT - 1u);
            } else {
                --selected;
            }
            draw_instruction_list(selected);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            ++selected;
            if (selected >= INSTRUCTION_COUNT) {
                selected = 0u;
            }
            draw_instruction_list(selected);
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) ||
                   (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u)) {
            wait_for_release();
            show_instruction_detail(&selected);
            draw_instruction_list(selected);
            last_buttons = GLIC_BUTTONS_NONE;
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) ||
                   (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u)) {
            wait_for_release();
            return;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void confirm_load_demo(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    glic_prepare_screen(GLIC_BLACK);
    text_write(3u, 2u, "LOAD DEMO?");
    text_write(6u, 2u, "A YES");
    text_write(8u, 2u, "B/C NO");

    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            load_demo_program();
            wait_for_release();
            return;
        }
        if ((button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) ||
            (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u)) {
            wait_for_release();
            return;
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static unsigned char run_menu(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    draw_menu();
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            wait_for_release();
            return ACTION_EDIT;
        }
        if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            wait_for_release();
            return ACTION_RUN;
        }
        if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            wait_for_release();
            confirm_load_demo();
            draw_menu();
            last_buttons = GLIC_BUTTONS_NONE;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            wait_for_release();
            show_instructions();
            draw_menu();
            last_buttons = GLIC_BUTTONS_NONE;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

void main(void) {
    unsigned char action;

    storage_init();
    while (1) {
        action = run_menu();
        if (action == ACTION_EDIT) {
            run_editor();
        } else if (action == ACTION_RUN) {
            run_program();
        }
    }
}
