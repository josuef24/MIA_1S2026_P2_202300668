// ============================================================
// mkdisk.h — Crear disco virtual (.mia)
// ============================================================
#pragma once
#include <string>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include "../utils/analyzer.h"
#include "../utils/diskmanager.h"
#include "../structures/structs.h"

inline std::string cmdMkdisk(const ParsedCommand& cmd) {
    // ── Validar parámetros ────────────────────────────────────
    if (cmd.params.find("size") == cmd.params.end())
        return "ERROR: Falta parámetro -size\n";
    if (cmd.params.find("path") == cmd.params.end())
        return "ERROR: Falta parámetro -path\n";

    int size = std::stoi(cmd.get("size"));
    if (size <= 0)
        return "ERROR: El tamaño debe ser positivo y mayor a 0\n";

    // ── Convertir a bytes según unidad ────────────────────────
    std::string unit = toUpper(cmd.get("unit", "M"));
    long totalBytes;
    if      (unit == "B") totalBytes = size;
    else if (unit == "K") totalBytes = (long)size * 1024;
    else if (unit == "M") totalBytes = (long)size * 1024 * 1024;
    else return "ERROR: Unidad inválida. Use B, K o M\n";

    // ── Validar tamaño mínimo ─────────────────────────────────
    if (totalBytes < (long)sizeof(MBR))
        return "ERROR: Tamaño demasiado pequeño\n";

    // ── Fit ───────────────────────────────────────────────────
    std::string fitStr = toUpper(cmd.get("fit", "FF"));
    if (fitStr != "BF" && fitStr != "FF" && fitStr != "WF")
        return "ERROR: Fit inválido. Use BF, FF o WF\n";
    char fit = fitStr[0]; // 'B', 'F', 'W'

    // ── Ruta ──────────────────────────────────────────────────
    std::string path = cmd.get("path");
    if (path.empty()) return "ERROR: Ruta vacía\n";

    // ── Crear directorios padres ──────────────────────────────
    createParentDirs(path);

    // ── Crear archivo binario con ceros ───────────────────────
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return "ERROR: No se pudo crear el archivo en " + path + "\n";

    // Escribir ceros (1MB a la vez)
    const int BUF = 1024 * 1024;
    char* zeros = new char[BUF]();
    long remaining = totalBytes;
    while (remaining > 0) {
        long toWrite = remaining < BUF ? remaining : BUF;
        f.write(zeros, toWrite);
        remaining -= toWrite;
    }
    delete[] zeros;
    f.close();

    // ── Crear e inicializar MBR ───────────────────────────────
    MBR mbr;
    memset(&mbr, 0, sizeof(MBR));
    mbr.mbr_tamano = (int)totalBytes;
    mbr.mbr_disk_fit = fit;
    mbr.mbr_disk_signature = rand() % 100000;
    std::string dt = currentDateTime();
    strncpy(mbr.mbr_fecha_creacion, dt.c_str(), 18);
    mbr.mbr_fecha_creacion[18] = '\0';

    // Inicializar particiones como libres
    for (int i = 0; i < 4; i++) {
        mbr.mbr_partitions[i].part_status = '0';
        mbr.mbr_partitions[i].part_start  = -1;
        mbr.mbr_partitions[i].part_size   = 0;
        mbr.mbr_partitions[i].part_correlative = 0;
        memset(mbr.mbr_partitions[i].part_name, 0, 16);
        memset(mbr.mbr_partitions[i].part_id, 0, 5);
    }

    writeObject(path, 0, mbr);

    return "SUCCESS: Disco creado en " + path +
           " [" + std::to_string(size) + unit + " = " +
           std::to_string(totalBytes) + " bytes, fit=" + fitStr + "]\n";
}
