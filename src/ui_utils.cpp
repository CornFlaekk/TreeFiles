#include "ui_utils.h"

void draw_terminal_border() {
    box(stdscr, 0, 0);
}

void print_directory_entries(const std::vector<EntryInfo>& entries, int selected, int start_row, int start_col) {
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::string size_str = human_readable_size(e.size);
        if ((int)i == selected) {
            attron(A_REVERSE);
        }
        mvprintw(start_row + i, start_col, "%s %s  %s", e.type.c_str(), e.name.c_str(), size_str.c_str());
        if ((int)i == selected) {
            attroff(A_REVERSE);
        }
    }
}