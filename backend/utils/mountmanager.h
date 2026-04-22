// ============================================================
// mountmanager.h — Tabla global de particiones montadas
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include "../structures/structs.h"

// ── Entrada de partición montada ─────────────────────────────
struct MountedPartition {
    std::string id;           // ID asignado (ej: "682A")
    std::string diskPath;     // Ruta al archivo .mia
    std::string partName;     // Nombre de la partición
    int         partStart;    // Inicio en bytes
    int         partSize;     // Tamaño en bytes
    char        partType;     // 'P', 'E', 'L'
    bool        isLogical;    // true si partición lógica
};

// ── Gestor global de montajes ────────────────────────────────
class MountManager {
public:
    // ── Instancia única ──────────────────────────────────────
    static MountManager& getInstance() {
        static MountManager instance;
        return instance;
    }

    std::vector<MountedPartition>  partitions;   // Lista de montadas
    std::map<std::string, int>     diskCounter;  // Contador correlativo por disco

    // ── Obtener letra de disco (A, B, C...) ─────────────────
    char getDiskLetter(const std::string& diskPath) {
        if (diskLetters.find(diskPath) == diskLetters.end()) {
            diskLetters[diskPath] = 'A' + (char)diskLetters.size();
        }
        return diskLetters[diskPath];
    }

    // ── Incrementar y obtener correlativo del disco ──────────
    int nextCorrelative(const std::string& diskPath) {
        return ++diskCounter[diskPath];
    }

    // ── Verificar si un ID está montado ─────────────────────
    MountedPartition* findById(const std::string& id) {
        for (auto& mp : partitions)
            if (mp.id == id) return &mp;
        return nullptr;
    }

    // ── Verificar si una partición por nombre está montada ───
    MountedPartition* findByNameAndDisk(const std::string& name,
                                        const std::string& disk) {
        for (auto& mp : partitions)
            if (mp.partName == name && mp.diskPath == disk) return &mp;
        return nullptr;
    }

    // ── Desmontar por ID ─────────────────────────────────────
    bool unmount(const std::string& id) {
        for (auto it = partitions.begin(); it != partitions.end(); ++it) {
            if (it->id == id) {
                partitions.erase(it);
                return true;
            }
        }
        return false;
    }

private:
    MountManager() {}
    std::map<std::string, char> diskLetters; // disco → letra
};
