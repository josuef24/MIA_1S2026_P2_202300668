// ============================================================
// analyzer.h — Parser de comandos (lexer + dispatcher)
// ============================================================
#pragma once
#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <vector>

// ── Convertir string a minúsculas ────────────────────────────
inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// ── Convertir string a mayúsculas ────────────────────────────
inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ── Estructura de parámetros parseados ──────────────────────
struct ParsedCommand {
    std::string name;                          // Nombre del comando
    std::map<std::string, std::string> params; // Parámetros clave=valor
    bool hasFlag(const std::string& f) const {
        return params.count(f) > 0;
    }
    std::string get(const std::string& key, const std::string& def = "") const {
        auto it = params.find(toLower(key));
        return (it != params.end()) ? it->second : def;
    }
};

// ── Eliminar espacios al inicio/fin ─────────────────────────
inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// ── Parsear una línea de comando ─────────────────────────────
// Soporta: -clave=valor, -clave="valor con espacios", -flag
inline ParsedCommand parseCommand(const std::string& rawLine) {
    ParsedCommand cmd;
    std::string line = trim(rawLine);
    if (line.empty() || line[0] == '#') return cmd;

    // Extraer nombre del comando (primera palabra)
    size_t firstSpace = line.find(' ');
    if (firstSpace == std::string::npos) {
        cmd.name = toLower(line);
        return cmd;
    }
    cmd.name = toLower(line.substr(0, firstSpace));
    std::string rest = line.substr(firstSpace + 1);

    // Parsear parámetros
    size_t i = 0;
    while (i < rest.size()) {
        // Saltar espacios
        while (i < rest.size() && rest[i] == ' ') i++;
        if (i >= rest.size()) break;

        if (rest[i] == '-') {
            i++; // saltar '-'
            // Leer clave
            size_t keyStart = i;
            while (i < rest.size() && rest[i] != '=' && rest[i] != ' ') i++;
            std::string key = toLower(rest.substr(keyStart, i - keyStart));

            if (i < rest.size() && rest[i] == '=') {
                i++; // saltar '='
                std::string val;
                if (i < rest.size() && rest[i] == '"') {
                    // Valor entre comillas
                    i++;
                    size_t valStart = i;
                    while (i < rest.size() && rest[i] != '"') i++;
                    val = rest.substr(valStart, i - valStart);
                    if (i < rest.size()) i++; // saltar '"'
                } else {
                    // Valor sin comillas
                    size_t valStart = i;
                    while (i < rest.size() && rest[i] != ' ') i++;
                    val = rest.substr(valStart, i - valStart);
                }
                // Manejar valores entre comillas en la posición
                if (!val.empty() && val.front() == '"' && val.back() == '"')
                    val = val.substr(1, val.size() - 2);
                cmd.params[key] = val;
            } else {
                // Flag sin valor (ej: -p, -r)
                cmd.params[key] = "true";
            }
        } else {
            // Saltar token no reconocido
            while (i < rest.size() && rest[i] != ' ') i++;
        }
    }
    return cmd;
}

// ── Parsear script completo (múltiples líneas) ───────────────
inline std::vector<ParsedCommand> parseScript(const std::string& script) {
    std::vector<ParsedCommand> commands;
    std::istringstream ss(script);
    std::string line;
    while (std::getline(ss, line)) {
        // Limpiar \r de Windows
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ParsedCommand cmd = parseCommand(line);
        if (!cmd.name.empty()) commands.push_back(cmd);
        else if (!trim(line).empty() && trim(line)[0] == '#') {
            // Preservar comentarios para la salida
            ParsedCommand comment;
            comment.name = "#comment";
            comment.params["text"] = trim(line);
            commands.push_back(comment);
        }
    }
    return commands;
}
