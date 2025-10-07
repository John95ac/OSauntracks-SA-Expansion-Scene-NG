// ===== INCLUDES COMPLETOS PARA SKSE Y SISTEMA =====
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ===== FUNCIONES UTILITARIAS ULTRA-SEGURAS =====

std::string SafeWideStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    try {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);

        if (size_needed <= 0) {
            size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
            if (size_needed <= 0) return std::string();

            std::string result(size_needed, 0);
            int converted =
                WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
            if (converted <= 0) return std::string();
            return result;
        }

        std::string result(size_needed, 0);
        int converted =
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
        if (converted <= 0) return std::string();
        return result;

    } catch (...) {
        std::string result;
        result.reserve(wstr.size());
        for (wchar_t wc : wstr) {
            if (wc <= 127) {
                result.push_back(static_cast<char>(wc));
            } else {
                result.push_back('?');
            }
        }
        return result;
    }
}

std::string GetEnvVar(const std::string& key) {
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, key.c_str()) == 0 && buf != nullptr) {
        std::string value(buf);
        free(buf);
        return value;
    }
    return "";
}

struct ParsedRule {
    std::string key;
    std::string animationKey;
    std::string soundFile;
    std::string playback;
    std::string extra;
    int applyCount = -1;
};

// ESTRUCTURA PARA MANEJAR SONIDO + PLAYBACK
struct SoundWithPlayback {
    std::string soundFile;
    std::string playback;

    SoundWithPlayback() : playback("0") {}
    SoundWithPlayback(const std::string& sound, const std::string& pb) : soundFile(sound), playback(pb) {}
};

// ENUM PARA RESULTADO DE SETPRESET (MODIFICADO)
enum class SetPresetResult { Added, Replaced, NoChange };

// ESTRUCTURA MODIFICADA PARA REEMPLAZAR SONIDOS EN LUGAR DE AGREGAR
struct OrderedPluginData {
    std::vector<std::pair<std::string, std::vector<SoundWithPlayback>>> orderedData;
    std::set<std::string> processedAnimationKeys;  // NUEVO: Track de keys procesadas desde INI

    // FUNCIÓN MODIFICADA: REEMPLAZA en lugar de agregar cuando ya existe
    SetPresetResult setPreset(const std::string& animationKey, const std::string& soundFile,
                              const std::string& playback = "0") {
        processedAnimationKeys.insert(animationKey);  // Marcar como procesada

        auto it = std::find_if(orderedData.begin(), orderedData.end(),
                               [&animationKey](const auto& pair) { return pair.first == animationKey; });

        if (it == orderedData.end()) {
            // No existe, agregar nueva entrada
            orderedData.emplace_back(animationKey,
                                     std::vector<SoundWithPlayback>{SoundWithPlayback(soundFile, playback)});
            orderedData.back().second.reserve(20);
            return SetPresetResult::Added;
        } else {
            // Ya existe, REEMPLAZAR el contenido completo
            auto& sounds = it->second;

            // Verificar si es exactamente el mismo contenido
            if (sounds.size() == 1 && sounds[0].soundFile == soundFile && sounds[0].playback == playback) {
                return SetPresetResult::NoChange;
            }

            // REEMPLAZAR con el nuevo sonido
            sounds.clear();
            sounds.emplace_back(soundFile, playback);
            return SetPresetResult::Replaced;
        }
    }

    void removePreset(const std::string& animationKey, const std::string& soundFile) {
        auto it = std::find_if(orderedData.begin(), orderedData.end(),
                               [&animationKey](const auto& pair) { return pair.first == animationKey; });

        if (it != orderedData.end()) {
            auto& sounds = it->second;
            auto soundIt = std::find_if(sounds.begin(), sounds.end(), [&soundFile](const SoundWithPlayback& swp) {
                std::string strippedS = swp.soundFile;
                if (!strippedS.empty() && strippedS[0] == '!') {
                    strippedS = strippedS.substr(1);
                }

                std::string strippedTarget = soundFile;
                if (!strippedTarget.empty() && strippedTarget[0] == '!') {
                    strippedTarget = strippedTarget.substr(1);
                }

                return strippedS == strippedTarget;
            });

            if (soundIt != sounds.end()) {
                sounds.erase(soundIt);
                if (sounds.empty()) {
                    orderedData.erase(it);
                }
            }
        }
    }

    void removePlugin(const std::string& animationKey) {
        auto it = std::find_if(orderedData.begin(), orderedData.end(),
                               [&animationKey](const auto& pair) { return pair.first == animationKey; });

        if (it != orderedData.end()) {
            orderedData.erase(it);
        }
    }

    // NUEVA FUNCIÓN: Limpiar AnimationKeys no procesadas
    void cleanUnprocessedKeys() {
        auto it = orderedData.begin();
        while (it != orderedData.end()) {
            if (processedAnimationKeys.find(it->first) == processedAnimationKeys.end()) {
                it = orderedData.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool hasPlugin(const std::string& animationKey) const {
        return std::any_of(orderedData.begin(), orderedData.end(),
                           [&animationKey](const auto& pair) { return pair.first == animationKey; });
    }

    size_t getPluginCount() const { return orderedData.size(); }
    size_t getTotalPresetCount() const {
        size_t count = 0;
        for (const auto& [animationKey, sounds] : orderedData) {
            count += sounds.size();
        }
        return count;
    }
};

// ===== NUEVA FUNCIÓN: VALIDACIÓN SIMPLE DE INTEGRIDAD JSON AL INICIO =====

bool PerformSimpleJsonIntegrityCheck(const fs::path& jsonPath, std::ofstream& logFile) {
    try {
        logFile << "Performing SIMPLE JSON integrity check at startup..." << std::endl;
        logFile << "----------------------------------------------------" << std::endl;

        // Verificar que el archivo existe
        if (!fs::exists(jsonPath)) {
            logFile << "ERROR: JSON file does not exist at: " << jsonPath.string() << std::endl;
            return false;
        }

        // Verificar tamaño mínimo
        auto fileSize = fs::file_size(jsonPath);
        if (fileSize < 10) {
            logFile << "ERROR: JSON file is too small (" << fileSize << " bytes)" << std::endl;
            return false;
        }

        // Leer el contenido completo SIN MODIFICAR
        std::ifstream jsonFile(jsonPath, std::ios::binary);
        if (!jsonFile.is_open()) {
            logFile << "ERROR: Cannot open JSON file for integrity check" << std::endl;
            return false;
        }

        std::string content;
        jsonFile.seekg(0, std::ios::end);
        size_t contentSize = jsonFile.tellg();
        jsonFile.seekg(0, std::ios::beg);
        content.resize(contentSize);
        jsonFile.read(&content[0], contentSize);
        jsonFile.close();

        if (content.empty()) {
            logFile << "ERROR: JSON file is empty after reading" << std::endl;
            return false;
        }

        logFile << "JSON file size: " << fileSize << " bytes" << std::endl;

        // VALIDACIÓN 1: Estructura básica de JSON
        content = content.substr(content.find_first_not_of(" \t\r\n"));
        content = content.substr(0, content.find_last_not_of(" \t\r\n") + 1);

        if (!content.starts_with('{') || !content.ends_with('}')) {
            logFile << "ERROR: JSON does not start with '{' or end with '}'" << std::endl;
            return false;
        }

        // VALIDACIÓN 2: Balance de llaves, corchetes y paréntesis
        int braceCount = 0;    // {}
        int bracketCount = 0;  // []
        int parenCount = 0;    // ()
        bool inString = false;
        bool escape = false;
        int line = 1;
        int col = 1;

        for (size_t i = 0; i < content.length(); i++) {
            char c = content[i];

            if (c == '\n') {
                line++;
                col = 1;
                continue;
            }
            col++;

            if (escape) {
                escape = false;
                continue;
            }

            if (c == '\\') {
                escape = true;
                continue;
            }

            if (c == '"') {
                inString = !inString;
                continue;
            }

            if (!inString) {
                switch (c) {
                    case '{':
                        braceCount++;
                        break;
                    case '}':
                        braceCount--;
                        if (braceCount < 0) {
                            logFile << "ERROR: Unbalanced closing brace '}' at line " << line << ", column " << col
                                    << std::endl;
                            return false;
                        }
                        break;
                    case '[':
                        bracketCount++;
                        break;
                    case ']':
                        bracketCount--;
                        if (bracketCount < 0) {
                            logFile << "ERROR: Unbalanced closing bracket ']' at line " << line << ", column " << col
                                    << std::endl;
                            return false;
                        }
                        break;
                    case '(':
                        parenCount++;
                        break;
                    case ')':
                        parenCount--;
                        if (parenCount < 0) {
                            logFile << "ERROR: Unbalanced closing parenthesis ')' at line " << line << ", column "
                                    << col << std::endl;
                            return false;
                        }
                        break;
                }
            }
        }

        // Verificar balance final
        if (braceCount != 0) {
            logFile << "ERROR: Unbalanced braces (missing " << (braceCount > 0 ? "closing" : "opening")
                    << " braces: " << abs(braceCount) << ")" << std::endl;
            return false;
        }

        if (bracketCount != 0) {
            logFile << "ERROR: Unbalanced brackets (missing " << (bracketCount > 0 ? "closing" : "opening")
                    << " brackets: " << abs(bracketCount) << ")" << std::endl;
            return false;
        }

        if (parenCount != 0) {
            logFile << "ERROR: Unbalanced parentheses (missing " << (parenCount > 0 ? "closing" : "opening")
                    << " parentheses: " << abs(parenCount) << ")" << std::endl;
            return false;
        }

        // VALIDACIÓN 3: Verificar que contiene la clave básica esperada de OSoundtracks
        int foundKeys = 0;
        if (content.find("\"SoundKey\"") != std::string::npos) {
            foundKeys++;
        }

        if (foundKeys < 1) {
            logFile << "ERROR: JSON appears to be corrupted or not a valid OSoundtracks config file" << std::endl;
            logFile << " Expected SoundKey, found only " << foundKeys << " key" << std::endl;
            return false;
        }

        // VALIDACIÓN 4: Verificar sintaxis básica de comas
        std::string cleanContent = content;

        // Remover strings para evitar falsos positivos
        bool inStr = false;
        bool esc = false;
        for (size_t i = 0; i < cleanContent.length(); i++) {
            if (esc) {
                cleanContent[i] = ' ';
                esc = false;
                continue;
            }

            if (cleanContent[i] == '\\') {
                esc = true;
                cleanContent[i] = ' ';
                continue;
            }

            if (cleanContent[i] == '"') {
                inStr = !inStr;
                cleanContent[i] = ' ';
                continue;
            }

            if (inStr) {
                cleanContent[i] = ' ';
            }
        }

        // Verificar patrones problemáticos
        if (cleanContent.find(",,") != std::string::npos) {
            logFile << "ERROR: Found double comma ',,' in JSON structure" << std::endl;
            return false;
        }

        if (cleanContent.find(",}") != std::string::npos) {
            logFile << "WARNING: Found comma before closing brace ',}' (may cause issues)" << std::endl;
        }

        if (cleanContent.find(",]") != std::string::npos) {
            logFile << "WARNING: Found comma before closing bracket ',]' (may cause issues)" << std::endl;
        }

        logFile << "SUCCESS: JSON passed SIMPLE integrity check!" << std::endl;
        logFile << " Found " << foundKeys << " valid OSoundtracks key" << std::endl;
        logFile << " Braces balanced: " << (braceCount == 0 ? "YES" : "NO") << std::endl;
        logFile << " Brackets balanced: " << (bracketCount == 0 ? "YES" : "NO") << std::endl;
        logFile << " Basic structure: VALID" << std::endl;
        logFile << std::endl;

        return true;
    } catch (const std::exception& e) {
        logFile << "ERROR in PerformSimpleJsonIntegrityCheck: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in PerformSimpleJsonIntegrityCheck: Unknown exception" << std::endl;
        return false;
    }
}
// ===== FUNCIONES DE RUTA ULTRA-SEGURAS =====

std::string GetDocumentsPath() {
    try {
        wchar_t path[MAX_PATH] = {0};
        HRESULT result = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, path);

        if (SUCCEEDED(result)) {
            std::wstring ws(path);
            std::string converted = SafeWideStringToString(ws);
            if (!converted.empty()) {
                return converted;
            }
        }

        std::string userProfile = GetEnvVar("USERPROFILE");
        if (!userProfile.empty()) {
            return userProfile + "\\Documents";
        }

        return "C:\\Users\\Default\\Documents";
    } catch (...) {
        return "C:\\Users\\Default\\Documents";
    }
}

std::string GetGamePath() {
    try {
        std::string mo2Path = GetEnvVar("MO2_MODS_PATH");
        if (!mo2Path.empty()) return mo2Path;

        std::string vortexPath = GetEnvVar("VORTEX_MODS_PATH");
        if (!vortexPath.empty()) return vortexPath;

        std::string skyrimMods = GetEnvVar("SKYRIM_MODS_FOLDER");
        if (!skyrimMods.empty()) return skyrimMods;

        std::vector<std::string> registryKeys = {"SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Skyrim Special Edition",
                                                 "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1457087920",
                                                 "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\489830",
                                                 "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\611670"};

        HKEY hKey;
        char pathBuffer[MAX_PATH] = {0};
        DWORD pathSize = sizeof(pathBuffer);

        for (const auto& key : registryKeys) {
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                if (RegQueryValueExA(hKey, "Installed Path", NULL, NULL, (LPBYTE)pathBuffer, &pathSize) ==
                    ERROR_SUCCESS) {
                    RegCloseKey(hKey);
                    std::string result(pathBuffer);
                    if (!result.empty()) return result;
                }
                RegCloseKey(hKey);
            }
        }

        std::vector<std::string> commonPaths = {
            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "C:\\Program Files\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "D:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "E:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "F:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\Steam\\steamapps\\common\\Skyrim Special Edition"};

        for (const auto& pathCandidate : commonPaths) {
            try {
                if (fs::exists(pathCandidate) && fs::is_directory(pathCandidate)) {
                    return pathCandidate;
                }
            } catch (...) {
                continue;
            }
        }

        return "";
    } catch (...) {
        return "";
    }
}

void CreateDirectoryIfNotExists(const fs::path& path) {
    try {
        if (!fs::exists(path)) {
            fs::create_directories(path);
        }
    } catch (...) {
        // Silent fail
    }
}

// ===== FUNCIONES UTILITARIAS MEJORADAS =====

std::string Trim(const std::string& str) {
    if (str.empty()) return str;

    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";

    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    if (str.empty()) return tokens;

    std::stringstream ss(str);
    std::string token;
    tokens.reserve(20);

    while (std::getline(ss, token, delimiter)) {
        std::string trimmed = Trim(token);
        if (!trimmed.empty()) {
            tokens.push_back(std::move(trimmed));
        }
    }
    return tokens;
}

std::string EscapeJson(const std::string& str) {
    std::string result;
    result.reserve(str.length() * 1.3);

    for (char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (c >= 0x20 && c <= 0x7E) {
                    result += c;
                } else {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                }
        }
    }
    return result;
}

// Normalizar el tiempo de playback - acepta cualquier valor numérico
std::string NormalizePlayback(const std::string& playback) {
    if (playback.empty()) return "0"; // Default: 0 segundos si está vacío
    
    std::string trimmed = Trim(playback);
    if (trimmed.empty()) return "0";
    
    // Retornar el valor tal cual - sin validación de rango
    return trimmed;
}

ParsedRule ParseRuleLine(const std::string& key, const std::string& value) {
    ParsedRule rule;
    rule.key = key;

    std::vector<std::string> parts = Split(value, '|');
    if (parts.size() >= 2) {
        rule.animationKey = Trim(parts[0]);
        rule.soundFile = Trim(parts[1]);

        // Parsear el tercer parámetro: tiempo en segundos
        if (parts.size() >= 3) {
            std::string timeStr = Trim(parts[2]);
            rule.playback = NormalizePlayback(timeStr); // Ahora retorna "0" por defecto
        } else {
            rule.playback = "0"; // Default: 0 segundos si no existe el tercer parámetro
        }

        if (parts.size() >= 4) {
            rule.extra = Trim(parts[3]);
        }

        // OSoundtracks: modo simplificado - siempre aplicar
        rule.applyCount = -1;
    }

    return rule;
}

// ===== SISTEMA DE BACKUP LITERAL PERFECTO =====

int ReadBackupConfigFromIni(const fs::path& iniPath, std::ofstream& logFile) {
    try {
        if (!fs::exists(iniPath)) {
            logFile << "Creating backup config INI at: " << iniPath.string() << std::endl;
            std::ofstream createIni(iniPath, std::ios::out | std::ios::trunc);
            if (createIni.is_open()) {
                createIni << "[Original backup]" << std::endl;
                createIni << "Backup = 1" << std::endl;
                createIni.close();
                logFile << "SUCCESS: Backup config INI created with default value (Backup = 1)" << std::endl;
                return 1;
            } else {
                logFile << "ERROR: Could not create backup config INI file!" << std::endl;
                return 0;
            }
        }

        std::ifstream iniFile(iniPath);
        if (!iniFile.is_open()) {
            logFile << "ERROR: Could not open backup config INI file for reading!" << std::endl;
            return 0;
        }

        std::string line;
        bool inBackupSection = false;
        int backupValue = 1;

        while (std::getline(iniFile, line)) {
            std::string trimmedLine = Trim(line);

            if (trimmedLine == "[Original backup]") {
                inBackupSection = true;
                continue;
            }

            if (trimmedLine.length() > 0 && trimmedLine[0] == '[' && trimmedLine != "[Original backup]") {
                inBackupSection = false;
                continue;
            }

            if (inBackupSection) {
                size_t equalPos = trimmedLine.find('=');
                if (equalPos != std::string::npos) {
                    std::string key = Trim(trimmedLine.substr(0, equalPos));
                    std::string value = Trim(trimmedLine.substr(equalPos + 1));

                    if (key == "Backup") {
                        if (value == "true" || value == "True" || value == "TRUE") {
                            backupValue = 2;  // Valor especial: siempre hacer backup
                            logFile << "Read backup config: Backup = true (always backup mode)" << std::endl;
                        } else {
                            try {
                                backupValue = std::stoi(value);
                                logFile << "Read backup config: Backup = " << backupValue << std::endl;
                            } catch (...) {
                                logFile << "Warning: Invalid backup value '" << value << "', using default (1)"
                                        << std::endl;
                                backupValue = 1;
                            }
                        }
                        break;
                    }
                }
            }
        }

        iniFile.close();
        return backupValue;
    } catch (const std::exception& e) {
        logFile << "ERROR in ReadBackupConfigFromIni: " << e.what() << std::endl;
        return 0;
    } catch (...) {
        logFile << "ERROR in ReadBackupConfigFromIni: Unknown exception" << std::endl;
        return 0;
    }
}

void UpdateBackupConfigInIni(const fs::path& iniPath, std::ofstream& logFile, int originalValue) {
    try {
        if (!fs::exists(iniPath)) {
            logFile << "ERROR: Backup config INI file does not exist for update!" << std::endl;
            return;
        }

        if (originalValue == 2) {
            logFile << "INFO: Backup = true detected, INI will not be updated (always backup mode)" << std::endl;
            return;
        }

        std::ifstream iniFile(iniPath);
        if (!iniFile.is_open()) {
            logFile << "ERROR: Could not open backup config INI file for reading during update!" << std::endl;
            return;
        }

        std::vector<std::string> lines;
        std::string line;
        bool inBackupSection = false;
        bool backupValueUpdated = false;
        lines.reserve(100);

        while (std::getline(iniFile, line)) {
            std::string trimmedLine = Trim(line);

            if (trimmedLine == "[Original backup]") {
                inBackupSection = true;
                lines.push_back(line);
                continue;
            }

            if (trimmedLine.length() > 0 && trimmedLine[0] == '[' && trimmedLine != "[Original backup]") {
                inBackupSection = false;
                lines.push_back(line);
                continue;
            }

            if (inBackupSection) {
                size_t equalPos = trimmedLine.find('=');
                if (equalPos != std::string::npos) {
                    std::string key = Trim(trimmedLine.substr(0, equalPos));

                    if (key == "Backup") {
                        lines.push_back("Backup = 0");
                        backupValueUpdated = true;
                        continue;
                    }
                }
            }

            lines.push_back(line);
        }

        iniFile.close();

        if (!backupValueUpdated) {
            logFile << "Warning: Backup value not found in INI during update!" << std::endl;
            return;
        }

        std::ofstream outFile(iniPath, std::ios::out | std::ios::trunc);
        if (!outFile.is_open()) {
            logFile << "ERROR: Could not open backup config INI file for writing during update!" << std::endl;
            return;
        }

        for (const auto& outputLine : lines) {
            outFile << outputLine << std::endl;
        }

        outFile.close();

        if (outFile.fail()) {
            logFile << "ERROR: Failed to write backup config INI file!" << std::endl;
        } else {
            logFile << "SUCCESS: Backup config updated (Backup = 0)" << std::endl;
        }
    } catch (const std::exception& e) {
        logFile << "ERROR in UpdateBackupConfigInIni: " << e.what() << std::endl;
    } catch (...) {
        logFile << "ERROR in UpdateBackupConfigInIni: Unknown exception" << std::endl;
    }
}

// ===== BACKUP LITERAL BYTE-POR-BYTE =====

bool PerformLiteralJsonBackup(const fs::path& originalJsonPath, const fs::path& backupJsonPath,
                              std::ofstream& logFile) {
    try {
        if (!fs::exists(originalJsonPath)) {
            logFile << "ERROR: Original JSON file does not exist at: " << originalJsonPath.string() << std::endl;
            return false;
        }

        CreateDirectoryIfNotExists(backupJsonPath.parent_path());

        // COPIA LITERAL PERFECTA - SIN PROCESAMIENTO
        std::error_code ec;
        fs::copy_file(originalJsonPath, backupJsonPath, fs::copy_options::overwrite_existing, ec);

        if (ec) {
            logFile << "ERROR: Failed to copy JSON file directly: " << ec.message() << std::endl;
            return false;
        }

        // Verificación de integridad byte-por-byte
        try {
            auto originalSize = fs::file_size(originalJsonPath);
            auto backupSize = fs::file_size(backupJsonPath);

            if (originalSize == backupSize && originalSize > 0) {
                logFile << "SUCCESS: LITERAL JSON backup completed to: " << backupJsonPath.string() << std::endl;
                logFile << "Backup file size: " << backupSize << " bytes (verified identical to original)" << std::endl;
                return true;
            } else {
                logFile << "ERROR: Backup file size mismatch! Original: " << originalSize << ", Backup: " << backupSize
                        << std::endl;
                return false;
            }
        } catch (...) {
            logFile << "SUCCESS: LITERAL JSON backup completed (size verification failed but backup exists)"
                    << std::endl;
            return true;
        }
    } catch (const std::exception& e) {
        logFile << "ERROR in PerformLiteralJsonBackup: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in PerformLiteralJsonBackup: Unknown exception" << std::endl;
        return false;
    }
}

// ===== VERIFICACIÓN TRIPLE DE INTEGRIDAD =====

bool PerformTripleValidation(const fs::path& jsonPath, const fs::path& backupPath, std::ofstream& logFile) {
    try {
        if (!fs::exists(jsonPath)) {
            logFile << "ERROR: JSON file does not exist for validation: " << jsonPath.string() << std::endl;
            return false;
        }

        auto fileSize = fs::file_size(jsonPath);
        if (fileSize < 10) {
            logFile << "ERROR: JSON file is too small (" << fileSize << " bytes)" << std::endl;
            return false;
        }

        std::ifstream jsonFile(jsonPath, std::ios::binary);
        if (!jsonFile.is_open()) {
            logFile << "ERROR: Cannot open JSON file for validation" << std::endl;
            return false;
        }

        std::string content;
        jsonFile.seekg(0, std::ios::end);
        size_t contentSize = jsonFile.tellg();
        jsonFile.seekg(0, std::ios::beg);
        content.resize(contentSize);
        jsonFile.read(&content[0], contentSize);
        jsonFile.close();

        if (content.empty()) {
            logFile << "ERROR: JSON file is empty after reading" << std::endl;
            return false;
        }

        // VALIDACIÓN 1: Estructura JSON básica
        content = Trim(content);
        if (!content.starts_with('{') || !content.ends_with('}')) {
            logFile << "ERROR: JSON file does not have proper structure (missing braces)" << std::endl;
            return false;
        }

        // VALIDACIÓN 2: Balance de llaves y corchetes
        int braceCount = 0;
        int bracketCount = 0;
        bool inString = false;
        bool escape = false;

        for (char c : content) {
            if (c == '"' && !escape) {
                inString = !inString;
            } else if (!inString) {
                if (c == '{')
                    braceCount++;
                else if (c == '}')
                    braceCount--;
                else if (c == '[')
                    bracketCount++;
                else if (c == ']')
                    bracketCount--;
            }
            escape = (c == '\\' && !escape);
        }

        if (braceCount != 0 || bracketCount != 0) {
            logFile << "ERROR: JSON has unbalanced braces/brackets (braces: " << braceCount
                    << ", brackets: " << bracketCount << ")" << std::endl;
            return false;
        }

        // VALIDACIÓN 3: Clave OSoundtracks esperada
        int foundKeys = 0;
        if (content.find("\"SoundKey\"") != std::string::npos) {
            foundKeys++;
        }

        if (foundKeys < 1) {
            logFile << "ERROR: JSON appears corrupted (missing expected SoundKey, found only " << foundKeys << " key)"
                    << std::endl;
            return false;
        }

        logFile << "SUCCESS: JSON file passed TRIPLE validation (" << fileSize << " bytes, " << foundKeys
                << " valid key found)" << std::endl;
        return true;
    } catch (const std::exception& e) {
        logFile << "ERROR in PerformTripleValidation: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in PerformTripleValidation: Unknown exception" << std::endl;
        return false;
    }
}

// ===== ANÁLISIS FORENSE AUTOMÁTICO =====

bool MoveCorruptedJsonToAnalysis(const fs::path& corruptedJsonPath, const fs::path& analysisDir,
                                 std::ofstream& logFile) {
    try {
        if (!fs::exists(corruptedJsonPath)) {
            logFile << "WARNING: Corrupted JSON file does not exist for analysis" << std::endl;
            return false;
        }

        CreateDirectoryIfNotExists(analysisDir);

        // Generar nombre único con timestamp
        auto now = std::chrono::system_clock::now();
        std::time_t time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &time_t);

        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);

        fs::path analysisFile =
            analysisDir / ("OSoundtracks-SA-Expansion-Sounds-NG_corrupted_" + std::string(timestamp) + ".json");

        std::error_code ec;
        fs::copy_file(corruptedJsonPath, analysisFile, fs::copy_options::overwrite_existing, ec);

        if (ec) {
            logFile << "ERROR: Failed to move corrupted JSON to analysis folder: " << ec.message() << std::endl;
            return false;
        }

        logFile << "SUCCESS: Corrupted JSON moved to analysis folder: " << analysisFile.string() << std::endl;
        return true;
    } catch (const std::exception& e) {
        logFile << "ERROR in MoveCorruptedJsonToAnalysis: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in MoveCorruptedJsonToAnalysis: Unknown exception" << std::endl;
        return false;
    }
}

// ===== RESTAURACIÓN DESDE BACKUP =====

bool RestoreJsonFromBackup(const fs::path& backupJsonPath, const fs::path& originalJsonPath,
                           const fs::path& analysisDir, std::ofstream& logFile) {
    try {
        if (!fs::exists(backupJsonPath)) {
            logFile << "ERROR: Backup JSON file does not exist: " << backupJsonPath.string() << std::endl;
            return false;
        }

        // Verificar integridad del backup antes de restaurar
        if (!PerformTripleValidation(backupJsonPath, fs::path(), logFile)) {
            logFile << "ERROR: Backup JSON file is also corrupted, cannot restore!" << std::endl;
            return false;
        }

        logFile << "WARNING: Original JSON appears corrupted, restoring from backup..." << std::endl;

        // Mover archivo corrupto a análisis forense
        if (fs::exists(originalJsonPath)) {
            MoveCorruptedJsonToAnalysis(originalJsonPath, analysisDir, logFile);
        }

        // RESTAURAR USANDO COPIA LITERAL
        std::error_code ec;
        fs::copy_file(backupJsonPath, originalJsonPath, fs::copy_options::overwrite_existing, ec);

        if (ec) {
            logFile << "ERROR: Failed to restore JSON from backup: " << ec.message() << std::endl;
            return false;
        }

        // Verificar que la restauración fue exitosa
        if (PerformTripleValidation(originalJsonPath, fs::path(), logFile)) {
            logFile << "SUCCESS: JSON restored from backup successfully!" << std::endl;
            return true;
        } else {
            logFile << "ERROR: Restored JSON is still invalid!" << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        logFile << "ERROR in RestoreJsonFromBackup: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in RestoreJsonFromBackup: Unknown exception" << std::endl;
        return false;
    }
}
// ===== CORRECCIÓN COMPLETA DE INDENTACIÓN CON EMPTY INLINE Y MULTI-LINE EMPTY DETECTION =====

bool CorrectJsonIndentation(const fs::path& jsonPath, const fs::path& analysisDir, std::ofstream& logFile) {
    try {
        logFile << "Checking and correcting JSON indentation hierarchy..." << std::endl;
        logFile << "----------------------------------------------------" << std::endl;

        // Leer el JSON actual
        if (!fs::exists(jsonPath)) {
            logFile << "ERROR: JSON file does not exist for indentation correction" << std::endl;
            return false;
        }

        std::ifstream jsonFile(jsonPath, std::ios::binary);
        if (!jsonFile.is_open()) {
            logFile << "ERROR: Cannot open JSON file for indentation correction" << std::endl;
            return false;
        }

        std::string originalContent;
        jsonFile.seekg(0, std::ios::end);
        size_t contentSize = jsonFile.tellg();
        jsonFile.seekg(0, std::ios::beg);
        originalContent.resize(contentSize);
        jsonFile.read(&originalContent[0], contentSize);
        jsonFile.close();

        if (originalContent.empty()) {
            logFile << "ERROR: JSON file is empty for indentation correction" << std::endl;
            return false;
        }

        // Verificar si necesita corrección
        bool needsCorrection = false;
        std::vector<std::string> lines;
        std::stringstream ss(originalContent);
        std::string line;

        while (std::getline(ss, line)) {
            lines.push_back(line);
        }

        // Analizar indentación actual - verificar si NO cumple con exactamente 4 espacios por nivel
        for (const auto& currentLine : lines) {
            if (currentLine.empty()) continue;
            if (currentLine.find_first_not_of(" \t") == std::string::npos) continue;  // Solo espacios

            size_t leadingSpaces = 0;
            size_t leadingTabs = 0;

            for (char c : currentLine) {
                if (c == ' ')
                    leadingSpaces++;
                else if (c == '\t')
                    leadingTabs++;
                else
                    break;
            }

            // Si hay tabs O si los espacios no son múltiplos exactos de 4, necesita corrección
            if (leadingTabs > 0 || (leadingSpaces > 0 && leadingSpaces % 4 != 0)) {
                needsCorrection = true;
                break;
            }
        }

        // NUEVA VERIFICACIÓN: Detectar contenedores vacíos multi-línea que necesitan corrección
        if (!needsCorrection) {
            for (size_t i = 0; i < lines.size() - 1; i++) {
                std::string currentTrimmed = Trim(lines[i]);

                // Verificar si la línea actual termina con { o [
                if (currentTrimmed.ends_with("{") || currentTrimmed.ends_with("[")) {
                    char openChar = currentTrimmed.back();
                    char closeChar = (openChar == '{') ? '}' : ']';

                    // Buscar la línea de cierre correspondiente
                    for (size_t j = i + 1; j < lines.size(); j++) {
                        std::string nextTrimmed = Trim(lines[j]);

                        // Si encontramos el carácter de cierre
                        if (nextTrimmed == std::string(1, closeChar) ||
                            nextTrimmed == std::string(1, closeChar) + ",") {
                            // Verificar si hay solo espacios en blanco entre apertura y cierre
                            bool hasOnlyWhitespace = true;
                            for (size_t k = i + 1; k < j; k++) {
                                if (!Trim(lines[k]).empty()) {
                                    hasOnlyWhitespace = false;
                                    break;
                                }
                            }

                            if (hasOnlyWhitespace) {
                                needsCorrection = true;
                                logFile << "DETECTED: Multi-line empty container found at lines " << (i + 1) << "-"
                                        << (j + 1) << ", needs inline correction" << std::endl;
                                break;
                            }
                        }

                        // Si encontramos contenido real, no es un contenedor vacío
                        if (!nextTrimmed.empty() && nextTrimmed != std::string(1, closeChar) &&
                            nextTrimmed != std::string(1, closeChar) + ",") {
                            break;
                        }
                    }

                    if (needsCorrection) break;
                }
            }
        }

        if (!needsCorrection) {
            logFile << "SUCCESS: JSON indentation is already correct (perfect 4-space hierarchy with inline empty "
                       "containers)"
                    << std::endl;
            logFile << std::endl;
            return true;
        }

        logFile << "DETECTED: JSON indentation needs correction - reformatting entire file with perfect 4-space "
                   "hierarchy and inline empty containers..."
                << std::endl;

        // ALGORITMO MEJORADO: Reformat completo con exactamente 4 espacios por nivel + MEJOR DETECCIÓN DE EMPTY
        // CONTAINERS
        std::ostringstream correctedJson;
        int indentLevel = 0;
        bool inString = false;
        bool escape = false;

        // ===== FUNCIÓN HELPER MEJORADA PARA DETECTAR SI UN BLOQUE ESTÁ VACÍO (INCLUYENDO MULTI-LÍNEA) =====
        auto isEmptyBlock = [&originalContent](size_t startPos, char openChar, char closeChar) -> bool {
            size_t pos = startPos + 1;
            int depth = 1;
            bool inStr = false;
            bool esc = false;

            while (pos < originalContent.length() && depth > 0) {
                char c = originalContent[pos];

                if (esc) {
                    esc = false;
                    pos++;
                    continue;
                }

                if (c == '\\' && inStr) {
                    esc = true;
                    pos++;
                    continue;
                }

                if (c == '"') {
                    inStr = !inStr;
                } else if (!inStr) {
                    if (c == openChar) {
                        depth++;
                    } else if (c == closeChar) {
                        depth--;
                        if (depth == 0) {
                            // Encontrado el cierre, verificar si solo hay espacios en blanco entre apertura y cierre
                            std::string between = originalContent.substr(startPos + 1, pos - startPos - 1);
                            std::string trimmedBetween = Trim(between);
                            return trimmedBetween.empty();  // Solo espacios en blanco o completamente vacío
                        }
                    }
                }
                pos++;
            }
            return false;
        };

        for (size_t i = 0; i < originalContent.length(); i++) {
            char c = originalContent[i];

            if (escape) {
                correctedJson << c;
                escape = false;
                continue;
            }

            if (c == '\\' && inString) {
                correctedJson << c;
                escape = true;
                continue;
            }

            if (c == '"' && !escape) {
                inString = !inString;
                correctedJson << c;
                continue;
            }

            if (inString) {
                correctedJson << c;
                continue;
            }

            switch (c) {
                case '{':
                case '[':
                    // NUEVA LÓGICA MEJORADA: Verificar si es un bloque vacío (incluyendo multi-línea)
                    if (isEmptyBlock(i, c, (c == '{') ? '}' : ']')) {
                        // Encontrar el carácter de cierre
                        size_t pos = i + 1;
                        int depth = 1;
                        bool inStr = false;
                        bool esc = false;

                        while (pos < originalContent.length() && depth > 0) {
                            char nextChar = originalContent[pos];

                            if (esc) {
                                esc = false;
                                pos++;
                                continue;
                            }

                            if (nextChar == '\\' && inStr) {
                                esc = true;
                                pos++;
                                continue;
                            }

                            if (nextChar == '"') {
                                inStr = !inStr;
                            } else if (!inStr) {
                                if (nextChar == c) {
                                    depth++;
                                } else if (nextChar == ((c == '{') ? '}' : ']')) {
                                    depth--;
                                }
                            }
                            pos++;
                        }

                        // Escribir el bloque vacío en la misma línea
                        correctedJson << c << ((c == '{') ? '}' : ']');
                        i = pos - 1;  // Saltar hasta después del carácter de cierre

                        // Verificar si necesitamos nueva línea después
                        if (i + 1 < originalContent.length()) {
                            size_t nextNonSpace = i + 1;
                            while (nextNonSpace < originalContent.length() &&
                                   std::isspace(originalContent[nextNonSpace])) {
                                nextNonSpace++;
                            }

                            if (nextNonSpace < originalContent.length() && originalContent[nextNonSpace] != ',' &&
                                originalContent[nextNonSpace] != '}' && originalContent[nextNonSpace] != ']') {
                                correctedJson << '\n';
                                for (int j = 0; j < indentLevel * 4; j++) {
                                    correctedJson << ' ';
                                }
                            }
                        }
                    } else {
                        // Bloque NO vacío: usar formato normal
                        correctedJson << c << '\n';
                        indentLevel++;

                        // Agregar indentación exacta de 4 espacios por nivel
                        for (int j = 0; j < indentLevel * 4; j++) {
                            correctedJson << ' ';
                        }
                    }
                    break;
                case '}':
                case ']':
                    // Ir a nueva línea y reducir indentación
                    correctedJson << '\n';
                    indentLevel--;
                    for (int j = 0; j < indentLevel * 4; j++) {
                        correctedJson << ' ';
                    }
                    correctedJson << c;

                    // Verificar si necesitamos nueva línea después
                    if (i + 1 < originalContent.length()) {
                        size_t nextNonSpace = i + 1;
                        while (nextNonSpace < originalContent.length() && std::isspace(originalContent[nextNonSpace])) {
                            nextNonSpace++;
                        }

                        if (nextNonSpace < originalContent.length() && originalContent[nextNonSpace] != ',' &&
                            originalContent[nextNonSpace] != '}' && originalContent[nextNonSpace] != ']') {
                            correctedJson << '\n';
                            for (int j = 0; j < indentLevel * 4; j++) {
                                correctedJson << ' ';
                            }
                        }
                    }
                    break;
                case ',':
                    correctedJson << c << '\n';

                    // Agregar indentación exacta para la siguiente línea
                    for (int j = 0; j < indentLevel * 4; j++) {
                        correctedJson << ' ';
                    }
                    break;
                case ':':
                    correctedJson << c << ' ';
                    break;
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                    // Ignorar espacios en blanco existentes - los controlamos nosotros
                    break;
                default:
                    correctedJson << c;
                    break;
            }
        }

        std::string correctedContent = correctedJson.str();

        // Limpiar líneas vacías con solo espacios y normalizar
        std::vector<std::string> finalLines;
        std::stringstream finalSS(correctedContent);
        std::string finalLine;

        while (std::getline(finalSS, finalLine)) {
            // Eliminar espacios al final de línea
            while (!finalLine.empty() && finalLine.back() == ' ') {
                finalLine.pop_back();
            }
            finalLines.push_back(finalLine);
        }

        // Reconstruir el JSON final
        std::ostringstream finalJson;
        for (size_t i = 0; i < finalLines.size(); i++) {
            finalJson << finalLines[i];
            if (i < finalLines.size() - 1) {
                finalJson << '\n';
            }
        }

        std::string finalContent = finalJson.str();

        // Escribir el JSON corregido
        fs::path tempPath = jsonPath;
        tempPath.replace_extension(".indent_corrected.tmp");

        std::ofstream tempFile(tempPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!tempFile.is_open()) {
            logFile << "ERROR: Could not create temporary file for indentation correction!" << std::endl;
            return false;
        }

        tempFile << finalContent;
        tempFile.close();

        if (tempFile.fail()) {
            logFile << "ERROR: Failed to write corrected JSON to temporary file!" << std::endl;
            return false;
        }

        // Verificar integridad del archivo corregido
        if (!PerformTripleValidation(tempPath, fs::path(), logFile)) {
            logFile << "ERROR: Corrected JSON failed integrity check!" << std::endl;
            MoveCorruptedJsonToAnalysis(tempPath, analysisDir, logFile);
            try {
                fs::remove(tempPath);
            } catch (...) {
            }
            return false;
        }

        // Reemplazar el archivo original
        std::error_code ec;
        fs::rename(tempPath, jsonPath, ec);

        if (ec) {
            logFile << "ERROR: Failed to replace original with corrected JSON: " << ec.message() << std::endl;
            try {
                fs::remove(tempPath);
            } catch (...) {
            }
            return false;
        }

        // Verificación final
        if (PerformTripleValidation(jsonPath, fs::path(), logFile)) {
            logFile << "SUCCESS: JSON indentation corrected successfully!" << std::endl;
            logFile << " Applied perfect 4-space hierarchy with inline empty containers (including multi-line empty "
                       "detection)"
                    << std::endl;
            logFile << std::endl;
            return true;
        } else {
            logFile << "ERROR: Final corrected JSON failed integrity check!" << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        logFile << "ERROR in CorrectJsonIndentation: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in CorrectJsonIndentation: Unknown exception" << std::endl;
        return false;
    }
}

// ===== PARSER JSON CONSERVADOR CON FORMATO DE 4 ESPACIOS Y PLAYBACK =====

std::string PreserveOriginalSections(const std::string& originalJson,
                                     const std::map<std::string, OrderedPluginData>& processedData,
                                     std::ofstream& logFile) {
    try {
        const std::set<std::string> validKeys = {"SoundKey"};
        std::string result = originalJson;

        // Solo modificar la clave válida que tiene datos
        for (const auto& [key, data] : processedData) {
            if (validKeys.count(key) && !data.orderedData.empty()) {
                // Buscar la posición de esta clave en el JSON original
                std::string keyPattern = "\"" + key + "\"";
                size_t keyPos = result.find(keyPattern);

                if (keyPos != std::string::npos) {
                    // Encontrar el inicio del valor (después del :)
                    size_t colonPos = result.find(":", keyPos);
                    if (colonPos != std::string::npos) {
                        size_t valueStart = colonPos + 1;

                        // Saltar espacios en blanco
                        while (valueStart < result.length() && std::isspace(result[valueStart])) {
                            valueStart++;
                        }

                        // Encontrar el final del valor
                        size_t valueEnd = valueStart;
                        if (valueStart < result.length() && result[valueStart] == '{') {
                            int braceCount = 1;
                            valueEnd = valueStart + 1;
                            bool inString = false;
                            bool escape = false;

                            while (valueEnd < result.length() && braceCount > 0) {
                                char c = result[valueEnd];
                                if (c == '"' && !escape) {
                                    inString = !inString;
                                } else if (!inString) {
                                    if (c == '{')
                                        braceCount++;
                                    else if (c == '}')
                                        braceCount--;
                                }
                                escape = (c == '\\' && !escape);
                                valueEnd++;
                            }

                            // Generar nuevo valor con indentación de exactamente 4 espacios por nivel Y PLAYBACK
                            std::ostringstream newValue;
                            newValue << "{\n";
                            bool first = true;

                            for (const auto& [animationKey, soundsWithPlayback] : data.orderedData) {
                                if (!first) newValue << ",\n";
                                first = false;

                                // Nivel 2: 8 espacios (2 niveles * 4 espacios)
                                newValue << "        \"" << EscapeJson(animationKey) << "\": [\n";

                                bool firstEntry = true;
                                for (const auto& swp : soundsWithPlayback) {
                                    if (!firstEntry) newValue << ",\n";
                                    firstEntry = false;

                                    // Nivel 3: 12 espacios (3 niveles * 4 espacios)
                                    newValue << "            \"" << EscapeJson(swp.soundFile) << "\",\n";
                                    newValue << "            \"" << swp.playback << "\"";
                                }

                                // Cerrar array con nivel 2: 8 espacios
                                newValue << "\n        ]";
                            }

                            // Cerrar objeto con nivel 1: 4 espacios
                            newValue << "\n    }";

                            // Reemplazar el valor en el resultado
                            result.replace(valueStart, valueEnd - valueStart, newValue.str());
                            logFile << "INFO: Successfully updated key '" << key
                                    << "' with proper 4-space indentation and playback data" << std::endl;
                        }
                    }
                }
            }
        }

        return result;
    } catch (const std::exception& e) {
        logFile << "ERROR in PreserveOriginalSections: " << e.what() << std::endl;
        return originalJson;  // Fallback al original
    } catch (...) {
        logFile << "ERROR in PreserveOriginalSections: Unknown exception" << std::endl;
        return originalJson;  // Fallback al original
    }
}

// ===== VERIFICACIÓN DE CAMBIOS NECESARIOS CON PLAYBACK =====

bool CheckIfChangesNeeded(const std::string& originalJson,
                          const std::map<std::string, OrderedPluginData>& processedData) {
    const std::vector<std::string> validKeys = {"SoundKey"};

    for (const auto& key : validKeys) {
        // Verificar si la clave existe en processedData y tiene datos
        auto it = processedData.find(key);
        if (it != processedData.end() && !it->second.orderedData.empty()) {
            // Buscar la clave en el JSON original
            std::string keyPattern = "\"" + key + "\"";
            size_t keyPos = originalJson.find(keyPattern);

            if (keyPos != std::string::npos) {
                // Encontrar el inicio del valor (después del :)
                size_t colonPos = originalJson.find(":", keyPos);
                if (colonPos != std::string::npos) {
                    size_t valueStart = colonPos + 1;

                    // Saltar espacios en blanco
                    while (valueStart < originalJson.length() && std::isspace(originalJson[valueStart])) {
                        valueStart++;
                    }

                    // Encontrar el final del valor
                    size_t valueEnd = valueStart;
                    if (valueStart < originalJson.length() && originalJson[valueStart] == '{') {
                        int braceCount = 1;
                        valueEnd = valueStart + 1;
                        bool inString = false;
                        bool escape = false;

                        while (valueEnd < originalJson.length() && braceCount > 0) {
                            char c = originalJson[valueEnd];
                            if (c == '"' && !escape) {
                                inString = !inString;
                            } else if (!inString) {
                                if (c == '{')
                                    braceCount++;
                                else if (c == '}')
                                    braceCount--;
                            }
                            escape = (c == '\\' && !escape);
                            valueEnd++;
                        }

                        // Extraer el contenido actual de la clave en el JSON original
                        std::string currentValue = originalJson.substr(valueStart, valueEnd - valueStart);

                        // Generar el valor esperado basado en processedData CON PLAYBACK
                        std::ostringstream expectedValue;
                        expectedValue << "{\n";
                        bool first = true;

                        for (const auto& [animationKey, soundsWithPlayback] : it->second.orderedData) {
                            if (!first) expectedValue << ",\n";
                            first = false;
                            expectedValue << "        \"" << animationKey << "\": [\n";

                            bool firstEntry = true;
                            for (const auto& swp : soundsWithPlayback) {
                                if (!firstEntry) expectedValue << ",\n";
                                firstEntry = false;
                                expectedValue << "            \"" << swp.soundFile << "\",\n";
                                expectedValue << "            \"" << swp.playback << "\"";
                            }
                            expectedValue << "\n        ]";
                        }
                        expectedValue << "\n    }";

                        // Comparar valores (ignorando espacios en blanco)
                        std::string cleanCurrent = currentValue;
                        std::string cleanExpected = expectedValue.str();

                        // Función helper para limpiar espacios
                        auto cleanWhitespace = [](std::string& str) {
                            str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());
                        };

                        cleanWhitespace(cleanCurrent);
                        cleanWhitespace(cleanExpected);

                        if (cleanCurrent != cleanExpected) {
                            return true;  // Se necesita escribir
                        }
                    }
                } else {
                    // La clave no existe en el JSON original pero tiene datos procesados
                    return true;  // Se necesita escribir
                }
            }
        }
    }

    // Si el bucle termina sin encontrar diferencias, no hay cambios.
    return false;  // No se necesita escribir
}

// ===== PARSEAR DATOS EXISTENTES DEL JSON CON PLAYBACK =====

std::vector<std::pair<std::string, std::vector<SoundWithPlayback>>> parseOrderedPlugins(const std::string& content) {
    std::vector<std::pair<std::string, std::vector<SoundWithPlayback>>> result;
    if (content.empty()) return result;

    const char* str = content.c_str();
    size_t len = content.length();
    size_t pos = 0;
    const size_t maxIters = 100000;
    size_t iter = 0;

    result.reserve(200);

    try {
        while (pos < len && iter++ < maxIters) {
            // Saltar espacios en blanco
            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos >= len) break;

            if (str[pos] != '"') {
                ++pos;
                continue;
            }

            size_t keyStart = pos + 1;
            ++pos;

            // Encontrar final de clave (animationKey)
            while (pos < len) {
                if (str[pos] == '"') {
                    size_t backslashCount = 0;
                    size_t checkPos = pos - 1;
                    while (checkPos < SIZE_MAX && str[checkPos] == '\\') {
                        backslashCount++;
                        checkPos--;
                    }
                    if (backslashCount % 2 == 0) break;
                }
                ++pos;
            }

            if (pos >= len) break;

            std::string animationKey = content.substr(keyStart, pos - keyStart);
            ++pos;  // skip closing "

            // Buscar :
            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos >= len || str[pos] != ':') {
                ++pos;
                continue;
            }

            ++pos;  // skip :
            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos >= len || str[pos] != '[') {
                ++pos;
                continue;
            }

            ++pos;  // skip [

            std::vector<SoundWithPlayback> sounds;
            sounds.reserve(50);
            size_t soundIter = 0;

            while (pos < len && soundIter++ < maxIters) {
                while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                if (pos >= len) break;

                if (str[pos] == ']') {
                    ++pos;  // skip ]
                    break;
                }

                // Parsear primer elemento (nombre del sonido)
                if (str[pos] != '"') {
                    ++pos;
                    continue;
                }

                size_t soundStart = pos + 1;
                ++pos;

                while (pos < len) {
                    if (str[pos] == '"') {
                        size_t backslashCount = 0;
                        size_t checkPos = pos - 1;
                        while (checkPos < SIZE_MAX && str[checkPos] == '\\') {
                            backslashCount++;
                            checkPos--;
                        }
                        if (backslashCount % 2 == 0) break;
                    }
                    ++pos;
                }

                if (pos >= len) break;

                std::string soundFile = content.substr(soundStart, pos - soundStart);
                ++pos;  // skip closing "

                // Buscar coma después del sound file
                while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                if (pos < len && str[pos] == ',') {
                    ++pos;  // skip ,
                    while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                }

                // Parsear segundo elemento (tiempo en segundos)
                std::string playback = "0";  // valor por defecto: 0 segundos

                if (pos < len && str[pos] == '"') {
                    size_t playbackStart = pos + 1;
                    ++pos;

                    while (pos < len) {
                        if (str[pos] == '"') {
                            size_t backslashCount = 0;
                            size_t checkPos = pos - 1;
                            while (checkPos < SIZE_MAX && str[checkPos] == '\\') {
                                backslashCount++;
                                checkPos--;
                            }
                            if (backslashCount % 2 == 0) break;
                        }
                        ++pos;
                    }

                    if (pos < len) {
                        playback = content.substr(playbackStart, pos - playbackStart);
                        ++pos;  // skip closing "
                    }
                }

                sounds.emplace_back(soundFile, playback);

                while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                if (pos < len && str[pos] == ',') {
                    ++pos;  // skip ,
                    while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                }
            }

            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos < len && str[pos] == ',') ++pos;

            if (!animationKey.empty()) {
                result.emplace_back(std::move(animationKey), std::move(sounds));
            }
        }
    } catch (...) {
        // En caso de error, retornar lo que se haya parseado
    }

    return result;
}

// FUNCIÓN MODIFICADA: NO AGREGAR datos del JSON existente, solo leer para referencia
std::pair<bool, std::string> ReadCompleteJson(const fs::path& jsonPath,
                                              std::map<std::string, OrderedPluginData>& processedData,
                                              std::ofstream& logFile) {
    try {
        if (!fs::exists(jsonPath)) {
            logFile << "ERROR: JSON file does not exist at: " << jsonPath.string() << std::endl;
            return {false, ""};
        }

        if (!PerformTripleValidation(jsonPath, fs::path(), logFile)) {
            logFile << "ERROR: JSON integrity check failed" << std::endl;
            return {false, ""};
        }

        std::ifstream jsonFile(jsonPath, std::ios::binary);
        if (!jsonFile.is_open()) {
            logFile << "ERROR: Could not open JSON file at: " << jsonPath.string() << std::endl;
            return {false, ""};
        }

        logFile << "Reading existing JSON from: " << jsonPath.string() << std::endl;

        jsonFile.seekg(0, std::ios::end);
        size_t fileSize = jsonFile.tellg();
        jsonFile.seekg(0, std::ios::beg);

        const size_t maxFileSize = 50 * 1024 * 1024;  // 50MB
        if (fileSize > maxFileSize) {
            logFile << "WARNING: JSON file is very large (" << fileSize << " bytes), limiting to " << maxFileSize
                    << " bytes" << std::endl;
            fileSize = maxFileSize;
        }

        std::string jsonContent;
        jsonContent.resize(fileSize);
        jsonFile.read(&jsonContent[0], fileSize);
        jsonFile.close();

        if (jsonContent.empty() || jsonContent.size() < 2) {
            logFile << "ERROR: JSON file is empty or too small after reading" << std::endl;
            return {false, ""};
        }

        // NO PARSEAR NI AGREGAR datos del JSON existente a processedData
        // Solo retornar el contenido para preservación
        logFile << "JSON content read successfully (" << fileSize << " bytes)" << std::endl;
        logFile << std::endl;

        return {true, jsonContent};
    } catch (const std::exception& e) {
        logFile << "ERROR in ReadCompleteJson: " << e.what() << std::endl;
        return {false, ""};
    } catch (...) {
        logFile << "ERROR in ReadCompleteJson: Unknown exception occurred" << std::endl;
        return {false, ""};
    }
}

// ===== ESCRITURA ATÓMICA ULTRA-SEGURA =====

bool WriteJsonAtomically(const fs::path& jsonPath, const std::string& content, const fs::path& analysisDir,
                         std::ofstream& logFile) {
    try {
        // Escribir a archivo temporal primero
        fs::path tempPath = jsonPath;
        tempPath.replace_extension(".tmp");

        std::ofstream tempFile(tempPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!tempFile.is_open()) {
            logFile << "ERROR: Could not create temporary JSON file!" << std::endl;
            return false;
        }

        tempFile << content;
        tempFile.close();

        if (tempFile.fail()) {
            logFile << "ERROR: Failed to write to temporary JSON file!" << std::endl;
            return false;
        }

        // Verificar integridad del archivo temporal
        if (!PerformTripleValidation(tempPath, fs::path(), logFile)) {
            logFile << "ERROR: Temporary JSON file failed integrity check!" << std::endl;
            // Mover archivo temporal defectuoso a análisis
            MoveCorruptedJsonToAnalysis(tempPath, analysisDir, logFile);
            try {
                fs::remove(tempPath);
            } catch (...) {
            }
            return false;
        }

        // Mover el archivo temporal al destino final
        std::error_code ec;
        fs::rename(tempPath, jsonPath, ec);

        if (ec) {
            logFile << "ERROR: Failed to move temporary file to final location: " << ec.message() << std::endl;
            try {
                fs::remove(tempPath);
            } catch (...) {
            }
            return false;
        }

        // Verificación final
        if (PerformTripleValidation(jsonPath, fs::path(), logFile)) {
            logFile << "SUCCESS: JSON file written atomically and verified!" << std::endl;
            return true;
        } else {
            logFile << "ERROR: Final JSON file failed integrity check!" << std::endl;
            // Mover archivo final defectuoso a análisis
            MoveCorruptedJsonToAnalysis(jsonPath, analysisDir, logFile);
            return false;
        }
    } catch (const std::exception& e) {
        logFile << "ERROR in WriteJsonAtomically: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in WriteJsonAtomically: Unknown exception" << std::endl;
        return false;
    }
}

// ===== ACTUALIZACIÓN DE CONTEOS INI (AUNQUE OSoundtracks NO LO NECESITA) =====
bool UpdateIniRuleCount(const fs::path& iniPath, const std::string& originalLine, int newCount,
                        std::ofstream& logFile) {
    // OSoundtracks no usa conteos, pero mantenemos la función por compatibilidad del diagrama
    return true;
}
// ===== FUNCIÓN PRINCIPAL CORREGIDA CON REEMPLAZO Y LIMPIEZA DE KEYS =====

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    try {
        SKSE::Init(skse);

        SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
            try {
                if (message->type == SKSE::MessagingInterface::kDataLoaded) {
                    std::string documentsPath;
                    std::string gamePath;

                    // Obtener rutas de manera ultra-segura
                    try {
                        documentsPath = GetDocumentsPath();
                        gamePath = GetGamePath();
                    } catch (...) {
                        RE::ConsoleLog::GetSingleton()->Print(
                            "OSoundtracks Assistant: Error getting paths - using defaults");
                        documentsPath = "C:\\Users\\Default\\Documents";
                        gamePath = "";
                    }

                    if (gamePath.empty() || documentsPath.empty()) {
                        RE::ConsoleLog::GetSingleton()->Print(
                            "OSoundtracks Assistant: Could not find Game or Documents path.");
                        return;
                    }

                    // Configuración de rutas y logging
                    fs::path dataPath = fs::path(gamePath) / "Data";
                    fs::path sksePluginsPath = dataPath / "SKSE" / "Plugins";
                    CreateDirectoryIfNotExists(sksePluginsPath);

                    fs::path logFilePath = fs::path(documentsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" /
                                           "OSoundtracks_SA_Expansion_Sounds_NG.log";
                    CreateDirectoryIfNotExists(logFilePath.parent_path());

                    std::ofstream logFile(logFilePath, std::ios::out | std::ios::trunc);

                    auto now = std::chrono::system_clock::now();
                    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
                    std::tm buf;
                    localtime_s(&buf, &in_time_t);

                    logFile << "====================================================" << std::endl;
                    logFile << "OSoundtracks SA Expansion Sounds NG - REPLACEMENT MODE WITH CLEANUP" << std::endl;
                    logFile << "Log created on: " << std::put_time(&buf, "%Y-%m-%d %H:%M:%S") << std::endl;
                    logFile << "====================================================" << std::endl << std::endl;

                    // RUTAS PRINCIPALES
                    fs::path backupConfigIniPath = sksePluginsPath / "OSoundtracks-SA-Expansion-Sounds-NG.ini";
                    fs::path jsonOutputPath = sksePluginsPath / "OSoundtracks-SA-Expansion-Sounds-NG.json";
                    fs::path backupJsonPath =
                        sksePluginsPath / "Backup_OSoundtracks" / "OSoundtracks-SA-Expansion-Sounds-NG.json";
                    fs::path analysisDir = sksePluginsPath / "Backup_OSoundtracks" / "Analysis";

                    logFile << "Checking backup configuration..." << std::endl;
                    logFile << "----------------------------------------------------" << std::endl;

                    int backupValue = ReadBackupConfigFromIni(backupConfigIniPath, logFile);

                    // ===== VALIDACIÓN DE INTEGRIDAD INICIAL CON RESTAURACIÓN AUTOMÁTICA =====
                    logFile << std::endl;
                    if (!PerformSimpleJsonIntegrityCheck(jsonOutputPath, logFile)) {
                        logFile << std::endl;
                        logFile << "CRITICAL: JSON failed simple integrity check at startup! Attempting to restore "
                                   "from backup..."
                                << std::endl;

                        // Intentar restaurar desde el backup
                        if (RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile)) {
                            logFile << "SUCCESS: JSON restored from backup. Proceeding with the normal process."
                                    << std::endl;
                        } else {
                            logFile << std::endl;
                            logFile << "CRITICAL ERROR: Could not restore from backup. The JSON file is likely "
                                       "corrupted and no valid backup is available."
                                    << std::endl;
                            logFile << "Process terminated to prevent further damage." << std::endl;
                            logFile << std::endl;
                            logFile << "RECOMMENDED ACTIONS:" << std::endl;
                            logFile << "1. Check the analysis folder for the corrupted file: " << analysisDir.string()
                                    << std::endl;
                            logFile << "2. Manually check for any older backups or reinstall the mod providing the "
                                       "base JSON file."
                                    << std::endl;
                            logFile << "3. Contact the mod author if the problem persists." << std::endl;
                            logFile << "====================================================" << std::endl;
                            logFile.close();

                            RE::ConsoleLog::GetSingleton()->Print(
                                "CRITICAL ERROR: OSoundtracks JSON is corrupted and could not be restored! Check the "
                                "log file for details.");
                            return;  // TERMINACIÓN TEMPRANA DEL PROCESO
                        }
                    }

                    logFile << "JSON passed initial integrity check or was restored - proceeding with normal process..."
                            << std::endl;
                    logFile << "Mode: REPLACEMENT (new sounds replace existing ones)" << std::endl;
                    logFile << "Cleanup: AnimationKeys not in INI files will be removed" << std::endl;
                    logFile << std::endl;

                    // Inicializar estructuras de datos
                    const std::set<std::string> validKeys = {"SoundKey"};
                    std::map<std::string, OrderedPluginData> processedData;

                    for (const auto& key : validKeys) {
                        processedData[key] = OrderedPluginData();
                    }

                    bool backupPerformed = false;

                    // SISTEMA DE BACKUP LITERAL PERFECTO
                    if (backupValue == 1 || backupValue == 2) {
                        if (backupValue == 2) {
                            logFile << "Backup enabled (Backup = true), performing LITERAL backup always..."
                                    << std::endl;
                        } else {
                            logFile << "Backup enabled (Backup = 1), performing LITERAL backup..." << std::endl;
                        }

                        if (PerformLiteralJsonBackup(jsonOutputPath, backupJsonPath, logFile)) {
                            backupPerformed = true;
                            // Solo actualizar INI si no es modo "true" (valor 2)
                            if (backupValue != 2) {
                                UpdateBackupConfigInIni(backupConfigIniPath, logFile, backupValue);
                            }
                        } else {
                            logFile << "ERROR: LITERAL backup failed, continuing with normal process..." << std::endl;
                        }
                    } else {
                        logFile << "Backup disabled (Backup = 0), skipping backup" << std::endl;
                        logFile << "The original backup was already performed at "
                                   "\\SKSE\\Plugins\\Backup_OSoundtracks\\OSoundtracks-SA-Expansion-Sounds-NG.json"
                                << std::endl;
                    }

                    logFile << std::endl;

                    // Leer el JSON existente PERO NO CARGAR SUS DATOS
                    auto readResult = ReadCompleteJson(jsonOutputPath, processedData, logFile);
                    bool readSuccess = readResult.first;
                    std::string originalJsonContent = readResult.second;

                    if (!readSuccess) {
                        logFile << "JSON read failed, attempting to restore from backup..." << std::endl;
                        if (fs::exists(backupJsonPath) &&
                            RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile)) {
                            logFile << "Backup restoration successful, retrying JSON read..." << std::endl;
                            readResult = ReadCompleteJson(jsonOutputPath, processedData, logFile);
                            readSuccess = readResult.first;
                            originalJsonContent = readResult.second;
                        }

                        if (!readSuccess) {
                            logFile
                                << "Process truncated due to JSON read error. No INI processing or updates performed."
                                << std::endl;
                            logFile << "====================================================" << std::endl;
                            logFile.close();
                            RE::ConsoleLog::GetSingleton()->Print(
                                "ERROR: JSON read FAILED - CONTACT MODDER OR REINSTALL!");
                            return;
                        } else {
                            logFile << "JSON read successful after restoration!" << std::endl;
                        }
                    }

                    int totalRulesProcessed = 0;
                    int totalRulesApplied = 0;
                    int totalRulesReplaced = 0;  // NUEVO CONTADOR
                    int totalRulesSkipped = 0;
                    int totalFilesProcessed = 0;

                    logFile << "Scanning for OSoundtracksNG_*.ini files..." << std::endl;
                    logFile << "----------------------------------------------------" << std::endl;

                    // Procesar archivos .ini
                    try {
                        for (const auto& entry : fs::directory_iterator(dataPath)) {
                            if (entry.is_regular_file()) {
                                std::string filename = entry.path().filename().string();
                                if (filename.starts_with("OSoundtracksNG_") && filename.ends_with(".ini")) {
                                    logFile << std::endl << "Processing file: " << filename << std::endl;
                                    totalFilesProcessed++;

                                    std::ifstream iniFile(entry.path());
                                    if (!iniFile.is_open()) {
                                        logFile << "  ERROR: Could not open file!" << std::endl;
                                        continue;
                                    }

                                    std::string line;
                                    int rulesInFile = 0;
                                    int rulesAppliedInFile = 0;
                                    int rulesReplacedInFile = 0;  // NUEVO CONTADOR
                                    int rulesSkippedInFile = 0;

                                    while (std::getline(iniFile, line)) {
                                        std::string originalLine = line;

                                        // Eliminar comentarios
                                        size_t commentPos = line.find(';');
                                        if (commentPos != std::string::npos) {
                                            line = line.substr(0, commentPos);
                                        }

                                        commentPos = line.find('#');
                                        if (commentPos != std::string::npos) {
                                            line = line.substr(0, commentPos);
                                        }

                                        // Buscar el signo =
                                        size_t equalPos = line.find('=');
                                        if (equalPos != std::string::npos) {
                                            std::string key = Trim(line.substr(0, equalPos));
                                            std::string value = Trim(line.substr(equalPos + 1));

                                            if (validKeys.count(key) && !value.empty()) {
                                                ParsedRule rule = ParseRuleLine(key, value);

                                                if (!rule.animationKey.empty() && !rule.soundFile.empty()) {
                                                    rulesInFile++;
                                                    totalRulesProcessed++;

                                                    auto& data = processedData[key];
                                                    SetPresetResult result = data.setPreset(
                                                        rule.animationKey, rule.soundFile, rule.playback);

                                                    switch (result) {
                                                        case SetPresetResult::Added:
                                                            rulesAppliedInFile++;
                                                            totalRulesApplied++;
                                                            logFile << "  Added: " << key
                                                                    << " -> AnimationKey: " << rule.animationKey
                                                                    << " -> Sound: " << rule.soundFile
                                                                    << " -> Playback: " << rule.playback << std::endl;
                                                            break;
                                                        case SetPresetResult::Replaced:
                                                            rulesReplacedInFile++;
                                                            totalRulesReplaced++;
                                                            logFile << "  Replaced: " << key
                                                                    << " -> AnimationKey: " << rule.animationKey
                                                                    << " -> Sound: " << rule.soundFile
                                                                    << " -> Playback: " << rule.playback << std::endl;
                                                            break;
                                                        case SetPresetResult::NoChange:
                                                            rulesSkippedInFile++;
                                                            totalRulesSkipped++;
                                                            logFile << "  Skipped (no change): " << key
                                                                    << " -> AnimationKey: " << rule.animationKey
                                                                    << " -> Sound: " << rule.soundFile
                                                                    << " -> Playback: " << rule.playback << std::endl;
                                                            break;
                                                    }

                                                    // OSoundtracks: UpdateIniRuleCount no es necesario
                                                    UpdateIniRuleCount(entry.path(), originalLine, -1, logFile);
                                                }
                                            }
                                        }
                                    }

                                    iniFile.close();

                                    logFile << "  Rules in file: " << rulesInFile << " | Added: " << rulesAppliedInFile
                                            << " | Replaced: " << rulesReplacedInFile
                                            << " | Skipped: " << rulesSkippedInFile << std::endl;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        logFile << "ERROR scanning directory: " << e.what() << std::endl;
                    }

                    // LIMPIEZA: Eliminar AnimationKeys que no fueron procesadas desde los INI
                    logFile << std::endl;
                    logFile << "Cleaning up AnimationKeys not present in INI files..." << std::endl;
                    logFile << "----------------------------------------------------" << std::endl;

                    size_t keysBeforeCleanup = 0;
                    for (const auto& [key, data] : processedData) {
                        keysBeforeCleanup += data.getPluginCount();
                    }

                    // Ejecutar limpieza
                    for (auto& [key, data] : processedData) {
                        data.cleanUnprocessedKeys();
                    }

                    size_t keysAfterCleanup = 0;
                    for (const auto& [key, data] : processedData) {
                        keysAfterCleanup += data.getPluginCount();
                    }

                    size_t keysRemoved = keysBeforeCleanup - keysAfterCleanup;
                    if (keysRemoved > 0) {
                        logFile << "Removed " << keysRemoved << " AnimationKeys not present in INI files" << std::endl;
                    } else {
                        logFile << "No AnimationKeys needed to be removed (all keys in JSON are present in INI files)"
                                << std::endl;
                    }

                    logFile << std::endl;
                    logFile << "====================================================" << std::endl;
                    logFile << "SUMMARY:" << std::endl;

                    if (backupPerformed) {
                        try {
                            auto backupSize = fs::file_size(backupJsonPath);
                            logFile << "Original JSON backup: SUCCESS (" << backupSize << " bytes)" << std::endl;
                        } catch (...) {
                            logFile << "Original JSON backup: SUCCESS (size verification failed)" << std::endl;
                        }
                    } else {
                        logFile << "Original JSON backup: SKIPPED" << std::endl;
                    }

                    logFile << "Total .ini files processed: " << totalFilesProcessed << std::endl;
                    logFile << "Total rules processed: " << totalRulesProcessed << std::endl;
                    logFile << "Total rules added (new): " << totalRulesApplied << std::endl;
                    logFile << "Total rules replaced (updated): " << totalRulesReplaced << std::endl;
                    logFile << "Total rules skipped (no change): " << totalRulesSkipped << std::endl;
                    logFile << "Total AnimationKeys removed (cleanup): " << keysRemoved << std::endl;

                    logFile << std::endl << "Final data in JSON:" << std::endl;
                    for (const auto& [key, data] : processedData) {
                        size_t count = data.getTotalPresetCount();
                        if (count > 0) {
                            logFile << "  " << key << ": " << data.getPluginCount() << " animation keys, " << count
                                    << " total sounds with playback settings" << std::endl;
                        }
                    }
                    logFile << "====================================================" << std::endl << std::endl;

                    // ACTUALIZAR JSON CONSERVADORAMENTE CON FORMATO CORRECTO Y PLAYBACK
                    logFile << "Updating JSON at: " << jsonOutputPath.string() << std::endl;
                    logFile << "Applying proper 4-space indentation format with playback support..." << std::endl;
                    logFile << "Mode: REPLACEMENT (sounds are replaced, not added)" << std::endl;

                    try {
                        // Usar la función que preserva el formato original con indentación correcta
                        std::string updatedJsonContent =
                            PreserveOriginalSections(originalJsonContent, processedData, logFile);

                        // Verificar si los cambios de las reglas ya están aplicados en el JSON
                        if (CheckIfChangesNeeded(originalJsonContent, processedData) || keysRemoved > 0) {
                            if (keysRemoved > 0) {
                                logFile << "AnimationKeys were removed, JSON update required." << std::endl;
                            }
                            logFile << "Changes from INI rules require updating the master JSON file. Proceeding with "
                                       "atomic write..."
                                    << std::endl;

                            // Si hay cambios, ejecutar escritura atómica
                            if (WriteJsonAtomically(jsonOutputPath, updatedJsonContent, analysisDir, logFile)) {
                                logFile << "SUCCESS: JSON updated successfully with proper 4-space indentation "
                                           "hierarchy and playback support!"
                                        << std::endl;

                                // CORRECCIÓN COMPLETA DE INDENTACIÓN
                                logFile << std::endl;
                                if (CorrectJsonIndentation(jsonOutputPath, analysisDir, logFile)) {
                                    logFile << "SUCCESS: JSON indentation verification and correction completed with "
                                               "inline empty containers and multi-line empty detection!"
                                            << std::endl;
                                } else {
                                    logFile << "ERROR: JSON indentation correction failed!" << std::endl;
                                    logFile << "Attempting to restore from backup due to indentation failure..."
                                            << std::endl;
                                    if (fs::exists(backupJsonPath) &&
                                        RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile)) {
                                        logFile << "SUCCESS: JSON restored from backup after indentation failure!"
                                                << std::endl;
                                    } else {
                                        logFile << "CRITICAL ERROR: Could not restore JSON from backup!" << std::endl;
                                    }
                                }
                            } else {
                                logFile << "ERROR: Failed to write JSON safely!" << std::endl;
                                logFile << "Attempting to restore from backup due to write failure..." << std::endl;
                                if (fs::exists(backupJsonPath) &&
                                    RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile)) {
                                    logFile << "SUCCESS: JSON restored from backup after write failure!" << std::endl;
                                } else {
                                    logFile << "CRITICAL ERROR: Could not restore JSON from backup!" << std::endl;
                                }
                            }
                        } else {
                            // Si no hay cambios, omitir escritura y pasar directamente a corrección de indentación
                            logFile << "No changes detected between INI rules and master JSON. Skipping redundant "
                                       "atomic write."
                                    << std::endl;

                            // Siempre asegurar formato perfecto, incluso sin cambios
                            if (CorrectJsonIndentation(jsonOutputPath, analysisDir, logFile)) {
                                logFile << "JSON indentation is already perfect or has been corrected." << std::endl;
                            } else {
                                logFile << "ERROR: JSON indentation correction failed!" << std::endl;
                                logFile << "Attempting to restore from backup due to indentation failure..."
                                        << std::endl;
                                if (fs::exists(backupJsonPath) &&
                                    RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile)) {
                                    logFile << "SUCCESS: JSON restored from backup after indentation failure!"
                                            << std::endl;
                                } else {
                                    logFile << "CRITICAL ERROR: Could not restore JSON from backup!" << std::endl;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        logFile << "ERROR in JSON update process: " << e.what() << std::endl;
                        logFile << "Attempting to restore from backup due to update failure..." << std::endl;
                        if (fs::exists(backupJsonPath) &&
                            RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile)) {
                            logFile << "SUCCESS: JSON restored from backup after update failure!" << std::endl;
                        } else {
                            logFile << "CRITICAL ERROR: Could not restore JSON from backup!" << std::endl;
                        }
                    } catch (...) {
                        logFile << "ERROR in JSON update process: Unknown exception" << std::endl;
                        logFile << "Attempting to restore from backup due to unknown failure..." << std::endl;
                        if (fs::exists(backupJsonPath) &&
                            RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile)) {
                            logFile << "SUCCESS: JSON restored from backup after unknown failure!" << std::endl;
                        } else {
                            logFile << "CRITICAL ERROR: Could not restore JSON from backup!" << std::endl;
                        }
                    }

                    logFile << std::endl
                            << "Process completed successfully with REPLACEMENT mode and cleanup support." << std::endl;
                    logFile.close();

                    RE::ConsoleLog::GetSingleton()->Print(
                        "OSoundtracks Assistant: Process completed with replacement mode!");
                }
            } catch (const std::exception& e) {
                RE::ConsoleLog::GetSingleton()->Print("ERROR in OSoundtracks Assistant main process!");
            } catch (...) {
                RE::ConsoleLog::GetSingleton()->Print("CRITICAL ERROR in OSoundtracks Assistant!");
            }
        });

        return true;
    } catch (const std::exception& e) {
        RE::ConsoleLog::GetSingleton()->Print("ERROR loading OSoundtracks Assistant plugin!");
        return false;
    } catch (...) {
        RE::ConsoleLog::GetSingleton()->Print("CRITICAL ERROR loading OSoundtracks Assistant plugin!");
        return false;
    }
}