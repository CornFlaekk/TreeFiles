#pragma once
#include <atomic>
#include <ncurses.h>
#include <vector>
#include <string>
#include "file_utils.h"

void draw_terminal_border();
void draw_header(int cols, const std::filesystem::path& current_path, int page, int total_pages);
void draw_footer(int rows, int cols, int selected, int total_entries, double last_scan_ms);
int footer_height(int cols);
void print_directory_entries(const std::vector<EntryInfo>& entries, int selected, int scroll_offset, int visible_rows, int total_entries, int start_row = 1, int start_col = 2);
bool confirm_popup(const std::string& message);
std::pair<int, int> bar_color_selection_popup();
std::string format_scan_time(double ms);
void show_loading_animation(std::atomic<bool>& loading, std::atomic<bool>& started);