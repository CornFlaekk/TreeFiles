#pragma once
#include <ncurses.h>
#include <vector>
#include <string>
#include "file_utils.h"

void draw_terminal_border();
void print_directory_entries(const std::vector<EntryInfo>& entries, int selected, int scroll_offset, int visible_rows, int start_row = 1, int start_col = 2);