#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <vector>
#include "ui_utils.h"
#include "platform_utils.h"
#include "localization.h"
#include <array>
#include <tuple>
#include <cwctype>
#include <cstdint>
#include <climits>

void draw_terminal_border() {
    // Empty: header and footer now draw their own borders.
}

static void draw_horizontal_line(int row, int col_start, int col_end, chtype left, chtype mid, chtype right) {
    mvaddch(row, col_start, left);
    for (int c = col_start + 1; c < col_end; ++c)
        mvaddch(row, c, mid);
    mvaddch(row, col_end, right);
}

static std::string utf8_prefix(const std::string& value, size_t max_bytes) {
    if (value.size() <= max_bytes) return value;
    if (max_bytes <= 3) return value.substr(0, max_bytes);
    size_t prefix = max_bytes - 3;
    while (prefix > 0 && (static_cast<unsigned char>(value[prefix]) & 0xc0) == 0x80) --prefix;
    return value.substr(0, prefix) + "...";
}

static std::string filter_summary(const FilterOptions& filter, size_t matching_files) {
    if (!filter_is_active(filter)) return {};
    const std::string query = filter.text.empty() ? "*" : utf8_prefix(filter.text, 13);
    const std::string extension = filter.extension.empty()
        ? "" : " ext:" + utf8_prefix(filter.extension, 9);
    return std::string(text(Text::FilterLabel)) + ": " + query + extension + " (" +
           std::to_string(matching_files) + ")";
}

void draw_header(int cols, const std::filesystem::path& current_path, int page, int total_pages,
                 SortOptions sort, const FilterOptions& filter, size_t matching_files) {
    // Top border
    draw_horizontal_line(0, 0, cols - 1, ACS_ULCORNER, ACS_HLINE, ACS_URCORNER);

    // Left: " TreeFiles " in bold
    attron(A_BOLD);
    mvaddstr(0, 2, " TreeFiles ");
    attroff(A_BOLD);

    // Separator after title
    mvaddch(0, 14, ACS_VLINE);

    // Center: path
    std::string path_str = current_path.u8string();
    auto home = home_directory().u8string();
    if (!home.empty() && path_str.compare(0, home.size(), home) == 0) {
        path_str = "~" + path_str.substr(home.size());
    }

    int path_x = 16;
    const Text sort_text = sort.key == SortKey::size ? Text::SortSize
        : sort.key == SortKey::name ? Text::SortName : Text::SortMtime;
    std::string right_status = filter_summary(filter, matching_files);
    if (!right_status.empty()) right_status += " | ";
    right_status += std::string(text(Text::SortLabel)) + ": " + text(sort_text) + "/" + sort_order_name(sort.order);
    if (total_pages > 1)
        right_status += " | " + std::string(text(Text::Page)) + " " + std::to_string(page + 1) + "/" + std::to_string(total_pages);
    int right_x = cols - 3 - static_cast<int>(right_status.size());
    int max_path_w = right_x - path_x - 4;
    if (max_path_w > 5 && (int)path_str.size() > max_path_w) {
        path_str = path_str.substr(0, max_path_w - 1) + "\u2026";
    }
    if (max_path_w > 0) {
        mvaddstr(0, path_x, path_str.c_str());
    }

    if (right_x > path_x + 2) {
        mvaddch(0, right_x - 2, ACS_VLINE);
        mvaddstr(0, right_x, right_status.c_str());
    }
}

struct FooterSection {
    Text name;
    std::vector<Text> bindings;
};

static const FooterSection sections[] = {
    {Text::Navigation, {Text::MoveBinding, Text::PageBinding, Text::EnterDirectoryBinding, Text::ParentDirectoryBinding}},
    {Text::Actions, {Text::ExpandBinding, Text::OpenBinding, Text::DeleteBinding, Text::ChangeRootBinding}},
    {Text::System, {Text::ColorBinding, Text::SortBinding, Text::FilterBinding, Text::WarningsBinding, Text::RefreshBinding, Text::QuitBinding}},
};

// Build a flat string of bindings for a section
static std::string section_bindings(const FooterSection& sec) {
    std::string s;
    for (size_t i = 0; i < sec.bindings.size(); ++i) {
        if (i > 0) s += " ";
        s += text(sec.bindings[i]);
    }
    return s;
}

static bool wide_footer(int cols) {
    if (cols <= 80) return false;
    for (const auto& sec : sections)
        if (static_cast<int>(section_bindings(sec).size()) > (cols - 4) / 3) return false;
    return true;
}

static int count_content_lines(int cols) {
    if (wide_footer(cols)) return 2;

    // Narrow: one line per section (+ wrapping if needed)
    int lines = 0;
    for (auto& sec : sections) {
        const int max_w = std::max(1, cols - 4);
        const int header_width = static_cast<int>(std::strlen(text(sec.name))) + 2;
        lines++;
        int remaining = static_cast<int>(section_bindings(sec).size()) - std::max(1, max_w - header_width);
        while (remaining > 0) {
            lines++;
            remaining -= std::max(1, max_w - 2);
        }
    }
    return lines;
}

int footer_height(int cols) {
    return count_content_lines(cols) + 2; // content + separator + bottom
}

void draw_footer(int rows, int cols, int selected, int total_entries, double last_scan_ms,
                 bool scan_has_warnings, const FilterOptions& filter, size_t matching_files) {
    int content_lines = count_content_lines(cols);
    int footer_start = rows - 2 - content_lines;

    // Top separator
    draw_horizontal_line(footer_start, 0, cols - 1, ACS_LTEE, ACS_HLINE, ACS_RTEE);

    std::string scan_str = format_scan_time(last_scan_ms);
    const char* scan_label = text(Text::Scan);
    std::string decorated_scan = scan_has_warnings ? std::string(scan_label) + "*" : scan_label;
    std::string right_info = filter_summary(filter, matching_files);
    if (!right_info.empty()) right_info += " | ";
    if (total_entries > 0) {
        right_info += std::to_string(selected + 1) + "/" + std::to_string(total_entries) +
            "  " + decorated_scan + ": " + scan_str;
    } else {
        right_info += decorated_scan + ": " + scan_str;
    }
    right_info = " " + right_info + " ";
    if (right_info.size() > static_cast<size_t>(std::max(0, cols - 4)))
        right_info = utf8_prefix(right_info, static_cast<size_t>(std::max(0, cols - 4)));

    if (wide_footer(cols)) {
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
            mvaddstr(row1, col, text(sections[s].name));
            attroff(A_BOLD);

            if (s < 2) {
                int mid = col + sect_w;
                mvaddch(row1, mid, ACS_VLINE);
            }

            mvaddstr(row2, col, section_bindings(sections[s]).c_str());
            if (s < 2) {
                int mid = col + sect_w;
                mvaddch(row2, mid, ACS_VLINE);
            }
        }
    } else {
        int row = footer_start + 1;
        for (auto& sec : sections) {
            mvaddch(row, 0, ACS_VLINE);
            mvaddch(row, cols - 1, ACS_VLINE);

            std::string header = std::string(text(sec.name)) + ": ";
            attron(A_BOLD);
            mvaddstr(row, 2, header.c_str());
            attroff(A_BOLD);

            int hdr_w = (int)header.size();
            int max_w = std::max(1, cols - 4);
            std::string binds = section_bindings(sec);
            std::string first_line = binds.substr(0, std::max(1, max_w - hdr_w));
            mvaddstr(row, 2 + hdr_w, first_line.c_str());

            size_t pos = first_line.size();
            while (pos < binds.size()) {
                row++;
                mvaddch(row, 0, ACS_VLINE);
                mvaddch(row, cols - 1, ACS_VLINE);
                std::string remnant = binds.substr(pos, std::max(1, max_w - 2));
                mvaddstr(row, 4, remnant.c_str());
                pos += remnant.size();
            }
            row++;
        }
    }

    // Bottom border with embedded scan/position info
    int right_w = static_cast<int>(right_info.size());
    int right_x = cols - 2 - right_w;
    if (right_x < 2) right_x = 2;

    mvaddch(rows - 1, 0, ACS_LLCORNER);
    for (int c = 1; c < right_x; ++c) mvaddch(rows - 1, c, ACS_HLINE);
    mvaddstr(rows - 1, right_x, right_info.c_str());
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
            double percent = e.size_status == SizeStatus::unavailable
                ? 0.0 : std::min(1.0, (double)e.size / parent_sizes[idx]);
            int bar_width = std::max(1, (int)((content_cols - start_col - indent_width) * percent));

            attron(COLOR_PAIR(2));
            for (int b = 0; b < bar_width; ++b) {
                mvaddch(bar_row, bar_col + b, ACS_CKBOARD);
            }
            attroff(COLOR_PAIR(2));

            std::string size_str = e.size_status == SizeStatus::unavailable
                ? text(Text::SizeUnavailable) : human_readable_size(e.size);
            if (e.size_status == SizeStatus::partial) size_str += " (" + std::string(text(Text::SizePartial)) + ")";
            std::string name_str = e.name;
            if (is_dir) name_str += "/";
            if (e.type == "[LINK]") name_str += " @";
            std::string entry_text = name_str + "  " + size_str;

            if (idx == selected) attron(A_REVERSE);
            int col = bar_col;
            for (size_t c = 0; c < entry_text.size() && col < cols - 2;) {
                // Send a complete UTF-8 character to either curses backend.
                size_t next = c + 1;
                while (next < entry_text.size() &&
                       (static_cast<unsigned char>(entry_text[next]) & 0xc0) == 0x80)
                    ++next;
                int pair = (col - bar_col < bar_width) ? 2 : 1;
                attron(COLOR_PAIR(pair));
                mvaddnstr(bar_row, col, entry_text.c_str() + c, static_cast<int>(next - c));
                attroff(COLOR_PAIR(pair));
                int next_col = getcurx(stdscr);
                if (next_col < col) break;
                col = next_col;
                c = next;
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

    const char* options[2] = {text(Text::Yes), text(Text::No)};
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
        if (ch == KEY_LEFT || ch == 'h' || ch == '\t') {
            selected = (selected + 1) % 2;
        } else if (ch == KEY_RIGHT || ch == 'l') {
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

void show_scan_diagnostics(const ScanResult& result) {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    if (rows < 7 || cols < 16 || result.diagnostics.empty()) return;

    std::vector<std::string> lines;
    lines.reserve(result.diagnostics.size());
    for (const auto& issue : result.diagnostics) {
        lines.push_back(issue.path.u8string() + " [" + issue.operation + "]: " + issue.error.message());
    }
    const int height = std::min(rows - 2, std::max(6, std::min(12, static_cast<int>(lines.size()) + 4)));
    const int width = std::min(cols - 2, 100);
    WINDOW* win = newwin(height, width, (rows - height) / 2, (cols - width) / 2);
    if (!win) return;
    keypad(win, TRUE);
    int first = 0;
    while (true) {
        werase(win);
        box(win, 0, 0);
        const std::string title = std::string(text(Text::ScanDiagnostics)) + " (" +
                                  std::to_string(lines.size()) + ")";
        mvwaddnstr(win, 0, 2, title.c_str(), width - 4);
        const int visible = height - 4;
        for (int line = 0; line < visible && first + line < static_cast<int>(lines.size()); ++line)
            mvwaddnstr(win, line + 1, 2, lines[first + line].c_str(), width - 4);
        const std::string hint = std::string(text(Text::PressAnyKey)) + " (Esc)";
        mvwaddnstr(win, height - 2, 2, hint.c_str(), width - 4);
        wrefresh(win);
        const int input = wgetch(win);
        if (input == KEY_UP || input == 'k') first = std::max(0, first - 1);
        else if (input == KEY_DOWN || input == 'j')
            first = std::min(std::max(0, static_cast<int>(lines.size()) - visible), first + 1);
        else break;
    }
    delwin(win);
    touchwin(stdscr);
    refresh();
}

static std::string wide_to_utf8(const std::wstring& value) {
    std::string encoded;
    for (size_t index = 0; index < value.size(); ++index) {
        std::uint32_t codepoint = static_cast<std::uint32_t>(value[index]);
#if WCHAR_MAX <= 0xffff
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && index + 1 < value.size()) {
            const std::uint32_t low = static_cast<std::uint32_t>(value[index + 1]);
            if (low >= 0xdc00 && low <= 0xdfff) {
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                ++index;
            } else {
                codepoint = 0xfffd;
            }
        } else if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
            codepoint = 0xfffd;
        }
#else
        if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
            codepoint = 0xfffd;
#endif
        if (codepoint <= 0x7f) {
            encoded.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            encoded.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            encoded.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            encoded.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }
    return encoded;
}

static std::wstring utf8_to_wide(const std::string& value) {
    std::wstring decoded;
    const auto append_codepoint = [&](std::uint32_t codepoint) {
#if WCHAR_MAX <= 0xffff
        if (codepoint > 0xffff) {
            codepoint -= 0x10000;
            decoded.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
            decoded.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
        } else {
            decoded.push_back(static_cast<wchar_t>(codepoint));
        }
#else
        decoded.push_back(static_cast<wchar_t>(codepoint));
#endif
    };
    for (size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        std::uint32_t codepoint = 0xfffd;
        size_t length = 1;
        if (first < 0x80) codepoint = first;
        else if ((first & 0xe0) == 0xc0 && index + 1 < value.size()) {
            const auto second = static_cast<unsigned char>(value[index + 1]);
            if ((second & 0xc0) == 0x80) {
                codepoint = ((first & 0x1f) << 6) | (second & 0x3f);
                length = 2;
                if (codepoint < 0x80) codepoint = 0xfffd;
            }
        } else if ((first & 0xf0) == 0xe0 && index + 2 < value.size()) {
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            if ((second & 0xc0) == 0x80 && (third & 0xc0) == 0x80) {
                codepoint = ((first & 0x0f) << 12) | ((second & 0x3f) << 6) | (third & 0x3f);
                length = 3;
                if (codepoint < 0x800 || (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    codepoint = 0xfffd;
            }
        } else if ((first & 0xf8) == 0xf0 && index + 3 < value.size()) {
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const auto fourth = static_cast<unsigned char>(value[index + 3]);
            if ((second & 0xc0) == 0x80 && (third & 0xc0) == 0x80 && (fourth & 0xc0) == 0x80) {
                codepoint = ((first & 0x07) << 18) | ((second & 0x3f) << 12) |
                    ((third & 0x3f) << 6) | (fourth & 0x3f);
                length = 4;
                if (codepoint < 0x10000 || codepoint > 0x10ffff) codepoint = 0xfffd;
            }
        }
        append_codepoint(codepoint);
        index += length;
    }
    return decoded;
}

bool prompt_for_path(std::string& utf8_path) {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    if (rows < 9 || cols < 30) return false;
    const int width = std::min(cols - 2, 82);
    const int height = 7;
    WINDOW* win = newwin(height, width, (rows - height) / 2, (cols - width) / 2);
    if (!win) return false;
    keypad(win, TRUE);
    std::wstring value;
    bool accepted = false;
    while (true) {
        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 1, 2, text(Text::PathPrompt), width - 4);
        mvwaddnstr(win, 5, 2, text(Text::PathPromptHint), width - 4);
        const int field_width = width - 4;
        const size_t start = value.size() > static_cast<size_t>(field_width)
            ? value.size() - static_cast<size_t>(field_width) : 0;
        if (start < value.size())
            mvwaddnwstr(win, 3, 2, value.data() + start, field_width);
        wmove(win, 3, 2 + static_cast<int>(std::min(value.size() - start,
                                                    static_cast<size_t>(field_width - 1))));
        wrefresh(win);

        wint_t input = 0;
        const int kind = wget_wch(win, &input);
        if (kind == ERR) continue;
        if ((kind == KEY_CODE_YES && input == KEY_BACKSPACE) || input == 8 || input == 127) {
            if (!value.empty()) {
                const wchar_t last = value.back();
                value.pop_back();
                if (last >= 0xdc00 && last <= 0xdfff && !value.empty() &&
                    value.back() >= 0xd800 && value.back() <= 0xdbff) value.pop_back();
            }
        } else if ((kind == KEY_CODE_YES && input == KEY_ENTER) || input == L'\n' || input == L'\r') {
            accepted = !value.empty();
            break;
        } else if (kind == OK && input == 27) {
            break;
        } else if (kind == OK && input >= 32 && value.size() < 512 &&
                   (std::iswprint(static_cast<wint_t>(input)) ||
                    (input >= 0xd800 && input <= 0xdfff))) {
            value.push_back(static_cast<wchar_t>(input));
        }
    }

    if (accepted) {
        utf8_path = wide_to_utf8(value);
    }
    delwin(win);
    touchwin(stdscr);
    refresh();
    return accepted;
}

bool prompt_for_filter(FilterOptions& filter) {
    int rows = 0, cols = 0;
    getmaxyx(stdscr, rows, cols);
    if (rows < 11 || cols < 30) return false;
    const int width = std::min(cols - 2, 82);
    const int height = 9;
    WINDOW* win = newwin(height, width, (rows - height) / 2, (cols - width) / 2);
    if (!win) return false;
    keypad(win, TRUE);
    std::wstring name = utf8_to_wide(filter.text);
    std::wstring extension = utf8_to_wide(filter.extension);
    std::wstring* fields[] = {&name, &extension};
    int active = 0;
    bool accepted = false;
    while (true) {
        werase(win);
        box(win, 0, 0);
        mvwaddnstr(win, 1, 2, text(Text::FilterNamePrompt), width - 4);
        mvwaddnstr(win, 4, 2, text(Text::FilterExtensionPrompt), width - 4);
        mvwaddnstr(win, 7, 2, text(Text::FilterHint), width - 4);
        const int field_width = width - 4;
        for (int field = 0; field < 2; ++field) {
            const auto& value = *fields[field];
            const int row = field == 0 ? 2 : 5;
            const size_t start = value.size() > static_cast<size_t>(field_width)
                ? value.size() - static_cast<size_t>(field_width) : 0;
            if (start < value.size())
                mvwaddnwstr(win, row, 2, value.data() + start, field_width);
            if (field == active)
                wattron(win, A_REVERSE);
            mvwaddch(win, row, 2 + std::min(field_width - 1,
                static_cast<int>(value.size() - start)), ' ');
            if (field == active)
                wattroff(win, A_REVERSE);
        }
        const auto& active_value = *fields[active];
        const int active_row = active == 0 ? 2 : 5;
        const size_t active_start = active_value.size() > static_cast<size_t>(field_width)
            ? active_value.size() - static_cast<size_t>(field_width) : 0;
        wmove(win, active_row, 2 + static_cast<int>(std::min(active_value.size() - active_start,
                                                              static_cast<size_t>(field_width - 1))));
        wrefresh(win);

        wint_t input = 0;
        const int kind = wget_wch(win, &input);
        if (kind == ERR) continue;
        if (kind == OK && input == 27) break;
        if (kind == OK && input == L'\t') {
            active = 1 - active;
            continue;
        }
        auto& value = *fields[active];
        if ((kind == KEY_CODE_YES && input == KEY_BACKSPACE) || input == 8 || input == 127) {
            if (!value.empty()) {
                const wchar_t last = value.back();
                value.pop_back();
                if (last >= 0xdc00 && last <= 0xdfff && !value.empty() &&
                    value.back() >= 0xd800 && value.back() <= 0xdbff) value.pop_back();
            }
        } else if ((kind == KEY_CODE_YES && input == KEY_ENTER) || input == L'\n' || input == L'\r') {
            accepted = true;
            break;
        } else if (kind == OK && input >= 32 && value.size() < 512 &&
                   (std::iswprint(static_cast<wint_t>(input)) ||
                    (input >= 0xd800 && input <= 0xdfff))) {
            value.push_back(static_cast<wchar_t>(input));
        }
    }
    if (accepted) {
        filter.text = wide_to_utf8(name);
        filter.extension = wide_to_utf8(extension);
    }
    delwin(win);
    touchwin(stdscr);
    refresh();
    return accepted;
}

std::pair<int, int> bar_color_selection_popup(int foreground, int background) {
    const std::array<const char*, 8> color_names = {
        text(Text::Black), text(Text::Red), text(Text::Green), text(Text::Yellow),
        text(Text::Blue), text(Text::Magenta), text(Text::Cyan), text(Text::White)
    };
    int selected_bg = background;
    int selected_fg = foreground;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int win_height = 12, win_width = 28;
    int starty = (rows - win_height) / 2;
    int startx = (cols - win_width) / 2;
    WINDOW* win = newwin(win_height, win_width, starty, startx);
    keypad(win, TRUE);

    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", text(Text::BackgroundColor));
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
        if ((ch == KEY_UP || ch == 'k') && selected_bg > 0) selected_bg--;
        else if ((ch == KEY_DOWN || ch == 'j') && selected_bg < (int)color_names.size() - 1) selected_bg++;
        else if (ch == '\n' || ch == KEY_ENTER) break;
        else if (ch == 27) { delwin(win); touchwin(stdscr); refresh(); return {-1, -1}; }
    }

    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", text(Text::ForegroundColor));
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
        if ((ch == KEY_UP || ch == 'k') && selected_fg > 0) selected_fg--;
        else if ((ch == KEY_DOWN || ch == 'j') && selected_fg < (int)color_names.size() - 1) selected_fg++;
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
    mvwprintw(win, 1, 3, "%s", text(Text::Loading));
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
