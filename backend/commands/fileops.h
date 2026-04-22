// ============================================================
// fileops.h — mkdir, mkfile, cat, edit, remove, rename
// ============================================================
#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "../utils/analyzer.h"
#include "../utils/session.h"
#include "../utils/mountmanager.h"
#include "../utils/permissions.h"
#include "../filesystem/filesystem.h"

// ── Helper: obtener SB y MP de sesión activa ─────────────────
static bool getSessionFS(Superblock& sb, MountedPartition*& mp) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return false;
    mp = MountManager::getInstance().findById(s.partitionId);
    if (!mp) return false;
    return getSB(mp->diskPath, mp->partStart, sb);
}

// ── MKDIR — Crear directorio ─────────────────────────────────
inline std::string cmdMkdir(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path = cmd.get("path");
    bool recursive   = cmd.hasFlag("p");
    if (path.empty()) return "ERROR: Falta -path\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS(sb, mp)) return "ERROR: Partición no disponible\n";

    // Si ya existe, retornar ok
    if (pathToInode(sb, mp->diskPath, path) != -1)
        return "INFO: El directorio ya existe: " + path + "\n";

    // Obtener carpetas a crear
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string token;
    while (std::getline(ss, token, '/'))
        if (!token.empty()) parts.push_back(token);

    std::string currentPath = "";
    int parentInode = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        currentPath += "/" + parts[i];
        int inodeNum = pathToInode(sb, mp->diskPath, currentPath);
        if (inodeNum != -1) {
            parentInode = inodeNum;
            continue;
        }
        if (!recursive && i < parts.size() - 1)
            return "ERROR: Ruta padre no existe: " + currentPath +
                   ". Use -p para creación recursiva\n";

        // Verificar permisos de escritura en padre
        Inode parentInodeObj;
        getInode(sb, mp->diskPath, parentInode, parentInodeObj);
        if (!canWrite(parentInodeObj))
            return "ERROR: Sin permisos de escritura en " + dirName(currentPath) + "\n";

        // Crear nuevo inodo de carpeta
        int newInode = allocInode(sb, mp->diskPath, mp->partStart);
        if (newInode == -1) return "ERROR: No hay inodos disponibles\n";
        int newBlock = allocBlock(sb, mp->diskPath, mp->partStart);
        if (newBlock == -1) return "ERROR: No hay bloques disponibles\n";

        Inode ni;
        memset(&ni, 0, sizeof(Inode));
        ni.i_uid  = s.uid;
        ni.i_gid  = s.gid;
        ni.i_type = '1';
        ni.i_size = 0;
        ni.i_perm[0] = 7; ni.i_perm[1] = 5; ni.i_perm[2] = 5;
        for (int b = 0; b < 15; b++) ni.i_block[b] = -1;
        ni.i_block[0] = newBlock;
        std::string dt = currentDateTime();
        strncpy(ni.i_atime, dt.c_str(), 18);
        strncpy(ni.i_ctime, dt.c_str(), 18);
        strncpy(ni.i_mtime, dt.c_str(), 18);
        setInode(sb, mp->diskPath, newInode, ni);

        FolderBlock fb;
        memset(&fb, 0, sizeof(FolderBlock));
        strncpy(fb.b_content[0].b_name, ".", 11);
        fb.b_content[0].b_inodo = newInode;
        strncpy(fb.b_content[1].b_name, "..", 11);
        fb.b_content[1].b_inodo = parentInode;
        fb.b_content[2].b_inodo = -1;
        fb.b_content[3].b_inodo = -1;
        setFolderBlock(sb, mp->diskPath, newBlock, fb);

        addDirEntry(sb, mp->diskPath, mp->partStart, parentInode, parts[i], newInode);
        JOURNAL_ADD(sb, mp->diskPath, "mkdir", currentPath, "");
        parentInode = newInode;
    }
    return "SUCCESS: Directorio creado: " + path + "\n";
}

// ── MKFILE — Crear archivo ────────────────────────────────────
inline std::string cmdMkfile(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path     = cmd.get("path");
    bool recursive       = cmd.hasFlag("r");
    std::string sizeStr  = cmd.get("size", "");
    std::string contenido = cmd.get("contenido", "");
    if (path.empty()) return "ERROR: Falta -path\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS(sb, mp)) return "ERROR: Partición no disponible\n";

    // Validar tamaño
    int fileSize = 0;
    if (!sizeStr.empty()) {
        fileSize = std::stoi(sizeStr);
        if (fileSize < 0) return "ERROR: El tamaño no puede ser negativo\n";
    }

    // Crear directorios padres si -r
    std::string dirPath = dirName(path);
    if (pathToInode(sb, mp->diskPath, dirPath) == -1) {
        if (!recursive)
            return "ERROR: Directorio padre no existe: " + dirPath + "\n";
        // Crear recursivamente (simulamos mkdir -p)
        ParsedCommand mkdirCmd;
        mkdirCmd.name = "mkdir";
        mkdirCmd.params["path"] = dirPath;
        mkdirCmd.params["p"]    = "true";
        std::string r = cmdMkdir(mkdirCmd);
        if (r.find("ERROR") != std::string::npos) return r;
        // Recargar SB
        getSB(mp->diskPath, mp->partStart, sb);
    }

    int parentInode = pathToInode(sb, mp->diskPath, dirPath);
    if (parentInode == -1)
        return "ERROR: Directorio padre no existe: " + dirPath + "\n";

    Inode parentObj;
    getInode(sb, mp->diskPath, parentInode, parentObj);
    if (!canWrite(parentObj))
        return "ERROR: Sin permisos de escritura en " + dirPath + "\n";

    // Construir contenido
    std::string content;
    if (!contenido.empty()) {
        // Leer desde archivo de sistema operativo (ruta en el VFS)
        int srcInode = pathToInode(sb, mp->diskPath, contenido);
        if (srcInode == -1) {
            // Intentar leer desde sistema de archivos real
            std::ifstream f(contenido);
            if (f.is_open()) {
                std::ostringstream oss;
                oss << f.rdbuf();
                content = oss.str();
            } else {
                return "ERROR: Archivo fuente no encontrado: " + contenido + "\n";
            }
        } else {
            content = readFileContent(sb, mp->diskPath, srcInode);
        }
    } else if (fileSize > 0) {
        // Rellenar con '0'
        content = std::string(fileSize, '0');
    }

    // Crear inodo de archivo
    int newInode = allocInode(sb, mp->diskPath, mp->partStart);
    if (newInode == -1) return "ERROR: No hay inodos disponibles\n";

    Inode ni;
    memset(&ni, 0, sizeof(Inode));
    ni.i_uid  = s.uid;
    ni.i_gid  = s.gid;
    ni.i_type = '0'; // archivo
    ni.i_size = 0;
    ni.i_perm[0] = 6; ni.i_perm[1] = 4; ni.i_perm[2] = 4;
    for (int b = 0; b < 15; b++) ni.i_block[b] = -1;
    std::string dt = currentDateTime();
    strncpy(ni.i_atime, dt.c_str(), 18);
    strncpy(ni.i_ctime, dt.c_str(), 18);
    strncpy(ni.i_mtime, dt.c_str(), 18);
    setInode(sb, mp->diskPath, newInode, ni);

    // Agregar entrada en carpeta padre
    std::string fname = baseName(path);
    if (!addDirEntry(sb, mp->diskPath, mp->partStart, parentInode, fname, newInode))
        return "ERROR: No hay espacio en el directorio padre\n";

    // Escribir contenido
    if (!content.empty()) {
        getSB(mp->diskPath, mp->partStart, sb);
        writeFileContent(sb, mp->diskPath, mp->partStart, newInode, content);
    }

    JOURNAL_ADD(sb, mp->diskPath, "mkfile", path, "");
    return "SUCCESS: Archivo creado: " + path + " [" +
           std::to_string(content.size()) + " bytes]\n";
}

// ── CAT — Mostrar contenido de archivo ───────────────────────
inline std::string cmdCat(const ParsedCommand& cmd) {
    std::string file1 = cmd.get("file1");
    if (file1.empty()) return "ERROR: Falta -file1\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS(sb, mp)) return "ERROR: Debe iniciar sesión\n";

    int inodeNum = pathToInode(sb, mp->diskPath, file1);
    if (inodeNum == -1)
        return "ERROR: Archivo no encontrado: " + file1 + "\n";

    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    if (!canRead(inode))
        return "ERROR: Sin permisos de lectura en " + file1 + "\n";
    if (inode.i_type != '0')
        return "ERROR: '" + file1 + "' no es un archivo\n";

    std::string content = readFileContent(sb, mp->diskPath, inodeNum);
    return content + "\n";
}

// ── EDIT — Editar contenido de archivo ───────────────────────
inline std::string cmdEdit(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path      = cmd.get("path");
    std::string contenido = cmd.get("contenido");
    if (path.empty() || contenido.empty())
        return "ERROR: Faltan -path o -contenido\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS(sb, mp)) return "ERROR: Partición no disponible\n";

    int inodeNum = pathToInode(sb, mp->diskPath, path);
    if (inodeNum == -1) return "ERROR: Archivo no encontrado: " + path + "\n";

    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    if (!canWrite(inode)) return "ERROR: Sin permisos de escritura\n";
    if (inode.i_type != '0') return "ERROR: No es un archivo\n";

    // Obtener nuevo contenido
    std::string content;
    int srcInode = pathToInode(sb, mp->diskPath, contenido);
    if (srcInode != -1) {
        content = readFileContent(sb, mp->diskPath, srcInode);
    } else {
        std::ifstream f(contenido);
        if (!f.is_open()) return "ERROR: Fuente no encontrada: " + contenido + "\n";
        std::ostringstream oss; oss << f.rdbuf();
        content = oss.str();
    }

    writeFileContent(sb, mp->diskPath, mp->partStart, inodeNum, content);
    JOURNAL_ADD(sb, mp->diskPath, "edit", path, "");
    return "SUCCESS: Archivo editado: " + path + "\n";
}

// ── REMOVE — Eliminar archivo o carpeta ──────────────────────
static std::string removeInode(Superblock& sb, MountedPartition* mp,
                                int inodeNum, const std::string& path,
                                bool recursive, int parentInode,
                                const std::string& entryName);

static std::string removeFolder(Superblock& sb, MountedPartition* mp,
                                 int inodeNum, const std::string& path,
                                 bool recursive) {
    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);

    // Verificar permiso de escritura
    if (!canWrite(inode))
        return "ERROR: Sin permisos de escritura en " + path + "\n";

    // Recorrer contenido
    std::vector<std::pair<std::string,int>> children;
    for (int b = 0; b < 15 && inode.i_block[b] != -1; b++) {
        FolderBlock fb;
        getFolderBlock(sb, mp->diskPath, inode.i_block[b], fb);
        for (int j = 0; j < MAX_CONTENT; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string n(fb.b_content[j].b_name);
            if (n == "." || n == "..") continue;
            children.push_back({n, fb.b_content[j].b_inodo});
        }
    }

    if (!children.empty() && !recursive)
        return "ERROR: La carpeta no está vacía. Use -r para eliminación recursiva\n";

    // Eliminar hijos
    for (auto& child : children) {
        std::string childPath = path + "/" + child.first;
        std::string r = removeInode(sb, mp, child.second, childPath,
                                     recursive, inodeNum, child.first);
        if (r.find("ERROR") != std::string::npos) return r;
    }
    return "";
}

static std::string removeInode(Superblock& sb, MountedPartition* mp,
                                int inodeNum, const std::string& path,
                                bool recursive, int parentInode,
                                const std::string& entryName) {
    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);

    if (inode.i_type == '1') {
        std::string r = removeFolder(sb, mp, inodeNum, path, recursive);
        if (!r.empty()) return r;
    } else {
        if (!canWrite(inode))
            return "ERROR: Sin permisos de escritura en " + path + "\n";
    }

    // Liberar bloques
    for (int b = 0; b < 15; b++) {
        if (inode.i_block[b] != -1) {
            freeBlock(sb, mp->diskPath, mp->partStart, inode.i_block[b]);
            inode.i_block[b] = -1;
        }
    }
    freeInode(sb, mp->diskPath, mp->partStart, inodeNum);

    // Quitar entrada del padre
    if (parentInode != -1)
        removeDirEntry(sb, mp->diskPath, parentInode, entryName);

    JOURNAL_ADD(sb, mp->diskPath, "remove", path, "");
    return "";
}

inline std::string cmdRemove(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path = cmd.get("path");
    bool recursive   = cmd.hasFlag("r");
    if (path.empty()) return "ERROR: Falta -path\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS(sb, mp)) return "ERROR: Partición no disponible\n";

    int parentInode;
    int inodeNum = pathToInode(sb, mp->diskPath, path, &parentInode);
    if (inodeNum == -1) return "ERROR: Ruta no encontrada: " + path + "\n";
    if (inodeNum == 0) return "ERROR: No se puede eliminar la raíz\n";

    std::string result = removeInode(sb, mp, inodeNum, path,
                                      recursive, parentInode, baseName(path));
    if (!result.empty()) return result;
    return "SUCCESS: Eliminado: " + path + "\n";
}

// ── RENAME — Renombrar archivo o carpeta ─────────────────────
inline std::string cmdRename(const ParsedCommand& cmd) {
    Session& s = Session::getInstance();
    if (!s.isLoggedIn) return "ERROR: Debe iniciar sesión\n";

    std::string path    = cmd.get("path");
    std::string newName = cmd.get("name");
    if (path.empty() || newName.empty())
        return "ERROR: Faltan -path o -name\n";

    Superblock sb; MountedPartition* mp;
    if (!getSessionFS(sb, mp)) return "ERROR: Partición no disponible\n";

    int parentInode;
    int inodeNum = pathToInode(sb, mp->diskPath, path, &parentInode);
    if (inodeNum == -1) return "ERROR: Ruta no encontrada: " + path + "\n";

    Inode inode;
    getInode(sb, mp->diskPath, inodeNum, inode);
    if (!canWrite(inode)) return "ERROR: Sin permisos de escritura\n";

    // Verificar que el nuevo nombre no exista en el mismo directorio
    std::string parentPath = dirName(path);
    std::string newPath    = parentPath + "/" + newName;
    if (pathToInode(sb, mp->diskPath, newPath) != -1)
        return "ERROR: Ya existe un archivo con el nombre '" + newName + "'\n";

    // Actualizar entrada en bloque del padre
    Inode parent;
    getInode(sb, mp->diskPath, parentInode, parent);
    std::string oldName = baseName(path);
    for (int b = 0; b < 15 && parent.i_block[b] != -1; b++) {
        FolderBlock fb;
        getFolderBlock(sb, mp->diskPath, parent.i_block[b], fb);
        bool updated = false;
        for (int j = 0; j < MAX_CONTENT; j++) {
            if (std::string(fb.b_content[j].b_name) == oldName) {
                memset(fb.b_content[j].b_name, 0, 12);
                strncpy(fb.b_content[j].b_name, newName.c_str(), 11);
                setFolderBlock(sb, mp->diskPath, parent.i_block[b], fb);
                updated = true;
                break;
            }
        }
        if (updated) break;
    }
    JOURNAL_ADD(sb, mp->diskPath, "rename", path, newName);
    return "SUCCESS: '" + path + "' renombrado a '" + newName + "'\n";
}
