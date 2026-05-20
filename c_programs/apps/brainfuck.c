#include "../common/glic80.h"

#define STORE_BASE ((volatile unsigned char *)0x6000)
#define STORE_MAGIC0 0x42u
#define STORE_MAGIC1 0x46u
#define STORE_MAGIC2 0x43u
#define STORE_MAGIC3 0x31u
#define STORE_SRC_LEN_LO 4u
#define STORE_SRC_LEN_HI 5u
#define STORE_INPUT_LEN 6u
#define SOURCE_TEXT (STORE_BASE + 8u)
#define SOURCE_CAPACITY 512u
#define INPUT_TEXT (SOURCE_TEXT + SOURCE_CAPACITY)
#define INPUT_CAPACITY 64u

#define KEY_ROWS 6u
#define KEY_COLS 8u
#define KEY_TOP 9u
#define EDIT_TEXT_FIRST_ROW 1u
#define EDIT_TEXT_ROWS 7u
#define EDIT_VIEW_COLS 15u
#define LOOP_DELAY 900u

#define EDIT_SOURCE 0u
#define EDIT_INPUT 1u

#define MENU_EDIT_SOURCE 0u
#define MENU_EDIT_INPUT 1u
#define MENU_RUN 2u
#define MENU_HELP 3u
#define MENU_COUNT 4u

#define MAX_CODE 384u
#define MAX_LOOP_STACK 16u

#define OP_ADD 1u
#define OP_MOVE 2u
#define OP_OUT 3u
#define OP_IN 4u
#define OP_JZ 5u
#define OP_JNZ 6u

static const unsigned char keyboard_keys[KEY_ROWS * KEY_COLS] = {
    '+', '-', '<', '>', '.', ',', '[', ']',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '0', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '_', '!', '?', '\n'
};

static const char demo_source[] = ",[.,]";
static const char demo_input[] = "HELLO\n";

static unsigned char code_op[MAX_CODE];
static unsigned char code_arg[MAX_CODE];
static unsigned int code_jump[MAX_CODE];
static unsigned int compile_loop_stack[MAX_LOOP_STACK];
static unsigned char tape[256];

static unsigned int code_len;
static unsigned char compile_error;
static const char *compile_message;
static unsigned int editor_view_start;
static unsigned char output_row;
static unsigned char output_col;

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
    text_put(row, (unsigned char)(col + 2u),
             (unsigned char)('0' + value));
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

static void wait_for_any_key(void) {
    wait_for_release();
    while (glic_read_buttons() == GLIC_BUTTONS_NONE) {
        glic_delay(800u);
    }
    wait_for_release();
}

static unsigned int source_len(void) {
    unsigned int len;

    len = STORE_BASE[STORE_SRC_LEN_LO];
    len |= ((unsigned int)STORE_BASE[STORE_SRC_LEN_HI]) << 8;
    if (len > SOURCE_CAPACITY) {
        return 0u;
    }
    return len;
}

static void set_source_len(unsigned int len) {
    if (len > SOURCE_CAPACITY) {
        len = SOURCE_CAPACITY;
    }
    STORE_BASE[STORE_SRC_LEN_LO] = (unsigned char)(len & 0xffu);
    STORE_BASE[STORE_SRC_LEN_HI] = (unsigned char)(len >> 8);
}

static unsigned int input_len(void) {
    unsigned int len;

    len = STORE_BASE[STORE_INPUT_LEN];
    if (len > INPUT_CAPACITY) {
        return 0u;
    }
    return len;
}

static void set_input_len(unsigned int len) {
    if (len > INPUT_CAPACITY) {
        len = INPUT_CAPACITY;
    }
    STORE_BASE[STORE_INPUT_LEN] = (unsigned char)len;
}

static volatile unsigned char *active_buffer(unsigned char kind) {
    if (kind == EDIT_INPUT) {
        return INPUT_TEXT;
    }
    return SOURCE_TEXT;
}

static unsigned int active_len(unsigned char kind) {
    if (kind == EDIT_INPUT) {
        return input_len();
    }
    return source_len();
}

static void set_active_len(unsigned char kind, unsigned int len) {
    if (kind == EDIT_INPUT) {
        set_input_len(len);
    } else {
        set_source_len(len);
    }
}

static unsigned int active_capacity(unsigned char kind) {
    if (kind == EDIT_INPUT) {
        return INPUT_CAPACITY;
    }
    return SOURCE_CAPACITY;
}

static void mark_storage_valid(void) {
    STORE_BASE[0] = STORE_MAGIC0;
    STORE_BASE[1] = STORE_MAGIC1;
    STORE_BASE[2] = STORE_MAGIC2;
    STORE_BASE[3] = STORE_MAGIC3;
}

static unsigned int copy_literal(volatile unsigned char *dst,
                                const char *src,
                                unsigned int capacity) {
    unsigned int len;

    len = 0u;
    while ((*src != 0) && (len < capacity)) {
        dst[len] = (unsigned char)*src;
        ++src;
        ++len;
    }
    if (len < capacity) {
        dst[len] = 0u;
    }
    return len;
}

static void load_demo_program(void) {
    unsigned int len;

    mark_storage_valid();
    len = copy_literal(SOURCE_TEXT, demo_source, SOURCE_CAPACITY);
    set_source_len(len);
    len = copy_literal(INPUT_TEXT, demo_input, INPUT_CAPACITY);
    set_input_len(len);
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

    len = source_len();
    if (len > SOURCE_CAPACITY) {
        load_demo_program();
        return;
    }
    if (len < SOURCE_CAPACITY) {
        SOURCE_TEXT[len] = 0u;
    }

    len = input_len();
    if (len > INPUT_CAPACITY) {
        load_demo_program();
        return;
    }
    if (len < INPUT_CAPACITY) {
        INPUT_TEXT[len] = 0u;
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

    for (row = 0u; row < KEY_ROWS; ++row) {
        text_clear_row((unsigned char)(KEY_TOP + row));
        for (col = 0u; col < KEY_COLS; ++col) {
            draw_key_label((unsigned char)(KEY_TOP + row),
                           (unsigned char)(col << 1),
                           key_at(row, col),
                           ((row == key_row) && (col == key_col)) ? 1u : 0u);
        }
    }
}

static unsigned char buffer_starts_new_row(unsigned int pos,
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

static unsigned int buffer_max_window_start(volatile unsigned char *buffer,
                                            unsigned int len) {
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
        ch = buffer[pos];
        ++pos;
        if (buffer_starts_new_row(pos, len, ch, &col) != 0u) {
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

static unsigned int buffer_prev_window_start(volatile unsigned char *buffer,
                                             unsigned int len,
                                             unsigned int start) {
    unsigned int pos;
    unsigned int prev;
    unsigned char col;
    unsigned char ch;

    pos = 0u;
    prev = 0u;
    col = 0u;

    while ((pos < len) && (pos < start)) {
        ch = buffer[pos];
        ++pos;
        if (buffer_starts_new_row(pos, len, ch, &col) != 0u) {
            if (pos >= start) {
                break;
            }
            prev = pos;
        }
    }
    return prev;
}

static unsigned int buffer_next_window_start(volatile unsigned char *buffer,
                                             unsigned int len,
                                             unsigned int start) {
    unsigned int max_start;
    unsigned int pos;
    unsigned char col;
    unsigned char ch;

    max_start = buffer_max_window_start(buffer, len);
    pos = 0u;
    col = 0u;

    while (pos < len) {
        ch = buffer[pos];
        ++pos;
        if (buffer_starts_new_row(pos, len, ch, &col) != 0u) {
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

static void clamp_editor_view(unsigned char kind) {
    volatile unsigned char *buffer;
    unsigned int max_start;

    buffer = active_buffer(kind);
    max_start = buffer_max_window_start(buffer, active_len(kind));
    if (editor_view_start > max_start) {
        editor_view_start = max_start;
    }
}

static void editor_view_to_end(unsigned char kind) {
    volatile unsigned char *buffer;

    buffer = active_buffer(kind);
    editor_view_start = buffer_max_window_start(buffer, active_len(kind));
}

static void editor_scroll_up(unsigned char kind) {
    volatile unsigned char *buffer;

    buffer = active_buffer(kind);
    editor_view_start = buffer_prev_window_start(buffer,
                                                active_len(kind),
                                                editor_view_start);
}

static void editor_scroll_down(unsigned char kind) {
    volatile unsigned char *buffer;
    unsigned int len;
    unsigned int max_start;

    buffer = active_buffer(kind);
    len = active_len(kind);
    max_start = buffer_max_window_start(buffer, len);
    if (editor_view_start < max_start) {
        editor_view_start = buffer_next_window_start(buffer,
                                                    len,
                                                    editor_view_start);
    }
}

static void draw_editor_scrollbar(unsigned char kind) {
    volatile unsigned char *buffer;
    unsigned int len;
    unsigned int max_start;
    unsigned int scaled;
    unsigned char row;
    unsigned char i;
    unsigned char knob_index;
    unsigned char knob_row;

    buffer = active_buffer(kind);
    len = active_len(kind);
    max_start = buffer_max_window_start(buffer, len);

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

static void draw_editor_preview(unsigned char kind) {
    volatile unsigned char *buffer;
    unsigned int len;
    unsigned int pos;
    unsigned char row;
    unsigned char col;
    unsigned char ch;

    for (row = 0u; row < EDIT_TEXT_ROWS; ++row) {
        text_clear_row((unsigned char)(EDIT_TEXT_FIRST_ROW + row));
    }

    buffer = active_buffer(kind);
    len = active_len(kind);
    clamp_editor_view(kind);
    if (len == 0u) {
        text_write(4u, 5u, "EMPTY");
        draw_editor_scrollbar(kind);
        return;
    }

    pos = editor_view_start;
    row = EDIT_TEXT_FIRST_ROW;
    col = 0u;

    while ((pos < len) &&
           (row < (unsigned char)(EDIT_TEXT_FIRST_ROW + EDIT_TEXT_ROWS))) {
        ch = buffer[pos];
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
    draw_editor_scrollbar(kind);
}

static void draw_editor_header(unsigned char kind,
                               unsigned char key_row,
                               unsigned char key_col,
                               const char *status) {
    text_clear_row(0u);
    if (kind == EDIT_INPUT) {
        text_write(0u, 0u, "INP ");
    } else {
        text_write(0u, 0u, "SRC ");
    }
    text_write_u16_3(0u, 4u, active_len(kind));
    text_put(0u, 7u, '/');
    text_write_u16_3(0u, 8u, active_capacity(kind));
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

static void draw_editor_shell(unsigned char kind,
                              unsigned char key_row,
                              unsigned char key_col,
                              const char *status) {
    glic_prepare_screen(GLIC_BLACK);
    draw_editor_header(kind, key_row, key_col, status);
    draw_editor_preview(kind);
    text_write(8u, 0u, "B DEL C MENU");
    draw_keyboard(key_row, key_col);
    text_write(15u, 0u, "AA RUN A UD SCR");
}

static void insert_char(unsigned char kind,
                        unsigned char key_row,
                        unsigned char key_col) {
    volatile unsigned char *buffer;
    unsigned int len;
    unsigned int capacity;

    buffer = active_buffer(kind);
    len = active_len(kind);
    capacity = active_capacity(kind);
    if (len >= capacity) {
        return;
    }

    buffer[len] = typed_key(key_row, key_col);
    ++len;
    set_active_len(kind, len);
    if (len < capacity) {
        buffer[len] = 0u;
    }
}

static void delete_char(unsigned char kind) {
    volatile unsigned char *buffer;
    unsigned int len;

    len = active_len(kind);
    if (len == 0u) {
        return;
    }

    buffer = active_buffer(kind);
    --len;
    set_active_len(kind, len);
    buffer[len] = 0u;
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
        ch = '?';
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

static void output_clear(void) {
    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 0u, "BF RUN");
    text_write(15u, 0u, "C STOP ANY BACK");
    output_row = 1u;
    output_col = 0u;
}

static void fail_compile(const char *message) {
    if (compile_error == 0u) {
        compile_error = 1u;
        compile_message = message;
    }
}

static unsigned char emit_op(unsigned char op,
                             unsigned char arg,
                             unsigned int jump) {
    if (code_len >= MAX_CODE) {
        fail_compile("CODE FULL");
        return 0u;
    }

    code_op[code_len] = op;
    code_arg[code_len] = arg;
    code_jump[code_len] = jump;
    ++code_len;
    return 1u;
}

static unsigned char emit_delta(unsigned char op, unsigned char delta) {
    unsigned int last;

    if (code_len != 0u) {
        last = (unsigned int)(code_len - 1u);
        if (code_op[last] == op) {
            code_arg[last] = (unsigned char)(code_arg[last] + delta);
            return 1u;
        }
    }
    return emit_op(op, delta, 0u);
}

static unsigned char compile_program(void) {
    unsigned int pos;
    unsigned int len;
    unsigned int open_pc;
    unsigned char loop_depth;
    unsigned char ch;

    code_len = 0u;
    compile_error = 0u;
    compile_message = "";
    loop_depth = 0u;
    len = source_len();

    for (pos = 0u; pos < len; ++pos) {
        ch = SOURCE_TEXT[pos];
        if (ch == '+') {
            if (emit_delta(OP_ADD, 1u) == 0u) {
                return 0u;
            }
        } else if (ch == '-') {
            if (emit_delta(OP_ADD, 255u) == 0u) {
                return 0u;
            }
        } else if (ch == '>') {
            if (emit_delta(OP_MOVE, 1u) == 0u) {
                return 0u;
            }
        } else if (ch == '<') {
            if (emit_delta(OP_MOVE, 255u) == 0u) {
                return 0u;
            }
        } else if (ch == '.') {
            if (emit_op(OP_OUT, 0u, 0u) == 0u) {
                return 0u;
            }
        } else if (ch == ',') {
            if (emit_op(OP_IN, 0u, 0u) == 0u) {
                return 0u;
            }
        } else if (ch == '[') {
            if (loop_depth >= MAX_LOOP_STACK) {
                fail_compile("LOOP DEEP");
                return 0u;
            }
            if (emit_op(OP_JZ, 0u, 0u) == 0u) {
                return 0u;
            }
            compile_loop_stack[loop_depth] = (unsigned int)(code_len - 1u);
            ++loop_depth;
        } else if (ch == ']') {
            if (loop_depth == 0u) {
                fail_compile("EXTRA ]");
                return 0u;
            }
            --loop_depth;
            open_pc = compile_loop_stack[loop_depth];
            if (emit_op(OP_JNZ, 0u, open_pc) == 0u) {
                return 0u;
            }
            code_jump[open_pc] = code_len;
        }
    }

    if (loop_depth != 0u) {
        fail_compile("OPEN [");
        return 0u;
    }
    return 1u;
}

static void clear_tape(void) {
    unsigned int i;

    for (i = 0u; i < 256u; ++i) {
        tape[i] = 0u;
    }
}

static unsigned char read_input_byte(unsigned int *input_pos,
                                     unsigned int len) {
    unsigned char ch;

    if (*input_pos >= len) {
        return 0u;
    }
    ch = INPUT_TEXT[*input_pos];
    ++(*input_pos);
    return ch;
}

static void execute_program(void) {
    unsigned int pc;
    unsigned int input_pos;
    unsigned int input_size;
    unsigned char data_ptr;
    unsigned char stopped;
    unsigned char poll;

    pc = 0u;
    input_pos = 0u;
    input_size = input_len();
    data_ptr = 0u;
    stopped = 0u;
    poll = 0u;
    clear_tape();

    while (pc < code_len) {
        ++poll;
        if ((poll & 31u) == 0u) {
            if (glic_button_pressed(glic_read_buttons(), GLIC_BTN_C) != 0u) {
                stopped = 1u;
                break;
            }
        }

        if (code_op[pc] == OP_ADD) {
            tape[data_ptr] = (unsigned char)(tape[data_ptr] + code_arg[pc]);
            ++pc;
        } else if (code_op[pc] == OP_MOVE) {
            data_ptr = (unsigned char)(data_ptr + code_arg[pc]);
            ++pc;
        } else if (code_op[pc] == OP_OUT) {
            output_char(tape[data_ptr]);
            ++pc;
        } else if (code_op[pc] == OP_IN) {
            tape[data_ptr] = read_input_byte(&input_pos, input_size);
            ++pc;
        } else if (code_op[pc] == OP_JZ) {
            if (tape[data_ptr] == 0u) {
                pc = code_jump[pc];
            } else {
                ++pc;
            }
        } else if (code_op[pc] == OP_JNZ) {
            if (tape[data_ptr] != 0u) {
                pc = code_jump[pc];
            } else {
                ++pc;
            }
        } else {
            ++pc;
        }
    }

    output_newline();
    if (stopped != 0u) {
        output_text("STOP");
    } else {
        output_text("DONE");
    }
    wait_for_any_key();
}

static void run_program(void) {
    wait_for_release();
    output_clear();
    text_write(1u, 0u, "COMPILING");
    if (compile_program() == 0u) {
        text_clear_row(1u);
        output_row = 1u;
        output_col = 0u;
        output_text("COMPILE ERR");
        output_newline();
        output_text(compile_message);
        wait_for_any_key();
        return;
    }

    text_clear_row(1u);
    output_row = 1u;
    output_col = 0u;
    execute_program();
}

static void run_editor(unsigned char kind) {
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
    editor_view_to_end(kind);
    status = "";
    draw_editor_shell(kind, key_row, key_col, status);
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            if (a_prefix != 0u) {
                editor_scroll_up(kind);
                a_prefix = 0u;
                status = "SC";
                draw_editor_header(kind, key_row, key_col, status);
                draw_editor_preview(kind);
            } else {
                old_row = key_row;
                old_col = key_col;
                if (key_row == 0u) {
                    key_row = (unsigned char)(KEY_ROWS - 1u);
                } else {
                    --key_row;
                }
                status = "";
                draw_editor_header(kind, key_row, key_col, status);
                move_editor_key(old_row, old_col, key_row, key_col);
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            if (a_prefix != 0u) {
                editor_scroll_down(kind);
                a_prefix = 0u;
                status = "SC";
                draw_editor_header(kind, key_row, key_col, status);
                draw_editor_preview(kind);
            } else {
                old_row = key_row;
                old_col = key_col;
                ++key_row;
                if (key_row >= KEY_ROWS) {
                    key_row = 0u;
                }
                status = "";
                draw_editor_header(kind, key_row, key_col, status);
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
            draw_editor_header(kind, key_row, key_col, status);
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
            draw_editor_header(kind, key_row, key_col, status);
            move_editor_key(old_row, old_col, key_row, key_col);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            a_prefix = 0u;
            if (active_len(kind) < active_capacity(kind)) {
                insert_char(kind, key_row, key_col);
                editor_view_to_end(kind);
                status = "";
            } else {
                status = "FL";
            }
            draw_editor_header(kind, key_row, key_col, status);
            draw_editor_preview(kind);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            a_prefix = 0u;
            delete_char(kind);
            editor_view_to_end(kind);
            status = "";
            draw_editor_header(kind, key_row, key_col, status);
            draw_editor_preview(kind);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            if (a_prefix != 0u) {
                a_prefix = 0u;
                run_program();
                status = "";
                draw_editor_shell(kind, key_row, key_col, status);
                last_buttons = GLIC_BUTTONS_NONE;
            } else {
                a_prefix = 1u;
                status = "A?";
                draw_editor_header(kind, key_row, key_col, status);
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            wait_for_release();
            return;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void draw_menu_item(unsigned char index,
                           unsigned char selected,
                           const char *label) {
    unsigned char row;

    row = (unsigned char)(4u + (unsigned char)(index << 1));
    text_clear_row(row);
    text_put(row, 0u, (selected != 0u) ? '>' : ' ');
    text_write(row, 2u, label);
}

static void draw_menu(unsigned char selected, const char *status) {
    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 3u, "BRAINFUCK");
    text_write(1u, 0u, "COMPILE AND RUN");
    text_write(2u, 0u, "SRC ");
    text_write_u16_3(2u, 4u, source_len());
    text_write(2u, 8u, "IN ");
    text_write_u16_3(2u, 11u, input_len());
    draw_menu_item(MENU_EDIT_SOURCE,
                   (selected == MENU_EDIT_SOURCE) ? 1u : 0u,
                   "EDIT PROGRAM");
    draw_menu_item(MENU_EDIT_INPUT,
                   (selected == MENU_EDIT_INPUT) ? 1u : 0u,
                   "EDIT INPUT");
    draw_menu_item(MENU_RUN,
                   (selected == MENU_RUN) ? 1u : 0u,
                   "RUN");
    draw_menu_item(MENU_HELP,
                   (selected == MENU_HELP) ? 1u : 0u,
                   "HELP");
    text_write(12u, 0u, "B LOAD DEMO");
    text_write(13u, 0u, status);
    text_write(15u, 0u, "A/CENTER SELECT");
}

static void show_help(void) {
    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 1u, "BRAINFUCK HELP");
    text_write(2u, 0u, "+- CELL VALUE");
    text_write(3u, 0u, "<> TAPE POINTER");
    text_write(4u, 0u, ". OUTPUT CHAR");
    text_write(5u, 0u, ", INPUT BYTE");
    text_write(6u, 0u, "[] LOOP WHILE");
    text_write(7u, 0u, "CELL NONZERO");
    text_write(9u, 0u, "INPUT EOF IS 0");
    text_write(10u, 0u, "256 BYTE TAPE");
    text_write(11u, 0u, "384 BYTECODE");
    text_write(12u, 0u, "NON BF IGNORED");
    text_write(13u, 0u, "C STOPS RUN");
    text_write(15u, 0u, "ANY KEY BACK");
    wait_for_any_key();
}

void main(void) {
    unsigned char selected;
    unsigned char buttons;
    unsigned char last_buttons;
    const char *status;

    storage_init();
    selected = MENU_EDIT_SOURCE;
    status = "";
    draw_menu(selected, status);
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            if (selected == 0u) {
                selected = (unsigned char)(MENU_COUNT - 1u);
            } else {
                --selected;
            }
            status = "";
            draw_menu(selected, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            ++selected;
            if (selected >= MENU_COUNT) {
                selected = 0u;
            }
            status = "";
            draw_menu(selected, status);
        } else if ((button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) ||
                   (button_edge(buttons,
                                last_buttons,
                                GLIC_BTN_CENTER) != 0u)) {
            wait_for_release();
            if (selected == MENU_EDIT_SOURCE) {
                run_editor(EDIT_SOURCE);
            } else if (selected == MENU_EDIT_INPUT) {
                run_editor(EDIT_INPUT);
            } else if (selected == MENU_RUN) {
                run_program();
            } else {
                show_help();
            }
            status = "";
            draw_menu(selected, status);
            last_buttons = GLIC_BUTTONS_NONE;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            load_demo_program();
            status = "DEMO LOADED";
            draw_menu(selected, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            show_help();
            status = "";
            draw_menu(selected, status);
            last_buttons = GLIC_BUTTONS_NONE;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}
