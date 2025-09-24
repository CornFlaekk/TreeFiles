#include <thread>
#include <chrono>
#include <string>
#include "ui_utils.h"
#include <array>
#include <tuple>

void draw_terminal_border() {
    box(stdscr, 0, 0);
}

void print_directory_entries(const std::vector<EntryInfo>& entries, int selected, int scroll_offset, int visible_rows, int start_row, int start_col) {
    if (entries.empty()) return;

    int cols = getmaxx(stdscr);

    // Precalcula para cada entrada el tamaño total de su padre inmediato
    std::vector<std::uintmax_t> parent_sizes(entries.size(), 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        int my_depth = entries[i].depth;
        if (my_depth == 0) {
            for (const auto& e : entries) {
                if (e.depth == 0) parent_sizes[i] += e.size;
            }
        } else {
            for (int j = i - 1; j >= 0; --j) {
                if (entries[j].depth == my_depth - 1) {
                    for (size_t k = j + 1; k < entries.size() && entries[k].depth >= my_depth; ++k) {
                        if (entries[k].depth == my_depth)
                            parent_sizes[i] += entries[k].size;
                        if (entries[k].depth < my_depth) break;
                    }
                    break;
                }
            }
        }
        if (parent_sizes[i] == 0) parent_sizes[i] = 1;
    }

    for (int i = 0; i < visible_rows; ++i) {
        int idx = scroll_offset + i;
        if (idx >= (int)entries.size()) break;
        const auto& e = entries[idx];

        std::string indent(e.depth * 2, ' ');
        int indent_width = indent.length();

        double percent = std::min(1.0, (double)e.size / parent_sizes[idx]);
        int bar_width = std::max(1, (int)((cols - start_col - indent_width - 2) * percent));

        int bar_row = start_row + i;
        int bar_col = start_col + indent_width;

        // 1. Prepara el texto
        std::string size_str = human_readable_size(e.size);
        std::string entry_text = e.type + " " + e.name + "  " + size_str;
        std::string full_text = indent + entry_text;

        attron(COLOR_PAIR(1));
        mvprintw(bar_row, start_col, "%s", indent.c_str());
        attroff(COLOR_PAIR(1));
        attron(COLOR_PAIR(2));
        for (int b = 0; b < bar_width; ++b) {
            mvaddch(bar_row, bar_col + b, ' ');
        }
        attroff(COLOR_PAIR(2));

        if (idx == selected) attron(A_REVERSE);
        for (size_t c = 0; c < full_text.size() && (start_col + (int)c) < cols - 1; ++c) {
            int col = start_col + c;
            // Si el carácter está en la indentación, fondo por defecto
            if ((int)c < indent_width) {
                attron(COLOR_PAIR(1));
                mvaddch(bar_row, col, full_text[c]);
                attroff(COLOR_PAIR(1));
            }
            // Si el carácter está sobre la barra, fondo cyan
            else if ((col - start_col - indent_width) < bar_width) {
                attron(COLOR_PAIR(2));
                mvaddch(bar_row, col, full_text[c]);
                attroff(COLOR_PAIR(2));
            }
            // Si el carácter está fuera de la barra, fondo por defecto
            else {
                attron(COLOR_PAIR(1));
                mvaddch(bar_row, col, full_text[c]);
                attroff(COLOR_PAIR(1));
            }
        }
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

std::pair<int, int> bar_color_selection_popup() {
    static const std::array<const char*, 8> color_names = {
        "Negro", "Rojo", "Verde", "Amarillo", "Azul", "Magenta", "Cyan", "Blanco"
    };
    int selected_bg = 0;
    int selected_fg = 0;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // Selección de fondo
    int win_height = 12, win_width = 28;
    int starty = (rows - win_height) / 2;
    int startx = (cols - win_width) / 2;
    WINDOW* win = newwin(win_height, win_width, starty, startx);
    keypad(win, TRUE);

    // Selección de fondo
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Color de FONDO barra:");
    while (true) {
        for (int i = 0; i < (int)color_names.size(); ++i) {
            if (i == selected_bg) {
                wattron(win, A_REVERSE);
                mvwprintw(win, 3 + i, 4, "%s", color_names[i]);
                wattroff(win, A_REVERSE);
            } else {
                mvwprintw(win, 3 + i, 4, "%s", color_names[i]);
            }
        }
        wrefresh(win);
        int ch = wgetch(win);
        if (ch == KEY_UP && selected_bg > 0) selected_bg--;
        else if (ch == KEY_DOWN && selected_bg < (int)color_names.size() - 1) selected_bg++;
        else if (ch == '\n' || ch == KEY_ENTER) break;
        else if (ch == 27) { delwin(win); touchwin(stdscr); refresh(); return {-1, -1}; }
    }

    // Selección de texto
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Color de TEXTO barra:");
    while (true) {
        for (int i = 0; i < (int)color_names.size(); ++i) {
            if (i == selected_fg) {
                wattron(win, A_REVERSE);
                mvwprintw(win, 3 + i, 4, "%s", color_names[i]);
                wattroff(win, A_REVERSE);
            } else {
                mvwprintw(win, 3 + i, 4, "%s", color_names[i]);
            }
        }
        wrefresh(win);
        int ch = wgetch(win);
        if (ch == KEY_UP && selected_fg > 0) selected_fg--;
        else if (ch == KEY_DOWN && selected_fg < (int)color_names.size() - 1) selected_fg++;
        else if (ch == '\n' || ch == KEY_ENTER) break;
        else if (ch == 27) { delwin(win); touchwin(stdscr); refresh(); return {-1, -1}; }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return {selected_fg, selected_bg};
}

void draw_help_box(int rows, int cols, bool show) {
    int box_height = show ? 5 : 1;
    int start_row = rows - box_height;
    WINDOW* help_win = newwin(box_height, cols, start_row, 0);
    box(help_win, 0, 0);
    if (show) {
        mvwprintw(help_win, 1, 2, "^H Ayuda  |  Flechas: Mover  |  E: Expandir/Colapsar  |  Espacio: Abrir  |  SUPR: Borrar");
        mvwprintw(help_win, 2, 2, "Y/N/Enter: Confirmar  |  Q: Salir");
        mvwprintw(help_win, 3, 2, "Ctrl+H: Ocultar ayuda");
        mvwprintw(help_win, 4, 2, "B: Cambiar color de barra");
    } else {
        mvwprintw(help_win, 0, 2, "Ctrl+H: Mostrar ayuda");
    }
    wrefresh(help_win);
    delwin(help_win);
}

std::string format_scan_time(double ms) {
    if (ms < 1000.0) {
        return std::to_string((int)ms) + " ms";
    } else {
        double secs = ms / 1000.0;
        if (secs < 60.0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f s", secs);
            return std::string(buf);
        } else {
            int min = (int)(secs / 60.0);
            double rem_secs = secs - min * 60.0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%dm %ds", min, (int)rem_secs);
            return std::string(buf);
        }
    }
}

void show_loading_animation(std::atomic<bool>& loading, std::atomic<bool>& started) {
    const char* frames[] = {
        "ooxooxoxx",
        "oxxooxoox",
        "xxxooxooo",
        "xxxxooooo",
        "xxoxooxoo",
        "xooxooxxo",
        "oooxooxxx",
        "oooooxxxx"
    };
    int num_frames = sizeof(frames) / sizeof(frames[0]);
    int frame = 0;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_height = 7, win_width = 13;
    int starty = (rows - win_height) / 2;
    int startx = (cols - win_width) / 2;
    int delay = 120; // ms por frame
    int waited = 0;
    while (loading && waited < 500) { // Espera hasta 500ms antes de mostrar
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        waited += delay;
    }
    if (!loading) return;
    started = true;
    WINDOW* win = newwin(win_height, win_width, starty, startx);
    box(win, 0, 0);
    mvwprintw(win, 1, 3, "Loading");
    wrefresh(win);
    while (loading) {
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                char c = frames[frame][y*3 + x];
                chtype ch = (c == 'x') ? ACS_DIAMOND : ' ';
                mvwaddch(win, 3 + y, 4 + x * 2, ch); // Espacio entre iconos
                mvwaddch(win, 3 + y, 4 + x * 2 + 1, ' '); // Espacio extra
            }
        }
        wrefresh(win);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        frame = (frame + 1) % num_frames;
    }
    delwin(win);
}
