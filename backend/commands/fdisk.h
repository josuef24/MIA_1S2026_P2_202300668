// ============================================================
// fdisk.h — Crear, modificar y eliminar particiones
// ============================================================
#pragma once
#include <string>
#include <cstring>
#include <algorithm>
#include "../utils/analyzer.h"
#include "../utils/diskmanager.h"
#include "../structures/structs.h"

// ── Helpers internos ─────────────────────────────────────────
static inline std::string toUpperFd(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ── Encontrar espacio libre en el disco con ajuste ───────────
static int findFreeSpace(MBR& mbr, int requiredSize, char fit,
                          int diskSize) {
    // Recolectar regiones ocupadas (particiones primarias/extendidas)
    std::vector<std::pair<int,int>> used; // (start, end)
    used.push_back({0, (int)sizeof(MBR) - 1}); // MBR al inicio
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '0' &&
            mbr.mbr_partitions[i].part_start != -1) {
            int s = mbr.mbr_partitions[i].part_start;
            int e = s + mbr.mbr_partitions[i].part_size - 1;
            used.push_back({s, e});
        }
    }
    std::sort(used.begin(), used.end());

    // Encontrar huecos libres
    std::vector<std::pair<int,int>> free_spaces;
    for (size_t i = 0; i + 1 < used.size(); i++) {
        int start = used[i].second + 1;
        int end   = used[i+1].first - 1;
        if (end - start + 1 >= requiredSize)
            free_spaces.push_back({start, end - start + 1});
    }
    // Espacio al final
    if (!used.empty()) {
        int start = used.back().second + 1;
        int avail = diskSize - start;
        if (avail >= requiredSize)
            free_spaces.push_back({start, avail});
    }

    if (free_spaces.empty()) return -1;

    if (fit == 'F') return free_spaces.front().first;     // First Fit
    if (fit == 'B') {
        // Best Fit: más pequeño que encaje
        auto best = free_spaces[0];
        for (auto& f : free_spaces)
            if (f.second >= requiredSize && f.second < best.second)
                best = f;
        return best.first;
    }
    if (fit == 'W') {
        // Worst Fit: más grande
        auto worst = free_spaces[0];
        for (auto& f : free_spaces)
            if (f.second > worst.second) worst = f;
        return worst.first;
    }
    return free_spaces.front().first;
}

// ── Crear partición primaria o extendida ─────────────────────
static std::string createPartition(MBR& mbr, const ParsedCommand& cmd,
                                    const std::string& path) {
    // Contar primarias + extendidas
    int countPE = 0;
    bool hasExtended = false;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '0') {
            countPE++;
            if (mbr.mbr_partitions[i].part_type == 'E') hasExtended = true;
        }
    }
    if (countPE >= 4)
        return "ERROR: Límite de 4 particiones primarias+extendidas alcanzado\n";

    char type = toUpperFd(cmd.get("type", "P"))[0];
    if (type == 'E' && hasExtended)
        return "ERROR: Ya existe una partición extendida en este disco\n";

    // Tamaño
    std::string unitStr = toUpperFd(cmd.get("unit", "K"));
    int size = std::stoi(cmd.get("size"));
    if (size <= 0) return "ERROR: El tamaño debe ser positivo\n";
    long bytes;
    if      (unitStr == "B") bytes = size;
    else if (unitStr == "K") bytes = (long)size * 1024;
    else if (unitStr == "M") bytes = (long)size * 1024 * 1024;
    else return "ERROR: Unidad inválida\n";

    // Fit
    std::string fitStr = toUpperFd(cmd.get("fit", "WF"));
    if (fitStr != "BF" && fitStr != "FF" && fitStr != "WF")
        return "ERROR: Fit inválido\n";
    char fit = fitStr[0];

    // Nombre
    std::string name = cmd.get("name");
    if (name.empty()) return "ERROR: Falta -name\n";
    // Verificar nombre único en el disco
    for (int i = 0; i < 4; i++)
        if (mbr.mbr_partitions[i].part_status != '0' &&
            std::string(mbr.mbr_partitions[i].part_name) == name)
            return "ERROR: Ya existe una partición con ese nombre\n";

    // Encontrar espacio libre
    int startPos = findFreeSpace(mbr, (int)bytes, fit, mbr.mbr_tamano);
    if (startPos == -1)
        return "ERROR: No hay espacio suficiente en el disco\n";

    // Asignar partición
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '0') {
            mbr.mbr_partitions[i].part_status = '1';
            mbr.mbr_partitions[i].part_type   = type;
            mbr.mbr_partitions[i].part_fit    = fit;
            mbr.mbr_partitions[i].part_start  = startPos;
            mbr.mbr_partitions[i].part_size   = (int)bytes;
            mbr.mbr_partitions[i].part_correlative = 0;
            strncpy(mbr.mbr_partitions[i].part_name, name.c_str(), 15);
            mbr.mbr_partitions[i].part_name[15] = '\0';

            // Si es extendida, inicializar primer EBR
            if (type == 'E') {
                EBR firstEBR;
                memset(&firstEBR, 0, sizeof(EBR));
                firstEBR.ebr_mount = '0';
                firstEBR.ebr_fit   = fit;
                firstEBR.ebr_start = startPos;
                firstEBR.ebr_size  = 0;
                firstEBR.ebr_next  = -1;
                writeObject(path, startPos, firstEBR);
            }
            break;
        }
    }
    return "SUCCESS: Partición " + name + " creada [" + fitStr + ", " +
           std::to_string(bytes) + " bytes]\n";
}

// ── Crear partición lógica dentro de extendida ───────────────
static std::string createLogical(MBR& mbr, const ParsedCommand& cmd,
                                  const std::string& path) {
    // Encontrar partición extendida
    int extIdx = -1;
    for (int i = 0; i < 4; i++)
        if (mbr.mbr_partitions[i].part_status != '0' &&
            mbr.mbr_partitions[i].part_type == 'E')
            { extIdx = i; break; }
    if (extIdx == -1)
        return "ERROR: No existe partición extendida en este disco\n";

    Partition& ext = mbr.mbr_partitions[extIdx];
    int extEnd = ext.part_start + ext.part_size;

    // Tamaño
    std::string unitStr = toUpperFd(cmd.get("unit", "K"));
    int size = std::stoi(cmd.get("size"));
    long bytes;
    if      (unitStr == "B") bytes = size;
    else if (unitStr == "K") bytes = (long)size * 1024;
    else if (unitStr == "M") bytes = (long)size * 1024 * 1024;
    else return "ERROR: Unidad inválida\n";

    std::string fitStr = toUpperFd(cmd.get("fit", "WF"));
    char fit = fitStr[0];
    std::string name = cmd.get("name");
    if (name.empty()) return "ERROR: Falta -name\n";

    // Recorrer EBRs para encontrar el último y espacio libre
    int ebrPos = ext.part_start;
    EBR prev, curr;
    memset(&prev, 0, sizeof(EBR));
    int lastEBRPos = ebrPos;
    int usedEnd = ext.part_start; // fin del último bloque lógico

    // Recorrer cadena de EBRs
    bool first = true;
    while (true) {
        readObject(path, ebrPos, curr);
        if (first) {
            first = false;
            if (curr.ebr_size == 0 && curr.ebr_next == -1) {
                // Primer EBR sin datos aún
                lastEBRPos = ebrPos;
                usedEnd = ebrPos + (int)sizeof(EBR);
                break;
            }
        }
        // Verificar nombre único
        if (curr.ebr_size > 0 && std::string(curr.ebr_name) == name)
            return "ERROR: Nombre de partición lógica ya existe\n";
        lastEBRPos = ebrPos;
        usedEnd = curr.ebr_start + (int)sizeof(EBR) + curr.ebr_size;
        if (curr.ebr_next == -1) break;
        ebrPos = curr.ebr_next;
        readObject(path, ebrPos, prev);
    }

    // Posición del nuevo EBR (después del último)
    int newEBRPos;
    readObject(path, lastEBRPos, curr);
    if (curr.ebr_size == 0) {
        // Primer lógico: usar el EBR inicial
        newEBRPos = ext.part_start;
    } else {
        newEBRPos = curr.ebr_start + curr.ebr_size;
        // Actualizar next del EBR anterior
        curr.ebr_next = newEBRPos;
        writeObject(path, lastEBRPos, curr);
    }

    // Verificar espacio dentro de la extendida
    int dataStart = newEBRPos + (int)sizeof(EBR);
    if (dataStart + bytes > extEnd)
        return "ERROR: No hay espacio en la partición extendida\n";

    // Escribir nuevo EBR
    EBR newEBR;
    memset(&newEBR, 0, sizeof(EBR));
    newEBR.ebr_mount = '0';
    newEBR.ebr_fit   = fit;
    newEBR.ebr_start = dataStart;
    newEBR.ebr_size  = (int)bytes;
    newEBR.ebr_next  = -1;
    strncpy(newEBR.ebr_name, name.c_str(), 15);
    newEBR.ebr_name[15] = '\0';
    writeObject(path, newEBRPos, newEBR);

    return "SUCCESS: Partición lógica " + name + " creada [" +
           std::to_string(bytes) + " bytes]\n";
}

// ── Agregar/quitar espacio a una partición ───────────────────
static std::string addPartition(MBR& mbr, const ParsedCommand& cmd,
                                 const std::string& path) {
    std::string name = cmd.get("name");
    std::string unitStr = toUpperFd(cmd.get("unit", "K"));
    int add = std::stoi(cmd.get("add"));
    long bytes;
    if      (unitStr == "B") bytes = add;
    else if (unitStr == "K") bytes = (long)add * 1024;
    else if (unitStr == "M") bytes = (long)add * 1024 * 1024;
    else return "ERROR: Unidad inválida\n";

    // Buscar en primarias/extendidas
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '0' &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            int newSize = mbr.mbr_partitions[i].part_size + (int)bytes;
            if (newSize <= 0)
                return "ERROR: El tamaño resultante sería no positivo\n";
            if (bytes > 0) {
                // Verificar espacio libre después de la partición
                int endCurrent = mbr.mbr_partitions[i].part_start +
                                 mbr.mbr_partitions[i].part_size;
                // Buscar si hay otra partición inmediatamente después
                for (int j = 0; j < 4; j++) {
                    if (j == i) continue;
                    if (mbr.mbr_partitions[j].part_status != '0' &&
                        mbr.mbr_partitions[j].part_start < endCurrent + bytes &&
                        mbr.mbr_partitions[j].part_start >= endCurrent)
                        return "ERROR: No hay espacio libre suficiente después de la partición\n";
                }
                if (endCurrent + bytes > mbr.mbr_tamano)
                    return "ERROR: No hay espacio libre en el disco\n";
            }
            mbr.mbr_partitions[i].part_size = newSize;
            return "SUCCESS: Partición " + name + " ajustada a " +
                   std::to_string(newSize) + " bytes\n";
        }
    }
    return "ERROR: Partición '" + name + "' no encontrada\n";
}

// ── Eliminar partición ───────────────────────────────────────
static std::string deletePartition(MBR& mbr, const ParsedCommand& cmd,
                                    const std::string& path) {
    std::string name   = cmd.get("name");
    std::string delStr = toUpperFd(cmd.get("delete"));
    if (delStr != "FAST" && delStr != "FULL")
        return "ERROR: Valor de -delete inválido. Use fast o full\n";

    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '0' &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            if (delStr == "FULL") {
                // Rellenar espacio con \0
                zeroFill(path, mbr.mbr_partitions[i].part_start,
                         mbr.mbr_partitions[i].part_size);
            }
            // Marcar como libre
            int sz = mbr.mbr_partitions[i].part_size;
            int st = mbr.mbr_partitions[i].part_start;
            memset(&mbr.mbr_partitions[i], 0, sizeof(Partition));
            mbr.mbr_partitions[i].part_status = '0';
            mbr.mbr_partitions[i].part_start  = -1;
            return "SUCCESS: Partición " + name + " eliminada (" +
                   delStr + ", " + std::to_string(sz) + " bytes desde " +
                   std::to_string(st) + ")\n";
        }
    }

    // Buscar en lógicas (EBR)
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '0' &&
            mbr.mbr_partitions[i].part_type == 'E') {
            int ebrPos = mbr.mbr_partitions[i].part_start;
            int prevPos = -1;
            EBR curr, prev;
            while (true) {
                readObject(path, ebrPos, curr);
                if (curr.ebr_size == 0 && curr.ebr_next == -1) break;
                if (std::string(curr.ebr_name) == name) {
                    if (delStr == "FULL")
                        zeroFill(path, curr.ebr_start, curr.ebr_size);
                    // Actualizar puntero anterior
                    if (prevPos != -1) {
                        readObject(path, prevPos, prev);
                        prev.ebr_next = curr.ebr_next;
                        writeObject(path, prevPos, prev);
                    }
                    // Limpiar EBR actual
                    int nextPos = curr.ebr_next;
                    memset(&curr, 0, sizeof(EBR));
                    curr.ebr_next = -1;
                    curr.ebr_start = ebrPos;
                    writeObject(path, ebrPos, curr);
                    return "SUCCESS: Partición lógica " + name + " eliminada\n";
                }
                if (curr.ebr_next == -1) break;
                prevPos = ebrPos;
                ebrPos = curr.ebr_next;
            }
        }
    }
    return "ERROR: Partición '" + name + "' no encontrada\n";
}

// ── Dispatcher principal de FDISK ────────────────────────────
inline std::string cmdFdisk(const ParsedCommand& cmd) {
    std::string path = cmd.get("path");
    if (path.empty()) return "ERROR: Falta parámetro -path\n";
    if (!fileExists(path)) return "ERROR: Disco no encontrado: " + path + "\n";

    MBR mbr;
    if (!readObject(path, 0, mbr))
        return "ERROR: No se pudo leer el MBR\n";

    std::string result;

    // ── Modo: eliminar ────────────────────────────────────────
    if (cmd.params.count("delete")) {
        std::string name = cmd.get("name");
        if (name.empty()) return "ERROR: Falta -name para eliminar\n";
        result = deletePartition(mbr, cmd, path);
        writeObject(path, 0, mbr);
        return result;
    }

    // ── Modo: agregar/quitar espacio ──────────────────────────
    if (cmd.params.count("add")) {
        result = addPartition(mbr, cmd, path);
        writeObject(path, 0, mbr);
        return result;
    }

    // ── Modo: crear partición ─────────────────────────────────
    if (!cmd.params.count("size"))
        return "ERROR: Falta -size para crear partición\n";
    if (!cmd.params.count("name"))
        return "ERROR: Falta -name para crear partición\n";

    std::string typeStr = toUpperFd(cmd.get("type", "P"));
    char type = typeStr[0];

    if (type == 'L')
        result = createLogical(mbr, cmd, path);
    else
        result = createPartition(mbr, cmd, path);

    writeObject(path, 0, mbr);
    return result;
}
