// ============================================================
// session.h — Gestión de sesión activa
// ============================================================
#pragma once
#include <string>

// ── Singleton de sesión global ───────────────────────────────
class Session {
public:
    // ── Instancia única ──────────────────────────────────────
    static Session& getInstance() {
        static Session instance;
        return instance;
    }

    bool        isLoggedIn   = false;
    std::string username;        // Usuario actual
    std::string groupname;       // Grupo del usuario
    std::string partitionId;     // ID de partición activa (ej: "682A")
    std::string diskPath;        // Ruta del disco donde está la partición
    int         partitionStart;  // Inicio de la partición en bytes
    int         uid;             // UID del usuario (inode user)
    int         gid;             // GID del grupo

    // ── Iniciar sesión ───────────────────────────────────────
    void login(const std::string& user, const std::string& group,
               const std::string& partId, const std::string& disk,
               int partStart, int userId, int groupId) {
        isLoggedIn     = true;
        username       = user;
        groupname      = group;
        partitionId    = partId;
        diskPath       = disk;
        partitionStart = partStart;
        uid            = userId;
        gid            = groupId;
    }

    // ── Cerrar sesión ────────────────────────────────────────
    void logout() {
        isLoggedIn     = false;
        username       = "";
        groupname      = "";
        partitionId    = "";
        diskPath       = "";
        partitionStart = 0;
        uid            = -1;
        gid            = -1;
    }

private:
    Session() : isLoggedIn(false), partitionStart(0), uid(-1), gid(-1) {}
};
