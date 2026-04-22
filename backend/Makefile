# ============================================================
# Makefile — Compilación del backend MIA P2
# ============================================================

# ── Compilador y flags ──────────────────────────────────────
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -pthread
TARGET   = servidor

# ── Fuentes (solo main.cpp; los .h son header-only) ─────────
SRCS = main.cpp

# ── Regla principal ──────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)
	@echo "✓ Compilado: ./$(TARGET)"

# ── Ejecutar localmente ──────────────────────────────────────
run: $(TARGET)
	./$(TARGET) 8080

# ── Ejecutar en puerto 8080 en background (para EC2) ────────
start: $(TARGET)
	nohup ./$(TARGET) 8080 > server.log 2>&1 &
	@echo "Servidor iniciado en background (ver server.log)"

# ── Detener servidor ─────────────────────────────────────────
stop:
	@pkill -f "$(TARGET)" 2>/dev/null || echo "Servidor no estaba corriendo"

# ── Limpiar binarios ─────────────────────────────────────────
clean:
	rm -f $(TARGET) server.log

.PHONY: all run start stop clean
