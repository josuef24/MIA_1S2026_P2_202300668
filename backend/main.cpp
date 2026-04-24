// ============================================================
// main.cpp — Servidor HTTP REST + Dispatcher de comandos
// ============================================================
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include "httplib.h"
#include "utils/analyzer.h"
#include "utils/session.h"
#include "utils/mountmanager.h"
#include "utils/diskmanager.h"
#include "structures/structs.h"
#include "filesystem/filesystem.h"
// ── Comandos ──────────────────────────────────────────────────
#include "commands/mkdisk.h"
#include "commands/rmdisk.h"
#include "commands/fdisk.h"
#include "commands/mount.h"
#include "commands/mkfs.h"
#include "commands/users.h"
#include "commands/fileops.h"
#include "commands/advanced.h"
#include "commands/journaling.h"

// ── toUpper para dispatch ────────────────────────────────────
static std::string upperStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ── Encabezados CORS para peticiones del frontend ───────────
static void setCORS(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin",  "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

// ── Ejecutar un único comando y retornar su salida ───────────
static std::string executeCommand(const ParsedCommand& cmd) {
    if (cmd.name.empty()) return "";
    if (cmd.name == "#comment")
        return cmd.get("text") + "\n";

    std::string name = upperStr(cmd.name);

    // ── Comandos de disco ─────────────────────────────────────
    if (name == "MKDISK")     return cmdMkdisk(cmd);
    if (name == "RMDISK")     return cmdRmdisk(cmd);
    if (name == "FDISK")      return cmdFdisk(cmd);
    if (name == "MOUNT")      return cmdMount(cmd);
    if (name == "UNMOUNT")    return cmdUnmount(cmd);
    if (name == "MOUNTED")    return cmdMounted(cmd);
    if (name == "MKFS")       return cmdMkfs(cmd);

    // ── Usuarios y sesión ─────────────────────────────────────
    if (name == "LOGOUT")     return cmdLogout();
    if (name == "MKGRP")      return cmdMkgrp(cmd);
    if (name == "RMGRP")      return cmdRmgrp(cmd);
    if (name == "MKUSR")      return cmdMkusr(cmd);
    if (name == "RMUSR")      return cmdRmusr(cmd);
    if (name == "CHGRP")      return cmdChgrp(cmd);

    // ── Archivos y directorios ────────────────────────────────
    if (name == "MKDIR")      return cmdMkdir(cmd);
    if (name == "MKFILE")     return cmdMkfile(cmd);
    if (name == "CAT")        return cmdCat(cmd);
    if (name == "EDIT")       return cmdEdit(cmd);
    if (name == "REMOVE")     return cmdRemove(cmd);
    if (name == "RENAME")     return cmdRename(cmd);

    // ── Operaciones avanzadas ─────────────────────────────────
    if (name == "COPY")       return cmdCopy(cmd);
    if (name == "MOVE")       return cmdMove(cmd);
    if (name == "FIND")       return cmdFind(cmd);
    if (name == "CHOWN")      return cmdChown(cmd);
    if (name == "CHMOD")      return cmdChmod(cmd);

    // ── EXT3 Journaling ───────────────────────────────────────
    if (name == "LOSS")       return cmdLoss(cmd);
    if (name == "RECOVERY")   return cmdRecovery(cmd);
    if (name == "JOURNALING") return cmdJournaling(cmd);

    return "ERROR: Comando desconocido: " + cmd.name + "\n";
}

// ── Extraer valor JSON simple (string) ───────────────────────
static std::string jsonGetStr(const std::string& body, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = body.find(search);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = body.find('"', pos);
    if (pos == std::string::npos) return "";
    pos++;
    
    // Encontrar la comilla de cierre ignorando las escapadas \"
    size_t end = pos;
    while (end < body.size()) {
        if (body[end] == '"') {
            // Verificar si está escapada
            size_t backslashes = 0;
            for (size_t k = end - 1; k >= pos && body[k] == '\\'; k--) {
                backslashes++;
            }
            if (backslashes % 2 == 0) {
                break; // No está escapada, es el fin
            }
        }
        end++;
    }
    
    if (end >= body.size()) return "";
    // Unescape básico
    std::string val = body.substr(pos, end - pos);
    std::string out;
    for (size_t i = 0; i < val.size(); i++) {
        if (val[i] == '\\' && i+1 < val.size()) {
            char next = val[i+1];
            if (next == 'n') { out += '\n'; i++; }
            else if (next == 'r') { out += '\r'; i++; }
            else if (next == 't') { out += '\t'; i++; }
            else if (next == '"') { out += '"'; i++; }
            else if (next == '\\') { out += '\\'; i++; }
            else out += val[i];
        } else {
            out += val[i];
        }
    }
    return out;
}

// ── Escapar string para JSON ──────────────────────────────────
static std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        if      (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else if (c == '\t') r += "\\t";
        else                 r += c;
    }
    return r;
}

// ── Construir JSON de árbol de archivos ──────────────────────
static std::string buildFileTreeJson(Superblock& sb, const std::string& disk,
                                      int inodeNum, const std::string& name,
                                      int depth = 0) {
    Inode inode;
    getInode(sb, disk, inodeNum, inode);

    std::string perm = std::to_string(inode.i_perm[0]) +
                       std::to_string(inode.i_perm[1]) +
                       std::to_string(inode.i_perm[2]);
    std::string type = (inode.i_type == '1') ? "folder" : "file";

    std::string json = "{\"name\":\"" + jsonEscape(name) +
                       "\",\"type\":\"" + type +
                       "\",\"inode\":" + std::to_string(inodeNum) +
                       ",\"perm\":\"" + perm +
                       "\",\"size\":" + std::to_string(inode.i_size);

    if (inode.i_type == '1' && depth < 10) {
        json += ",\"children\":[";
        bool first = true;
        for (int b = 0; b < 15 && inode.i_block[b] != -1; b++) {
            FolderBlock fb;
            getFolderBlock(sb, disk, inode.i_block[b], fb);
            for (int j = 0; j < MAX_CONTENT; j++) {
                if (fb.b_content[j].b_inodo == -1) continue;
                std::string cn(fb.b_content[j].b_name);
                if (cn == "." || cn == "..") continue;
                if (!first) json += ",";
                json += buildFileTreeJson(sb, disk, fb.b_content[j].b_inodo,
                                          cn, depth + 1);
                first = false;
            }
        }
        json += "]";
    }
    json += "}";
    return json;
}

// ── main ──────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);

    httplib::Server svr;

    // ── CORS preflight ────────────────────────────────────────
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.status = 204;
    });

    // ── POST /execute — ejecutar un comando ───────────────────
    svr.Post("/execute", [](const httplib::Request& req, httplib::Response& res) {
        setCORS(res);
        std::string cmdStr = jsonGetStr(req.body, "command");
        if (cmdStr.empty()) {
            res.set_content("{\"error\":\"Comando vacío\"}", "application/json");
            return;
        }
        ParsedCommand cmd = parseCommand(cmdStr);

        // LOGIN: tratamiento especial (recibe user/pass/id)
        if (upperStr(cmd.name) == "LOGIN") {
            std::string u = cmd.get("user");
            std::string p = cmd.get("pass");
            std::string i = cmd.get("id");
            std::string out = cmdLogin(u, p, i);
            bool ok = out.find("SUCCESS") != std::string::npos;
            res.set_content("{\"output\":\"" + jsonEscape(out) +
                            "\",\"error\":" + (ok ? "false" : "true") + "}",
                            "application/json");
            return;
        }

        std::string output = executeCommand(cmd);
        bool isError = output.find("ERROR") == 0;
        res.set_content("{\"output\":\"" + jsonEscape(output) +
                        "\",\"error\":" + (isError ? "true" : "false") + "}",
                        "application/json");
    });

    // ── POST /execute_script — ejecutar script .smia ──────────
    svr.Post("/execute_script", [](const httplib::Request& req,
                                    httplib::Response& res) {
        setCORS(res);
        std::string script = jsonGetStr(req.body, "script");
        if (script.empty()) {
            res.set_content("{\"output\":\"Script vacío\"}", "application/json");
            return;
        }
        auto commands = parseScript(script);
        std::ostringstream out;
        for (auto& cmd : commands) {
            if (upperStr(cmd.name) == "LOGIN") {
                out << cmdLogin(cmd.get("user"), cmd.get("pass"), cmd.get("id"));
            } else {
                out << executeCommand(cmd);
            }
        }
        res.set_content("{\"output\":\"" + jsonEscape(out.str()) + "\"}",
                        "application/json");
    });

    // ── POST /login ───────────────────────────────────────────
    svr.Post("/login", [](const httplib::Request& req, httplib::Response& res) {
        setCORS(res);
        std::string user    = jsonGetStr(req.body, "user");
        std::string pass    = jsonGetStr(req.body, "pass");
        std::string partId  = jsonGetStr(req.body, "id");
        std::string result  = cmdLogin(user, pass, partId);
        bool ok = result.find("SUCCESS") != std::string::npos;
        Session& s = Session::getInstance();
        res.set_content("{\"success\":" + std::string(ok ? "true" : "false") +
                        ",\"message\":\"" + jsonEscape(result) +
                        "\",\"user\":\"" + jsonEscape(s.username) +
                        "\",\"group\":\"" + jsonEscape(s.groupname) + "\"}",
                        "application/json");
    });

    // ── POST /logout ──────────────────────────────────────────
    svr.Post("/logout", [](const httplib::Request& req, httplib::Response& res) {
        setCORS(res);
        std::string result = cmdLogout();
        res.set_content("{\"success\":true,\"message\":\"" +
                        jsonEscape(result) + "\"}", "application/json");
    });

    // ── GET /session — estado de sesión actual ────────────────
    svr.Get("/session", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        Session& s = Session::getInstance();
        res.set_content("{\"loggedIn\":" + std::string(s.isLoggedIn?"true":"false") +
                        ",\"user\":\"" + jsonEscape(s.username) +
                        "\",\"group\":\"" + jsonEscape(s.groupname) +
                        "\",\"partitionId\":\"" + jsonEscape(s.partitionId) + "\"}",
                        "application/json");
    });

    // ── GET /mounted — particiones montadas ───────────────────
    svr.Get("/mounted", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        MountManager& mm = MountManager::getInstance();
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        for (auto& mp : mm.partitions) {
            if (!first) oss << ",";
            // Leer tipo de FS
            Superblock sb;
            int fsType = 0;
            if (getSB(mp.diskPath, mp.partStart, sb))
                fsType = sb.sb_filesystem_type;
            oss << "{\"id\":\"" << mp.id
                << "\",\"disk\":\"" << jsonEscape(mp.diskPath)
                << "\",\"name\":\"" << jsonEscape(mp.partName)
                << "\",\"type\":\"" << mp.partType
                << "\",\"size\":" << mp.partSize
                << ",\"fs\":" << fsType << "}";
            first = false;
        }
        oss << "]";
        res.set_content(oss.str(), "application/json");
    });

    // ── GET /disks — lista de discos registrados ──────────────
    svr.Get("/disks", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        MountManager& mm = MountManager::getInstance();
        // Recolectar discos únicos
        std::vector<std::string> disks;
        for (auto& mp : mm.partitions) {
            bool found = false;
            for (auto& d : disks) if (d == mp.diskPath) { found=true; break; }
            if (!found) disks.push_back(mp.diskPath);
        }
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        for (auto& disk : disks) {
            if (!first) oss << ",";
            MBR mbr; readObject(disk, 0, mbr);
            oss << "{\"path\":\"" << jsonEscape(disk)
                << "\",\"name\":\"" << jsonEscape(disk.substr(disk.rfind('/')+1))
                << "\",\"size\":" << mbr.mbr_tamano
                << ",\"fit\":\"" << mbr.mbr_disk_fit << "\"}";
            first = false;
        }
        oss << "]";
        res.set_content(oss.str(), "application/json");
    });

    // ── GET /partition/:id/files — árbol de archivos ──────────
    svr.Get(R"(/partition/([^/]+)/files)", [](const httplib::Request& req,
                                               httplib::Response& res) {
        setCORS(res);
        std::string id = req.matches[1];
        MountedPartition* mp = MountManager::getInstance().findById(id);
        if (!mp) {
            res.set_content("{\"error\":\"ID no montado\"}", "application/json");
            return;
        }
        Superblock sb;
        if (!getSB(mp->diskPath, mp->partStart, sb)) {
            res.set_content("{\"error\":\"SB no leído\"}", "application/json");
            return;
        }
        std::string tree = buildFileTreeJson(sb, mp->diskPath, 0, "/");
        res.set_content(tree, "application/json");
    });

    // ── GET /partition/:id/file?path=... — contenido archivo ──
    svr.Get(R"(/partition/([^/]+)/file)", [](const httplib::Request& req,
                                              httplib::Response& res) {
        setCORS(res);
        std::string id   = req.matches[1];
        std::string path = req.get_param_value("path");
        MountedPartition* mp = MountManager::getInstance().findById(id);
        if (!mp) {
            res.set_content("{\"error\":\"ID no montado\"}", "application/json");
            return;
        }
        Superblock sb;
        getSB(mp->diskPath, mp->partStart, sb);
        int inodeNum = pathToInode(sb, mp->diskPath, path);
        if (inodeNum == -1) {
            res.set_content("{\"error\":\"Ruta no encontrada\"}", "application/json");
            return;
        }
        std::string content = readFileContent(sb, mp->diskPath, inodeNum);
        res.set_content("{\"content\":\"" + jsonEscape(content) + "\"}",
                        "application/json");
    });

    // ── GET /partition/:id/journal — journal EXT3 ─────────────
    svr.Get(R"(/partition/([^/]+)/journal)", [](const httplib::Request& req,
                                                 httplib::Response& res) {
        setCORS(res);
        std::string id = req.matches[1];
        res.set_content(cmdJournalingJson(id), "application/json");
    });

    // ── GET /health ───────────────────────────────────────────
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.set_content("{\"status\":\"ok\",\"carnet\":\"" CARNET "\"}", "application/json");
    });

    std::cout << "=================================\n";
    std::cout << " MIA Proyecto 2 — Backend C++\n";
    std::cout << " Carnet: " << CARNET << "\n";
    std::cout << " Puerto: " << port << "\n";
    std::cout << "=================================\n";
    std::cout << "Servidor iniciado en http://0.0.0.0:" << port << "\n";

    svr.listen("0.0.0.0", port);
    return 0;
}
