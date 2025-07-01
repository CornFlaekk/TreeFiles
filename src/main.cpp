#include <ncurses.h>
#include <ui_utils.h>
#include <file_utils.h>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>

int main() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);

    bool running = true;
    int input;
    int selected = 0;

    std::filesystem::path current_path = ".";

    while (running) {
        clear();

        draw_terminal_border();

        auto entries = get_directory_entries(current_path);
        int n = entries.size();
        if (n == 0) selected = 0;
        else if (selected >= n) selected = n - 1;

        print_directory_entries(entries, selected, 1, 2);

        mvprintw(0, 2, "Presiona 'q' para salir. Usa flechas para moverte. Espacio para abrir en Nautilus.");

        refresh();

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
            case ' ': // Espacio
                if (!entries.empty()) {
                    std::string full_path = (current_path / entries[selected].name).string();
                    std::string cmd = "xdg-open \"" + full_path + "\" > /dev/null 2>&1 &";
                    system(cmd.c_str());
                }
                break;
        }
    }

    endwin();
    return 0;
}