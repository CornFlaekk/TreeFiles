#include <ncurses.h>
#include <ui_utils.h>
#include <file_utils.h>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <set>
#include <cstdio>

int main() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);

    bool running = true;
    int input;
    int selected = 0;
    int scroll_offset = 0;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int visible_rows = rows - 2; // 1 para borde superior, 1 para borde inferior
    bool show_help = true;

    std::filesystem::path current_path = ".";
    auto& expanded_dirs = get_expanded_dirs();

    while (running) {
        clear();

        draw_terminal_border();

        getmaxyx(stdscr, rows, cols);

        visible_rows = rows - (show_help ? 6 : 3); // Ajusta para no tapar la ayuda

        std::vector<EntryInfo> entries;
        build_tree_entries(current_path, expanded_dirs, entries, 0);

        int n = entries.size();
        if (n == 0) selected = 0;
        else if (selected >= n) selected = n - 1;

        print_directory_entries(entries, selected, scroll_offset, visible_rows, 1, 2);

        mvprintw(0, 2, "Flechas: mover | E: expandir/colapsar | Espacio: abrir | q: salir");

        refresh();

        draw_help_box(rows, cols, show_help);

        input = getch();

        switch (input) {
            case 'q':
                running = false;
                break;
            case KEY_UP:
                if (selected > 0) selected--;
                break;
            case KEY_DOWN:
                if (selected < n - 1) selected++;
                break;
            case ' ': // Espacio: abrir con xdg-open
                if (!entries.empty()) {
                    std::string full_path = entries[selected].full_path.string();
                    std::string cmd = "xdg-open \"" + full_path + "\" > /dev/null 2>&1 &";
                    system(cmd.c_str());
                }
                break;
            case 'e':
            case 'E':
                if (!entries.empty() && entries[selected].type == "[DIR] ") {
                    auto dir_path = entries[selected].full_path;
                    if (expanded_dirs.count(dir_path)) {
                        expanded_dirs.erase(dir_path);
                    } else {
                        expanded_dirs.insert(dir_path);
                    }
                }
                break;
            case KEY_DC: // SUPR
            if (!entries.empty()) {
                const auto& entry = entries[selected];
                std::string msg = "Delete \"" + entry.name + "\"?";
                if (confirm_popup(msg)) {
                    try {
                        if (entry.type == "[DIR] ") {
                            std::filesystem::remove_all(entry.full_path);
                        } else {
                            std::filesystem::remove(entry.full_path);
                        }
                        clear_dir_size_cache();
                    } catch (const std::exception& ex) {
                        confirm_popup(std::string("Error: ") + ex.what());
                    }
                }
            }
            break;
            case 8: // Ctrl+H
                show_help = !show_help;
                break;
        }

        if (selected < scroll_offset) {
            scroll_offset = selected;
        } else if (selected >= scroll_offset + visible_rows) {
            scroll_offset = selected - visible_rows + 1;
        }
    }

    endwin();
    return 0;
}