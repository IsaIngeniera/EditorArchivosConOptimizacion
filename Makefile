# ============================================================
#  Makefile — Editor de Archivos con Optimización de I/O
# ============================================================
#
# Comandos disponibles:
#   make          → Compila el proyecto completo
#   make clean    → Elimina los binarios y objetos
#   make test     → Compila y ejecuta los tests
#   make valgrind → Corre el editor bajo valgrind (para Persona 3)
#   make debug    → Compila con símbolos de depuración
#
# Dependencias del sistema (WSL/Ubuntu):
#   sudo apt install libncurses5-dev libncursesw5-dev valgrind
# ============================================================

CC      = gcc
TARGET  = editor
TESTBIN = test_runner

# Flags de compilación
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11
CFLAGS += -I./src

# Flag de ncurses (librería para la UI)
LIBS    = -lncurses

# Flags de optimización para release
RELEASE_FLAGS = -O2

# Flags para debug (con símbolos, sin optimización)
DEBUG_FLAGS   = -g -O0 -DDEBUG

# ── Archivos fuente ─────────────────────────────────────────
SRCS = src/main.c                   \
       src/editor/gap_buffer.c      \
       src/editor/rich_text.c       \
       src/editor/line_list.c       \
       src/editor/editor_api.c      \
       src/ui/ncurses_ui.c          \
       src/io/file_manager.c        \
       src/io/compressor.c

# Archivos de test (Persona 3 corre estos)
TEST_SRCS = tests/test_gap_buffer.c  \
            tests/test_editor.c      \
            src/editor/gap_buffer.c  \
            src/editor/rich_text.c   \
            src/editor/line_list.c   \
            src/editor/editor_api.c  \
            src/io/file_manager.c    \
            src/io/compressor.c

# Objetos generados desde los sources (reemplazamos .c → .o)
OBJS = $(SRCS:.c=.o)

# ── Target principal ────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $@ $^ $(LIBS)
	@echo ""
	@echo "✅  Compilación exitosa → ./$(TARGET)"
	@echo "    Uso: ./$(TARGET) [archivo]"

# Compilación de cada .c → .o
%.o: %.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -c -o $@ $<

# ── Debug ────────────────────────────────────────────────────
debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)
	@echo "🔍  Build de debug listo → ./$(TARGET)"

# ── Tests ────────────────────────────────────────────────────
test:
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) -o $(TESTBIN) $(TEST_SRCS) $(LIBS)
	@echo "🧪  Tests compilados → ./$(TESTBIN)"
	./$(TESTBIN)

# ── Valgrind (para Persona 3) ────────────────────────────────
#
# Corre el editor bajo valgrind para detectar memory leaks.
# La salida del test se redirige automáticamente para que
# valgrind pueda terminar (simula un flujo de edición y salida).
valgrind: test
	@echo "🔬  Corriendo bajo Valgrind..."
	valgrind \
	    --leak-check=full \
	    --show-leak-kinds=all \
	    --track-origins=yes \
	    --verbose \
	    --log-file=valgrind_report.txt \
	    ./$(TESTBIN)
	@echo "📄  Reporte de Valgrind guardado en valgrind_report.txt"

# ── Strace (para Persona 3) ──────────────────────────────────
#
# Cuenta las llamadas al sistema. Persona 3 usa esto para
# comparar el enfoque clásico vs nuestro enfoque optimizado.
strace-profile:
	@echo "📊  Profiling con strace..."
	printf "Hola mundo\023\030s" | strace -c -o strace_report.txt ./$(TARGET)
	@echo "📄  Reporte de strace guardado en strace_report.txt"
	cat strace_report.txt

# ── Limpieza ─────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET) $(TESTBIN)
	rm -f valgrind_report.txt strace_report.txt
	@echo "🧹  Limpieza completa"

# ── Help ─────────────────────────────────────────────────────
help:
	@echo "Comandos disponibles:"
	@echo "  make          → Compila el proyecto"
	@echo "  make debug    → Compila con símbolos de depuración"
	@echo "  make test     → Corre los tests unitarios"
	@echo "  make valgrind → Detecta memory leaks (Persona 3)"
	@echo "  make strace-profile → Cuenta syscalls (Persona 3)"
	@echo "  make clean    → Elimina binarios"

.PHONY: all debug test valgrind strace-profile clean help