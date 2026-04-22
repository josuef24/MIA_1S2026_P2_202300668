// ============================================================
// users.h — Login, logout y gestión de usuarios/grupos
// ============================================================
#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include "../utils/analyzer.h"
#include "../utils/session.h"
#include "../utils/mountmanager.h"
#include "../filesystem/filesystem.h"

// ── Parsear users.txt y retornar líneas ──────────────────────
static std::vector<std::string> parseUsersFile(const std::string& content) {
    std::vector<std::string> lines;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// ── Reconstruir users.txt desde líneas ──────────────────────
static std::string buildUsersContent(const std::vector<std::string>& lines) {
    std::string out;
    for (auto& l : lines) out += l + "\n";
    return out;
}

// ── Obtener inodo y SB de la partición activa ────────────────
static bool getActivePartition(Superblock& sb, MountedPartition*& mp) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return false;
    mp = MountManager::getInstance().findById(s.partitionId);
    if (!mp) return false;
    return getSB(mp->diskPath, mp->partStart, sb);
}

// ── Leer users.txt de la partición activa ────────────────────
static std::string readUsersFile(Superblock& sb, MountedPartition* mp) {
    int usersInode = pathToInode(sb, mp->diskPath, "/users.txt");
    if (usersInode == -1) return "";
    return readFileContent(sb, mp->diskPath, usersInode);
}

// ── Escribir users.txt ────────────────────────────────────────
static void writeUsersFile(Superblock& sb, MountedPartition* mp,
                            const std::string& content) {
    int usersInode = pathToInode(sb, mp->diskPath, "/users.txt");
    if (usersInode == -1) return;
    writeFileContent(sb, mp->diskPath, mp->partStart, usersInode, content);
}

// ── LOGIN ─────────────────────────────────────────────────────
inline std::string cmdLogin(const std::string& user, const std::string& pass,
                              const std::string& partId) {
    Session& s = Session::getInstance();
    if (s.isLoggedIn)
        return "ERROR: Ya hay una sesión activa. Haga logout primero\n";

    MountedPartition* mp = MountManager::getInstance().findById(partId);
    if (!mp) return "ERROR: Partición '" + partId + "' no montada\n";

    Superblock sb;
    if (!getSB(mp->diskPath, mp->partStart, sb))
        return "ERROR: No se pudo leer el superbloque\n";

    std::string usersContent = readUsersFile(sb, mp);
    auto lines = parseUsersFile(usersContent);

    // Buscar usuario y verificar contraseña
    int uid = -1, gid = -1;
    std::string foundGroup;
    for (auto& line : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(line);
        std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        // Formato usuario: id,U,grupo,nombre,pass
        if (tok.size() >= 5 && tok[1] == "U" &&
            tok[3] == user && tok[4] == pass) {
            uid = std::stoi(tok[0]);
            foundGroup = tok[2];
            break;
        }
    }
    if (uid == -1) return "ERROR: Usuario o contraseña incorrectos\n";

    // Obtener GID del grupo
    for (auto& line : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(line);
        std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        if (tok.size() >= 3 && tok[1] == "G" && tok[2] == foundGroup) {
            gid = std::stoi(tok[0]);
            break;
        }
    }

    s.login(user, foundGroup, partId, mp->diskPath, mp->partStart, uid, gid);
    return "SUCCESS: Sesión iniciada. Usuario: " + user +
           ", Grupo: " + foundGroup + ", Partición: " + partId + "\n";
}

// ── LOGOUT ────────────────────────────────────────────────────
inline std::string cmdLogout() {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: No hay sesión activa\n";
    std::string user = s.username;
    s.logout();
    return "SUCCESS: Sesión de '" + user + "' cerrada\n";
}

// ── MKGRP — Crear grupo ──────────────────────────────────────
inline std::string cmdMkgrp(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";
    if (s.username != "root") return "ERROR: Solo root puede crear grupos\n";
    std::string name = cmd.get("name");
    if (name.empty()) return "ERROR: Falta -name\n";

    Superblock sb; MountedPartition* mp;
    if (!getActivePartition(sb, mp)) return "ERROR: Partición no disponible\n";

    std::string content = readUsersFile(sb, mp);
    auto lines = parseUsersFile(content);

    // Verificar si ya existe
    for (auto& l : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(l); std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        if (tok.size() >= 3 && tok[1] == "G" && tok[2] == name)
            return "ERROR: El grupo '" + name + "' ya existe\n";
    }

    // Obtener siguiente ID
    int nextId = 1;
    for (auto& l : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(l); std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        if (!tok.empty() && std::stoi(tok[0]) >= nextId)
            nextId = std::stoi(tok[0]) + 1;
    }

    lines.push_back(std::to_string(nextId) + ",G," + name);
    writeUsersFile(sb, mp, buildUsersContent(lines));
    JOURNAL_ADD(sb, mp->diskPath, "mkgrp", "/users.txt", name);
    return "SUCCESS: Grupo '" + name + "' creado\n";
}

// ── RMGRP — Eliminar grupo ────────────────────────────────────
inline std::string cmdRmgrp(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";
    if (s.username != "root") return "ERROR: Solo root puede eliminar grupos\n";
    std::string name = cmd.get("name");
    if (name.empty()) return "ERROR: Falta -name\n";
    if (name == "root") return "ERROR: No se puede eliminar el grupo root\n";

    Superblock sb; MountedPartition* mp;
    if (!getActivePartition(sb, mp)) return "ERROR: Partición no disponible\n";

    std::string content = readUsersFile(sb, mp);
    auto lines = parseUsersFile(content);
    bool found = false;
    std::vector<std::string> newLines;
    for (auto& l : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(l); std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        // Eliminar grupo y poner id=0 para usuarios de ese grupo
        if (tok.size() >= 3 && tok[1] == "G" && tok[2] == name) {
            found = true;
            // Marcar como eliminado con id=0
            newLines.push_back("0," + l.substr(l.find(',')+1));
        } else if (tok.size() >= 4 && tok[1] == "U" && tok[2] == name) {
            // Usuarios de ese grupo también se deshabilitan
            newLines.push_back("0," + l.substr(l.find(',')+1));
        } else {
            newLines.push_back(l);
        }
    }
    if (!found) return "ERROR: Grupo '" + name + "' no existe\n";
    writeUsersFile(sb, mp, buildUsersContent(newLines));
    JOURNAL_ADD(sb, mp->diskPath, "rmgrp", "/users.txt", name);
    return "SUCCESS: Grupo '" + name + "' eliminado\n";
}

// ── MKUSR — Crear usuario ─────────────────────────────────────
inline std::string cmdMkusr(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";
    if (s.username != "root") return "ERROR: Solo root puede crear usuarios\n";
    std::string user = cmd.get("user");
    std::string pass = cmd.get("pass");
    std::string grp  = cmd.get("grp");
    if (user.empty() || pass.empty() || grp.empty())
        return "ERROR: Faltan parámetros -user, -pass o -grp\n";

    Superblock sb; MountedPartition* mp;
    if (!getActivePartition(sb, mp)) return "ERROR: Partición no disponible\n";

    std::string content = readUsersFile(sb, mp);
    auto lines = parseUsersFile(content);

    bool grpExists = false;
    int nextId = 1;
    for (auto& l : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(l); std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        if (tok.size() >= 3 && tok[1] == "G" && tok[2] == grp && tok[0] != "0")
            grpExists = true;
        if (tok.size() >= 4 && tok[1] == "U" && tok[3] == user && tok[0] != "0")
            return "ERROR: El usuario '" + user + "' ya existe\n";
        if (!tok.empty()) {
            int id = std::stoi(tok[0]);
            if (id >= nextId) nextId = id + 1;
        }
    }
    if (!grpExists) return "ERROR: Grupo '" + grp + "' no existe\n";

    lines.push_back(std::to_string(nextId) + ",U," + grp + "," + user + "," + pass);
    writeUsersFile(sb, mp, buildUsersContent(lines));
    JOURNAL_ADD(sb, mp->diskPath, "mkusr", "/users.txt", user);
    return "SUCCESS: Usuario '" + user + "' creado en grupo '" + grp + "'\n";
}

// ── RMUSR — Eliminar usuario ─────────────────────────────────
inline std::string cmdRmusr(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";
    if (s.username != "root") return "ERROR: Solo root puede eliminar usuarios\n";
    std::string user = cmd.get("user");
    if (user.empty()) return "ERROR: Falta -user\n";
    if (user == "root") return "ERROR: No se puede eliminar el usuario root\n";

    Superblock sb; MountedPartition* mp;
    if (!getActivePartition(sb, mp)) return "ERROR: Partición no disponible\n";

    std::string content = readUsersFile(sb, mp);
    auto lines = parseUsersFile(content);
    bool found = false;
    std::vector<std::string> newLines;
    for (auto& l : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(l); std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        if (tok.size() >= 4 && tok[1] == "U" && tok[3] == user && tok[0] != "0") {
            found = true;
            newLines.push_back("0," + l.substr(l.find(',')+1));
        } else {
            newLines.push_back(l);
        }
    }
    if (!found) return "ERROR: Usuario '" + user + "' no existe\n";
    writeUsersFile(sb, mp, buildUsersContent(newLines));
    JOURNAL_ADD(sb, mp->diskPath, "rmusr", "/users.txt", user);
    return "SUCCESS: Usuario '" + user + "' eliminado\n";
}

// ── CHGRP — Cambiar grupo de usuario ─────────────────────────
inline std::string cmdChgrp(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";
    if (s.username != "root") return "ERROR: Solo root puede cambiar grupos\n";
    std::string user = cmd.get("user");
    std::string grp  = cmd.get("grp");
    if (user.empty() || grp.empty()) return "ERROR: Faltan parámetros\n";

    Superblock sb; MountedPartition* mp;
    if (!getActivePartition(sb, mp)) return "ERROR: Partición no disponible\n";

    std::string content = readUsersFile(sb, mp);
    auto lines = parseUsersFile(content);

    bool grpExists = false, userFound = false;
    for (auto& l : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(l); std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        if (tok.size() >= 3 && tok[1] == "G" && tok[2] == grp && tok[0] != "0")
            grpExists = true;
    }
    if (!grpExists) return "ERROR: Grupo '" + grp + "' no existe\n";

    std::vector<std::string> newLines;
    for (auto& l : lines) {
        std::vector<std::string> tok;
        std::istringstream ss(l); std::string t;
        while (std::getline(ss, t, ',')) tok.push_back(t);
        if (tok.size() >= 5 && tok[1] == "U" && tok[3] == user && tok[0] != "0") {
            newLines.push_back(tok[0] + ",U," + grp + "," + tok[3] + "," + tok[4]);
            userFound = true;
        } else {
            newLines.push_back(l);
        }
    }
    if (!userFound) return "ERROR: Usuario '" + user + "' no existe\n";
    writeUsersFile(sb, mp, buildUsersContent(newLines));
    return "SUCCESS: Usuario '" + user + "' movido al grupo '" + grp + "'\n";
}
