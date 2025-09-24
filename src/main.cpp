#include <ncurses.h>
#include <ui_utils.h>
#include <file_utils.h>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <set>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

int main() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);

    int bar_bg = COLOR_YELLOW;
    int bar_fg = COLOR_BLACK;
    
    if (has_colors()) {
        start_color();
        use_default_colors();
        // Texto blanco sobre fondo terminal
        init_pair(1, COLOR_WHITE, -1);
        // Texto blanco sobre fondo cyan
        init_pair(2, bar_fg, bar_bg);
    }
    
    endwin();
    refresh();

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
    double last_scan_ms = 0.0;
    std::atomic<bool> loading(false);
    std::atomic<bool> anim_started(false);

    std::vector<EntryInfo> entries;
    bool need_refresh = true;
    while (running) {
        clear();
        draw_terminal_border();
        getmaxyx(stdscr, rows, cols);
        visible_rows = rows - (show_help ? 6 : 3);
        if (need_refresh) {
            loading = true;
            anim_started = false;
            std::thread loader([&]() {
                entries.clear();
                auto t0 = std::chrono::high_resolution_clock::now();
                build_tree_entries(current_path, expanded_dirs, entries, 0);
                auto t1 = std::chrono::high_resolution_clock::now();
                last_scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                loading = false;
            });
            int waited = 0;
            int anim_delay = 120;
            while (loading && waited < 500) {
                std::this_thread::sleep_for(std::chrono::milliseconds(anim_delay));
                waited += anim_delay;
            }
            std::thread anim;
            if (loading) {
                anim = std::thread([&]() {
                    show_loading_animation(loading, anim_started);
                });
            }
            loader.join();
            loading = false;
            if (anim.joinable()) anim.join();
            if (anim_started) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            need_refresh = false;
        }
        // Redibujar la UI principal y la ayuda tras la animación
        clear();
        draw_terminal_border();
        print_directory_entries(entries, selected, scroll_offset, visible_rows, 1, 2);
        mvprintw(0, 2, "Flechas: mover | E: expandir/colapsar | Espacio: abrir | q: salir");
        std::string scan_str = format_scan_time(last_scan_ms);
        mvprintw(rows-1, cols-15, "Scan: %s", scan_str.c_str());
        draw_help_box(rows, cols, show_help);
        refresh();
        int n = entries.size();
        if (n == 0) selected = 0;
        else if (selected >= n) selected = n - 1;
        input = getch();
        switch (input) {
            case 'q':
            case 'Q':
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
                    need_refresh = true;
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
                        need_refresh = true;
                    }
                }
                break;
            case 8: // Ctrl+H
                show_help = !show_help;
                break;
            case 'b': // Cambiar color de barra
                if (has_colors()) {
                    auto [fg, bg] = bar_color_selection_popup();
                    if (fg >= 0 && bg >= 0) {
                        bar_fg = fg;
                        bar_bg = bg;
                        init_pair(2, bar_fg, bar_bg);
                    }
                } else {
                    confirm_popup("Colores no soportados en esta terminal.");
                }
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