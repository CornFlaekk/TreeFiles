#pragma once
#include <atomic>
#include <ncurses.h>
#include <vector>
#include <string>
#include "file_utils.h"

void draw_terminal_border();
void print_directory_entries(const std::vector<EntryInfo>& entries, int selected, int scroll_offset, int visible_rows, int start_row = 1, int start_col = 2);
bool confirm_popup(const std::string& message);
std::pair<int, int> bar_color_selection_popup();
void draw_help_box(int rows, int cols, bool show);
std::string format_scan_time(double ms);
void show_loading_animation(std::atomic<bool>& loading, std::atomic<bool>& started);