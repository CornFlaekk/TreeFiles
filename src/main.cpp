#include <clocale>
#include <ncurses.h>
#include <ui_utils.h>
#include <file_utils.h>
#include "i18n.h"
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

static void headless_dump_frame(const std::vector<EntryInfo>& entries, int selected,
                                int scroll_offset, int visible_rows,
                                const std::filesystem::path& current_path,
                                const std::set<std::filesystem::path>& expanded_dirs,
                                double last_scan_ms,
                                int bar_fg, int bar_bg, int frame_num,
                                int total_pages, int current_page) {
    std::cout << "=== FRAME " << frame_num << " ===" << std::endl;
    std::cout << "current_path: " << current_path.string() << std::endl;
    std::cout << "selected_index: " << selected << std::endl;
    std::cout << "scroll_offset: " << scroll_offset << std::endl;
    std::cout << "visible_rows: " << visible_rows << std::endl;
    std::cout << "total_entries: " << entries.size() << std::endl;
    if (total_pages > 1) {
        std::cout << "pagination: page " << (current_page + 1) << " of " << total_pages << std::endl;
    }

    std::cout << "expanded_dirs: {";
    bool first_dir = true;
    for (const auto& d : expanded_dirs) {
        if (!first_dir) std::cout << ", ";
        std::cout << d.string();
        first_dir = false;
    }
    std::cout << "}" << std::endl;

    std::cout << "last_scan_ms: " << last_scan_ms << std::endl;
    std::cout << "bar_fg: " << bar_fg << std::endl;
    std::cout << "bar_bg: " << bar_bg << std::endl;

    std::vector<std::uintmax_t> parent_sizes(entries.size(), 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        int my_depth = entries[i].depth;
        if (my_depth == 0) {
            for (const auto& e : entries) {
                if (e.depth == 0) parent_sizes[i] += e.size;
            }
        } else {
            for (int j = (int)i - 1; j >= 0; --j) {
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

    std::cout << "entries:" << std::endl;
    const int BAR_WIDTH = 40;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        bool is_nav = (e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]");
        std::string indent(e.depth * 2, ' ');
        std::string marker = ((int)i == selected) ? ">>>" : "   ";

        if (is_nav) {
            std::cout << "  " << i << ": " << marker << " " << indent
                      << "--- " << e.name << " ---" << std::endl;
        } else {
            double percent = std::min(1.0, (double)e.size / parent_sizes[i]);
            int filled = (int)(BAR_WIDTH * percent);
            int unfilled = BAR_WIDTH - filled;

            std::string size_str = human_readable_size(e.size);
            char pct_buf[16];
            snprintf(pct_buf, sizeof(pct_buf), "%3d%%", (int)(percent * 100));

            std::string bar;
            for (int b = 0; b < filled; ++b) bar += "\u2588";
            for (int b = 0; b < unfilled; ++b) bar += "\u2591";

            std::cout << "  " << i << ": " << marker << " " << indent
                      << e.type << " " << e.name
                      << "  " << size_str << "  (" << pct_buf << ") " << bar << std::endl;
        }
    }
    std::cout << "=== END FRAME ===" << std::endl;
}

static int parse_headless_event(const std::string& line) {
    if (line == "UP")          return KEY_UP;
    if (line == "DOWN")        return KEY_DOWN;
    if (line == "LEFT")        return KEY_LEFT;
    if (line == "RIGHT")       return KEY_RIGHT;
    if (line == "SPACE")       return ' ';
    if (line == "DELETE")      return KEY_DC;
    if (line == "CTRL_H")      return 8;
    if (line == "ENTER")       return '\n';
    if (line.size() == 1)      return line[0];
    return -1;
}

static void clamp_and_skip_selection(const std::vector<EntryInfo>& entries, int& selected,
                                      std::filesystem::path& select_first_owner,
                                      std::filesystem::path& select_last_owner) {
    int n = (int)entries.size();
    if (!select_first_owner.empty()) {
        selected = 0;
        for (int i = 0; i < n; ++i) {
            if (entries[i].type == "[RESTO_PREV]" && entries[i].full_path == select_first_owner) {
                selected = i + 1;
                break;
            }
        }
        select_first_owner.clear();
    }
    if (!select_last_owner.empty()) {
        selected = n - 1;
        for (int i = n - 1; i >= 0; --i) {
            if (entries[i].type == "[RESTO_NEXT]" && entries[i].full_path == select_last_owner) {
                selected = i - 1;
                break;
            }
        }
        select_last_owner.clear();
    }
    if (n == 0) selected = 0;
    else if (selected >= n) selected = n - 1;
}

static std::filesystem::path find_pagination_owner(const std::vector<EntryInfo>& entries,
                                                     int selected,
                                                     const std::filesystem::path& root) {
    if (entries.empty() || selected < 0 || selected >= (int)entries.size())
        return root;

    const auto& e = entries[selected];

    if (e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]")
        return e.full_path;
    if (e.type == "[DIR] ")
        return e.full_path;

    int sel_depth = e.depth;
    for (int i = selected - 1; i >= 0; --i) {
        if (entries[i].depth < sel_depth)
            return entries[i].full_path;
    }

    return root;
}

static void update_scroll(int selected, int& scroll_offset, int visible_rows) {
    if (selected < scroll_offset) {
        scroll_offset = selected;
    } else if (selected >= scroll_offset + visible_rows) {
        scroll_offset = selected - visible_rows + 1;
    }
}

int main(int argc, char* argv[]) {
    bool headless = false;
    std::filesystem::path start_path = ".";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg[0] != '-') {
            start_path = arg;
        }
    }

    // ==================== HEADLESS MODE ====================
    if (headless) {
        bool running = true;
        int selected = 0;
        int scroll_offset = 0;
        int visible_rows = 30;

        int bar_bg = COLOR_YELLOW;
        int bar_fg = COLOR_BLACK;

        std::filesystem::path current_path = start_path;
        auto& expanded_dirs = get_expanded_dirs();
        double last_scan_ms = 0.0;

        std::vector<EntryInfo> entries;
        bool need_refresh = true;
        std::filesystem::path select_first_owner;
        std::filesystem::path select_last_owner;
        int frame_num = 0;

        auto rebuild_tree = [&]() {
            entries.clear();
            auto t0 = std::chrono::high_resolution_clock::now();
            build_tree_entries(current_path, expanded_dirs, entries, 0, 30);
            auto t1 = std::chrono::high_resolution_clock::now();
            last_scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            need_refresh = false;
        };

        auto get_page_info = [&](int& total_pages, int& current_page) {
            current_page = ::get_current_page(current_path);
            total_pages = 1;
            for (const auto& e : entries) {
                if ((e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]") && e.full_path == current_path) {
                    std::string label = e.name;
                    auto paren = label.rfind('(');
                    auto slash = label.rfind('/');
                    if (paren != std::string::npos && slash != std::string::npos && slash > paren) {
                        total_pages = std::stoi(label.substr(paren + 1, slash - paren - 1));
                    }
                    break;
                }
            }
        };

        rebuild_tree();
        int total_pages = 1, current_page = 0;
        get_page_info(total_pages, current_page);
        headless_dump_frame(entries, selected, scroll_offset, visible_rows,
                            current_path, expanded_dirs, last_scan_ms,
                            bar_fg, bar_bg, frame_num, total_pages, current_page);
        frame_num++;

        std::string event_line;
        while (running && std::getline(std::cin, event_line)) {
            if (event_line.empty() || event_line[0] == '#')
                continue;

            int input = parse_headless_event(event_line);
            if (input == -1)
                continue;

            if (need_refresh)
                rebuild_tree();

            bool popup_handled = false;

            switch (input) {
            case 'q':
            case 'Q':
                running = false;
                break;
            case KEY_UP:
                if (selected > 0) selected--;
                break;
            case KEY_DOWN: {
                int n = (int)entries.size();
                if (selected < n - 1) selected++;
                break;
            }
            case ' ':
                if (!entries.empty()) {
                    std::cout << "=== ACTION open ===" << std::endl;
                    std::cout << "file: " << entries[selected].full_path.string() << std::endl;
                    std::cout << "=== END ACTION ===" << std::endl;
                }
                break;
            case 'e':
            case 'E':
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[DIR] ") {
                        auto dir_path = entry.full_path;
                        if (expanded_dirs.count(dir_path)) {
                            expanded_dirs.erase(dir_path);
                        } else {
                            expanded_dirs.insert(dir_path);
                        }
                        need_refresh = true;
                    } else if (entry.type == "[RESTO_NEXT]") {
                        expand_resto(entry.full_path);
                        need_refresh = true;
                        select_first_owner = entry.full_path;
                    } else if (entry.type == "[RESTO_PREV]") {
                        prev_resto(entry.full_path);
                        need_refresh = true;
                        select_last_owner = entry.full_path;
                    }
                }
                break;
            case 'n':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    bool has_next = false;
                    for (const auto& e : entries) {
                        if (e.type == "[RESTO_NEXT]" && e.full_path == owner) {
                            has_next = true;
                            break;
                        }
                    }
                    if (has_next) {
                        expand_resto(owner);
                        need_refresh = true;
                        select_first_owner = owner;
                    }
                }
                break;
            case 'p':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    if (get_current_page(owner) > 0) {
                        prev_resto(owner);
                        need_refresh = true;
                        select_last_owner = owner;
                    }
                }
                break;
            case KEY_DC:
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[RESTO_PREV]" || entry.type == "[RESTO_NEXT]")
                        break;
                    std::cout << "=== POPUP confirm_delete ===" << std::endl;
                    std::cout << "message: Delete \"" << entry.name << "\"?" << std::endl;
                    std::cout << "=== END POPUP ===" << std::endl;

                    std::string response;
                    std::getline(std::cin, response);

                    if (response == "y" || response == "Y" || response == "ENTER") {
                        try {
                            if (entry.type == "[DIR] ") {
                                std::filesystem::remove_all(entry.full_path);
                            } else {
                                std::filesystem::remove(entry.full_path);
                            }
                            clear_dir_size_cache();
                            std::cout << "=== ACTION deleted ===" << std::endl;
                            std::cout << "path: " << entry.full_path.string() << std::endl;
                            std::cout << "=== END ACTION ===" << std::endl;
                        } catch (const std::exception& ex) {
                            std::cout << "=== POPUP error ===" << std::endl;
                            std::cout << "message: " << ex.what() << std::endl;
                            std::cout << "=== END POPUP ===" << std::endl;
                        }
                        need_refresh = true;
                    } else {
                        std::cout << "=== ACTION cancel_delete ===" << std::endl;
                        std::cout << "=== END ACTION ===" << std::endl;
                    }
                    popup_handled = true;
                }
                break;
            case 'b':
                std::cout << "=== POPUP bar_color ===" << std::endl;
                std::cout << "colors: black, red, green, yellow, blue, magenta, cyan, white"
                          << std::endl;
                std::cout << "=== END POPUP ===" << std::endl;
                popup_handled = true;
                break;
            }

            if (!popup_handled || need_refresh) {
                if (need_refresh)
                    rebuild_tree();
                clamp_and_skip_selection(entries, selected, select_first_owner, select_last_owner);
                update_scroll(selected, scroll_offset, visible_rows);
                int tp = 1, cp = 0;
                get_page_info(tp, cp);
                headless_dump_frame(entries, selected, scroll_offset, visible_rows,
                                    current_path, expanded_dirs, last_scan_ms,
                                    bar_fg, bar_bg, frame_num, tp, cp);
                frame_num++;
            }
        }
        return 0;
    }

    // ==================== NCURSES MODE ====================
    setlocale(LC_ALL, "");
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
        init_pair(1, COLOR_WHITE, -1);
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
    int visible_rows = rows - 1 - footer_height(cols);

    std::filesystem::path current_path = start_path;
    auto& expanded_dirs = get_expanded_dirs();
    double last_scan_ms = 0.0;
    std::atomic<bool> loading(false);
    std::atomic<bool> anim_started(false);

    int total_pages = 1;
    int current_page = 0;

    std::vector<EntryInfo> entries;
    bool need_refresh = true;
    std::filesystem::path select_first_owner;
    std::filesystem::path select_last_owner;
    while (running) {
        clear();
        getmaxyx(stdscr, rows, cols);
        visible_rows = rows - 4;
        if (visible_rows < 1) visible_rows = 1;
        if (need_refresh) {
            loading = true;
            anim_started = false;
            std::thread loader([&]() {
                entries.clear();
                auto t0 = std::chrono::high_resolution_clock::now();
                build_tree_entries(current_path, expanded_dirs, entries, 0, 30);
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
        clamp_and_skip_selection(entries, selected, select_first_owner, select_last_owner);
        update_scroll(selected, scroll_offset, visible_rows);

        current_page = get_current_page(current_path);
        total_pages = 1;
        for (const auto& e : entries) {
            if ((e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]") && e.full_path == current_path) {
                std::string label = e.name;
                auto paren = label.rfind('(');
                auto slash = label.rfind('/');
                if (paren != std::string::npos && slash != std::string::npos && slash > paren) {
                    total_pages = std::stoi(label.substr(paren + 1, slash - paren - 1));
                }
                break;
            }
        }

        clear();
        draw_header(cols, current_path, current_page, total_pages);
        print_directory_entries(entries, selected, scroll_offset, visible_rows, (int)entries.size(), 1, 2);
        draw_footer(rows, cols, selected, (int)entries.size(), last_scan_ms);
        refresh();
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
                if (selected < (int)entries.size() - 1) selected++;
                break;
            case ' ':
                if (!entries.empty()) {
                    std::string full_path = entries[selected].full_path.string();
                    std::string cmd = "xdg-open \"" + full_path + "\" > /dev/null 2>&1 &";
                    system(cmd.c_str());
                }
                break;
            case 'e':
            case 'E':
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[DIR] ") {
                        auto dir_path = entry.full_path;
                        if (expanded_dirs.count(dir_path)) {
                            expanded_dirs.erase(dir_path);
                        } else {
                            expanded_dirs.insert(dir_path);
                        }
                        need_refresh = true;
                    } else if (entry.type == "[RESTO_NEXT]") {
                        expand_resto(entry.full_path);
                        need_refresh = true;
                        select_first_owner = entry.full_path;
                    } else if (entry.type == "[RESTO_PREV]") {
                        prev_resto(entry.full_path);
                        need_refresh = true;
                        select_last_owner = entry.full_path;
                    }
                }
                break;
            case 'n':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    bool has_next = false;
                    for (const auto& e : entries) {
                        if (e.type == "[RESTO_NEXT]" && e.full_path == owner) {
                            has_next = true;
                            break;
                        }
                    }
                    if (has_next) {
                        expand_resto(owner);
                        need_refresh = true;
                        select_first_owner = owner;
                    }
                }
                break;
            case 'p':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    if (get_current_page(owner) > 0) {
                        prev_resto(owner);
                        need_refresh = true;
                        select_last_owner = owner;
                    }
                }
                break;
            case KEY_DC:
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[RESTO_PREV]" || entry.type == "[RESTO_NEXT]")
                        break;
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
            case 'b':
                if (has_colors()) {
                    auto [fg, bg] = bar_color_selection_popup();
                    if (fg >= 0 && bg >= 0) {
                        bar_fg = fg;
                        bar_bg = bg;
                        init_pair(2, bar_fg, bar_bg);
                    }
                } else {
                    confirm_popup(L->term_no_colors);
                }
                break;
        }
        update_scroll(selected, scroll_offset, visible_rows);
    }

    endwin();
    return 0;
}
