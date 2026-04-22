// ============================================================
// permissions.h — Verificación de permisos UGO sobre inodos
// ============================================================
#pragma once
#include "../structures/structs.h"
#include "session.h"

// ── Bits de permiso ──────────────────────────────────────────
#define PERM_READ    4
#define PERM_WRITE   2
#define PERM_EXEC    1

// ── Verificar permiso sobre un inodo ────────────────────────
// tipo: PERM_READ, PERM_WRITE o PERM_EXEC
inline bool checkPermission(const Inode& inode, int tipo) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return false;

    // root ignora permisos
    if (s.username == "root") return true;

    int perm;
    if (s.uid == inode.i_uid) {
        perm = inode.i_perm[0]; // User
    } else if (s.gid == inode.i_gid) {
        perm = inode.i_perm[1]; // Group
    } else {
        perm = inode.i_perm[2]; // Others
    }

    return (perm & tipo) != 0;
}

// ── Verificar permiso de lectura ─────────────────────────────
inline bool canRead(const Inode& inode) {
    return checkPermission(inode, PERM_READ);
}

// ── Verificar permiso de escritura ───────────────────────────
inline bool canWrite(const Inode& inode) {
    return checkPermission(inode, PERM_WRITE);
}

// ── Verificar si el usuario es dueño ────────────────────────
inline bool isOwner(const Inode& inode) {
    Session& s = Session::getInstance();
    return s.username == "root" || s.uid == inode.i_uid;
}

// ── Validar formato UGO (3 dígitos 0-7) ─────────────────────
inline bool validUGO(const std::string& ugo) {
    if (ugo.size() != 3) return false;
    for (char c : ugo)
        if (c < '0' || c > '7') return false;
    return true;
}
