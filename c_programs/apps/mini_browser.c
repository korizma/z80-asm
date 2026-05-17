#include "../common/glic80.h"

#define BROWSER_STORE ((volatile unsigned char *)0x6000)
#define STORE_MAGIC0 0x48u
#define STORE_MAGIC1 0x54u
#define STORE_MAGIC2 0x4du
#define STORE_MAGIC3 0x31u
#define STORE_LEN_LO 4u
#define STORE_LEN_HI 5u
#define STORE_HTML (BROWSER_STORE + 6u)
#define STORE_CAPACITY 2048u

#define DOC_COLS 64u
#define VIEW_COLS 16u
#define VIEW_FIRST_ROW 1u
#define VIEW_ROWS 14u
#define H_SCROLL_STEP 4u
#define MAX_SCROLL_Y 999u
#define LOOP_DELAY 850u

#define TAG_BUF_SIZE 192u
#define STYLE_BUF_SIZE 160u
#define SELECTOR_BUF_SIZE 24u
#define PROP_BUF_SIZE 24u
#define VALUE_BUF_SIZE 48u
#define STYLE_STACK_DEPTH 20u

#define TAG_UNKNOWN 0u
#define TAG_HTML 1u
#define TAG_HEAD 2u
#define TAG_STYLE 3u
#define TAG_SCRIPT 4u
#define TAG_TITLE 5u
#define TAG_BODY 6u
#define TAG_MAIN 7u
#define TAG_SECTION 8u
#define TAG_ARTICLE 9u
#define TAG_HEADER 10u
#define TAG_FOOTER 11u
#define TAG_NAV 12u
#define TAG_DIV 13u
#define TAG_P 14u
#define TAG_H1 15u
#define TAG_H2 16u
#define TAG_H3 17u
#define TAG_SPAN 18u
#define TAG_A 19u
#define TAG_BR 20u
#define TAG_HR 21u
#define TAG_UL 22u
#define TAG_OL 23u
#define TAG_LI 24u
#define TAG_PRE 25u
#define TAG_CODE 26u
#define TAG_BLOCKQUOTE 27u
#define TAG_IMG 28u
#define TAG_META 29u
#define TAG_LINK 30u
#define TAG_INPUT 31u
#define TAG_COUNT 32u

#define ALIGN_LEFT 0u
#define ALIGN_CENTER 1u
#define ALIGN_RIGHT 2u

typedef struct {
    unsigned char display_none;
    unsigned char before;
    unsigned char after;
    unsigned char indent;
    unsigned char border;
    unsigned char uppercase;
    unsigned char pre;
    unsigned char align;
} CssStyle;

typedef struct {
    unsigned char tag;
    unsigned char started_block;
    unsigned char old_indent;
    unsigned char old_uppercase;
    unsigned char old_pre;
    unsigned char old_hidden;
    unsigned char border;
    unsigned char after;
} TagFrame;

static const char default_html[] =
    "<!doctype html>"
    "<html><head><title>GLIC80</title>"
    "<style>"
    "h1{text-transform:uppercase;margin-bottom:8px;}"
    "h2{margin-top:16px;text-transform:uppercase;}"
    "p{margin-bottom:8px;}"
    "blockquote{margin-left:16px;border:1px solid white;}"
    "pre{white-space:pre;border:1px solid white;margin-top:8px;}"
    "</style></head>"
    "<body>"
    "<h1>Mini Browser</h1>"
    "<p style=\"font-weight:bold\">This GLIC80 app renders a small useful subset of HTML and CSS.</p>"
    "<p>The virtual document is sixty four text columns wide, exactly four times the screen width. "
    "Use left and right to pan across wide layouts, and up and down to move through the page.</p>"
    "<h2>Supported HTML</h2>"
    "<ul><li>Headings, paragraphs, divs, spans and links.</li>"
    "<li>Lists, blockquotes, pre/code, line breaks and rules.</li>"
    "<li>Images render as compact text placeholders.</li></ul>"
    "<blockquote style=\"padding-left:8px\">CSS is monochrome here, so borders, spacing, hidden content, "
    "preformatted text and bold/uppercase are mapped onto text.</blockquote>"
    "<h2>Wide page test</h2>"
    "<p>Column 00........10........20........30........40........50........60.</p>"
    "<pre>body { width: 512px; }\n"
    ".card { margin-left: 16px; border: 1px solid; }\n"
    "&lt;p&gt;HTML entities are decoded.&lt;/p&gt;</pre>"
    "<p style=\"margin-left:24px\">Indented content proves that CSS spacing moves with horizontal scroll.</p>"
    "<p>To load your own site, place HTM1 plus a two byte length at 0x6000 and the HTML bytes at 0x6006, "
    "or replace the default_html string and rebuild.</p>"
    "<hr><p>End of demo page.</p>"
    "</body></html>";

static unsigned char tag_buf[TAG_BUF_SIZE];
static char style_buf[STYLE_BUF_SIZE];
static char selector_buf[SELECTOR_BUF_SIZE];
static char prop_buf[PROP_BUF_SIZE];
static char value_buf[VALUE_BUF_SIZE];
static CssStyle css_rules[TAG_COUNT];
static TagFrame style_stack[STYLE_STACK_DEPTH];

static unsigned char active_page_ram;
static unsigned int active_page_len;
static unsigned char view_x;
static unsigned int view_y;
static unsigned int content_rows;

static unsigned char stack_depth;
static unsigned char out_x;
static unsigned int out_y;
static unsigned char out_indent;
static unsigned char out_uppercase;
static unsigned char out_pre;
static unsigned char out_hidden;
static unsigned char out_space_pending;
static unsigned char out_started;

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

static void text_write_u16_2(unsigned char row,
                             unsigned char col,
                             unsigned int value) {
    unsigned char tens;

    if (value > 99u) {
        value = 99u;
    }
    tens = 0u;
    while (value >= 10u) {
        value = (unsigned int)(value - 10u);
        ++tens;
    }
    text_put(row, col, (unsigned char)('0' + tens));
    text_put(row, (unsigned char)(col + 1u), (unsigned char)('0' + value));
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

static unsigned char ascii_lower(unsigned char ch) {
    if ((ch >= 'A') && (ch <= 'Z')) {
        return (unsigned char)(ch + ('a' - 'A'));
    }
    return ch;
}

static unsigned char ascii_equal_ci(unsigned char left, unsigned char right) {
    return ascii_lower(left) == ascii_lower(right);
}

static unsigned char ascii_space(unsigned char ch) {
    return ((ch == ' ') || (ch == '\n') || (ch == '\r') || (ch == '\t'));
}

static unsigned char ascii_digit(unsigned char ch) {
    return ((ch >= '0') && (ch <= '9'));
}

static unsigned char ident_char(unsigned char ch) {
    if (((ch >= 'a') && (ch <= 'z')) ||
        ((ch >= 'A') && (ch <= 'Z')) ||
        ((ch >= '0') && (ch <= '9')) ||
        (ch == '-')) {
        return 1u;
    }
    return 0u;
}

static unsigned int cstring_len(const char *text) {
    unsigned int len;

    len = 0u;
    while (text[len] != 0) {
        ++len;
    }
    return len;
}

static unsigned char html_at(unsigned int pos) {
    if (pos >= active_page_len) {
        return 0u;
    }
    if (active_page_ram != 0u) {
        return STORE_HTML[pos];
    }
    return (unsigned char)default_html[pos];
}

static unsigned char string_equals_ci(const char *left, const char *right) {
    unsigned char i;

    i = 0u;
    while ((left[i] != 0) && (right[i] != 0)) {
        if (ascii_equal_ci((unsigned char)left[i],
                           (unsigned char)right[i]) == 0u) {
            return 0u;
        }
        ++i;
    }
    return ((left[i] == 0) && (right[i] == 0));
}

static unsigned char string_contains_ci(const char *text, const char *needle) {
    unsigned char i;
    unsigned char j;

    if (needle[0] == 0) {
        return 1u;
    }
    i = 0u;
    while (text[i] != 0) {
        j = 0u;
        while ((needle[j] != 0) &&
               (text[(unsigned char)(i + j)] != 0) &&
               (ascii_equal_ci((unsigned char)text[(unsigned char)(i + j)],
                               (unsigned char)needle[j]) != 0u)) {
            ++j;
        }
        if (needle[j] == 0) {
            return 1u;
        }
        ++i;
    }
    return 0u;
}

static unsigned char buf_name_equals_lit(unsigned char start,
                                         unsigned char len,
                                         const char *lit) {
    unsigned char i;

    i = 0u;
    while ((i < len) && (lit[i] != 0)) {
        if (ascii_equal_ci(tag_buf[(unsigned char)(start + i)],
                           (unsigned char)lit[i]) == 0u) {
            return 0u;
        }
        ++i;
    }
    return ((i == len) && (lit[i] == 0));
}

static unsigned char selector_equals_lit(unsigned char len, const char *lit) {
    unsigned char i;

    i = 0u;
    while ((i < len) && (lit[i] != 0)) {
        if (ascii_equal_ci((unsigned char)selector_buf[i],
                           (unsigned char)lit[i]) == 0u) {
            return 0u;
        }
        ++i;
    }
    return ((i == len) && (lit[i] == 0));
}

static unsigned char tag_from_name(unsigned char start, unsigned char len) {
    if (buf_name_equals_lit(start, len, "html") != 0u) {
        return TAG_HTML;
    }
    if (buf_name_equals_lit(start, len, "head") != 0u) {
        return TAG_HEAD;
    }
    if (buf_name_equals_lit(start, len, "style") != 0u) {
        return TAG_STYLE;
    }
    if (buf_name_equals_lit(start, len, "script") != 0u) {
        return TAG_SCRIPT;
    }
    if (buf_name_equals_lit(start, len, "title") != 0u) {
        return TAG_TITLE;
    }
    if (buf_name_equals_lit(start, len, "body") != 0u) {
        return TAG_BODY;
    }
    if (buf_name_equals_lit(start, len, "main") != 0u) {
        return TAG_MAIN;
    }
    if (buf_name_equals_lit(start, len, "section") != 0u) {
        return TAG_SECTION;
    }
    if (buf_name_equals_lit(start, len, "article") != 0u) {
        return TAG_ARTICLE;
    }
    if (buf_name_equals_lit(start, len, "header") != 0u) {
        return TAG_HEADER;
    }
    if (buf_name_equals_lit(start, len, "footer") != 0u) {
        return TAG_FOOTER;
    }
    if (buf_name_equals_lit(start, len, "nav") != 0u) {
        return TAG_NAV;
    }
    if (buf_name_equals_lit(start, len, "div") != 0u) {
        return TAG_DIV;
    }
    if (buf_name_equals_lit(start, len, "p") != 0u) {
        return TAG_P;
    }
    if (buf_name_equals_lit(start, len, "h1") != 0u) {
        return TAG_H1;
    }
    if (buf_name_equals_lit(start, len, "h2") != 0u) {
        return TAG_H2;
    }
    if (buf_name_equals_lit(start, len, "h3") != 0u) {
        return TAG_H3;
    }
    if (buf_name_equals_lit(start, len, "span") != 0u) {
        return TAG_SPAN;
    }
    if (buf_name_equals_lit(start, len, "a") != 0u) {
        return TAG_A;
    }
    if (buf_name_equals_lit(start, len, "br") != 0u) {
        return TAG_BR;
    }
    if (buf_name_equals_lit(start, len, "hr") != 0u) {
        return TAG_HR;
    }
    if (buf_name_equals_lit(start, len, "ul") != 0u) {
        return TAG_UL;
    }
    if (buf_name_equals_lit(start, len, "ol") != 0u) {
        return TAG_OL;
    }
    if (buf_name_equals_lit(start, len, "li") != 0u) {
        return TAG_LI;
    }
    if (buf_name_equals_lit(start, len, "pre") != 0u) {
        return TAG_PRE;
    }
    if (buf_name_equals_lit(start, len, "code") != 0u) {
        return TAG_CODE;
    }
    if (buf_name_equals_lit(start, len, "blockquote") != 0u) {
        return TAG_BLOCKQUOTE;
    }
    if (buf_name_equals_lit(start, len, "img") != 0u) {
        return TAG_IMG;
    }
    if (buf_name_equals_lit(start, len, "meta") != 0u) {
        return TAG_META;
    }
    if (buf_name_equals_lit(start, len, "link") != 0u) {
        return TAG_LINK;
    }
    if (buf_name_equals_lit(start, len, "input") != 0u) {
        return TAG_INPUT;
    }
    return TAG_UNKNOWN;
}

static unsigned char selector_to_tag(void) {
    unsigned char i;
    unsigned char start;
    unsigned char len;

    i = 0u;
    while (ascii_space((unsigned char)selector_buf[i]) != 0u) {
        ++i;
    }
    if ((selector_buf[i] == '.') ||
        (selector_buf[i] == '#') ||
        (selector_buf[i] == '*')) {
        return TAG_UNKNOWN;
    }
    start = i;
    while (ident_char((unsigned char)selector_buf[i]) != 0u) {
        ++i;
    }
    len = (unsigned char)(i - start);
    if (len == 0u) {
        return TAG_UNKNOWN;
    }

    i = 0u;
    while (i < len) {
        tag_buf[i] = (unsigned char)selector_buf[(unsigned char)(start + i)];
        ++i;
    }
    return tag_from_name(0u, len);
}

static void style_clear(CssStyle *style) {
    style->display_none = 0u;
    style->before = 0u;
    style->after = 0u;
    style->indent = 0u;
    style->border = 0u;
    style->uppercase = 0u;
    style->pre = 0u;
    style->align = ALIGN_LEFT;
}

static void style_copy(CssStyle *dst, const CssStyle *src) {
    dst->display_none = src->display_none;
    dst->before = src->before;
    dst->after = src->after;
    dst->indent = src->indent;
    dst->border = src->border;
    dst->uppercase = src->uppercase;
    dst->pre = src->pre;
    dst->align = src->align;
}

static void css_default_rules(void) {
    unsigned char i;

    for (i = 0u; i < TAG_COUNT; ++i) {
        style_clear(&css_rules[i]);
    }
    css_rules[TAG_H1].before = 1u;
    css_rules[TAG_H1].after = 1u;
    css_rules[TAG_H1].uppercase = 1u;
    css_rules[TAG_H2].before = 1u;
    css_rules[TAG_H2].after = 1u;
    css_rules[TAG_H2].uppercase = 1u;
    css_rules[TAG_H3].before = 1u;
    css_rules[TAG_H3].after = 1u;
    css_rules[TAG_P].after = 1u;
    css_rules[TAG_DIV].after = 1u;
    css_rules[TAG_SECTION].after = 1u;
    css_rules[TAG_ARTICLE].after = 1u;
    css_rules[TAG_HEADER].after = 1u;
    css_rules[TAG_FOOTER].before = 1u;
    css_rules[TAG_NAV].after = 1u;
    css_rules[TAG_UL].after = 1u;
    css_rules[TAG_OL].after = 1u;
    css_rules[TAG_LI].indent = 2u;
    css_rules[TAG_PRE].before = 1u;
    css_rules[TAG_PRE].after = 1u;
    css_rules[TAG_PRE].pre = 1u;
    css_rules[TAG_PRE].border = 1u;
    css_rules[TAG_BLOCKQUOTE].before = 1u;
    css_rules[TAG_BLOCKQUOTE].after = 1u;
    css_rules[TAG_BLOCKQUOTE].indent = 2u;
}

static unsigned char css_first_number(const char *value) {
    unsigned char i;
    unsigned char number;

    i = 0u;
    while ((value[i] != 0) && (ascii_digit((unsigned char)value[i]) == 0u)) {
        ++i;
    }
    number = 0u;
    while ((value[i] != 0) && (ascii_digit((unsigned char)value[i]) != 0u)) {
        if (number < 25u) {
            number = (unsigned char)((number * 10u) +
                                     (unsigned char)(value[i] - '0'));
        }
        ++i;
    }
    return number;
}

static unsigned char css_cols(const char *value) {
    unsigned char number;
    unsigned char cols;

    number = css_first_number(value);
    if (number == 0u) {
        return 0u;
    }
    if (string_contains_ci(value, "em") != 0u) {
        cols = (unsigned char)(number << 1);
    } else {
        cols = (unsigned char)((number + 7u) >> 3);
    }
    if (cols > 20u) {
        return 20u;
    }
    return cols;
}

static unsigned char css_lines(const char *value) {
    unsigned char number;
    unsigned char lines;

    number = css_first_number(value);
    if (number == 0u) {
        return 0u;
    }
    if (string_contains_ci(value, "em") != 0u) {
        lines = number;
    } else {
        lines = (unsigned char)((number + 7u) >> 3);
    }
    if (lines > 4u) {
        return 4u;
    }
    return lines;
}

static void apply_css_decl(CssStyle *style) {
    if (string_equals_ci(prop_buf, "display") != 0u) {
        if (string_contains_ci(value_buf, "none") != 0u) {
            style->display_none = 1u;
        } else {
            style->display_none = 0u;
        }
    } else if ((string_equals_ci(prop_buf, "margin-left") != 0u) ||
               (string_equals_ci(prop_buf, "padding-left") != 0u) ||
               (string_equals_ci(prop_buf, "padding") != 0u)) {
        style->indent = css_cols(value_buf);
    } else if ((string_equals_ci(prop_buf, "margin-top") != 0u) ||
               (string_equals_ci(prop_buf, "padding-top") != 0u)) {
        style->before = css_lines(value_buf);
    } else if ((string_equals_ci(prop_buf, "margin-bottom") != 0u) ||
               (string_equals_ci(prop_buf, "padding-bottom") != 0u)) {
        style->after = css_lines(value_buf);
    } else if ((string_equals_ci(prop_buf, "border") != 0u) ||
               (string_equals_ci(prop_buf, "border-width") != 0u)) {
        if ((string_contains_ci(value_buf, "none") != 0u) ||
            (css_first_number(value_buf) == 0u)) {
            style->border = 0u;
        } else {
            style->border = 1u;
        }
    } else if (string_equals_ci(prop_buf, "font-weight") != 0u) {
        if (string_contains_ci(value_buf, "bold") != 0u) {
            style->uppercase = 1u;
        }
    } else if (string_equals_ci(prop_buf, "text-transform") != 0u) {
        if (string_contains_ci(value_buf, "uppercase") != 0u) {
            style->uppercase = 1u;
        } else if (string_contains_ci(value_buf, "none") != 0u) {
            style->uppercase = 0u;
        }
    } else if (string_equals_ci(prop_buf, "white-space") != 0u) {
        if (string_contains_ci(value_buf, "pre") != 0u) {
            style->pre = 1u;
        } else {
            style->pre = 0u;
        }
    } else if (string_equals_ci(prop_buf, "text-align") != 0u) {
        if (string_contains_ci(value_buf, "center") != 0u) {
            style->align = ALIGN_CENTER;
        } else if (string_contains_ci(value_buf, "right") != 0u) {
            style->align = ALIGN_RIGHT;
        } else {
            style->align = ALIGN_LEFT;
        }
    }
}

static void apply_css_declarations(CssStyle *style, const char *decls) {
    unsigned char pos;
    unsigned char len;

    pos = 0u;
    while (decls[pos] != 0) {
        while ((decls[pos] != 0) &&
               ((ascii_space((unsigned char)decls[pos]) != 0u) ||
                (decls[pos] == ';'))) {
            ++pos;
        }
        len = 0u;
        while ((decls[pos] != 0) &&
               (decls[pos] != ':') &&
               (decls[pos] != ';') &&
               (len < (PROP_BUF_SIZE - 1u))) {
            if (ascii_space((unsigned char)decls[pos]) == 0u) {
                prop_buf[len] = decls[pos];
                ++len;
            }
            ++pos;
        }
        prop_buf[len] = 0;

        if (decls[pos] != ':') {
            while ((decls[pos] != 0) && (decls[pos] != ';')) {
                ++pos;
            }
            continue;
        }
        ++pos;

        len = 0u;
        while ((decls[pos] != 0) &&
               (decls[pos] != ';') &&
               (len < (VALUE_BUF_SIZE - 1u))) {
            if ((len != 0u) ||
                (ascii_space((unsigned char)decls[pos]) == 0u)) {
                value_buf[len] = decls[pos];
                ++len;
            }
            ++pos;
        }
        while ((len != 0u) &&
               (ascii_space((unsigned char)value_buf[(unsigned char)(len - 1u)]) != 0u)) {
            --len;
        }
        value_buf[len] = 0;

        if (prop_buf[0] != 0) {
            apply_css_decl(style);
        }
        if (decls[pos] == ';') {
            ++pos;
        }
    }
}

static void copy_html_range(unsigned int start,
                            unsigned int end,
                            char *dest,
                            unsigned char cap) {
    unsigned char i;

    i = 0u;
    while ((start < end) && (i < (unsigned char)(cap - 1u))) {
        dest[i] = (char)html_at(start);
        ++i;
        ++start;
    }
    dest[i] = 0;
}

static void parse_css_range(unsigned int start, unsigned int end) {
    unsigned int pos;
    unsigned int selector_start;
    unsigned int selector_end;
    unsigned int decl_start;
    unsigned int decl_end;
    unsigned char tag;

    pos = start;
    while (pos < end) {
        while ((pos < end) && (ascii_space(html_at(pos)) != 0u)) {
            ++pos;
        }
        selector_start = pos;
        while ((pos < end) && (html_at(pos) != '{')) {
            ++pos;
        }
        if (pos >= end) {
            return;
        }
        selector_end = pos;
        ++pos;
        decl_start = pos;
        while ((pos < end) && (html_at(pos) != '}')) {
            ++pos;
        }
        decl_end = pos;
        if (pos < end) {
            ++pos;
        }

        copy_html_range(selector_start, selector_end,
                        selector_buf, SELECTOR_BUF_SIZE);
        tag = selector_to_tag();
        if (tag != TAG_UNKNOWN) {
            copy_html_range(decl_start, decl_end,
                            style_buf, STYLE_BUF_SIZE);
            apply_css_declarations(&css_rules[tag], style_buf);
        }
    }
}

static unsigned char read_tag_at(unsigned int pos, unsigned int *after_tag) {
    unsigned char i;
    unsigned char ch;

    if (html_at(pos) != '<') {
        return 0u;
    }
    ++pos;
    i = 0u;
    while ((pos < active_page_len) && (html_at(pos) != '>')) {
        ch = html_at(pos);
        if (i < (TAG_BUF_SIZE - 1u)) {
            tag_buf[i] = ch;
            ++i;
        }
        ++pos;
    }
    tag_buf[i] = 0u;
    if ((pos < active_page_len) && (html_at(pos) == '>')) {
        ++pos;
    }
    *after_tag = pos;
    return 1u;
}

static void tag_info(unsigned char *closing,
                     unsigned char *self_closing,
                     unsigned char *tag) {
    unsigned char i;
    unsigned char start;
    unsigned char len;
    unsigned char last;

    *closing = 0u;
    *self_closing = 0u;
    *tag = TAG_UNKNOWN;
    i = 0u;
    while (ascii_space(tag_buf[i]) != 0u) {
        ++i;
    }
    if ((tag_buf[i] == '!') || (tag_buf[i] == '?')) {
        *self_closing = 1u;
        return;
    }
    if (tag_buf[i] == '/') {
        *closing = 1u;
        ++i;
        while (ascii_space(tag_buf[i]) != 0u) {
            ++i;
        }
    }
    start = i;
    while (ident_char(tag_buf[i]) != 0u) {
        ++i;
    }
    len = (unsigned char)(i - start);
    if (len == 0u) {
        *self_closing = 1u;
        return;
    }
    *tag = tag_from_name(start, len);

    last = i;
    while (tag_buf[last] != 0u) {
        ++last;
    }
    while ((last != 0u) && (ascii_space(tag_buf[(unsigned char)(last - 1u)]) != 0u)) {
        --last;
    }
    if ((last != 0u) && (tag_buf[(unsigned char)(last - 1u)] == '/')) {
        *self_closing = 1u;
    }

    if ((*tag == TAG_BR) || (*tag == TAG_HR) || (*tag == TAG_IMG) ||
        (*tag == TAG_META) || (*tag == TAG_LINK) || (*tag == TAG_INPUT)) {
        *self_closing = 1u;
    }
}

static unsigned int find_raw_end_tag(unsigned int pos, unsigned char wanted_tag) {
    unsigned int after_tag;
    unsigned char closing;
    unsigned char self_closing;
    unsigned char tag;

    while (pos < active_page_len) {
        if (html_at(pos) == '<') {
            read_tag_at(pos, &after_tag);
            tag_info(&closing, &self_closing, &tag);
            if ((closing != 0u) && (tag == wanted_tag)) {
                return pos;
            }
            pos = after_tag;
        } else {
            ++pos;
        }
    }
    return active_page_len;
}

static void collect_css_rules(void) {
    unsigned int pos;
    unsigned int after_tag;
    unsigned int end_tag;
    unsigned char closing;
    unsigned char self_closing;
    unsigned char tag;

    css_default_rules();
    pos = 0u;
    while (pos < active_page_len) {
        if (html_at(pos) != '<') {
            ++pos;
            continue;
        }
        read_tag_at(pos, &after_tag);
        tag_info(&closing, &self_closing, &tag);
        if ((closing == 0u) && (tag == TAG_STYLE)) {
            end_tag = find_raw_end_tag(after_tag, TAG_STYLE);
            parse_css_range(after_tag, end_tag);
            pos = end_tag;
        } else {
            pos = after_tag;
        }
    }
}

static unsigned char attr_name_matches(unsigned char start,
                                       unsigned char len,
                                       const char *name) {
    unsigned char i;

    i = 0u;
    while ((i < len) && (name[i] != 0)) {
        if (ascii_equal_ci(tag_buf[(unsigned char)(start + i)],
                           (unsigned char)name[i]) == 0u) {
            return 0u;
        }
        ++i;
    }
    return ((i == len) && (name[i] == 0));
}

static unsigned char copy_attr_value(const char *name,
                                     char *dest,
                                     unsigned char cap) {
    unsigned char i;
    unsigned char attr_start;
    unsigned char attr_len;
    unsigned char out_len;
    unsigned char quote;

    i = 0u;
    dest[0] = 0;
    while (tag_buf[i] != 0u) {
        while ((tag_buf[i] != 0u) && (ascii_space(tag_buf[i]) != 0u)) {
            ++i;
        }
        attr_start = i;
        while ((tag_buf[i] != 0u) &&
               (tag_buf[i] != '=') &&
               (ascii_space(tag_buf[i]) == 0u)) {
            ++i;
        }
        attr_len = (unsigned char)(i - attr_start);
        while ((tag_buf[i] != 0u) && (ascii_space(tag_buf[i]) != 0u)) {
            ++i;
        }
        if (tag_buf[i] != '=') {
            continue;
        }
        ++i;
        while ((tag_buf[i] != 0u) && (ascii_space(tag_buf[i]) != 0u)) {
            ++i;
        }
        quote = 0u;
        if ((tag_buf[i] == '"') || (tag_buf[i] == '\'')) {
            quote = tag_buf[i];
            ++i;
        }
        if (attr_name_matches(attr_start, attr_len, name) != 0u) {
            out_len = 0u;
            while ((tag_buf[i] != 0u) &&
                   (((quote != 0u) && (tag_buf[i] != quote)) ||
                    ((quote == 0u) && (ascii_space(tag_buf[i]) == 0u))) &&
                   (out_len < (unsigned char)(cap - 1u))) {
                dest[out_len] = (char)tag_buf[i];
                ++out_len;
                ++i;
            }
            dest[out_len] = 0;
            return 1u;
        }
        while ((tag_buf[i] != 0u) &&
               (((quote != 0u) && (tag_buf[i] != quote)) ||
                ((quote == 0u) && (ascii_space(tag_buf[i]) == 0u)))) {
            ++i;
        }
        if ((quote != 0u) && (tag_buf[i] == quote)) {
            ++i;
        }
    }
    return 0u;
}

static unsigned char tag_is_block(unsigned char tag) {
    if ((tag == TAG_HTML) || (tag == TAG_BODY) || (tag == TAG_MAIN) ||
        (tag == TAG_SECTION) || (tag == TAG_ARTICLE) ||
        (tag == TAG_HEADER) || (tag == TAG_FOOTER) ||
        (tag == TAG_NAV) || (tag == TAG_DIV) || (tag == TAG_P) ||
        (tag == TAG_H1) || (tag == TAG_H2) || (tag == TAG_H3) ||
        (tag == TAG_UL) || (tag == TAG_OL) || (tag == TAG_LI) ||
        (tag == TAG_PRE) || (tag == TAG_BLOCKQUOTE)) {
        return 1u;
    }
    return 0u;
}

static unsigned char tag_hidden_by_default(unsigned char tag) {
    if ((tag == TAG_HEAD) || (tag == TAG_STYLE) || (tag == TAG_SCRIPT) ||
        (tag == TAG_TITLE) || (tag == TAG_META) || (tag == TAG_LINK)) {
        return 1u;
    }
    return 0u;
}

static void mark_content_row(void) {
    unsigned int rows;

    rows = (unsigned int)(out_y + 1u);
    if (content_rows < rows) {
        content_rows = rows;
    }
}

static void put_view_char(unsigned char ch) {
    unsigned int bottom;
    unsigned char row;
    unsigned char col;

    bottom = (unsigned int)(view_y + VIEW_ROWS);
    if ((out_y >= view_y) && (out_y < bottom) &&
        (out_x >= view_x) &&
        (out_x < (unsigned char)(view_x + VIEW_COLS))) {
        row = (unsigned char)(VIEW_FIRST_ROW + (unsigned char)(out_y - view_y));
        col = (unsigned char)(out_x - view_x);
        text_put(row, col, ch);
    }
}

static void line_break(unsigned char rows) {
    if (rows == 0u) {
        rows = 1u;
    }
    out_space_pending = 0u;
    while (rows != 0u) {
        ++out_y;
        --rows;
    }
    out_x = out_indent;
    out_started = 1u;
    mark_content_row();
}

static void emit_raw_char(unsigned char ch) {
    if (out_hidden != 0u) {
        return;
    }
    if (out_x >= DOC_COLS) {
        line_break(1u);
    }
    if (out_uppercase != 0u) {
        if ((ch >= 'a') && (ch <= 'z')) {
            ch = (unsigned char)(ch - ('a' - 'A'));
        }
    }
    if ((ch < 32u) || (ch > 126u)) {
        ch = '?';
    }
    put_view_char(ch);
    ++out_x;
    out_started = 1u;
    mark_content_row();
}

static void emit_text_char(unsigned char ch) {
    if (out_hidden != 0u) {
        return;
    }
    if (out_pre != 0u) {
        if (ch == '\r') {
            return;
        }
        if (ch == '\n') {
            line_break(1u);
            return;
        }
        emit_raw_char(ch);
        return;
    }
    if (ascii_space(ch) != 0u) {
        out_space_pending = 1u;
        return;
    }
    if ((out_space_pending != 0u) && (out_x > out_indent)) {
        emit_raw_char(' ');
    }
    out_space_pending = 0u;
    emit_raw_char(ch);
}

static void emit_literal(const char *text) {
    while (*text != 0) {
        emit_raw_char((unsigned char)*text);
        ++text;
    }
}

static void block_break_before(unsigned char extra_rows) {
    if (out_hidden != 0u) {
        return;
    }
    if ((out_started != 0u) && (out_x != out_indent)) {
        line_break(1u);
    }
    while (extra_rows != 0u) {
        line_break(1u);
        --extra_rows;
    }
}

static void emit_rule_line(void) {
    unsigned char col;

    block_break_before(0u);
    col = out_x;
    while (col < DOC_COLS) {
        emit_raw_char('-');
        ++col;
    }
}

static void start_block(unsigned char tag, const CssStyle *style) {
    unsigned char new_indent;

    block_break_before(style->before);

    if (style->border != 0u) {
        emit_rule_line();
        line_break(1u);
    }

    new_indent = (unsigned char)(out_indent + style->indent);
    if (tag != TAG_BODY) {
        if ((style->align == ALIGN_CENTER) && (new_indent < 16u)) {
            new_indent = 16u;
        } else if ((style->align == ALIGN_RIGHT) && (new_indent < 32u)) {
            new_indent = 32u;
        }
    }
    if (new_indent > (DOC_COLS - 2u)) {
        new_indent = (unsigned char)(DOC_COLS - 2u);
    }
    out_indent = new_indent;
    out_x = out_indent;

    if (tag == TAG_LI) {
        emit_literal("* ");
    }
}

static unsigned char pop_frame(void) {
    TagFrame frame;
    unsigned char tag;
    unsigned char ended_block;

    if (stack_depth == 0u) {
        return TAG_UNKNOWN;
    }
    --stack_depth;
    frame = style_stack[stack_depth];
    tag = frame.tag;
    ended_block = frame.started_block;

    if (frame.started_block != 0u) {
        if (out_x != out_indent) {
            line_break(1u);
        }
        out_indent = frame.old_indent;
        out_x = out_indent;
        if (frame.border != 0u) {
            emit_rule_line();
            line_break(1u);
        }
        while (frame.after != 0u) {
            line_break(1u);
            --frame.after;
        }
    }

    out_indent = frame.old_indent;
    out_uppercase = frame.old_uppercase;
    out_pre = frame.old_pre;
    out_hidden = frame.old_hidden;
    if (ended_block != 0u) {
        out_x = out_indent;
    } else if (out_x < out_indent) {
        out_x = out_indent;
    }
    return tag;
}

static void close_tag(unsigned char tag) {
    unsigned char popped;

    while (stack_depth != 0u) {
        popped = pop_frame();
        if ((popped == tag) || (tag == TAG_UNKNOWN)) {
            return;
        }
    }
}

static void push_frame(unsigned char tag,
                       unsigned char started_block,
                       const CssStyle *style,
                       unsigned char old_indent,
                       unsigned char old_uppercase,
                       unsigned char old_pre,
                       unsigned char old_hidden) {
    TagFrame *frame;

    if (stack_depth >= STYLE_STACK_DEPTH) {
        return;
    }
    frame = &style_stack[stack_depth];
    frame->tag = tag;
    frame->started_block = started_block;
    frame->old_indent = old_indent;
    frame->old_uppercase = old_uppercase;
    frame->old_pre = old_pre;
    frame->old_hidden = old_hidden;
    frame->border = style->border;
    frame->after = style->after;
    ++stack_depth;
}

static void emit_img_placeholder(void) {
    if (copy_attr_value("alt", style_buf, STYLE_BUF_SIZE) == 0u) {
        copy_attr_value("src", style_buf, STYLE_BUF_SIZE);
    }
    emit_literal("[IMG");
    if (style_buf[0] != 0) {
        emit_raw_char(' ');
        emit_literal(style_buf);
    }
    emit_raw_char(']');
}

static void start_tag(unsigned char tag, unsigned char self_closing) {
    CssStyle style;
    unsigned char old_indent;
    unsigned char old_uppercase;
    unsigned char old_pre;
    unsigned char old_hidden;
    unsigned char hidden_now;
    unsigned char started_block;

    if (tag == TAG_UNKNOWN) {
        return;
    }

    style_copy(&style, &css_rules[tag]);
    if (copy_attr_value("style", style_buf, STYLE_BUF_SIZE) != 0u) {
        apply_css_declarations(&style, style_buf);
    }

    if (tag == TAG_BR) {
        if (out_hidden == 0u) {
            line_break(1u);
        }
        return;
    }
    if (tag == TAG_HR) {
        if (out_hidden == 0u) {
            emit_rule_line();
            line_break(1u);
        }
        return;
    }
    if (tag == TAG_IMG) {
        if ((out_hidden == 0u) && (style.display_none == 0u)) {
            emit_img_placeholder();
        }
        return;
    }

    old_indent = out_indent;
    old_uppercase = out_uppercase;
    old_pre = out_pre;
    old_hidden = out_hidden;
    hidden_now = old_hidden;
    if ((style.display_none != 0u) || (tag_hidden_by_default(tag) != 0u)) {
        hidden_now = 1u;
    }

    started_block = 0u;
    if ((tag_is_block(tag) != 0u) &&
        (old_hidden == 0u) &&
        (hidden_now == 0u)) {
        start_block(tag, &style);
        started_block = 1u;
    }

    out_hidden = hidden_now;
    if (style.uppercase != 0u) {
        out_uppercase = 1u;
    }
    if (style.pre != 0u) {
        out_pre = 1u;
    }

    push_frame(tag,
               started_block,
               &style,
               old_indent,
               old_uppercase,
               old_pre,
               old_hidden);

    if (self_closing != 0u) {
        close_tag(tag);
    }
}

static unsigned char match_html_lit(unsigned int pos, const char *lit) {
    unsigned char i;

    i = 0u;
    while (lit[i] != 0) {
        if (ascii_equal_ci(html_at(pos), (unsigned char)lit[i]) == 0u) {
            return 0u;
        }
        ++pos;
        ++i;
    }
    return 1u;
}

static unsigned char emit_entity(unsigned int pos, unsigned int *next_pos) {
    if (match_html_lit(pos, "&amp;") != 0u) {
        emit_text_char('&');
        *next_pos = (unsigned int)(pos + 5u);
        return 1u;
    }
    if (match_html_lit(pos, "&lt;") != 0u) {
        emit_text_char('<');
        *next_pos = (unsigned int)(pos + 4u);
        return 1u;
    }
    if (match_html_lit(pos, "&gt;") != 0u) {
        emit_text_char('>');
        *next_pos = (unsigned int)(pos + 4u);
        return 1u;
    }
    if (match_html_lit(pos, "&quot;") != 0u) {
        emit_text_char('"');
        *next_pos = (unsigned int)(pos + 6u);
        return 1u;
    }
    if (match_html_lit(pos, "&nbsp;") != 0u) {
        emit_raw_char(' ');
        *next_pos = (unsigned int)(pos + 6u);
        return 1u;
    }
    return 0u;
}

static void renderer_reset(void) {
    stack_depth = 0u;
    out_x = 0u;
    out_y = 0u;
    out_indent = 0u;
    out_uppercase = 0u;
    out_pre = 0u;
    out_hidden = 0u;
    out_space_pending = 0u;
    out_started = 0u;
    content_rows = 1u;
}

static void draw_header(void) {
    text_clear_row(0u);
    text_write(0u, 0u, "BROWSER ");
    if (active_page_ram != 0u) {
        text_write(0u, 8u, "RAM");
    } else {
        text_write(0u, 8u, "DEMO");
    }
}

static void draw_status(void) {
    text_clear_row(15u);
    text_put(15u, 0u, 'X');
    text_write_u16_2(15u, 1u, view_x);
    text_write(15u, 3u, " Y");
    text_write_u16_3(15u, 5u, view_y);
    text_write(15u, 8u, " C HELP");
}

static void render_page(void) {
    unsigned int pos;
    unsigned int after_tag;
    unsigned int next_pos;
    unsigned char closing;
    unsigned char self_closing;
    unsigned char tag;
    unsigned char ch;

    glic_prepare_screen(GLIC_BLACK);
    draw_header();
    renderer_reset();

    pos = 0u;
    while (pos < active_page_len) {
        ch = html_at(pos);
        if (ch == '<') {
            if (read_tag_at(pos, &after_tag) == 0u) {
                ++pos;
                continue;
            }
            tag_info(&closing, &self_closing, &tag);
            if (closing != 0u) {
                close_tag(tag);
            } else {
                start_tag(tag, self_closing);
            }
            pos = after_tag;
        } else if (ch == '&') {
            if (emit_entity(pos, &next_pos) != 0u) {
                pos = next_pos;
            } else {
                emit_text_char(ch);
                ++pos;
            }
        } else {
            emit_text_char(ch);
            ++pos;
        }
    }
    while (stack_depth != 0u) {
        pop_frame();
    }

    draw_status();
}

static void select_active_page(void) {
    unsigned int len;

    active_page_ram = 0u;
    active_page_len = cstring_len(default_html);

    if ((BROWSER_STORE[0] == STORE_MAGIC0) &&
        (BROWSER_STORE[1] == STORE_MAGIC1) &&
        (BROWSER_STORE[2] == STORE_MAGIC2) &&
        (BROWSER_STORE[3] == STORE_MAGIC3)) {
        len = BROWSER_STORE[STORE_LEN_LO];
        len |= ((unsigned int)BROWSER_STORE[STORE_LEN_HI]) << 8;
        if ((len != 0u) && (len <= STORE_CAPACITY)) {
            active_page_ram = 1u;
            active_page_len = len;
        }
    }

    collect_css_rules();
}

static void show_help(void) {
    unsigned char buttons;
    unsigned char last_buttons;

    glic_prepare_screen(GLIC_BLACK);
    text_write(0u, 1u, "MINI BROWSER");
    text_write(2u, 0u, "UP/DN SCROLL Y");
    text_write(3u, 0u, "LEFT/RIGHT X");
    text_write(4u, 0u, "A/B PAGE DN/UP");
    text_write(5u, 0u, "CENTER HOME");
    text_write(7u, 0u, "HTML AT 0x6006");
    text_write(8u, 0u, "HEADER HTM1 LEN");
    text_write(10u, 0u, "TAGS CSS SUBSET");
    text_write(13u, 0u, "C/CENTER BACK");

    wait_for_release();
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

static void page_down(void) {
    if (view_y > (unsigned int)(MAX_SCROLL_Y - VIEW_ROWS)) {
        view_y = MAX_SCROLL_Y;
    } else {
        view_y = (unsigned int)(view_y + VIEW_ROWS);
    }
}

static void page_up(void) {
    if (view_y < VIEW_ROWS) {
        view_y = 0u;
    } else {
        view_y = (unsigned int)(view_y - VIEW_ROWS);
    }
}

static void run_browser(void) {
    unsigned char buttons;
    unsigned char last_buttons;
    unsigned char changed;
    unsigned char max_x;

    select_active_page();
    view_x = 0u;
    view_y = 0u;
    max_x = (unsigned char)(DOC_COLS - VIEW_COLS);
    render_page();
    wait_for_release();
    last_buttons = GLIC_BUTTONS_NONE;

    while (1) {
        changed = 0u;
        buttons = glic_read_buttons();
        if (button_edge(buttons, last_buttons, GLIC_BTN_UP) != 0u) {
            if (view_y != 0u) {
                --view_y;
                changed = 1u;
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_DOWN) != 0u) {
            if (view_y < MAX_SCROLL_Y) {
                ++view_y;
                changed = 1u;
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_LEFT) != 0u) {
            if (view_x < H_SCROLL_STEP) {
                if (view_x != 0u) {
                    view_x = 0u;
                    changed = 1u;
                }
            } else {
                view_x = (unsigned char)(view_x - H_SCROLL_STEP);
                changed = 1u;
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_RIGHT) != 0u) {
            if (view_x < max_x) {
                view_x = (unsigned char)(view_x + H_SCROLL_STEP);
                if (view_x > max_x) {
                    view_x = max_x;
                }
                changed = 1u;
            }
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_A) != 0u) {
            page_down();
            changed = 1u;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_B) != 0u) {
            page_up();
            changed = 1u;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_CENTER) != 0u) {
            view_x = 0u;
            view_y = 0u;
            changed = 1u;
        } else if (button_edge(buttons, last_buttons, GLIC_BTN_C) != 0u) {
            show_help();
            changed = 1u;
        }

        if (changed != 0u) {
            render_page();
        }
        last_buttons = buttons;
        glic_delay(LOOP_DELAY);
    }
}

void main(void) {
    run_browser();
}
