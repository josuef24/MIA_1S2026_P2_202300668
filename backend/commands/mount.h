// ============================================================
// mount.h — Montar y desmontar particiones
// ============================================================
#pragma once
#include <string>
#include <sstream>
#include <cstring>
#include "../utils/analyzer.h"
#include "../utils/diskmanager.h"
#include "../utils/mountmanager.h"
#include "../structures/structs.h"

// ── Montar partición ─────────────────────────────────────────
inline std::string cmdMount(const ParsedCommand& cmd) {
    std::string path = cmd.get("path");
    std::string name = cmd.get("name");
    if (path.empty()) return "ERROR: Falta -path\n";
    if (name.empty()) return "ERROR: Falta -name\n";
    if (!fileExists(path)) return "ERROR: Disco no encontrado: " + path + "\n";

    MountManager& mm = MountManager::getInstance();

    // ── Incrementar correlativo (incluso si falla) ────────────
    // Esto es necesario para que los IDs coincidan con el archivo de prueba
    int corr = mm.nextCorrelative(path);
    char diskLetter = mm.getDiskLetter(path);

    // ── Buscar partición en MBR ───────────────────────────────
    MBR mbr;
    if (!readObject(path, 0, mbr))
        return "ERROR: No se pudo leer el MBR\n";

    // Verificar si ya está montada
    if (mm.findByNameAndDisk(name, path))
        return "ERROR: La partición '" + name + "' ya está montada\n";

    // Buscar en primarias y extendidas
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '0' &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {

            // Generar ID: carnet + correlativo + letra disco
            char idBuf[10];
            snprintf(idBuf, sizeof(idBuf), "%s%d%c",
                     CARNET, corr, diskLetter);
            std::string id(idBuf);

            // Actualizar correlativo en MBR
            mbr.mbr_partitions[i].part_correlative = corr;
            strncpy(mbr.mbr_partitions[i].part_id, id.c_str(), 4);
            mbr.mbr_partitions[i].part_id[4] = '\0';
            writeObject(path, 0, mbr);

            // Registrar en tabla de montajes
            MountedPartition mp;
            mp.id         = id;
            mp.diskPath   = path;
            mp.partName   = name;
            mp.partStart  = mbr.mbr_partitions[i].part_start;
            mp.partSize   = mbr.mbr_partitions[i].part_size;
            mp.partType   = mbr.mbr_partitions[i].part_type;
            mp.isLogical  = false;
            mm.partitions.push_back(mp);

            return "SUCCESS: Partición '" + name + "' montada con ID: " + id + "\n";
        }
    }

    // Buscar en lógicas (EBR)
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '0' &&
            mbr.mbr_partitions[i].part_type == 'E') {
            int ebrPos = mbr.mbr_partitions[i].part_start;
            EBR ebr;
            while (true) {
                readObject(path, ebrPos, ebr);
                if (ebr.ebr_size > 0 &&
                    std::string(ebr.ebr_name) == name) {
                    char idBuf[10];
                    snprintf(idBuf, sizeof(idBuf), "%s%d%c",
                             CARNET, corr, diskLetter);
                    std::string id(idBuf);

                    ebr.ebr_mount = '1';
                    writeObject(path, ebrPos, ebr);

                    MountedPartition mp;
                    mp.id        = id;
                    mp.diskPath  = path;
                    mp.partName  = name;
                    mp.partStart = ebr.ebr_start;
                    mp.partSize  = ebr.ebr_size;
                    mp.partType  = 'L';
                    mp.isLogical = true;
                    mm.partitions.push_back(mp);

                    return "SUCCESS: Partición lógica '" + name +
                           "' montada con ID: " + id + "\n";
                }
                if (ebr.ebr_next == -1) break;
                ebrPos = ebr.ebr_next;
            }
        }
    }

    return "ERROR: Partición '" + name + "' no encontrada en " + path + "\n";
}

// ── Desmontar partición ──────────────────────────────────────
inline std::string cmdUnmount(const ParsedCommand& cmd) {
    std::string id = cmd.get("id");
    if (id.empty()) return "ERROR: Falta parámetro -id\n";

    MountManager& mm = MountManager::getInstance();
    MountedPartition* mp = mm.findById(id);
    if (!mp) return "ERROR: ID '" + id + "' no está montado\n";

    std::string partName = mp->partName;
    std::string diskPath = mp->diskPath;

    // Actualizar correlativo a 0 en MBR
    MBR mbr;
    if (readObject(diskPath, 0, mbr)) {
        for (int i = 0; i < 4; i++) {
            if (std::string(mbr.mbr_partitions[i].part_name) == partName) {
                mbr.mbr_partitions[i].part_correlative = 0;
                memset(mbr.mbr_partitions[i].part_id, 0, 5);
                writeObject(diskPath, 0, mbr);
                break;
            }
        }
    }

    mm.unmount(id);
    return "SUCCESS: Partición ID '" + id + "' (" + partName + ") desmontada\n";
}

// ── Listar particiones montadas ───────────────────────────────
inline std::string cmdMounted(const ParsedCommand& cmd) {
    MountManager& mm = MountManager::getInstance();
    if (mm.partitions.empty())
        return "INFO: No hay particiones montadas actualmente\n";

    std::ostringstream oss;
    oss << "Particiones montadas:\n";
    oss << "┌──────────────┬─────────────────────────────────────┬─────────────┬──────┐\n";
    oss << "│ ID           │ Disco                               │ Partición   │ Tipo │\n";
    oss << "├──────────────┼─────────────────────────────────────┼─────────────┼──────┤\n";
    for (auto& mp : mm.partitions) {
        oss << "│ " << mp.id << std::string(13 - mp.id.size(), ' ')
            << "│ " << mp.diskPath.substr(mp.diskPath.rfind('/')+1)
            << std::string(36 - mp.diskPath.substr(mp.diskPath.rfind('/')+1).size(), ' ')
            << "│ " << mp.partName
            << std::string(12 - mp.partName.size(), ' ')
            << "│  " << mp.partType << "   │\n";
    }
    oss << "└──────────────┴─────────────────────────────────────┴─────────────┴──────┘\n";
    return oss.str();
}
