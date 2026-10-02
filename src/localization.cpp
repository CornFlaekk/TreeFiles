#include "localization.h"
#include <array>
#include <cstddef>

namespace {
bool spanish = false;
constexpr size_t text_count = static_cast<size_t>(Text::Count);
const std::array<const char*, text_count> english = {
    "Previous", "Next", "Page", "Navigate", "Actions", "System",
    "[j/k] move", "[N/P] page", "[h/l/E] fold", "[Spc] open", "[Del] delete", "[B] color", "[Q] quit",
    "Scan", " Yes ", " No ", "Bar BACKGROUND color:", "Bar TEXT color:",
    "Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White",
    "Loading", "Delete", "Error", "Colors are not supported by this terminal.",
    "Usage: treefiles [--headless] [--page-size N] [--lang en|es] [--save-settings] [--sort size|name|mtime] [--order asc|desc] [directory]\n"
    "  --page-size N  Entries per directory page (default: 30).\n"
    "                 Positive integer, maximum 2147483647.\n"
    "  --lang en|es   Interface language (default: en).\n"
    "  --sort KEY     Sort by size, name, or mtime (default: size).\n"
    "  --order DIR    Sort ascending or descending (default: desc).\n"
    "                 S cycles sort key; T toggles order.\n"
    "  --headless     Read events from stdin and print state frames.\n"
    "  --save-settings Save the effective language and page size for future runs.\n"
    "Settings priority: defaults, config.ini, then command-line options.\n"
    "                 Enter navigates, Backspace returns to parent, O enters a path.\n"
    "                 Headless: ENTER, BACKSPACE, CD <path>, REFRESH, SORT <key> <order>.\n"
    "  --version      Show the application version.\n"
    "  --help, -h     Show this help.\n",
    "--page-size requires a positive integer from 1 to 2147483647.",
    "--lang requires en or es.",
    "Unknown option: ", "Use --help for usage.", "Not a readable directory: ",
    "Cannot read color settings: ", "Cannot save color settings: ",
    "Cannot open this path", "Cannot run xdg-open: ", "bytes", "Invalid COLOR event.",
    "[W] warnings", "Scan completed with omitted items", "Scan diagnostics", "Press any key",
    "partial", "unavailable", "[R] refresh", "[Enter] enter", "[Bksp] parent", "[O] path",
    "Directory path (relative to the current root or absolute):", "Enter accepts, Esc cancels",
    "Cannot change directory: ", "--sort requires size, name, or mtime.",
    "--order requires asc or desc.", "Invalid SORT event.",
    "[S] sort [T] order", "Sort", "size", "name", "mtime"
};
const std::array<const char*, text_count> spanish_text = {
    "Anterior", "Siguiente", "Pag", "Navegar", "Acciones", "Sistema",
    "[j/k] mover", "[N/P] pag", "[h/l/E] exp", "[Spc] abrir", "[Del] borrar", "[B] color", "[Q] salir",
    "Escaneo", " S\u00ed ", " No ", "Color de FONDO barra:", "Color de TEXTO barra:",
    "Negro", "Rojo", "Verde", "Amarillo", "Azul", "Magenta", "Cian", "Blanco",
    "Cargando", "Borrar", "Error", "Colores no soportados en esta terminal.",
    "Uso: treefiles [--headless] [--page-size N] [--lang en|es] [--save-settings] [--sort size|name|mtime] [--order asc|desc] [directorio]\n"
    "  --page-size N  Elementos por p\u00e1gina y directorio (predeterminado: 30).\n"
    "                 Entero positivo, m\u00e1ximo 2147483647.\n"
    "  --lang en|es   Idioma de la interfaz (predeterminado: en).\n"
    "  --sort KEY     Ordena por tama\u00f1o, nombre o mtime (predeterminado: size).\n"
    "  --order DIR    Orden ascendente o descendente (predeterminado: desc).\n"
    "                 S cambia el criterio; T alterna el sentido.\n"
    "  --headless     Lee eventos de stdin e imprime el estado.\n"
    "  --save-settings Guarda el idioma y tama\u00f1o efectivos para futuras sesiones.\n"
    "Prioridad: valores por defecto, config.ini y opciones CLI.\n"
    "                 Enter navega, Backspace vuelve al padre, O escribe una ruta.\n"
    "                 Headless: ENTER, BACKSPACE, CD <ruta>, REFRESH, SORT <key> <order>.\n"
    "  --version      Muestra la versi\u00f3n de la aplicaci\u00f3n.\n"
    "  --help, -h     Muestra esta ayuda.\n",
    "--page-size requiere un entero positivo de 1 a 2147483647.",
    "--lang requiere en o es.",
    "Opci\u00f3n desconocida: ", "Usa --help para ver la ayuda.", "No se puede leer el directorio: ",
    "No se pueden leer los colores guardados: ", "No se pueden guardar los colores: ",
    "No se puede abrir esta ruta", "No se puede ejecutar xdg-open: ", "bytes", "Evento COLOR no v\u00e1lido.",
    "[W] avisos", "Escaneo terminado con elementos omitidos", "Diagn\u00f3sticos del escaneo", "Pulsa cualquier tecla",
    "parcial", "no disponible", "[R] actualizar", "[Enter] entrar", "[Bksp] padre", "[O] ruta",
    "Ruta del directorio (relativa a la ra\u00edz actual o absoluta):", "Enter acepta, Esc cancela",
    "No se puede cambiar de directorio: ", "--sort requiere size, name o mtime.",
    "--order requiere asc o desc.", "Evento SORT no v\u00e1lido.",
    "[S] ordenar [T] sentido", "Orden", "tama\u00f1o", "nombre", "fecha"
};
}

bool set_language(const std::string& code) {
    if (code != "en" && code != "es") return false;
    spanish = code == "es";
    return true;
}

const char* language_code() { return spanish ? "es" : "en"; }

const char* text(Text key) {
    return (spanish ? spanish_text : english).at(static_cast<size_t>(key));
}
