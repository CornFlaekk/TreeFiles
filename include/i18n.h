#pragma once

struct Lang {
    const char* page_fmt;
    const char* nav_section;
    const char* move;
    const char* page_abbr;
    const char* act_section;
    const char* expand;
    const char* open;
    const char* delete_;
    const char* sys_section;
    const char* color;
    const char* quit;
    const char* col_names[8];
    const char* bar_bg_color;
    const char* bar_fg_color;
    const char* term_no_colors;
    const char* prev_label;
    const char* next_label;
};

inline const Lang LANG_EN = {
    "Page %d/%d",
    "Navigate", "move", "page",
    "Actions", "expand", "open", "delete",
    "System", "color", "quit",
    {"Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White"},
    "BAR background color:", "BAR text color:",
    "Colors not supported in this terminal.",
    "\u25c2\u25c2 Previous (", "\u25b8\u25b8 Next ("
};

inline const Lang LANG_ES = {
    "Pag %d/%d",
    "Navegar", "mover", "pag",
    "Acciones", "exp", "abrir", "borrar",
    "Sistema", "color", "salir",
    {"Negro", "Rojo", "Verde", "Amarillo", "Azul", "Magenta", "Cyan", "Blanco"},
    "Color de FONDO barra:", "Color de TEXTO barra:",
    "Colores no soportados en esta terminal.",
    "\u25c2\u25c2 Anterior (", "\u25b8\u25b8 Siguiente ("
};

inline const Lang* L = &LANG_EN;
