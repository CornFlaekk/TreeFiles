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

bool confirm_popup(const std::string& message) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_height = 7, win_width = std::max((int)message.size() + 10, 28);
    int starty = (rows - win_height) / 2;
    int startx = (cols - win_width) / 2;
    WINDOW* win = newwin(win_height, win_width, starty, startx);
    keypad(win, TRUE);
    box(win, 0, 0);
    mvwprintw(win, 2, 2, "%s", message.c_str());

    const char* options[2] = {" Yes ", " No "};
    int selected = 0; // 0 = Yes, 1 = No

    while (true) {
        // Draw options
        for (int i = 0; i < 2; ++i) {
            int opt_x = (win_width / 2) - 8 + i * 10;
            if (i == selected) {
                wattron(win, A_REVERSE);
                mvwprintw(win, 4, opt_x, "%s", options[i]);
                wattroff(win, A_REVERSE);
            } else {
                mvwprintw(win, 4, opt_x, "%s", options[i]);
            }
        }
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == KEY_LEFT || ch == '\t') {
            selected = (selected + 1) % 2; // Toggle
        } else if (ch == KEY_RIGHT) {
            selected = (selected + 1) % 2; // Toggle
        } else if (ch == '\n' || ch == KEY_ENTER) {
            delwin(win);
            touchwin(stdscr);
            refresh();
            return selected == 0;
        } else if (ch == 'y' || ch == 'Y') {
            delwin(win);
            touchwin(stdscr);
            refresh();
            return true;
        } else if (ch == 'n' || ch == 'N') {
            delwin(win);
            touchwin(stdscr);
            refresh();
            return false;
        }
    }
}

void draw_help_box(int rows, int cols, bool show) {
    int box_height = show ? 5 : 1;
    int start_row = rows - box_height;
    WINDOW* help_win = newwin(box_height, cols, start_row, 0);
    box(help_win, 0, 0);
    if (show) {
        mvwprintw(help_win, 1, 2, "^H Ayuda  |  Flechas: Mover  |  E: Expandir/Colapsar  |  Espacio: Abrir  |  SUPR: Borrar");
        mvwprintw(help_win, 2, 2, "Y/N/Enter: Confirmar  |  q: Salir");
        mvwprintw(help_win, 3, 2, "Ctrl+H: Ocultar ayuda");
    } else {
        mvwprintw(help_win, 0, 2, "Ctrl+H: Mostrar ayuda");
    }
    wrefresh(help_win);
    delwin(help_win);
}