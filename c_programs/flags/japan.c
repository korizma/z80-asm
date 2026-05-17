#include "../common/glic80.h"

void main(void) {
    glic_prepare_screen(GLIC_WHITE);

    glic_fill_page_run(4u, 56u, 16u, GLIC_BLACK);
    glic_fill_page_run(5u, 48u, 32u, GLIC_BLACK);
    glic_fill_page_run(6u, 42u, 44u, GLIC_BLACK);
    glic_fill_page_run(7u, 38u, 52u, GLIC_BLACK);
    glic_fill_page_run(8u, 38u, 52u, GLIC_BLACK);
    glic_fill_page_run(9u, 42u, 44u, GLIC_BLACK);
    glic_fill_page_run(10u, 48u, 32u, GLIC_BLACK);
    glic_fill_page_run(11u, 56u, 16u, GLIC_BLACK);
}
