#include "../common/glic80.h"

#define STORAGE_BASE ((volatile unsigned char *)0x6000)
#define STORAGE_HEADER_BYTES 4u
#define STORAGE_MAGIC0 0x54u
#define STORAGE_MAGIC1 0x58u
#define STORAGE_MAGIC2 0x54u
#define STORAGE_MAGIC3 0x31u

#define FILE_COUNT 3u
#define TEXT_CAPACITY 192u
#define FILE_RECORD_SIZE (TEXT_CAPACITY + 1u)

#define ACTION_EDIT 1u
#define ACTION_VIEW 2u

#define EDIT_TEXT_FIRST_ROW 1u
#define EDIT_TEXT_ROWS 7u
#define EDIT_VISIBLE_CHARS 112u
#define KEY_TOP 9u
#define KEY_ROWS 5u
#define KEY_COLS 8u
#define LOOP_DELAY 1200u

static unsigned char selected_file;

static const unsigned char keyboard_keys[KEY_ROWS * KEY_COLS] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '0', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '_', '.', ',', '!'
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

static volatile unsigned char *file_record(unsigned char file) {
    volatile unsigned char *p;

    p = STORAGE_BASE + STORAGE_HEADER_BYTES;
    while (file != 0u) {
        p += FILE_RECORD_SIZE;
        --file;
    }
    return p;
}

static volatile unsigned char *file_data(unsigned char file) {
    return file_record(file) + 1u;
}

static unsigned char file_length(unsigned char file) {
    return *file_record(file);
}

static void set_file_length(unsigned char file, unsigned char len) {
    *file_record(file) = len;
}

static void clear_file(unsigned char file) {
    volatile unsigned char *p;
    unsigned char i;

    p = file_record(file);
    *p++ = 0u;
    for (i = 0u; i < TEXT_CAPACITY; ++i) {
        *p++ = 0u;
    }
}

static void storage_init(void) {
    unsigned char file;

    if ((STORAGE_BASE[0] != STORAGE_MAGIC0) ||
        (STORAGE_BASE[1] != STORAGE_MAGIC1) ||
        (STORAGE_BASE[2] != STORAGE_MAGIC2) ||
        (STORAGE_BASE[3] != STORAGE_MAGIC3)) {
        STORAGE_BASE[0] = STORAGE_MAGIC0;
        STORAGE_BASE[1] = STORAGE_MAGIC1;
        STORAGE_BASE[2] = STORAGE_MAGIC2;
        STORAGE_BASE[3] = STORAGE_MAGIC3;
        for (file = 0u; file < FILE_COUNT; ++file) {
            clear_file(file);
        }
        return;
    }

    for (file = 0u; file < FILE_COUNT; ++file) {
        if (file_length(file) > TEXT_CAPACITY) {
            clear_file(file);
        }
    }
}

static unsigned char key_at(unsigned char row, unsigned char col) {
    return keyboard_keys[(row << 3) + col];
}

static unsigned char typed_key(unsigned char row, unsigned char col) {
    unsigned char ch;

    ch = key_at(row, col);
    if (ch == '_') {
        return ' ';
    }
    return ch;
}

static unsigned char display_key(unsigned char row, unsigned char col) {
    return key_at(row, col);
}

static unsigned char edit_window_start(unsigned char len) {
    unsigned char start;

    start = 0u;
    while ((start <= (TEXT_CAPACITY - EDIT_VISIBLE_CHARS)) &&
           ((unsigned char)(start + EDIT_VISIBLE_CHARS) <= len)) {
        start = (unsigned char)(start + 16u);
    }
    return start;
}

static void draw_text_rows(unsigned char file,
                           unsigned char first_row,
                           unsigned char rows,
                           unsigned char start) {
    volatile unsigned char *data;
    unsigned char len;
    unsigned char row;
    unsigned char col;
    unsigned char pos;
    unsigned char ch;

    data = file_data(file);
    len = file_length(file);
    pos = start;

    for (row = 0u; row < rows; ++row) {
        text_clear_row((unsigned char)(first_row + row));
        for (col = 0u; col < 16u; ++col) {
            if (pos < len) {
                ch = data[pos];
                if (ch == 0u) {
                    ch = ' ';
                }
            } else {
                ch = ' ';
            }
            text_put((unsigned char)(first_row + row), col, ch);
            ++pos;
        }
    }
}

static void draw_menu_files(void) {
    unsigned char file;
    unsigned char row;
    unsigned char len;

    for (file = 0u; file < FILE_COUNT; ++file) {
        row = (unsigned char)(3u + (file << 1));
        text_clear_row(row);
        text_put(row, 0u, (file == selected_file) ? '>' : ' ');
        text_write(row, 2u, "FILE ");
        text_put(row, 7u, (unsigned char)('1' + file));
        len = file_length(file);
        if (len == 0u) {
            text_write(row, 9u, "EMPTY");
        } else {
            text_write(row, 9u, "LEN");
            text_write_u8_3(row, 13u, len);
        }
    }
}

static void draw_menu(void) {
    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 2u, "TEXT EDITOR");
    text_write(1u, 3u, "RAM FILES");
    draw_menu_files();
    text_write(10u, 0u, "UP/DN PICK");
    text_write(11u, 0u, "CENTER EDIT");
    text_write(12u, 0u, "A VIEW B ERASE");
    text_write(13u, 0u, "C HELP");
    text_write(15u, 0u, "RUNNING RAM SAVE");
}

static void draw_confirm_clear(void) {
    glic_prepare_screen(GLIC_BLACK);
    text_write(3u, 2u, "CLEAR FILE ");
    text_put(3u, 13u, (unsigned char)('1' + selected_file));
    text_write(6u, 2u, "A YES");
    text_write(8u, 2u, "B/C NO");
}

static void confirm_clear(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    draw_confirm_clear();
    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            clear_file(selected_file);
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

static void show_help(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 2u, "TEXT EDITOR");
    text_write(2u, 0u, "MENU UP/DN PICK");
    text_write(3u, 0u, "CENTER EDIT");
    text_write(4u, 0u, "A VIEW B ERASE");
    text_write(6u, 0u, "EDIT ARROWS KEY");
    text_write(7u, 0u, "CENTER TYPE");
    text_write(8u, 0u, "A SAVE B DEL");
    text_write(9u, 0u, "C MENU");
    text_write(11u, 0u, "VIEW < > FILE");
    text_write(12u, 0u, "CENTER EDIT");
    text_write(13u, 0u, "C BACK");
    text_write(15u, 4u, "PRESS C");

    last_buttons = GLIC_BUTTONS_NONE;
    while (1) {
        buttons = glic_read_buttons();
        if ((button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) ||
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
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            if (selected_file == 0u) {
                selected_file = (unsigned char)(FILE_COUNT - 1u);
            } else {
                --selected_file;
            }
            draw_menu_files();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            ++selected_file;
            if (selected_file >= FILE_COUNT) {
                selected_file = 0u;
            }
            draw_menu_files();
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            wait_for_release();
            return ACTION_EDIT;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            wait_for_release();
            return ACTION_VIEW;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            wait_for_release();
            confirm_clear();
            draw_menu();
            last_buttons = GLIC_BUTTONS_NONE;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            wait_for_release();
            show_help();
            draw_menu();
            last_buttons = GLIC_BUTTONS_NONE;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void draw_keyboard(unsigned char key_row, unsigned char key_col) {
    unsigned char row;
    unsigned char col;
    unsigned char screen_col;

    for (row = 0u; row < KEY_ROWS; ++row) {
        text_clear_row((unsigned char)(KEY_TOP + row));
        for (col = 0u; col < KEY_COLS; ++col) {
            screen_col = (unsigned char)(col << 1);
            text_put((unsigned char)(KEY_TOP + row),
                     screen_col,
                     key_at(row, col));
            if ((row == key_row) && (col == key_col)) {
                text_put((unsigned char)(KEY_TOP + row),
                         (unsigned char)(screen_col + 1u),
                         '<');
            }
        }
    }
}

static void draw_editor_header(unsigned char file,
                               unsigned char key_row,
                               unsigned char key_col,
                               const char *status) {
    unsigned char len;

    text_clear_row(0u);
    text_write(0u, 0u, "EDIT FILE ");
    text_put(0u, 10u, (unsigned char)('1' + file));
    text_write(0u, 12u, status);

    len = file_length(file);
    text_clear_row(8u);
    text_write_u8_3(8u, 0u, len);
    text_write(8u, 3u, "/192 KEY ");
    text_put(8u, 12u, display_key(key_row, key_col));
}

static void draw_editor_text(unsigned char file) {
    draw_text_rows(file,
                   EDIT_TEXT_FIRST_ROW,
                   EDIT_TEXT_ROWS,
                   edit_window_start(file_length(file)));
}

static void draw_editor(unsigned char file,
                        unsigned char key_row,
                        unsigned char key_col,
                        const char *status) {
    glic_prepare_screen(GLIC_BLACK);
    draw_editor_header(file, key_row, key_col, status);
    draw_editor_text(file);
    draw_keyboard(key_row, key_col);
    text_write(14u, 0u, "A SAVE B DEL");
    text_write(15u, 0u, "C MENU CTR TYPE");
}

static void insert_char(unsigned char file,
                        unsigned char key_row,
                        unsigned char key_col) {
    volatile unsigned char *data;
    unsigned char len;

    len = file_length(file);
    if (len >= TEXT_CAPACITY) {
        return;
    }

    data = file_data(file);
    data[len] = typed_key(key_row, key_col);
    ++len;
    set_file_length(file, len);
    if (len < TEXT_CAPACITY) {
        data[len] = 0u;
    }
}

static void delete_char(unsigned char file) {
    volatile unsigned char *data;
    unsigned char len;

    len = file_length(file);
    if (len == 0u) {
        return;
    }
    --len;
    set_file_length(file, len);
    data = file_data(file);
    data[len] = 0u;
}

static void run_editor(unsigned char file) {
    unsigned char buttons;
    unsigned char last_buttons;
    unsigned char key_row;
    unsigned char key_col;
    const char *status;

    key_row = 0u;
    key_col = 0u;
    status = "";
    draw_editor(file, key_row, key_col, status);
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            if (key_row == 0u) {
                key_row = (unsigned char)(KEY_ROWS - 1u);
            } else {
                --key_row;
            }
            status = "";
            draw_editor(file, key_row, key_col, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            ++key_row;
            if (key_row >= KEY_ROWS) {
                key_row = 0u;
            }
            status = "";
            draw_editor(file, key_row, key_col, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_LEFT) != 0u) {
            if (key_col == 0u) {
                key_col = (unsigned char)(KEY_COLS - 1u);
            } else {
                --key_col;
            }
            status = "";
            draw_editor(file, key_row, key_col, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_RIGHT) != 0u) {
            ++key_col;
            if (key_col >= KEY_COLS) {
                key_col = 0u;
            }
            status = "";
            draw_editor(file, key_row, key_col, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            if (file_length(file) < TEXT_CAPACITY) {
                insert_char(file, key_row, key_col);
                status = "";
            } else {
                status = "FULL";
            }
            draw_editor(file, key_row, key_col, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            delete_char(file);
            status = "";
            draw_editor(file, key_row, key_col, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            STORAGE_BASE[0] = STORAGE_MAGIC0;
            STORAGE_BASE[1] = STORAGE_MAGIC1;
            STORAGE_BASE[2] = STORAGE_MAGIC2;
            STORAGE_BASE[3] = STORAGE_MAGIC3;
            status = "SAV";
            draw_editor(file, key_row, key_col, status);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            wait_for_release();
            return;
        }

        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

static void draw_viewer(unsigned char file) {
    unsigned char len;

    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 0u, "VIEW FILE ");
    text_put(0u, 10u, (unsigned char)('1' + file));
    len = file_length(file);
    if (len == 0u) {
        text_write(5u, 5u, "EMPTY");
    } else {
        draw_text_rows(file, 1u, 12u, 0u);
    }
    text_write(13u, 0u, "LEN ");
    text_write_u8_3(13u, 4u, len);
    text_write(14u, 0u, "CENTER EDIT");
    text_write(15u, 0u, "C MENU  < > FILE");
}

static void run_viewer(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    draw_viewer(selected_file);
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_LEFT) != 0u) {
            if (selected_file == 0u) {
                selected_file = (unsigned char)(FILE_COUNT - 1u);
            } else {
                --selected_file;
            }
            draw_viewer(selected_file);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_RIGHT) != 0u) {
            ++selected_file;
            if (selected_file >= FILE_COUNT) {
                selected_file = 0u;
            }
            draw_viewer(selected_file);
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            wait_for_release();
            run_editor(selected_file);
            draw_viewer(selected_file);
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

void main(void) {
    unsigned char action;

    selected_file = 0u;
    storage_init();

    while (1) {
        action = run_menu();
        if (action == ACTION_EDIT) {
            run_editor(selected_file);
        } else if (action == ACTION_VIEW) {
            run_viewer();
        }
    }
}
