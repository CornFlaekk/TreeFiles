#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <vector>
#include "ui_utils.h"
#include "i18n.h"
#include <array>
#include <tuple>

void draw_terminal_border() {
    // Empty: header and footer now draw their own borders.
}

static void draw_horizontal_line(int row, int col_start, int col_end, chtype left, chtype mid, chtype right) {
    mvaddch(row, col_start, left);
    for (int c = col_start + 1; c < col_end; ++c)
        mvaddch(row, c, mid);
    mvaddch(row, col_end, right);
}

void draw_header(int cols, const std::filesystem::path& current_path, int page, int total_pages) {
    // Top border
    draw_horizontal_line(0, 0, cols - 1, ACS_ULCORNER, ACS_HLINE, ACS_URCORNER);

    // Left: " TreeFiles " in bold
    attron(A_BOLD);
    mvaddstr(0, 2, " TreeFiles ");
    attroff(A_BOLD);

    // Separator after title
    mvaddch(0, 14, ACS_VLINE);

    // Center: path
    std::string path_str = current_path.string();
    const char* home = getenv("HOME");
    if (home && path_str.compare(0, strlen(home), home) == 0) {
        path_str = "~" + path_str.substr(strlen(home));
    }

    int path_x = 16;
    int max_path_w = cols - path_x - 20;
    if (max_path_w > 5 && (int)path_str.size() > max_path_w) {
        path_str = path_str.substr(0, max_path_w - 1) + "\u2026";
    }
    if (max_path_w > 0) {
        mvaddstr(0, path_x, path_str.c_str());
    }

    // Right: page info if applicable
    if (total_pages > 1) {
        char buf[32];
        snprintf(buf, sizeof(buf), L->page_fmt, page + 1, total_pages);
        int right_x = cols - 3 - (int)strlen(buf);
        if (right_x > path_x + 2) {
            mvaddch(0, right_x - 2, ACS_VLINE);
            mvaddstr(0, right_x, buf);
        }
    }
}

struct FooterSection {
    const char* name;
    std::vector<const char*> bindings;
};

static std::string keycap(const char* key, const char* action) {
    return std::string("[") + key + "] " + action;
}

static std::vector<FooterSection> build_footer_sections() {
    static std::string s_move = keycap("\u2191\u2193", L->move);
    static std::string s_page = keycap("N/P", L->page_abbr);
    static std::string s_exp  = keycap("E", L->expand);
    static std::string s_open = keycap("Spc", L->open);
    static std::string s_del  = keycap("Del", L->delete_);
    static std::string s_col  = keycap("B", L->color);
    static std::string s_quit = keycap("Q", L->quit);

    return {
        {L->nav_section,  {s_move.c_str(), s_page.c_str()}},
        {L->act_section,  {s_exp.c_str(), s_open.c_str(), s_del.c_str()}},
        {L->sys_section,  {s_col.c_str(), s_quit.c_str()}},
    };
}

static std::string section_bindings(const FooterSection& sec) {
    std::string s;
    for (size_t i = 0; i < sec.bindings.size(); ++i) {
        if (i > 0) s += " ";
        s += sec.bindings[i];
    }
    return s;
}

static int count_content_lines(int cols) {
    if (cols > 80) return 2;
    auto secs = build_footer_sections();
    int lines = 0;
    for (auto& sec : secs) {
        std::string line = sec.name + std::string(": ");
        line += section_bindings(sec);
        int max_w = cols - 4;
        lines++;
        int remaining = (int)line.size() - max_w;
        while (remaining > 0) {
            lines++;
            remaining -= max_w;
        }
    }
    return lines;
}

int footer_height(int cols) {
    return count_content_lines(cols) + 2;
}

void draw_footer(int rows, int cols, int selected, int total_entries, double last_scan_ms) {
    int content_lines = count_content_lines(cols);
    int footer_start = rows - 2 - content_lines;
    auto secs = build_footer_sections();

    // Top separator
    draw_horizontal_line(footer_start, 0, cols - 1, ACS_LTEE, ACS_HLINE, ACS_RTEE);

    char right_buf[64];
    std::string scan_str = format_scan_time(last_scan_ms);
    if (total_entries > 0) {
        snprintf(right_buf, sizeof(right_buf), " %d/%d  Scan: %s ", selected + 1, total_entries, scan_str.c_str());
    } else {
        snprintf(right_buf, sizeof(right_buf), " Scan: %s ", scan_str.c_str());
    }

    if (cols > 80) {
        int row1 = footer_start + 1;
        int row2 = footer_start + 2;

        mvaddch(row1, 0, ACS_VLINE);
        mvaddch(row2, 0, ACS_VLINE);
        mvaddch(row1, cols - 1, ACS_VLINE);
        mvaddch(row2, cols - 1, ACS_VLINE);

        int sect_w = (cols - 4) / 3;
        for (int s = 0; s < 3; ++s) {
            int col = 2 + s * (sect_w + 1);

            attron(A_BOLD);
            mvaddstr(row1, col, secs[s].name);
            attroff(A_BOLD);

            if (s < 2) {
                int mid = col + sect_w;
                mvaddch(row1, mid, ACS_VLINE);
            }

            mvaddstr(row2, col, section_bindings(secs[s]).c_str());
            if (s < 2) {
                int mid = col + sect_w;
                mvaddch(row2, mid, ACS_VLINE);
            }
        }
    } else {
        int row = footer_start + 1;
        for (auto& sec : secs) {
            mvaddch(row, 0, ACS_VLINE);
            mvaddch(row, cols - 1, ACS_VLINE);

            std::string header = sec.name + std::string(": ");
            attron(A_BOLD);
            mvaddstr(row, 2, header.c_str());
            attroff(A_BOLD);

            int hdr_w = (int)header.size();
            int max_w = cols - 4;
            std::string binds = section_bindings(sec);
            std::string first_line = binds.substr(0, max_w - hdr_w);
            mvaddstr(row, 2 + hdr_w, first_line.c_str());

            size_t pos = first_line.size();
            while (pos < binds.size()) {
                row++;
                mvaddch(row, 0, ACS_VLINE);
                mvaddch(row, cols - 1, ACS_VLINE);
                std::string remnant = binds.substr(pos, max_w - 2);
                mvaddstr(row, 4, remnant.c_str());
                pos += remnant.size();
            }
            row++;
        }
    }

    int right_w = (int)strlen(right_buf);
    int right_x = cols - 2 - right_w;
    if (right_x < 2) right_x = 2;

    mvaddch(rows - 1, 0, ACS_LLCORNER);
    for (int c = 1; c < right_x; ++c) mvaddch(rows - 1, c, ACS_HLINE);
    mvaddstr(rows - 1, right_x, right_buf);
    for (int c = right_x + right_w; c < cols - 1; ++c) mvaddch(rows - 1, c, ACS_HLINE);
    mvaddch(rows - 1, cols - 1, ACS_LRCORNER);
}

static bool is_last_sibling(const std::vector<EntryInfo>& entries, int idx) {
    int my_depth = entries[idx].depth;
    for (int j = idx + 1; j < (int)entries.size(); ++j) {
        if (entries[j].depth < my_depth) return true;
        if (entries[j].depth == my_depth) return false;
    }
    return true;
}

static int find_ancestor(const std::vector<EntryInfo>& entries, int idx, int level) {
    for (int j = idx - 1; j >= 0; --j) {
        if (entries[j].depth == level) return j;
    }
    return -1;
}

static std::string build_tree_prefix(const std::vector<EntryInfo>& entries, int idx) {
    int depth = entries[idx].depth;
    std::string prefix;

    for (int l = 0; l < depth; ++l) {
        int anc = find_ancestor(entries, idx, l);
        if (anc >= 0 && !is_last_sibling(entries, anc)) {
            prefix += "\u2502    ";
        } else {
            prefix += "     ";
        }
    }

    // First entry uses corner instead of T-branch (no parent above)
    if (idx == 0) {
        if (is_last_sibling(entries, idx))
            prefix += "\u2514\u2500\u2500\u2500 ";
        else
            prefix += "\u250c\u2500\u2500\u2500 ";
    } else if (is_last_sibling(entries, idx)) {
        prefix += "\u2514\u2500\u2500\u2500 ";
    } else {
        prefix += "\u251c\u2500\u2500\u2500 ";
    }

    return prefix;
}

void print_directory_entries(const std::vector<EntryInfo>& entries, int selected, int scroll_offset, int visible_rows, int total_entries, int start_row, int start_col) {
    if (entries.empty()) return;

    int cols = getmaxx(stdscr);
    int content_cols = cols - 2;

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

    // Scrollbar thumb position
    int thumb_pos = start_row;
    int thumb_size = 1;
    if (total_entries > visible_rows) {
        thumb_size = std::max(1, visible_rows * visible_rows / total_entries);
        thumb_pos = start_row + (scroll_offset * (visible_rows - thumb_size)) / std::max(1, total_entries - visible_rows);
    }

    for (int i = 0; i < visible_rows; ++i) {
        int idx = scroll_offset + i;
        int bar_row = start_row + i;

        // Left border
        mvaddch(bar_row, 0, ACS_VLINE);

        // Right border / scrollbar
        if (total_entries > visible_rows) {
            if (bar_row >= thumb_pos && bar_row < thumb_pos + thumb_size) {
                mvaddch(bar_row, cols - 1, ACS_CKBOARD);
            } else {
                mvaddch(bar_row, cols - 1, ACS_VLINE);
            }
        } else {
            mvaddch(bar_row, cols - 1, ACS_VLINE);
        }

        if (idx >= (int)entries.size()) continue;
        const auto& e = entries[idx];

        bool is_nav = (e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]");
        bool is_dir = (e.type == "[DIR] ");

        std::string tree_prefix = build_tree_prefix(entries, idx);
        // Display width: tree chars are 1 column each, spaces are 1 column
        // Prefix: [1-char-bar + 4 spaces]*depth + [1-char-branch + 3 dashes + 1 space]
        int indent_width = 5 * (e.depth + 1);
        int bar_col = start_col + indent_width;

        // Draw tree prefix
        attron(COLOR_PAIR(1));
        mvaddstr(bar_row, start_col, tree_prefix.c_str());
        attroff(COLOR_PAIR(1));

        if (is_nav) {
            if (idx == selected) attron(A_REVERSE);
            attron(A_BOLD);
            mvaddstr(bar_row, bar_col, e.name.c_str());
            attroff(A_BOLD);
            if (idx == selected) attroff(A_REVERSE);
        } else {
            double percent = std::min(1.0, (double)e.size / parent_sizes[idx]);
            int bar_width = std::max(1, (int)((content_cols - start_col - indent_width) * percent));

            attron(COLOR_PAIR(2));
            for (int b = 0; b < bar_width; ++b) {
                mvaddch(bar_row, bar_col + b, ACS_CKBOARD);
            }
            attroff(COLOR_PAIR(2));

            std::string size_str = human_readable_size(e.size);
            std::string name_str = e.name;
            if (is_dir) name_str += "/";
            std::string entry_text = name_str + "  " + size_str;

            if (idx == selected) attron(A_REVERSE);
            for (size_t c = 0; c < entry_text.size() && bar_col + (int)c < cols - 2; ++c) {
                int col = bar_col + (int)c;
                if ((int)c < bar_width) {
                    attron(COLOR_PAIR(2));
                    mvaddch(bar_row, col, entry_text[c]);
                    attroff(COLOR_PAIR(2));
                } else {
                    attron(COLOR_PAIR(1));
                    mvaddch(bar_row, col, entry_text[c]);
                    attroff(COLOR_PAIR(1));
                }
            }
            if (idx == selected) attroff(A_REVERSE);
        }
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
    int selected = 0;

    while (true) {
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
            selected = (selected + 1) % 2;
        } else if (ch == KEY_RIGHT) {
            selected = (selected + 1) % 2;
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
    int selected_bg = 0;
    int selected_fg = 0;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int win_height = 12, win_width = 28;
    int starty = (rows - win_height) / 2;
    int startx = (cols - win_width) / 2;
    WINDOW* win = newwin(win_height, win_width, starty, startx);
    keypad(win, TRUE);

    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", L->bar_bg_color);
    while (true) {
        for (int i = 0; i < 8; ++i) {
            if (i == selected_bg) {
                wattron(win, A_REVERSE);
                mvwprintw(win, 3 + i, 4, "%s", L->col_names[i]);
                wattroff(win, A_REVERSE);
            } else {
                mvwprintw(win, 3 + i, 4, "%s", L->col_names[i]);
            }
        }
        wrefresh(win);
        int ch = wgetch(win);
        if (ch == KEY_UP && selected_bg > 0) selected_bg--;
        else if (ch == KEY_DOWN && selected_bg < 7) selected_bg++;
        else if (ch == '\n' || ch == KEY_ENTER) break;
        else if (ch == 27) { delwin(win); touchwin(stdscr); refresh(); return {-1, -1}; }
    }

    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", L->bar_fg_color);
    while (true) {
        for (int i = 0; i < 8; ++i) {
            if (i == selected_fg) {
                wattron(win, A_REVERSE);
                mvwprintw(win, 3 + i, 4, "%s", L->col_names[i]);
                wattroff(win, A_REVERSE);
            } else {
                mvwprintw(win, 3 + i, 4, "%s", L->col_names[i]);
            }
        }
        wrefresh(win);
        int ch = wgetch(win);
        if (ch == KEY_UP && selected_fg > 0) selected_fg--;
        else if (ch == KEY_DOWN && selected_fg < 7) selected_fg++;
        else if (ch == '\n' || ch == KEY_ENTER) break;
        else if (ch == 27) { delwin(win); touchwin(stdscr); refresh(); return {-1, -1}; }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return {selected_fg, selected_bg};
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
    int delay = 120;
    int waited = 0;
    while (loading && waited < 500) {
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
                mvwaddch(win, 3 + y, 4 + x * 2, ch);
                mvwaddch(win, 3 + y, 4 + x * 2 + 1, ' ');
            }
        }
        wrefresh(win);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        frame = (frame + 1) % num_frames;
    }
    delwin(win);
}
