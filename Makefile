# Nombre del ejecutable
TARGET = treefiles

# Compilador y flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Iinclude

# Librerías necesarias
LIBS = -lncursesw

# Directorios
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Archivos fuente y objeto
SRC = $(wildcard $(SRC_DIR)/*.cpp)
OBJ = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRC))

# Testing
TEST_DIR = tests
TEST_BUILD_DIR = build/tests
TEST_TMP = /tmp/treefiles_test
TEST_SRC = $(wildcard $(TEST_DIR)/test_*.cpp)
TEST_BIN = $(patsubst $(TEST_DIR)/%.cpp, $(TEST_BUILD_DIR)/%, $(TEST_SRC))

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

# Crear carpeta de tests
$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

# Compilar cada test como ejecutable independiente
$(TEST_BUILD_DIR)/%: $(TEST_DIR)/%.cpp build/file_utils.o | $(TEST_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Ejecutar tests unitarios
test-unit: $(TEST_BIN)
	@passed=0; failed=0; \
	for t in $(TEST_BIN); do \
		printf "Running $$t... "; \
		if $$t; then \
			echo "PASS"; \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "$$passed passed, $$failed failed"; \
	test $$failed -eq 0

# Ejecutar tests de integración
test-integration:
	@if [ -f scripts/test_scenarios.sh ]; then \
		bash scripts/test_scenarios.sh; \
	else \
		echo "scripts/test_scenarios.sh not found (will be added in integration tests branch)"; \
	fi

# Ejecutar todos los tests
test: test-unit test-integration

# Instalar symlink treef → treefiles en ~/.local/bin
install: $(TARGET)
	mkdir -p "$$HOME/.local/bin"
	ln -sf "$(PWD)/$(TARGET)" "$$HOME/.local/bin/treef"
	@echo "treef → $(PWD)/$(TARGET)"
	@echo "Asegúrate de tener ~/.local/bin en tu PATH"

# Limpiar archivos generados
clean:
	rm -rf $(BUILD_DIR) $(TARGET)
