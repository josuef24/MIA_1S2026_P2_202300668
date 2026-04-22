// ============================================================
// filesystem.h — Operaciones de bajo nivel sobre EXT2/EXT3
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include "../structures/structs.h"
#include "../utils/diskmanager.h"
#include "../utils/session.h"
#include "../utils/permissions.h"

// ── Leer superbloque de una partición montada ────────────────
inline bool getSB(const std::string& disk, int partStart, Superblock& sb) {
    return readObject(disk, partStart, sb);
}

// ── Leer inodo por número ─────────────────────────────────────
inline bool getInode(const Superblock& sb, const std::string& disk,
                     int inodeNum, Inode& inode) {
    long off = sb.sb_inode_start + (long)inodeNum * sizeof(Inode);
    return readObject(disk, off, inode);
}

// ── Escribir inodo por número ─────────────────────────────────
inline bool setInode(const Superblock& sb, const std::string& disk,
                     int inodeNum, const Inode& inode) {
    long off = sb.sb_inode_start + (long)inodeNum * sizeof(Inode);
    return writeObject(disk, off, inode);
}

// ── Leer bloque carpeta ───────────────────────────────────────
inline bool getFolderBlock(const Superblock& sb, const std::string& disk,
                            int blockNum, FolderBlock& fb) {
    long off = sb.sb_block_start + (long)blockNum * BLOCK_SIZE;
    return readObject(disk, off, fb);
}

// ── Escribir bloque carpeta ───────────────────────────────────
inline bool setFolderBlock(const Superblock& sb, const std::string& disk,
                            int blockNum, const FolderBlock& fb) {
    long off = sb.sb_block_start + (long)blockNum * BLOCK_SIZE;
    return writeObject(disk, off, fb);
}

// ── Leer bloque archivo ───────────────────────────────────────
inline bool getFileBlock(const Superblock& sb, const std::string& disk,
                          int blockNum, FileBlock& fileB) {
    long off = sb.sb_block_start + (long)blockNum * BLOCK_SIZE;
    return readObject(disk, off, fileB);
}

// ── Escribir bloque archivo ───────────────────────────────────
inline bool setFileBlock(const Superblock& sb, const std::string& disk,
                          int blockNum, const FileBlock& fileB) {
    long off = sb.sb_block_start + (long)blockNum * BLOCK_SIZE;
    return writeObject(disk, off, fileB);
}

// ── Asignar inodo libre ───────────────────────────────────────
inline int allocInode(Superblock& sb, const std::string& disk, int partStart) {
    int idx = findFreeBit(disk, sb.sb_bm_inode_start, sb.sb_inodes_count);
    if (idx == -1) return -1;
    writeBitmap(disk, sb.sb_bm_inode_start, idx, '1');
    sb.sb_free_inodes_count--;
    sb.sb_first_ino = idx + 1;
    writeObject(disk, partStart, sb);
    return idx;
}

// ── Asignar bloque libre ──────────────────────────────────────
inline int allocBlock(Superblock& sb, const std::string& disk, int partStart) {
    int idx = findFreeBit(disk, sb.sb_bm_block_start, sb.sb_blocks_count);
    if (idx == -1) return -1;
    writeBitmap(disk, sb.sb_bm_block_start, idx, '1');
    sb.sb_free_blocks_count--;
    sb.sb_first_blo = idx + 1;
    writeObject(disk, partStart, sb);
    return idx;
}

// ── Liberar inodo ────────────────────────────────────────────
inline void freeInode(Superblock& sb, const std::string& disk,
                       int partStart, int inodeNum) {
    writeBitmap(disk, sb.sb_bm_inode_start, inodeNum, '0');
    sb.sb_free_inodes_count++;
    writeObject(disk, partStart, sb);
}

// ── Liberar bloque ────────────────────────────────────────────
inline void freeBlock(Superblock& sb, const std::string& disk,
                       int partStart, int blockNum) {
    writeBitmap(disk, sb.sb_bm_block_start, blockNum, '0');
    sb.sb_free_blocks_count++;
    writeObject(disk, partStart, sb);
}

// ── Navegar ruta y retornar número de inodo ──────────────────
// Retorna -1 si no existe. parentInode recibe el inodo padre.
inline int pathToInode(const Superblock& sb, const std::string& disk,
                        const std::string& path, int* parentInodeNum = nullptr) {
    if (path == "/") return 0;

    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string token;
    while (std::getline(ss, token, '/'))
        if (!token.empty()) parts.push_back(token);

    int currentInode = 0;
    int prevInode    = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        Inode inode;
        getInode(sb, disk, currentInode, inode);
        if (inode.i_type != '1') return -1; // No es carpeta

        bool found = false;
        for (int b = 0; b < 15 && inode.i_block[b] != -1; b++) {
            FolderBlock fb;
            getFolderBlock(sb, disk, inode.i_block[b], fb);
            for (int j = 0; j < MAX_CONTENT; j++) {
                if (fb.b_content[j].b_inodo != -1 &&
                    fb.b_content[j].b_inodo != 0  &&
                    std::string(fb.b_content[j].b_name) == parts[i]) {
                    if (i == parts.size() - 1 && parentInodeNum)
                        *parentInodeNum = currentInode;
                    prevInode    = currentInode;
                    currentInode = fb.b_content[j].b_inodo;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return -1;
    }
    return currentInode;
}

// ── Obtener nombre del último componente de una ruta ─────────
inline std::string baseName(const std::string& path) {
    size_t pos = path.rfind('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// ── Obtener directorio padre de una ruta ─────────────────────
inline std::string dirName(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) return "/";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

// ── Agregar entrada en bloque de carpeta ─────────────────────
// Retorna false si la carpeta está llena (sin bloques libres disponibles)
inline bool addDirEntry(Superblock& sb, const std::string& disk,
                         int partStart, int parentInode,
                         const std::string& entryName, int childInode) {
    Inode parent;
    getInode(sb, disk, parentInode, parent);

    // Buscar entrada libre en bloques existentes
    for (int b = 0; b < 15 && parent.i_block[b] != -1; b++) {
        FolderBlock fb;
        getFolderBlock(sb, disk, parent.i_block[b], fb);
        for (int j = 0; j < MAX_CONTENT; j++) {
            if (fb.b_content[j].b_inodo == -1 ||
                fb.b_content[j].b_inodo == 0 &&
                fb.b_content[j].b_name[0] == '\0') {
                strncpy(fb.b_content[j].b_name, entryName.c_str(), 11);
                fb.b_content[j].b_name[11] = '\0';
                fb.b_content[j].b_inodo = childInode;
                setFolderBlock(sb, disk, parent.i_block[b], fb);
                parent.i_size++;
                setInode(sb, disk, parentInode, parent);
                return true;
            }
        }
    }

    // Necesita nuevo bloque
    for (int b = 0; b < 15; b++) {
        if (parent.i_block[b] == -1) {
            int newBlock = allocBlock(sb, disk, partStart);
            if (newBlock == -1) return false;
            parent.i_block[b] = newBlock;
            FolderBlock fb;
            memset(&fb, 0, sizeof(FolderBlock));
            fb.b_content[0].b_inodo = childInode;
            strncpy(fb.b_content[0].b_name, entryName.c_str(), 11);
            fb.b_content[0].b_name[11] = '\0';
            for (int j = 1; j < MAX_CONTENT; j++)
                fb.b_content[j].b_inodo = -1;
            setFolderBlock(sb, disk, newBlock, fb);
            parent.i_size++;
            setInode(sb, disk, parentInode, parent);
            return true;
        }
    }
    return false;
}

// ── Eliminar entrada de bloque de carpeta ────────────────────
inline bool removeDirEntry(Superblock& sb, const std::string& disk,
                             int parentInode, const std::string& entryName) {
    Inode parent;
    getInode(sb, disk, parentInode, parent);
    for (int b = 0; b < 15 && parent.i_block[b] != -1; b++) {
        FolderBlock fb;
        getFolderBlock(sb, disk, parent.i_block[b], fb);
        for (int j = 0; j < MAX_CONTENT; j++) {
            if (std::string(fb.b_content[j].b_name) == entryName &&
                fb.b_content[j].b_inodo != -1) {
                fb.b_content[j].b_inodo = -1;
                memset(fb.b_content[j].b_name, 0, 12);
                setFolderBlock(sb, disk, parent.i_block[b], fb);
                parent.i_size--;
                setInode(sb, disk, parentInode, parent);
                return true;
            }
        }
    }
    return false;
}

// ── Leer contenido completo de un archivo ────────────────────
inline std::string readFileContent(const Superblock& sb, const std::string& disk,
                                    int inodeNum) {
    Inode inode;
    getInode(sb, disk, inodeNum, inode);
    std::string content;
    for (int b = 0; b < 15 && inode.i_block[b] != -1; b++) {
        FileBlock fb;
        getFileBlock(sb, disk, inode.i_block[b], fb);
        content += std::string(fb.b_content, BLOCK_SIZE);
    }
    // Recortar al tamaño real
    if ((int)content.size() > inode.i_size)
        content = content.substr(0, inode.i_size);
    return content;
}

// ── Escribir contenido a un archivo ──────────────────────────
inline bool writeFileContent(Superblock& sb, const std::string& disk,
                               int partStart, int inodeNum,
                               const std::string& content) {
    Inode inode;
    getInode(sb, disk, inodeNum, inode);

    // Liberar bloques existentes
    for (int b = 0; b < 15; b++) {
        if (inode.i_block[b] != -1) {
            freeBlock(sb, disk, partStart, inode.i_block[b]);
            inode.i_block[b] = -1;
        }
    }

    // Escribir nuevo contenido en bloques
    size_t written = 0;
    int blockIdx = 0;
    while (written < content.size() && blockIdx < 15) {
        int blockNum = allocBlock(sb, disk, partStart);
        if (blockNum == -1) break;
        inode.i_block[blockIdx] = blockNum;
        FileBlock fb;
        memset(&fb, 0, sizeof(FileBlock));
        size_t toWrite = std::min((size_t)BLOCK_SIZE,
                                   content.size() - written);
        memcpy(fb.b_content, content.c_str() + written, toWrite);
        setFileBlock(sb, disk, blockNum, fb);
        written += toWrite;
        blockIdx++;
    }

    inode.i_size = (int)content.size();
    std::string dt = currentDateTime();
    strncpy(inode.i_mtime, dt.c_str(), 18);
    inode.i_mtime[18] = '\0';
    setInode(sb, disk, inodeNum, inode);
    return true;
}

// ── Registrar operación en journal EXT3 ──────────────────────
inline void journalAdd(const std::string& disk, int journalStart,
                        const std::string& operation, const std::string& path,
                        const std::string& content) {
    Journal j;
    readObject(disk, journalStart, j);
    if (j.j_count >= MAX_JOURNAL) return; // Límite alcanzado

    JournalContent& entry = j.j_content[j.j_count];
    strncpy(entry.j_operation, operation.c_str(), 9); entry.j_operation[9] = '\0';
    strncpy(entry.j_path,      path.c_str(),      31); entry.j_path[31]      = '\0';
    strncpy(entry.j_content,   content.c_str(),   63); entry.j_content[63]   = '\0';
    std::string dt = currentDateTime();
    strncpy(entry.j_date, dt.c_str(), 18); entry.j_date[18] = '\0';
    j.j_count++;
    writeObject(disk, journalStart, j);
}

// ── Macro para registrar en journal si EXT3 ──────────────────
#define JOURNAL_ADD(sb, disk, op, path, content) \
    if ((sb).sb_filesystem_type == 3) \
        journalAdd((disk), (sb).sb_journal_start, (op), (path), (content))
