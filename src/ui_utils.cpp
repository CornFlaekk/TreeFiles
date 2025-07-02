#include "ui_utils.h"

void draw_terminal_border() {
    box(stdscr, 0, 0);
}

void print_directory_entries(const std::vector<EntryInfo>& entries, int selected, int scroll_offset, int visible_rows, int start_row, int start_col) {
    for (int i = 0; i < visible_rows; ++i) {
        int idx = scroll_offset + i;
        if (idx >= (int)entries.size()) break;
        const auto& e = entries[idx];
        std::string size_str = human_readable_size(e.size);
        std::string indent(e.depth * 2, ' ');
        if (idx == selected) attron(A_REVERSE);
        mvprintw(start_row + i, start_col, "%s%s %s  %s", indent.c_str(), e.type.c_str(), e.name.c_str(), size_str.c_str());
        if (idx == selected) attroff(A_REVERSE);
    }
}