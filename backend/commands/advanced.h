// ============================================================
// advanced.h — copy, move, find, chown, chmod
// ============================================================
#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <regex>
#include "../utils/analyzer.h"
#include "../utils/session.h"
#include "../utils/mountmanager.h"
#include "../utils/permissions.h"
#include "../filesystem/filesystem.h"

// ── Helper: obtener FS de sesión ─────────────────────────────
static bool getSessionFS2(Superblock& sb, MountedPartition*& mp) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return false;
    mp = MountManager::getInstance().findById(s.partitionId);
    if (!mp) return false;
    return getSB(mp->diskPath, mp->partStart, sb);
}

// ── Copiar inodo recursivamente ──────────────────────────────
static std::string copyInodeRecursive(Superblock& sb, MountedPartition* mp,
                                       int srcInode, int destParent,
                                       const std::string& name) {
    Inode src;
    getInode(sb, mp->diskPath, srcInode, src);
    if (!canRead(src)) return ""; // Sin permiso de lectura, saltar

    // Crear nuevo inodo
    int newInode = allocInode(sb, mp->diskPath, mp->partStart);
    if (newInode == -1) return "ERROR: Sin inodos disponibles\n";

    Inode ni = src;
    ni.i_uid  = Session::getInstance().uid;
    ni.i_gid  = Session::getInstance().gid;
    for (int b = 0; b < 15; b++) ni.i_block[b] = -1;
    std::string dt = currentDateTime();
    strncpy(ni.i_ctime, dt.c_str(), 18);

    if (src.i_type == '0') {
        // Archivo: copiar contenido
        std::string content = readFileContent(sb, mp->diskPath, srcInode);
        setInode(sb, mp->diskPath, newInode, ni);
        writeFileContent(sb, mp->diskPath, mp->partStart, newInode, content);
    } else {
        // Carpeta: crear bloque y copiar hijos
        int newBlock = allocBlock(sb, mp->diskPath, mp->partStart);
        if (newBlock == -1) return "ERROR: Sin bloques disponibles\n";
        ni.i_block[0] = newBlock;
        setInode(sb, mp->diskPath, newInode, ni);
        FolderBlock fb;
        memset(&fb, 0, sizeof(FolderBlock));
        strncpy(fb.b_content[0].b_name, ".", 11);
        fb.b_content[0].b_inodo = newInode;
        strncpy(fb.b_content[1].b_name, "..", 11);
        fb.b_content[1].b_inodo = destParent;
        fb.b_content[2].b_inodo = -1;
        fb.b_content[3].b_inodo = -1;
        setFolderBlock(sb, mp->diskPath, newBlock, fb);

        // Copiar hijos
        for (int b = 0; b < 15 && src.i_block[b] != -1; b++) {
            FolderBlock srcFb;
            getFolderBlock(sb, mp->diskPath, src.i_block[b], srcFb);
            for (int j = 0; j < MAX_CONTENT; j++) {
                if (srcFb.b_content[j].b_inodo == -1) continue;
                std::string cn(srcFb.b_content[j].b_name);
                if (cn == "." || cn == "..") continue;
                std::string r = copyInodeRecursive(sb, mp,
                    srcFb.b_content[j].b_inodo, newInode, cn);
                if (r.find("ERROR") != std::string::npos) return r;
                // addDirEntry ya fue llamado en copyInodeRecursive
            }
        }
    }
    addDirEntry(sb, mp->diskPath, mp->partStart, destParent, name, newInode);
    return "";
}

// ── COPY — Copiar archivo o carpeta ──────────────────────────
inline std::string cmdCopy(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string srcPath  = cmd.get("path");
    std::string destPath = cmd.get("destino");
    if (srcPath.empty() || destPath.empty())
        return "ERROR: Faltan -path o -destino\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS2(sb, mp)) return "ERROR: Partición no disponible\n";

    int srcInode = pathToInode(sb, mp->diskPath, srcPath);
    if (srcInode == -1) return "ERROR: Origen no encontrado: " + srcPath + "\n";

    // El destino debe ser el nombre final del archivo/carpeta
    std::string destDir  = dirName(destPath);
    std::string destName = baseName(destPath);

    int destDirInode = pathToInode(sb, mp->diskPath, destDir);
    if (destDirInode == -1)
        return "ERROR: Directorio destino no existe: " + destDir + "\n";

    Inode destDirObj;
    getInode(sb, mp->diskPath, destDirInode, destDirObj);
    if (!canWrite(destDirObj))
        return "ERROR: Sin permisos de escritura en destino\n";

    getSB(mp->diskPath, mp->partStart, sb);
    std::string result = copyInodeRecursive(sb, mp, srcInode, destDirInode, destName);
    if (!result.empty()) return result;

    JOURNAL_ADD(sb, mp->diskPath, "copy", srcPath, destPath);
    return "SUCCESS: Copiado '" + srcPath + "' → '" + destPath + "'\n";
}

// ── MOVE — Mover archivo o carpeta ───────────────────────────
inline std::string cmdMove(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string srcPath  = cmd.get("path");
    std::string destPath = cmd.get("destino");
    if (srcPath.empty() || destPath.empty())
        return "ERROR: Faltan -path o -destino\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS2(sb, mp)) return "ERROR: Partición no disponible\n";

    int parentInode;
    int srcInode = pathToInode(sb, mp->diskPath, srcPath, &parentInode);
    if (srcInode == -1) return "ERROR: Origen no encontrado: " + srcPath + "\n";

    std::string destDir  = dirName(destPath);
    std::string destName = baseName(destPath);

    int destDirInode = pathToInode(sb, mp->diskPath, destDir);
    if (destDirInode == -1)
        return "ERROR: Directorio destino no existe: " + destDir + "\n";

    Inode destDirObj;
    getInode(sb, mp->diskPath, destDirInode, destDirObj);
    if (!canWrite(destDirObj))
        return "ERROR: Sin permisos de escritura en destino\n";

    // Agregar entrada en nuevo destino
    addDirEntry(sb, mp->diskPath, mp->partStart, destDirInode, destName, srcInode);

    // Quitar entrada del origen
    removeDirEntry(sb, mp->diskPath, parentInode, baseName(srcPath));

    JOURNAL_ADD(sb, mp->diskPath, "move", srcPath, destPath);
    return "SUCCESS: Movido '" + srcPath + "' → '" + destPath + "'\n";
}

// ── FIND — Buscar por nombre (con wildcards * y ?) ───────────
static bool matchWildcard(const std::string& pattern, const std::string& name) {
    // Convertir wildcard a regex
    std::string regexStr;
    for (char c : pattern) {
        if      (c == '*') regexStr += ".*";
        else if (c == '?') regexStr += ".";
        else if (c == '.') regexStr += "\\.";
        else                regexStr += c;
    }
    try {
        std::regex re(regexStr, std::regex::icase);
        return std::regex_match(name, re);
    } catch (...) {
        return name == pattern;
    }
}

static void findRecursive(Superblock& sb, MountedPartition* mp,
                           int inodeNum, const std::string& currentPath,
                           const std::string& pattern,
                           std::vector<std::string>& results) {
    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    if (inode.i_type != '1') return; // Solo carpetas

    for (int b = 0; b < 15 && inode.i_block[b] != -1; b++) {
        FolderBlock fb;
        getFolderBlock(sb, mp->diskPath, inode.i_block[b], fb);
        for (int j = 0; j < MAX_CONTENT; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string name(fb.b_content[j].b_name);
            if (name == "." || name == "..") continue;

            std::string childPath = currentPath + "/" + name;
            if (currentPath == "/") childPath = "/" + name;

            Inode child;
            getInode(sb, mp->diskPath, fb.b_content[j].b_inodo, child);
            if (!canRead(child)) continue;

            if (matchWildcard(pattern, name))
                results.push_back(childPath + (child.i_type == '1' ? "/" : ""));

            if (child.i_type == '1')
                findRecursive(sb, mp, fb.b_content[j].b_inodo,
                               childPath, pattern, results);
        }
    }
}

inline std::string cmdFind(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path    = cmd.get("path");
    std::string pattern = cmd.get("name");
    if (path.empty() || pattern.empty())
        return "ERROR: Faltan -path o -name\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS2(sb, mp)) return "ERROR: Partición no disponible\n";

    int startInode = pathToInode(sb, mp->diskPath, path);
    if (startInode == -1)
        return "ERROR: Ruta no encontrada: " + path + "\n";

    std::vector<std::string> results;
    findRecursive(sb, mp, startInode, path, pattern, results);

    if (results.empty())
        return "INFO: No se encontraron coincidencias para '" + pattern + "'\n";

    std::string out = "Resultados de búsqueda '" + pattern + "':\n";
    for (auto& r : results) out += "  " + r + "\n";
    return out;
}

// ── CHOWN — Cambiar propietario ──────────────────────────────
static int getUidByName(Superblock& sb, MountedPartition* mp,
                          const std::string& username) {
    int inodeNum = pathToInode(sb, mp->diskPath, "/users.txt");
    if (inodeNum == -1) return -1;
    std::string content = readFileContent(sb, mp->diskPath, inodeNum);
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        std::vector<std::string> tok;
        std::istringstream ls(line); std::string t;
        while (std::getline(ls, t, ',')) tok.push_back(t);
        if (tok.size() >= 4 && tok[1] == "U" && tok[3] == username && tok[0] != "0")
            return std::stoi(tok[0]);
    }
    return -1;
}

static void chownRecursive(Superblock& sb, MountedPartition* mp,
                             int inodeNum, int newUid) {
    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    Session& s = Session::getInstance();

    // Solo root o dueño puede cambiar
    if (s.username == "root" || inode.i_uid == s.uid) {
        inode.i_uid = newUid;
        setInode(sb, mp->diskPath, inodeNum, inode);
    }

    if (inode.i_type != '1') return;
    for (int b = 0; b < 15 && inode.i_block[b] != -1; b++) {
        FolderBlock fb;
        getFolderBlock(sb, mp->diskPath, inode.i_block[b], fb);
        for (int j = 0; j < MAX_CONTENT; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string n(fb.b_content[j].b_name);
            if (n == "." || n == "..") continue;
            chownRecursive(sb, mp, fb.b_content[j].b_inodo, newUid);
        }
    }
}

inline std::string cmdChown(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path = cmd.get("path");
    std::string user = cmd.get("user"); // también puede ser -usuario
    if (user.empty()) user = cmd.get("usuario");
    bool recursive   = cmd.hasFlag("r");
    if (path.empty() || user.empty())
        return "ERROR: Faltan -path o -user\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS2(sb, mp)) return "ERROR: Partición no disponible\n";

    int newUid = getUidByName(sb, mp, user);
    if (newUid == -1) return "ERROR: Usuario '" + user + "' no existe\n";

    int inodeNum = pathToInode(sb, mp->diskPath, path);
    if (inodeNum == -1) return "ERROR: Ruta no encontrada: " + path + "\n";

    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    if (s.username != "root" && inode.i_uid != s.uid)
        return "ERROR: Solo root o el dueño puede cambiar el propietario\n";

    if (recursive) {
        chownRecursive(sb, mp, inodeNum, newUid);
    } else {
        inode.i_uid = newUid;
        setInode(sb, mp->diskPath, inodeNum, inode);
    }

    JOURNAL_ADD(sb, mp->diskPath, "chown", path, user);
    return "SUCCESS: Propietario de '" + path + "' cambiado a '" + user + "'\n";
}

// ── CHMOD — Cambiar permisos ──────────────────────────────────
static void chmodRecursive(Superblock& sb, MountedPartition* mp,
                             int inodeNum, int u, int g, int o) {
    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    Session& s = Session::getInstance();

    if (s.username == "root" || inode.i_uid == s.uid) {
        inode.i_perm[0] = u;
        inode.i_perm[1] = g;
        inode.i_perm[2] = o;
        setInode(sb, mp->diskPath, inodeNum, inode);
    }

    if (inode.i_type != '1') return;
    for (int b = 0; b < 15 && inode.i_block[b] != -1; b++) {
        FolderBlock fb;
        getFolderBlock(sb, mp->diskPath, inode.i_block[b], fb);
        for (int j = 0; j < MAX_CONTENT; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string n(fb.b_content[j].b_name);
            if (n == "." || n == "..") continue;
            chmodRecursive(sb, mp, fb.b_content[j].b_inodo, u, g, o);
        }
    }
}

inline std::string cmdChmod(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path = cmd.get("path");
    std::string ugo  = cmd.get("ugo");
    bool recursive   = cmd.hasFlag("r");
    if (path.empty() || ugo.empty())
        return "ERROR: Faltan -path o -ugo\n";
    if (!validUGO(ugo))
        return "ERROR: Permisos UGO inválidos. Use 3 dígitos del 0 al 7\n";

    int u = ugo[0] - '0';
    int g = ugo[1] - '0';
    int o = ugo[2] - '0';

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS2(sb, mp)) return "ERROR: Partición no disponible\n";

    int inodeNum = pathToInode(sb, mp->diskPath, path);
    if (inodeNum == -1) return "ERROR: Ruta no encontrada: " + path + "\n";

    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    if (s.username != "root" && inode.i_uid != s.uid)
        return "ERROR: Sin permisos para cambiar permisos de " + path + "\n";

    if (recursive) {
        chmodRecursive(sb, mp, inodeNum, u, g, o);
    } else {
        inode.i_perm[0] = u;
        inode.i_perm[1] = g;
        inode.i_perm[2] = o;
        setInode(sb, mp->diskPath, inodeNum, inode);
    }

    JOURNAL_ADD(sb, mp->diskPath, "chmod", path, ugo);
    return "SUCCESS: Permisos de '" + path + "' cambiados a " + ugo + "\n";
}
