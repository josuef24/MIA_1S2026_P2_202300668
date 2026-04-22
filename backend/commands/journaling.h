// ============================================================
// journaling.h — loss, recovery, journaling (EXT3)
// ============================================================
#pragma once
#include <string>
#include <sstream>
#include "../utils/analyzer.h"
#include "../utils/mountmanager.h"
#include "../utils/diskmanager.h"
#include "../filesystem/filesystem.h"
#include "../structures/structs.h"

// ── LOSS — Simular pérdida del sistema EXT3 ──────────────────
// Limpia bitmaps + área de inodos + área de bloques
inline std::string cmdLoss(const ParsedCommand& cmd) {
    std::string id = cmd.get("id");
    if (id.empty()) return "ERROR: Falta -id\n";

    MountedPartition* mp = MountManager::getInstance().findById(id);
    if (!mp) return "ERROR: ID '" + id + "' no montado\n";

    Superblock sb;
    if (!getSB(mp->diskPath, mp->partStart, sb))
        return "ERROR: No se pudo leer el superbloque\n";

    if (sb.sb_filesystem_type != 3)
        return "ERROR: LOSS solo aplica a particiones EXT3\n";

    // ── Limpiar bitmap de inodos ──────────────────────────────
    int bmInodeSize = sb.sb_inodes_count;
    zeroFill(mp->diskPath, sb.sb_bm_inode_start, bmInodeSize);

    // ── Limpiar bitmap de bloques ─────────────────────────────
    int bmBlockSize = sb.sb_blocks_count;
    zeroFill(mp->diskPath, sb.sb_bm_block_start, bmBlockSize);

    // ── Limpiar área de inodos ────────────────────────────────
    long inodeAreaSize = (long)sb.sb_inodes_count * sizeof(Inode);
    zeroFill(mp->diskPath, sb.sb_inode_start, inodeAreaSize);

    // ── Limpiar área de bloques ───────────────────────────────
    long blockAreaSize = (long)sb.sb_blocks_count * BLOCK_SIZE;
    zeroFill(mp->diskPath, sb.sb_block_start, blockAreaSize);

    // El journal NO se limpia (es lo que permite recovery)
    return "WARNING: Sistema de archivos perdido en partición " + id +
           "\nEl journal sigue disponible para recovery\n";
}

// ── RECOVERY — Recuperar desde journal ───────────────────────
inline std::string cmdRecovery(const ParsedCommand& cmd) {
    std::string id = cmd.get("id");
    if (id.empty()) return "ERROR: Falta -id\n";

    MountedPartition* mp = MountManager::getInstance().findById(id);
    if (!mp) return "ERROR: ID '" + id + "' no montado\n";

    Superblock sb;
    if (!getSB(mp->diskPath, mp->partStart, sb))
        return "ERROR: No se pudo leer el superbloque\n";

    if (sb.sb_filesystem_type != 3)
        return "ERROR: RECOVERY solo aplica a particiones EXT3\n";

    // Leer journal
    Journal j;
    readObject(mp->diskPath, sb.sb_journal_start, j);

    if (j.j_count == 0)
        return "INFO: El journal está vacío, no hay qué recuperar\n";

    // Reinicializar estructuras básicas (superbloque, bitmaps)
    // Resetear free counts
    sb.sb_free_inodes_count = sb.sb_inodes_count;
    sb.sb_free_blocks_count = sb.sb_blocks_count;
    sb.sb_first_ino = 0;
    sb.sb_first_blo = 0;
    writeObject(mp->diskPath, mp->partStart, sb);

    // Re-inicializar bitmaps
    for (int i = 0; i < sb.sb_inodes_count; i++)
        writeBitmap(mp->diskPath, sb.sb_bm_inode_start, i, '0');
    for (int i = 0; i < sb.sb_blocks_count; i++)
        writeBitmap(mp->diskPath, sb.sb_bm_block_start, i, '0');

    // Reinicializar raíz (inodo 0 y bloque 0)
    std::string dt = currentDateTime();

    Inode rootInode;
    memset(&rootInode, 0, sizeof(Inode));
    rootInode.i_uid = 1; rootInode.i_gid = 1;
    rootInode.i_type = '1'; rootInode.i_size = 0;
    rootInode.i_perm[0] = 7; rootInode.i_perm[1] = 7; rootInode.i_perm[2] = 7;
    for (int b = 0; b < 15; b++) rootInode.i_block[b] = -1;
    rootInode.i_block[0] = 0;
    strncpy(rootInode.i_atime, dt.c_str(), 18);
    strncpy(rootInode.i_ctime, dt.c_str(), 18);
    strncpy(rootInode.i_mtime, dt.c_str(), 18);
    writeObject(mp->diskPath, sb.sb_inode_start, rootInode);
    writeBitmap(mp->diskPath, sb.sb_bm_inode_start, 0, '1');

    FolderBlock rootBlock;
    memset(&rootBlock, 0, sizeof(FolderBlock));
    strncpy(rootBlock.b_content[0].b_name, ".", 11);
    rootBlock.b_content[0].b_inodo = 0;
    strncpy(rootBlock.b_content[1].b_name, "..", 11);
    rootBlock.b_content[1].b_inodo = 0;
    rootBlock.b_content[2].b_inodo = -1;
    rootBlock.b_content[3].b_inodo = -1;
    writeObject(mp->diskPath, sb.sb_block_start, rootBlock);
    writeBitmap(mp->diskPath, sb.sb_bm_block_start, 0, '1');

    // Reproducir operaciones del journal en orden
    std::ostringstream log;
    log << "Recuperando desde journal (" << j.j_count << " operaciones):\n";

    // Re-setear sesión simulada para recovery
    getSB(mp->diskPath, mp->partStart, sb);
    sb.sb_free_inodes_count = sb.sb_inodes_count - 1;
    sb.sb_free_blocks_count = sb.sb_blocks_count - 1;
    sb.sb_first_ino = 1;
    sb.sb_first_blo = 1;
    writeObject(mp->diskPath, mp->partStart, sb);

    for (int i = 0; i < j.j_count && i < MAX_JOURNAL; i++) {
        JournalContent& entry = j.j_content[i];
        std::string op(entry.j_operation);
        std::string path(entry.j_path);
        std::string content(entry.j_content);

        log << "  [" << (i+1) << "] " << op << " " << path << "\n";

        getSB(mp->diskPath, mp->partStart, sb);

        if (op == "mkdir") {
            ParsedCommand c; c.name = "mkdir";
            c.params["path"] = path; c.params["p"] = "true";
            // Crear directorio directamente (sin sesión requerida para recovery)
            // Navegamos manualmente
            std::vector<std::string> parts;
            std::istringstream ss(path);
            std::string tok;
            while (std::getline(ss, tok, '/'))
                if (!tok.empty()) parts.push_back(tok);

            int parentInode = 0;
            std::string curPath = "";
            for (auto& part : parts) {
                curPath += "/" + part;
                int existing = pathToInode(sb, mp->diskPath, curPath);
                if (existing != -1) { parentInode = existing; continue; }

                int ni = allocInode(sb, mp->diskPath, mp->partStart);
                int nb = allocBlock(sb, mp->diskPath, mp->partStart);
                if (ni == -1 || nb == -1) break;

                Inode dir; memset(&dir, 0, sizeof(Inode));
                dir.i_uid = 1; dir.i_gid = 1;
                dir.i_type = '1'; dir.i_perm[0]=7; dir.i_perm[1]=5; dir.i_perm[2]=5;
                for (int b = 0; b < 15; b++) dir.i_block[b] = -1;
                dir.i_block[0] = nb;
                strncpy(dir.i_atime, dt.c_str(), 18);
                strncpy(dir.i_ctime, dt.c_str(), 18);
                strncpy(dir.i_mtime, dt.c_str(), 18);
                setInode(sb, mp->diskPath, ni, dir);

                FolderBlock fb; memset(&fb, 0, sizeof(FolderBlock));
                strncpy(fb.b_content[0].b_name, ".", 11);
                fb.b_content[0].b_inodo = ni;
                strncpy(fb.b_content[1].b_name, "..", 11);
                fb.b_content[1].b_inodo = parentInode;
                fb.b_content[2].b_inodo = -1; fb.b_content[3].b_inodo = -1;
                setFolderBlock(sb, mp->diskPath, nb, fb);

                getSB(mp->diskPath, mp->partStart, sb);
                addDirEntry(sb, mp->diskPath, mp->partStart, parentInode, part, ni);
                getSB(mp->diskPath, mp->partStart, sb);
                parentInode = ni;
            }
        }
        else if (op == "mkfile") {
            std::string dname = dirName(path);
            std::string fname = baseName(path);
            getSB(mp->diskPath, mp->partStart, sb);
            int parent = pathToInode(sb, mp->diskPath, dname);
            if (parent == -1) continue;
            if (pathToInode(sb, mp->diskPath, path) != -1) continue;

            int ni = allocInode(sb, mp->diskPath, mp->partStart);
            if (ni == -1) continue;

            Inode fi; memset(&fi, 0, sizeof(Inode));
            fi.i_uid = 1; fi.i_gid = 1;
            fi.i_type = '0'; fi.i_perm[0]=6; fi.i_perm[1]=4; fi.i_perm[2]=4;
            for (int b = 0; b < 15; b++) fi.i_block[b] = -1;
            strncpy(fi.i_atime, dt.c_str(), 18);
            strncpy(fi.i_ctime, dt.c_str(), 18);
            strncpy(fi.i_mtime, dt.c_str(), 18);
            setInode(sb, mp->diskPath, ni, fi);

            getSB(mp->diskPath, mp->partStart, sb);
            addDirEntry(sb, mp->diskPath, mp->partStart, parent, fname, ni);
        }
        // mkgrp, mkusr, etc. se recuperan si users.txt fue journalizado
    }

    return log.str() + "SUCCESS: Recovery completado\n";
}

// ── JOURNALING — Mostrar bitácora ────────────────────────────
inline std::string cmdJournaling(const ParsedCommand& cmd) {
    std::string id = cmd.get("id");
    if (id.empty()) return "ERROR: Falta -id\n";

    MountedPartition* mp = MountManager::getInstance().findById(id);
    if (!mp) return "ERROR: ID '" + id + "' no montado\n";

    Superblock sb;
    if (!getSB(mp->diskPath, mp->partStart, sb))
        return "ERROR: No se pudo leer el superbloque\n";

    if (sb.sb_filesystem_type != 3)
        return "ERROR: Journaling solo aplica a particiones EXT3\n";

    Journal j;
    readObject(mp->diskPath, sb.sb_journal_start, j);

    if (j.j_count == 0)
        return "INFO: El journal está vacío (ninguna operación registrada)\n";

    std::ostringstream oss;
    oss << "Journal de partición " << id << " ("
        << j.j_count << " entradas):\n";
    oss << "┌────┬──────────────┬──────────────────────────────────┬────────────────────┐\n";
    oss << "│ #  │ Operación    │ Ruta                             │ Fecha              │\n";
    oss << "├────┼──────────────┼──────────────────────────────────┼────────────────────┤\n";
    for (int i = 0; i < j.j_count && i < MAX_JOURNAL; i++) {
        JournalContent& e = j.j_content[i];
        std::string op(e.j_operation);
        std::string path(e.j_path);
        std::string date(e.j_date);
        oss << "│ " << std::to_string(i+1) << std::string(3-std::to_string(i+1).size(),' ')
            << "│ " << op << std::string(13-op.size(), ' ')
            << "│ " << path << std::string(33-path.size(), ' ')
            << "│ " << date << std::string(19-date.size(), ' ') << "│\n";
    }
    oss << "└────┴──────────────┴──────────────────────────────────┴────────────────────┘\n";
    return oss.str();
}

// ── JOURNALING como JSON (para la API) ───────────────────────
inline std::string cmdJournalingJson(const std::string& id) {
    MountedPartition* mp = MountManager::getInstance().findById(id);
    if (!mp) return "{\"error\":\"ID no montado\"}";

    Superblock sb;
    if (!getSB(mp->diskPath, mp->partStart, sb))
        return "{\"error\":\"No se pudo leer SB\"}";

    if (sb.sb_filesystem_type != 3)
        return "{\"error\":\"Solo EXT3 soporta journaling\"}";

    Journal j;
    readObject(mp->diskPath, sb.sb_journal_start, j);

    std::ostringstream oss;
    oss << "{\"id\":\"" << id << "\",\"count\":" << j.j_count << ",\"entries\":[";
    for (int i = 0; i < j.j_count && i < MAX_JOURNAL; i++) {
        JournalContent& e = j.j_content[i];
        if (i > 0) oss << ",";
        auto esc = [](std::string s) {
            std::string r; for (char c : s) {
                if (c == '"') r += "\\\"";
                else r += c;
            } return r; };
        oss << "{\"op\":\"" << esc(std::string(e.j_operation))
            << "\",\"path\":\"" << esc(std::string(e.j_path))
            << "\",\"content\":\"" << esc(std::string(e.j_content))
            << "\",\"date\":\"" << esc(std::string(e.j_date)) << "\"}";
    }
    oss << "]}";
    return oss.str();
}
