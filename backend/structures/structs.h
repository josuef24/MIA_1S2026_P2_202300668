// ============================================================
// structs.h — Estructuras binarias del sistema de archivos
// ============================================================
#pragma once
#include <ctime>

// ── Constantes globales ──────────────────────────────────────
#define CARNET          "68"         // Últimos 2 dígitos del carnet
#define MAGIC_NUMBER    0xEF53       // Número mágico EXT2/3
#define MAX_JOURNAL     50           // Entradas máximas en journal EXT3
#define MAX_CONTENT     4            // Contenidos por bloque carpeta
#define BLOCK_SIZE      64           // Tamaño de bloque en bytes

// ── Partición dentro del MBR ─────────────────────────────────
struct Partition {
    char part_status;       // '0'=libre, '1'=usado, '3'=eliminado
    char part_type;         // 'P'=primaria, 'E'=extendida, 'L'=lógica
    char part_fit;          // 'B'=best, 'F'=first, 'W'=worst
    int  part_start;        // Inicio en bytes dentro del disco
    int  part_size;         // Tamaño en bytes
    char part_name[16];     // Nombre de la partición
    int  part_correlative;  // Correlativo de montaje (0=no montado)
    char part_id[5];        // ID asignado al montar (ej: "682A")
};

// ── MBR — Master Boot Record ─────────────────────────────────
struct MBR {
    int  mbr_tamano;            // Tamaño total del disco en bytes
    char mbr_fecha_creacion[19];// Fecha de creación "YYYY-MM-DD HH:MM:SS"
    int  mbr_disk_signature;    // Firma aleatoria del disco
    char mbr_disk_fit;          // Ajuste del disco: 'B','F','W'
    Partition mbr_partitions[4];// Tabla de particiones (máx 4)
};

// ── EBR — Extended Boot Record ───────────────────────────────
struct EBR {
    char ebr_mount;     // '1'=montado, '0'=no montado
    char ebr_fit;       // Ajuste: 'B','F','W'
    int  ebr_start;     // Inicio en bytes dentro del disco
    int  ebr_size;      // Tamaño en bytes
    int  ebr_next;      // Posición del siguiente EBR (-1=último)
    char ebr_name[16];  // Nombre de la partición lógica
};

// ── Superbloque EXT2/EXT3 ────────────────────────────────────
struct Superblock {
    int  sb_filesystem_type;   // 2=EXT2, 3=EXT3
    int  sb_inodes_count;      // Total de inodos
    int  sb_blocks_count;      // Total de bloques
    int  sb_free_blocks_count; // Bloques libres
    int  sb_free_inodes_count; // Inodos libres
    char sb_mtime[19];         // Último montaje
    char sb_umtime[19];        // Último desmontaje
    int  sb_mnt_count;         // Contador de montajes
    int  sb_magic;             // Número mágico (0xEF53)
    int  sb_inode_size;        // Tamaño de un inodo
    int  sb_block_size;        // Tamaño de un bloque
    int  sb_first_ino;         // Primer inodo libre
    int  sb_first_blo;         // Primer bloque libre
    int  sb_bm_inode_start;    // Inicio bitmap de inodos
    int  sb_bm_block_start;    // Inicio bitmap de bloques
    int  sb_inode_start;       // Inicio área de inodos
    int  sb_block_start;       // Inicio área de bloques
    int  sb_journal_start;     // Inicio área de journal (EXT3)
};

// ── Inodo ────────────────────────────────────────────────────
struct Inode {
    int  i_uid;         // UID del propietario
    int  i_gid;         // GID del grupo
    int  i_size;        // Tamaño en bytes
    char i_atime[19];   // Último acceso
    char i_ctime[19];   // Creación
    char i_mtime[19];   // Última modificación
    int  i_block[15];   // Punteros a bloques (-1=vacío)
    char i_type;        // '0'=archivo, '1'=carpeta
    int  i_perm[3];     // Permisos UGO (0-7 cada uno)
};

// ── Entrada de bloque carpeta ────────────────────────────────
struct FolderContent {
    char b_name[12];    // Nombre del archivo/carpeta
    int  b_inodo;       // Número de inodo (-1=vacío)
};

// ── Bloque de carpeta ────────────────────────────────────────
struct FolderBlock {
    FolderContent b_content[4]; // 4 entradas por bloque
};

// ── Bloque de archivo ────────────────────────────────────────
struct FileBlock {
    char b_content[64]; // Contenido del archivo (64 bytes)
};

// ── Entrada individual del journal ───────────────────────────
struct JournalContent {
    char j_operation[10]; // Operación realizada (ej: "mkdir")
    char j_path[32];      // Ruta afectada
    char j_content[64];   // Contenido (si aplica)
    char j_date[19];      // Fecha y hora "YYYY-MM-DD HH:MM:SS"
};

// ── Journal EXT3 (área de 50 entradas) ───────────────────────
struct Journal {
    int            j_count;              // Entradas registradas
    JournalContent j_content[MAX_JOURNAL]; // Bitácora de operaciones
};
