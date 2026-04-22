// ============================================================
// diskmanager.h — Utilidades de lectura/escritura en disco
// ============================================================
#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <sys/stat.h>
#include <ctime>
#include "../structures/structs.h"

// ── Fecha/hora actual como string ────────────────────────────
inline std::string currentDateTime() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return std::string(buf);
}

// ── Crear directorio recursivamente ─────────────────────────
inline bool mkdirRecursive(const std::string& path) {
    std::string tmp = path;
    for (size_t i = 1; i < tmp.size(); i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp.c_str(), 0755);
            tmp[i] = '/';
        }
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

// ── Crear directorio padre de una ruta ───────────────────────
inline void createParentDirs(const std::string& filepath) {
    size_t pos = filepath.rfind('/');
    if (pos != std::string::npos) {
        mkdirRecursive(filepath.substr(0, pos));
    }
}

// ── Escribir estructura en posición del archivo ──────────────
template<typename T>
bool writeObject(const std::string& path, long offset, const T& obj) {
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f.is_open()) return false;
    f.seekp(offset, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&obj), sizeof(T));
    f.close();
    return true;
}

// ── Leer estructura de posición del archivo ──────────────────
template<typename T>
bool readObject(const std::string& path, long offset, T& obj) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.seekg(offset, std::ios::beg);
    f.read(reinterpret_cast<char*>(&obj), sizeof(T));
    f.close();
    return true;
}

// ── Escribir bytes nulos en un rango del disco ───────────────
inline bool zeroFill(const std::string& path, long offset, long size) {
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f.is_open()) return false;
    f.seekp(offset, std::ios::beg);
    const int BUF = 4096;
    char zeros[BUF] = {};
    long remaining = size;
    while (remaining > 0) {
        long toWrite = remaining < BUF ? remaining : BUF;
        f.write(zeros, toWrite);
        remaining -= toWrite;
    }
    f.close();
    return true;
}

// ── Verificar que un archivo existe ─────────────────────────
inline bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// ── Leer un byte del bitmap ──────────────────────────────────
inline char readBitmap(const std::string& diskPath, long bmStart, int index) {
    std::ifstream f(diskPath, std::ios::binary);
    if (!f.is_open()) return -1;
    f.seekg(bmStart + index, std::ios::beg);
    char val;
    f.read(&val, 1);
    f.close();
    return val;
}

// ── Escribir un byte en el bitmap ────────────────────────────
inline bool writeBitmap(const std::string& diskPath, long bmStart, int index, char val) {
    std::fstream f(diskPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!f.is_open()) return false;
    f.seekp(bmStart + index, std::ios::beg);
    f.write(&val, 1);
    f.close();
    return true;
}

// ── Encontrar primer bit libre en bitmap ─────────────────────
inline int findFreeBit(const std::string& diskPath, long bmStart, int total) {
    std::ifstream f(diskPath, std::ios::binary);
    if (!f.is_open()) return -1;
    f.seekg(bmStart, std::ios::beg);
    for (int i = 0; i < total; i++) {
        char val;
        f.read(&val, 1);
        if (val == '0') { f.close(); return i; }
    }
    f.close();
    return -1;
}
