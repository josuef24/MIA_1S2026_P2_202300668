// ============================================================
// rmdisk.h — Eliminar disco virtual
// ============================================================
#pragma once
#include <string>
#include <cstdio>
#include "../utils/analyzer.h"
#include "../utils/diskmanager.h"

inline std::string cmdRmdisk(const ParsedCommand& cmd) {
    // ── Validar parámetros ────────────────────────────────────
    std::string path = cmd.get("path");
    if (path.empty()) return "ERROR: Falta parámetro -path\n";

    // ── Verificar que existe ──────────────────────────────────
    if (!fileExists(path))
        return "ERROR: El disco no existe: " + path + "\n";

    // ── Eliminar archivo ─────────────────────────────────────
    if (std::remove(path.c_str()) != 0)
        return "ERROR: No se pudo eliminar el disco: " + path + "\n";

    return "SUCCESS: Disco eliminado: " + path + "\n";
}
