# Nombre del ejecutable
TARGET = treefiles

# Compilador y flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -std=c++17 -Iinclude

# Librerías necesarias
LIBS = -lncurses

# Directorios
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Archivos fuente y objeto
SRC = $(wildcard $(SRC_DIR)/*.cpp)
OBJ = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRC))

# Regla por defecto
all: $(BUILD_DIR) $(TARGET)

# Crear ejecutable
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Crear carpeta build si no existe
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Regla para compilar .cpp a .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Limpiar archivos generados
clean:
	rm -rf $(BUILD_DIR) $(TARGET)
