// ============================================================
// mkfs.h — Formatear partición EXT2 o EXT3
// ============================================================
#pragma once
#include <string>
#include <cstring>
#include <cmath>
#include "../utils/analyzer.h"
#include "../utils/diskmanager.h"
#include "../utils/mountmanager.h"
#include "../structures/structs.h"

// ── Calcular n (número de inodos) para EXT2 ─────────────────
// partition_size = sizeof(SB) + n + 3n + n*sizeof(Inode) + 3n*sizeof(Block)
static int calcN_EXT2(int partSize) {
    double num = partSize - sizeof(Superblock);
    double den = 1 + 3 + sizeof(Inode) + 3.0 * BLOCK_SIZE;
    return (int)floor(num / den);
}

// ── Calcular n (número de inodos) para EXT3 ─────────────────
// partition_size = sizeof(SB) + n*sizeof(Journal) + n + 3n + n*sizeof(Inode) + 3n*sizeof(Block)
// Nota: Journal tiene 50 entradas fijas, sizeof(Journal) es el struct completo
static int calcN_EXT3(int partSize) {
    double num = partSize - sizeof(Superblock);
    double den = sizeof(JournalContent) + 1 + 3 + sizeof(Inode) + 3.0 * BLOCK_SIZE;
    return (int)floor(num / den);
}

// ── Dar formato a una partición ──────────────────────────────
inline std::string cmdMkfs(const ParsedCommand& cmd) {
    std::string id = cmd.get("id");
    if (id.empty()) return "ERROR: Falta parámetro -id\n";

    // Obtener partición montada
    MountManager& mm = MountManager::getInstance();
    MountedPartition* mp = mm.findById(id);
    if (!mp)
        return "ERROR: ID '" + id + "' no está montado\n";

    // ── Parámetros opcionales ─────────────────────────────────
    std::string fsStr = cmd.get("fs", "2fs");
    int fsType = (fsStr == "3fs") ? 3 : 2;

    // ── Calcular n ────────────────────────────────────────────
    int partSize  = mp->partSize;
    int n = (fsType == 3) ? calcN_EXT3(partSize) : calcN_EXT2(partSize);
    if (n <= 0) return "ERROR: Partición demasiado pequeña para formatear\n";

    int diskStart = mp->partStart;
    std::string diskPath = mp->diskPath;

    // ── Calcular offsets de cada área ────────────────────────
    int sbStart      = diskStart;
    int journalStart = sbStart + sizeof(Superblock);
    int bmInodeStart, bmBlockStart, inodeStart, blockStart;

    if (fsType == 3) {
        // EXT3: journal area tiene n entradas de JournalContent
        // (el journal se almacena como array de JournalContent, más el contador)
        int journalAreaSize = sizeof(int) + MAX_JOURNAL * sizeof(JournalContent);
        bmInodeStart = journalStart + journalAreaSize;
    } else {
        bmInodeStart = journalStart; // EXT2: no journal
    }
    bmBlockStart = bmInodeStart + n;        // 1 byte por inodo
    inodeStart   = bmBlockStart + 3 * n;    // 1 byte por bloque (3 tipos)
    blockStart   = inodeStart + n * sizeof(Inode);

    // ── Inicializar superbloque ───────────────────────────────
    Superblock sb;
    memset(&sb, 0, sizeof(Superblock));
    sb.sb_filesystem_type    = fsType;
    sb.sb_inodes_count       = n;
    sb.sb_blocks_count       = 3 * n;
    sb.sb_free_inodes_count  = n - 2; // inodo 0=root, 1=users.txt
    sb.sb_free_blocks_count  = 3 * n - 2;
    sb.sb_mnt_count          = 1;
    sb.sb_magic              = MAGIC_NUMBER;
    sb.sb_inode_size         = sizeof(Inode);
    sb.sb_block_size         = BLOCK_SIZE;
    sb.sb_first_ino          = 2;  // primer libre (0,1 usados)
    sb.sb_first_blo          = 2;
    sb.sb_bm_inode_start     = bmInodeStart;
    sb.sb_bm_block_start     = bmBlockStart;
    sb.sb_inode_start        = inodeStart;
    sb.sb_block_start        = blockStart;
    sb.sb_journal_start      = journalStart;
    std::string dt = currentDateTime();
    strncpy(sb.sb_mtime,  dt.c_str(), 18); sb.sb_mtime[18]  = '\0';
    strncpy(sb.sb_umtime, dt.c_str(), 18); sb.sb_umtime[18] = '\0';
    writeObject(diskPath, sbStart, sb);

    // ── Inicializar bitmaps con '0' ───────────────────────────
    // Bitmap inodos
    for (int i = 0; i < n; i++) writeBitmap(diskPath, bmInodeStart, i, '0');
    // Bitmap bloques
    for (int i = 0; i < 3*n; i++) writeBitmap(diskPath, bmBlockStart, i, '0');

    // ── Si EXT3: inicializar journal vacío ────────────────────
    if (fsType == 3) {
        Journal j;
        memset(&j, 0, sizeof(Journal));
        j.j_count = 0;
        writeObject(diskPath, journalStart, j);
    }

    // ── Inodo 0: carpeta raíz "/" ─────────────────────────────
    Inode rootInode;
    memset(&rootInode, 0, sizeof(Inode));
    rootInode.i_uid    = 1; // root uid=1
    rootInode.i_gid    = 1;
    rootInode.i_size   = 0;
    rootInode.i_type   = '1'; // carpeta
    rootInode.i_perm[0] = 7;  // u=rwx
    rootInode.i_perm[1] = 7;  // g=rwx
    rootInode.i_perm[2] = 7;  // o=rwx
    for (int b = 0; b < 15; b++) rootInode.i_block[b] = -1;
    rootInode.i_block[0] = 0; // primer bloque
    strncpy(rootInode.i_atime, dt.c_str(), 18);
    strncpy(rootInode.i_ctime, dt.c_str(), 18);
    strncpy(rootInode.i_mtime, dt.c_str(), 18);
    writeObject(diskPath, inodeStart, rootInode);
    writeBitmap(diskPath, bmInodeStart, 0, '1');

    // ── Bloque 0: contenido de "/" ────────────────────────────
    FolderBlock rootBlock;
    memset(&rootBlock, 0, sizeof(FolderBlock));
    // Entrada "."
    strncpy(rootBlock.b_content[0].b_name, ".", 11);
    rootBlock.b_content[0].b_inodo = 0;
    // Entrada ".."
    strncpy(rootBlock.b_content[1].b_name, "..", 11);
    rootBlock.b_content[1].b_inodo = 0;
    // Entrada "users.txt"
    strncpy(rootBlock.b_content[2].b_name, "users.txt", 11);
    rootBlock.b_content[2].b_inodo = 1;
    // Libre
    rootBlock.b_content[3].b_inodo = -1;
    writeObject(diskPath, blockStart, rootBlock);
    writeBitmap(diskPath, bmBlockStart, 0, '1');

    // ── Inodo 1: archivo users.txt ───────────────────────────
    Inode usersInode;
    memset(&usersInode, 0, sizeof(Inode));
    usersInode.i_uid    = 1;
    usersInode.i_gid    = 1;
    usersInode.i_type   = '0'; // archivo
    usersInode.i_perm[0] = 6;  // rw
    usersInode.i_perm[1] = 6;
    usersInode.i_perm[2] = 6;
    for (int b = 0; b < 15; b++) usersInode.i_block[b] = -1;
    usersInode.i_block[0] = 1;
    strncpy(usersInode.i_atime, dt.c_str(), 18);
    strncpy(usersInode.i_ctime, dt.c_str(), 18);
    strncpy(usersInode.i_mtime, dt.c_str(), 18);
    writeObject(diskPath, inodeStart + sizeof(Inode), usersInode);
    writeBitmap(diskPath, bmInodeStart, 1, '1');

    // ── Bloque 1: contenido inicial de users.txt ─────────────
    // Formato: "1,G,root\n1,U,root,root,123\n"
    std::string usersContent = "1,G,root\n1,U,root,root,123\n";
    FileBlock usersBlock;
    memset(&usersBlock, 0, sizeof(FileBlock));
    strncpy(usersBlock.b_content, usersContent.c_str(), 63);
    usersInode.i_size = (int)usersContent.size();
    writeObject(diskPath, inodeStart + sizeof(Inode), usersInode);
    writeObject(diskPath, blockStart + BLOCK_SIZE, usersBlock);
    writeBitmap(diskPath, bmBlockStart, 1, '1');

    // Actualizar superbloque con free counts correctos
    sb.sb_free_inodes_count = n - 2;
    sb.sb_free_blocks_count = 3 * n - 2;
    sb.sb_first_ino = 2;
    sb.sb_first_blo = 2;
    writeObject(diskPath, sbStart, sb);

    return "SUCCESS: Partición " + mp->partName + " formateada como EXT" +
           std::to_string(fsType) + " [n=" + std::to_string(n) +
           " inodos, " + std::to_string(3*n) + " bloques]\n";
}
