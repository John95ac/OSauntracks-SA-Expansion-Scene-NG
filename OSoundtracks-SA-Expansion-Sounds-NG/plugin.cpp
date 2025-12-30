// ===== COMPLETE INCLUDES FOR SKSE AND SYSTEM =====
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <shlobj.h>
#include <windows.h>
#include <knownfolders.h>

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

static constexpr const char* PLUGIN_VERSION = "3.1.0";

static const std::vector<std::string> PRIORITY_ANIMATION_KEYS = {"Start", "OStimAlignMenu"};
static const std::vector<std::string> ORDERED_JSON_KEYS = {"SoundKey", "SoundEffectKey", "SoundPositionKey", "SoundTAGKey", "SoundMenuKey"};

// ===== ULTRA-SAFE UTILITY FUNCTIONS =====

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

std::string RemoveBOM(const std::string& content) {
    if (content.size() >= 3) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(content.c_str());
        if (bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            return content.substr(3);
        }
    }
    return content;
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
    int pista = 0;
    std::string extra;
    int applyCount = -1;
};

struct SoundWithPlayback {
    std::string soundFile;
    std::string listIndex;
    std::string playback;
    int pista;

    SoundWithPlayback() : listIndex("list-1"), playback("0"), pista(0) {}
    SoundWithPlayback(const std::string& sound, const std::string& list, const std::string& pb, int p = 0) 
        : soundFile(sound), listIndex(list), playback(pb), pista(p) {}
};

enum class SetPresetResult { Added, Replaced, NoChange, Accumulated };

struct OrderedPluginData {
    std::vector<std::pair<std::string, std::vector<SoundWithPlayback>>> orderedData;
    std::set<std::string> processedAnimationKeys;

    SetPresetResult setPreset(const std::string& animationKey, const std::string& soundFile,
                              const std::string& playback = "0", int pista = 0) {
        processedAnimationKeys.insert(animationKey);

        auto it = std::find_if(orderedData.begin(), orderedData.end(),
                               [&animationKey](const auto& pair) { return pair.first == animationKey; });

        if (it == orderedData.end()) {
            std::string listPrefix = (pista == 0) ? "list" : "list" + std::to_string(pista);
            std::string listIndex = listPrefix + "-1";
            orderedData.emplace_back(animationKey,
                                     std::vector<SoundWithPlayback>{SoundWithPlayback(soundFile, listIndex, playback, pista)});
            orderedData.back().second.reserve(20);
            sortOrderedData();
            return SetPresetResult::Added;
        } else {
            auto& sounds = it->second;

            for (const auto& existing : sounds) {
                if (existing.soundFile == soundFile) {
                    return SetPresetResult::NoChange;
                }
            }

            int nextListNumber =1;
            for (const auto& existing : sounds) {
                if (existing.pista == pista) {
                    nextListNumber++;
                }
            }

            std::string listPrefix = (pista == 0) ? "list" : "list" + std::to_string(pista);
            std::string listIndex = listPrefix + "-" + std::to_string(nextListNumber);
            sounds.emplace_back(soundFile, listIndex, playback, pista);
            return SetPresetResult::Accumulated;
        }
    }

    void sortOrderedData() {
        std::stable_sort(orderedData.begin(), orderedData.end(),
                        [](const auto& a, const auto& b) {
                            auto isPriorityA = std::find(PRIORITY_ANIMATION_KEYS.begin(), 
                                                        PRIORITY_ANIMATION_KEYS.end(), 
                                                        a.first) != PRIORITY_ANIMATION_KEYS.end();
                            auto isPriorityB = std::find(PRIORITY_ANIMATION_KEYS.begin(), 
                                                        PRIORITY_ANIMATION_KEYS.end(), 
                                                        b.first) != PRIORITY_ANIMATION_KEYS.end();

                            if (isPriorityA && !isPriorityB) return true;
                            if (!isPriorityA && isPriorityB) return false;

                            if (isPriorityA && isPriorityB) {
                                auto posA = std::find(PRIORITY_ANIMATION_KEYS.begin(), 
                                                     PRIORITY_ANIMATION_KEYS.end(), a.first);
                                auto posB = std::find(PRIORITY_ANIMATION_KEYS.begin(), 
                                                     PRIORITY_ANIMATION_KEYS.end(), b.first);
                                return posA < posB;
                            }

                            return a.first < b.first;
                        });
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
                
                std::map<int, int> pistaCounters;
                for (auto& sound : sounds) {
                    int currentPista = sound.pista;
                    pistaCounters[currentPista]++;
                    
                    std::string listPrefix = (currentPista == 0) ? "list" : "list" + std::to_string(currentPista);
                    sound.listIndex = listPrefix + "-" + std::to_string(pistaCounters[currentPista]);
                }
                
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

    void cleanUnprocessedKeys(const std::set<std::string>& allProcessedKeys) {
        auto it = orderedData.begin();
        while (it != orderedData.end()) {
            if (allProcessedKeys.find(it->first) == allProcessedKeys.end()) {
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

// ===== NEW PATH VALIDATION WITH DLL VERIFICATION =====

bool IsValidPluginPath(const fs::path& pluginPath, std::ofstream& logFile) {
    const std::vector<std::string> dllNames = {
        "OSoundtracks-SA-Expansion-Sounds-NG.dll"
    };
    
    for (const auto& dllName : dllNames) {
        fs::path dllPath = pluginPath / dllName;
        
        try {
            if (fs::exists(dllPath)) {
                logFile << "DLL validation passed: Found " << dllName << std::endl;
                return true;
            }
        } catch (...) {
            continue;
        }
    }
    
    logFile << "DLL validation failed: No valid DLL found in path" << std::endl;
    return false;
}

// ===== CASE-INSENSITIVE FILE SEARCH WITH WABBAJACK SUPPORT =====

bool FindFileWithFallback(const fs::path& basePath, const std::string& filename, 
                          fs::path& foundPath, std::ofstream& logFile) {
    try {
        fs::path normalPath = basePath / filename;
        if (fs::exists(normalPath)) {
            foundPath = normalPath;
            return true;
        }
        
        std::string basePathStr = basePath.string();
        if (!basePathStr.empty() && basePathStr.back() != '\\') {
            basePathStr += '\\';
        }
        basePathStr += '\\';
        basePathStr += filename;
        
        fs::path doubleBackslashPath(basePathStr);
        try {
            doubleBackslashPath = fs::canonical(doubleBackslashPath);
            if (fs::exists(doubleBackslashPath)) {
                foundPath = doubleBackslashPath;
                return true;
            }
        } catch (...) {}
        
        if (fs::exists(basePath) && fs::is_directory(basePath)) {
            std::string lowerFilename = filename;
            std::transform(lowerFilename.begin(), lowerFilename.end(), 
                         lowerFilename.begin(), ::tolower);
            
            for (const auto& entry : fs::directory_iterator(basePath)) {
                try {
                    std::string entryFilename = entry.path().filename().string();
                    std::string lowerEntryFilename = entryFilename;
                    std::transform(lowerEntryFilename.begin(), lowerEntryFilename.end(), 
                                 lowerEntryFilename.begin(), ::tolower);
                    
                    if (lowerEntryFilename == lowerFilename) {
                        foundPath = entry.path();
                        return true;
                    }
                } catch (...) {
                    continue;
                }
            }
        }
        
        return false;
        
    } catch (...) {
        return false;
    }
}

// ===== CASE-INSENSITIVE PATH BUILDING FOR WABBAJACK =====

fs::path BuildPathCaseInsensitive(const fs::path& basePath, 
                                  const std::vector<std::string>& components, 
                                  std::ofstream& logFile) {
    try {
        fs::path currentPath = basePath;
        
        for (const auto& component : components) {
            fs::path testPath = currentPath / component;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            std::string lowerComponent = component;
            std::transform(lowerComponent.begin(), lowerComponent.end(), 
                         lowerComponent.begin(), ::tolower);
            testPath = currentPath / lowerComponent;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            std::string upperComponent = component;
            std::transform(upperComponent.begin(), upperComponent.end(), 
                         upperComponent.begin(), ::toupper);
            testPath = currentPath / upperComponent;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            bool found = false;
            if (fs::exists(currentPath) && fs::is_directory(currentPath)) {
                for (const auto& entry : fs::directory_iterator(currentPath)) {
                    try {
                        std::string entryName = entry.path().filename().string();
                        std::string lowerEntryName = entryName;
                        std::transform(lowerEntryName.begin(), lowerEntryName.end(), 
                                     lowerEntryName.begin(), ::tolower);
                        
                        if (lowerEntryName == lowerComponent) {
                            currentPath = entry.path();
                            found = true;
                            break;
                        }
                    } catch (...) {
                        continue;
                    }
                }
            }
            
            if (!found) {
                currentPath = currentPath / component;
            }
        }
        
        return currentPath;
        
    } catch (...) {
        return basePath;
    }
}

// ===== ENHANCED GAME PATH DETECTION WITH EXTENDED REGISTRY =====

std::string GetGamePathEnhanced(std::ofstream& logFile) {
    try {
        std::string mo2Path = GetEnvVar("MO2_MODS_PATH");
        if (!mo2Path.empty()) {
            logFile << "Detected MO2 from environment variable" << std::endl;
            return mo2Path;
        }

        std::string vortexPath = GetEnvVar("VORTEX_MODS_PATH");
        if (!vortexPath.empty()) {
            logFile << "Detected Vortex from environment variable" << std::endl;
            return vortexPath;
        }

        std::vector<std::pair<std::string, std::string>> registryKeys = {
            {"SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Skyrim Special Edition", "Installed Path"},
            {"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1457087920", "path"},
            {"SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\489830", "InstallLocation"},
            {"SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\611670", "InstallLocation"}
        };

        HKEY hKey;
        char pathBuffer[MAX_PATH] = {0};
        DWORD pathSize = sizeof(pathBuffer);

        for (const auto& [key, valueName] : registryKeys) {
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                if (RegQueryValueExA(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)pathBuffer, &pathSize) ==
                    ERROR_SUCCESS) {
                    RegCloseKey(hKey);
                    std::string result(pathBuffer);
                    if (!result.empty()) {
                        logFile << "Detected game path from registry: " << key << std::endl;
                        return result;
                    }
                }
                RegCloseKey(hKey);
            }
            pathSize = sizeof(pathBuffer);
        }

        std::vector<std::string> commonPaths = {
            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "C:\\Program Files\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "D:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "E:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "F:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "C:\\GOG Games\\Skyrim Special Edition",
            "D:\\GOG Games\\Skyrim Special Edition"
        };

        for (const auto& pathCandidate : commonPaths) {
            try {
                if (fs::exists(pathCandidate) && fs::is_directory(pathCandidate)) {
                    logFile << "Detected game path from common locations" << std::endl;
                    return pathCandidate;
                }
            } catch (...) {
                continue;
            }
        }

        logFile << "No standard game path detected" << std::endl;
        return "";
        
    } catch (...) {
        logFile << "ERROR in GetGamePathEnhanced" << std::endl;
        return "";
    }
}

// ===== DLL DIRECTORY DETECTION FOR WABBAJACK FALLBACK =====

fs::path GetDllDirectory(std::ofstream& logFile) {
    try {
        HMODULE hModule = nullptr;

        static int dummyVariable = 0;

        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&dummyVariable), &hModule) &&
            hModule != nullptr) {
            wchar_t dllPath[MAX_PATH] = {0};
            DWORD size = GetModuleFileNameW(hModule, dllPath, MAX_PATH);

            if (size > 0) {
                std::wstring wsDllPath(dllPath);
                std::string dllPathStr = SafeWideStringToString(wsDllPath);

                if (!dllPathStr.empty()) {
                    fs::path dllDir = fs::path(dllPathStr).parent_path();
                    return dllDir;
                }
            }
        }

        return fs::path();

    } catch (const std::exception& e) {
        logFile << "ERROR in GetDllDirectory: " << e.what() << std::endl;
        return fs::path();
    } catch (...) {
        logFile << "ERROR in GetDllDirectory: Unknown exception" << std::endl;
        return fs::path();
    }
}

// ===== SIMPLE JSON INTEGRITY CHECK AT STARTUP =====

bool PerformSimpleJsonIntegrityCheck(const fs::path& jsonPath, std::ofstream& logFile) {
    try {
        logFile << "Performing SIMPLE JSON integrity check at startup..." << std::endl;
        logFile << "----------------------------------------------------" << std::endl;

        if (!fs::exists(jsonPath)) {
            logFile << "ERROR: JSON file does not exist at: " << jsonPath.string() << std::endl;
            return false;
        }

        auto fileSize = fs::file_size(jsonPath);
        if (fileSize < 10) {
            logFile << "ERROR: JSON file is too small (" << fileSize << " bytes)" << std::endl;
            return false;
        }

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

        content = RemoveBOM(content);
        content = content.substr(content.find_first_not_of(" \t\r\n"));
        content = content.substr(0, content.find_last_not_of(" \t\r\n") + 1);

        if (!content.starts_with('{') || !content.ends_with('}')) {
            logFile << "ERROR: JSON does not start with '{' or end with '}'" << std::endl;
            return false;
        }

        int braceCount = 0;
        int bracketCount = 0;
        int parenCount = 0;
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

        int foundKeys = 0;
        if (content.find("\"SoundKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundEffectKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundPositionKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundTAGKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundMenuKey\"") != std::string::npos) {
            foundKeys++;
        }

        if (foundKeys < 1) {
            logFile << "ERROR: JSON appears to be corrupted or not a valid OSoundtracks config file" << std::endl;
            logFile << " Expected at least one valid key (SoundKey, SoundEffectKey, SoundPositionKey, SoundTAGKey, SoundMenuKey), found " << foundKeys << std::endl;
            return false;
        }

        std::string cleanContent = content;

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
        logFile << " Found " << foundKeys << " valid OSoundtracks keys (SoundKey, SoundEffectKey, SoundPositionKey, SoundTAGKey, SoundMenuKey)" << std::endl;
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

// ===== ADVANCED PATH FUNCTIONS (WABBAJACK/MO2/NOLVUS COMPATIBLE) =====

std::string GetDocumentsPath() {
    try {
        wchar_t* path = nullptr;
        HRESULT hr = SHGetKnownFolderPath(
            FOLDERID_Documents,
            0,
            nullptr,
            &path
        );
        
        if (SUCCEEDED(hr) && path != nullptr) {
            std::wstring ws(path);
            CoTaskMemFree(path);
            std::string converted = SafeWideStringToString(ws);
            if (!converted.empty()) {
                return converted;
            }
        }
        
        wchar_t pathBuffer[MAX_PATH] = {0};
        HRESULT result = SHGetFolderPathW(
            nullptr,
            CSIDL_PERSONAL,
            nullptr,
            SHGFP_TYPE_CURRENT,
            pathBuffer
        );
        
        if (SUCCEEDED(result)) {
            std::wstring ws(pathBuffer);
            std::string converted = SafeWideStringToString(ws);
            if (!converted.empty()) {
                return converted;
            }
        }
        
        std::string userProfile = GetEnvVar("USERPROFILE");
        if (!userProfile.empty()) {
            return userProfile + "\\Documents";
        }
        
        std::string homeDrive = GetEnvVar("HOMEDRIVE");
        std::string homePath = GetEnvVar("HOMEPATH");
        if (!homeDrive.empty() && !homePath.empty()) {
            return homeDrive + homePath + "\\Documents";
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
    }
}

// ===== IMPROVED UTILITY FUNCTIONS =====

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

std::string NormalizePlayback(const std::string& playback) {
    if (playback.empty()) return "0";
    
    std::string trimmed = Trim(playback);
    if (trimmed.empty()) return "0";
    
    return trimmed;
}

ParsedRule ParseRuleLine(const std::string& key, const std::string& value) {
    ParsedRule rule;
    rule.key = key;

    std::vector<std::string> parts = Split(value, '|');
    if (parts.size() >= 2) {
        rule.animationKey = Trim(parts[0]);
        rule.soundFile = Trim(parts[1]);

        if (parts.size() >= 3) {
            std::string timeStr = Trim(parts[2]);
            rule.playback = NormalizePlayback(timeStr);
        } else {
            rule.playback = "0";
        }

        if (parts.size() >= 4) {
            std::string pistaStr = Trim(parts[3]);
            if (!pistaStr.empty()) {
                try {
                    rule.pista = std::stoi(pistaStr);
                    if (rule.pista < 0) {
                        rule.pista = 0;
                    }
                } catch (...) {
                    rule.pista = 0;
                }
            }
        }

        if (parts.size() >= 5) {
            rule.extra = Trim(parts[4]);
        }

        rule.applyCount = -1;
    }

    return rule;
}

// ===== PERFECT LITERAL BACKUP SYSTEM =====

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
                            backupValue = 2;
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

bool PerformLiteralJsonBackup(const fs::path& originalJsonPath, const fs::path& backupJsonPath,
                              std::ofstream& logFile) {
    try {
        if (!fs::exists(originalJsonPath)) {
            logFile << "ERROR: Original JSON file does not exist at: " << originalJsonPath.string() << std::endl;
            return false;
        }

        CreateDirectoryIfNotExists(backupJsonPath.parent_path());

        std::error_code ec;
        fs::copy_file(originalJsonPath, backupJsonPath, fs::copy_options::overwrite_existing, ec);

        if (ec) {
            logFile << "ERROR: Failed to copy JSON file directly: " << ec.message() << std::endl;
            return false;
        }

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

        content = RemoveBOM(content);
        content = Trim(content);
        if (!content.starts_with('{') || !content.ends_with('}')) {
            logFile << "ERROR: JSON file does not have proper structure (missing braces)" << std::endl;
            return false;
        }

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

        int foundKeys = 0;
        if (content.find("\"SoundKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundEffectKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundPositionKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundTAGKey\"") != std::string::npos) {
            foundKeys++;
        }
        if (content.find("\"SoundMenuKey\"") != std::string::npos) {
            foundKeys++;
        }

        if (foundKeys < 1) {
            logFile << "ERROR: JSON appears corrupted (missing expected keys, found " << foundKeys << " valid keys)"
                    << std::endl;
            return false;
        }

        logFile << "SUCCESS: JSON file passed TRIPLE validation (" << fileSize << " bytes, " << foundKeys
                << " valid keys found)" << std::endl;
        return true;
    } catch (const std::exception& e) {
        logFile << "ERROR in PerformTripleValidation: " << e.what() << std::endl;
        return false;
    } catch (...) {
        logFile << "ERROR in PerformTripleValidation: Unknown exception" << std::endl;
        return false;
    }
}

// ===== AUTOMATIC FORENSIC ANALYSIS =====

bool MoveCorruptedJsonToAnalysis(const fs::path& corruptedJsonPath, const fs::path& analysisDir,
                                 std::ofstream& logFile) {
    try {
        if (!fs::exists(corruptedJsonPath)) {
            logFile << "WARNING: Corrupted JSON file does not exist for analysis" << std::endl;
            return false;
        }

        CreateDirectoryIfNotExists(analysisDir);

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

// ===== RESTORE FROM BACKUP =====

bool RestoreJsonFromBackup(const fs::path& backupJsonPath, const fs::path& originalJsonPath,
                           const fs::path& analysisDir, std::ofstream& logFile) {
    try {
        if (!fs::exists(backupJsonPath)) {
            logFile << "ERROR: Backup JSON file does not exist: " << backupJsonPath.string() << std::endl;
            return false;
        }

        if (!PerformTripleValidation(backupJsonPath, fs::path(), logFile)) {
            logFile << "ERROR: Backup JSON file is also corrupted, cannot restore!" << std::endl;
            return false;
        }

        logFile << "WARNING: Original JSON appears corrupted, restoring from backup..." << std::endl;

        if (fs::exists(originalJsonPath)) {
            MoveCorruptedJsonToAnalysis(originalJsonPath, analysisDir, logFile);
        }

        std::error_code ec;
        fs::copy_file(backupJsonPath, originalJsonPath, fs::copy_options::overwrite_existing, ec);

        if (ec) {
            logFile << "ERROR: Failed to restore JSON from backup: " << ec.message() << std::endl;
            return false;
        }

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

// ===== COMPLETE INDENTATION CORRECTION WITH EMPTY INLINE AND MULTI-LINE EMPTY DETECTION =====

bool CorrectJsonIndentation(const fs::path& jsonPath, const fs::path& analysisDir, std::ofstream& logFile) {
    try {
        logFile << "Checking and correcting JSON indentation hierarchy..." << std::endl;
        logFile << "----------------------------------------------------" << std::endl;

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

        bool needsCorrection = false;
        std::vector<std::string> lines;
        std::stringstream ss(originalContent);
        std::string line;

        while (std::getline(ss, line)) {
            lines.push_back(line);
        }

        for (const auto& currentLine : lines) {
            if (currentLine.empty()) continue;
            if (currentLine.find_first_not_of(" \t") == std::string::npos) continue;

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

            if (leadingTabs > 0 || (leadingSpaces > 0 && leadingSpaces % 4 != 0)) {
                needsCorrection = true;
                break;
            }
        }

        if (!needsCorrection) {
            for (size_t i = 0; i < lines.size() - 1; i++) {
                std::string currentTrimmed = Trim(lines[i]);

                if (currentTrimmed.ends_with("{") || currentTrimmed.ends_with("[")) {
                    char openChar = currentTrimmed.back();
                    char closeChar = (openChar == '{') ? '}' : ']';

                    for (size_t j = i + 1; j < lines.size(); j++) {
                        std::string nextTrimmed = Trim(lines[j]);

                        if (nextTrimmed == std::string(1, closeChar) ||
                            nextTrimmed == std::string(1, closeChar) + ",") {
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

        std::ostringstream correctedJson;
        int indentLevel = 0;
        bool inString = false;
        bool escape = false;

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
                            std::string between = originalContent.substr(startPos + 1, pos - startPos - 1);
                            std::string trimmedBetween = Trim(between);
                            return trimmedBetween.empty();
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
                    if (isEmptyBlock(i, c, (c == '{') ? '}' : ']')) {
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

                        correctedJson << c << ((c == '{') ? '}' : ']');
                        i = pos - 1;

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
                        correctedJson << c << '\n';
                        indentLevel++;

                        for (int j = 0; j < indentLevel * 4; j++) {
                            correctedJson << ' ';
                        }
                    }
                    break;
                case '}':
                case ']':
                    correctedJson << '\n';
                    indentLevel--;
                    for (int j = 0; j < indentLevel * 4; j++) {
                        correctedJson << ' ';
                    }
                    correctedJson << c;

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
                    break;
                default:
                    correctedJson << c;
                    break;
            }
        }

        std::string correctedContent = correctedJson.str();

        std::vector<std::string> finalLines;
        std::stringstream finalSS(correctedContent);
        std::string finalLine;

        while (std::getline(finalSS, finalLine)) {
            while (!finalLine.empty() && finalLine.back() == ' ') {
                finalLine.pop_back();
            }
            finalLines.push_back(finalLine);
        }

        std::ostringstream finalJson;
        for (size_t i = 0; i < finalLines.size(); i++) {
            finalJson << finalLines[i];
            if (i < finalLines.size() - 1) {
                finalJson << '\n';
            }
        }

        std::string finalContent = finalJson.str();

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

        if (!PerformTripleValidation(tempPath, fs::path(), logFile)) {
            logFile << "ERROR: Corrected JSON failed integrity check!" << std::endl;
            MoveCorruptedJsonToAnalysis(tempPath, analysisDir, logFile);
            try {
                fs::remove(tempPath);
            } catch (...) {
            }
            return false;
        }

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

// ===== CONSERVATIVE JSON PARSER WITH 4-SPACE FORMAT AND PLAYBACK =====

std::string RebuildJsonFromScratch(const std::map<std::string, OrderedPluginData>& processedData,
                                   std::ofstream& logFile) {
    try {
        logFile << "REBUILDING JSON FROM SCRATCH using INI data..." << std::endl;
        
        std::ostringstream json;
        json << "{\n";

        bool firstKey = true;
        for (const auto& mainKey : ORDERED_JSON_KEYS) {
            if (!firstKey) {
                json << ",\n";
            }
            firstKey = false;

            json << "    \"" << mainKey << "\": ";

            auto it = processedData.find(mainKey);
            if (it != processedData.end() && !it->second.orderedData.empty()) {
                json << "{\n";
                bool firstEntry = true;
                
                for (const auto& [animationKey, soundsWithPlayback] : it->second.orderedData) {
                    if (!firstEntry) {
                        json << ",\n";
                    }
                    firstEntry = false;

                    json << "        \"" << EscapeJson(animationKey) << "\": [\n";

                    bool firstSound = true;
                    for (const auto& swp : soundsWithPlayback) {
                        if (!firstSound) {
                            json << ",\n";
                        }
                        firstSound = false;

                        json << "            [\n";
                        json << "                \"" << EscapeJson(swp.soundFile) << "\",\n";
                        json << "                \"" << swp.listIndex << "\",\n";
                        json << "                \"" << swp.playback << "\"\n";
                        json << "            ]";
                    }

                    json << "\n        ]";
                }
                json << "\n    }";
            } else {
                json << "{}";
            }
        }

        json << "\n}";

        std::string result = json.str();
        logFile << "SUCCESS: JSON rebuilt from scratch with guaranteed order" << std::endl;
        logFile << "Order: SoundKey -> SoundEffectKey -> SoundPositionKey -> SoundTAGKey -> SoundMenuKey" << std::endl;
        
        return result;
        
    } catch (const std::exception& e) {
        logFile << "ERROR in RebuildJsonFromScratch: " << e.what() << std::endl;
        return "{\n    \"SoundKey\": {},\n    \"SoundEffectKey\": {},\n    \"SoundPositionKey\": {},\n    \"SoundTAGKey\": {}\n}";
    } catch (...) {
        logFile << "ERROR in RebuildJsonFromScratch: Unknown exception" << std::endl;
        return "{\n    \"SoundKey\": {},\n    \"SoundEffectKey\": {},\n    \"SoundPositionKey\": {},\n    \"SoundTAGKey\": {}\n}";
    }
}

std::string PreserveOriginalSections(const std::string& originalJson,
                                     const std::map<std::string, OrderedPluginData>& processedData,
                                     std::ofstream& logFile) {
    try {
        std::ostringstream finalJson;
        finalJson << "{\n";

        bool firstKey = true;
        for (const auto& mainKey : ORDERED_JSON_KEYS) {
            if (!firstKey) {
                finalJson << ",\n";
            }
            firstKey = false;

            finalJson << "    \"" << mainKey << "\": ";

            auto it = processedData.find(mainKey);
            if (it != processedData.end() && !it->second.orderedData.empty()) {
                finalJson << "{\n";
                bool firstEntry = true;
                for (const auto& [animationKey, soundsWithPlayback] : it->second.orderedData) {
                    if (!firstEntry) {
                        finalJson << ",\n";
                    }
                    firstEntry = false;

                    finalJson << "        \"" << EscapeJson(animationKey) << "\": [\n";

                    bool firstSound = true;
                    for (const auto& swp : soundsWithPlayback) {
                        if (!firstSound) {
                            finalJson << ",\n";
                        }
                        firstSound = false;

                        finalJson << "            [\n";
                        finalJson << "                \"" << EscapeJson(swp.soundFile) << "\",\n";
                        finalJson << "                \"" << swp.listIndex << "\",\n";
                        finalJson << "                \"" << swp.playback << "\"\n";
                        finalJson << "            ]";
                    }

                    finalJson << "\n        ]";
                }
                finalJson << "\n    }";
            } else {
                finalJson << "{}";
            }
        }

        finalJson << "\n}";

        std::string result = finalJson.str();
        logFile << "INFO: JSON structure rebuilt with guaranteed key order: SoundKey -> SoundEffectKey -> SoundPositionKey -> SoundTAGKey -> SoundMenuKey" << std::endl;

        return result;
    } catch (const std::exception& e) {
        logFile << "ERROR in PreserveOriginalSections: " << e.what() << std::endl;
        return originalJson;
    } catch (...) {
        logFile << "ERROR in PreserveOriginalSections: Unknown exception" << std::endl;
        return originalJson;
    }
}

std::string PreserveOriginalSectionsOLD(const std::string& originalJson,
                                     const std::map<std::string, OrderedPluginData>& processedData,
                                     std::ofstream& logFile) {
    try {
        const std::set<std::string> validKeys = {"SoundKey", "SoundEffectKey", "SoundPositionKey", "SoundTAGKey", "SoundMenuKey"};
        std::string result = originalJson;

        for (const auto& [key, data] : processedData) {
            if (validKeys.count(key) && !data.orderedData.empty()) {
                std::string keyPattern = "\"" + key + "\"";
                size_t keyPos = result.find(keyPattern);

                if (keyPos != std::string::npos) {
                    size_t colonPos = result.find(":", keyPos);
                    if (colonPos != std::string::npos) {
                        size_t valueStart = colonPos + 1;

                        while (valueStart < result.length() && std::isspace(result[valueStart])) {
                            valueStart++;
                        }

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

                            std::ostringstream newValue;
                            newValue << "{\n";
                            bool first = true;

                            for (const auto& [animationKey, soundsWithPlayback] : data.orderedData) {
                                if (!first) newValue << ",\n";
                                first = false;

                                newValue << "        \"" << EscapeJson(animationKey) << "\": [\n";

                                bool firstEntry = true;
                                for (const auto& swp : soundsWithPlayback) {
                                    if (!firstEntry) newValue << ",\n";
                                    firstEntry = false;

                                    newValue << "            [\n";
                                    newValue << "                \"" << EscapeJson(swp.soundFile) << "\",\n";
                                    newValue << "                \"" << swp.listIndex << "\",\n";
                                    newValue << "                \"" << swp.playback << "\"\n";
                                    newValue << "            ]";
                                }

                                newValue << "\n        ]";
                            }

                            newValue << "\n    }";

                            result.replace(valueStart, valueEnd - valueStart, newValue.str());
                            logFile << "INFO: Successfully updated key '" << key
                                    << "' with proper 4-space indentation and playback data" << std::endl;
                        }
                    }
                } else {
                    if (data.orderedData.empty()) {
                        continue;
                    }
                    // Key does NOT exist in JSON - INSERT it in correct order before final closing brace
                    const std::vector<std::string> keyOrder = {"SoundKey", "SoundEffectKey", "SoundPositionKey", "SoundTAGKey", "SoundMenuKey"};
                    size_t insertPos = std::string::npos;

                    for (size_t i = 0; i < keyOrder.size(); i++) {
                        if (key == keyOrder[i]) {
                            // Find position to insert this key
                            bool foundPreviousKey = false;
                            for (int j = static_cast<int>(i) - 1; j >= 0; j--) {
                                std::string prevKeyPattern = "\"" + keyOrder[j] + "\"";
                                if (result.find(prevKeyPattern) != std::string::npos) {
                                    // Previous key exists, insert after it
                                    size_t prevKeyPos = result.find(prevKeyPattern);
                                    size_t prevColonPos = result.find(":", prevKeyPos);
                                    size_t prevValueStart = prevColonPos + 1;
                                    while (prevValueStart < result.length() && std::isspace(static_cast<unsigned char>(result[prevValueStart]))) {
                                        prevValueStart++;
                                    }

                                    if (prevValueStart < result.length() && result[prevValueStart] == '{') {
                                        int braceCount = 1;
                                        size_t prevValueEnd = prevValueStart + 1;
                                        bool inString = false;
                                        bool escape = false;

                                        while (prevValueEnd < result.length() && braceCount > 0) {
                                            char c = result[prevValueEnd];
                                            if (c == '"' && !escape) {
                                                inString = !inString;
                                            } else if (!inString) {
                                                if (c == '{')
                                                    braceCount++;
                                                else if (c == '}')
                                                    braceCount--;
                                            }
                                            escape = (c == '\\' && !escape);
                                            prevValueEnd++;
                                        }

                                        insertPos = prevValueEnd;
                                        while (insertPos < result.length() && std::isspace(static_cast<unsigned char>(result[insertPos]))) {
                                            insertPos++;
                                        }
                                    }
                                    foundPreviousKey = true;
                                    break;
                                }
                            }

                            if (!foundPreviousKey) {
                                // No previous key found, insert at beginning after first opening brace
                                size_t firstBrace = result.find('{');
                                if (firstBrace != std::string::npos) {
                                    insertPos = firstBrace + 1;
                                }
                            }

                            break;
                        }
                    }

                    if (insertPos == std::string::npos || insertPos >= result.length()) {
                        // Fallback: insert before final closing brace
                        insertPos = result.rfind('}');
                        while (insertPos > 0 && std::isspace(static_cast<unsigned char>(result[insertPos - 1]))) {
                            insertPos--;
                        }
                    }

                    // Check if comma is needed
                    bool needsComma = false;
                    for (size_t i = insertPos; i > 0 && !needsComma; i--) {
                        char c = result[i - 1];
                        if (c == ':' || c == '{' || c == '}') {
                            if (c == '}' && insertPos < result.length() && result[insertPos] != '}') {
                                needsComma = true;
                            }
                            break;
                        } else if (!std::isspace(static_cast<unsigned char>(c))) {
                            needsComma = true;
                            break;
                        }
                    }

                    std::ostringstream newSection;
                    newSection << "\n    \"" << key << "\": {\n";

                    bool first = true;
                    for (const auto& [animationKey, soundsWithPlayback] : data.orderedData) {
                        if (!first) newSection << ",\n";
                        first = false;

                        newSection << "        \"" << EscapeJson(animationKey) << "\": [\n";

                        bool firstEntry = true;
                        for (const auto& swp : soundsWithPlayback) {
                            if (!firstEntry) newSection << ",\n";
                            firstEntry = false;

                            newSection << "            [\n";
                            newSection << "                \"" << EscapeJson(swp.soundFile) << "\",\n";
                            newSection << "                \"" << swp.listIndex << "\",\n";
                            newSection << "                \"" << swp.playback << "\"\n";
                            newSection << "            ]";
                        }

                        newSection << "\n        ]";
                    }

                    newSection << "\n    }";
                    if (needsComma) {
                        newSection << ",";
                    }

                    result.insert(insertPos, newSection.str());
                    logFile << "INFO: Inserted NEW key '" << key << "' into JSON structure with alphabetical order" << std::endl;
                }

            }
        }


        return result;
    } catch (const std::exception& e) {
        logFile << "ERROR in PreserveOriginalSections: " << e.what() << std::endl;
        return originalJson;
    } catch (...) {
        logFile << "ERROR in PreserveOriginalSections: Unknown exception" << std::endl;
        return originalJson;
    }
}

// ===== CHECK IF CHANGES ARE NEEDED WITH PLAYBACK =====

bool CheckIfChangesNeeded(const std::string& originalJson,
                          const std::map<std::string, OrderedPluginData>& processedData) {
    const std::vector<std::string> validKeys = {"SoundKey", "SoundEffectKey", "SoundPositionKey", "SoundTAGKey", "SoundMenuKey"};

    for (const auto& key : validKeys) {
        auto it = processedData.find(key);
        if (it != processedData.end() && !it->second.orderedData.empty()) {
            std::string keyPattern = "\"" + key + "\"";
            size_t keyPos = originalJson.find(keyPattern);

            if (keyPos != std::string::npos) {
                size_t colonPos = originalJson.find(":", keyPos);
                if (colonPos != std::string::npos) {
                    size_t valueStart = colonPos + 1;

                    while (valueStart < originalJson.length() && std::isspace(originalJson[valueStart])) {
                        valueStart++;
                    }

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

                        std::string currentValue = originalJson.substr(valueStart, valueEnd - valueStart);

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
                                expectedValue << "            [\n";
                                expectedValue << "                \"" << swp.soundFile << "\",\n";
                                expectedValue << "                \"" << swp.listIndex << "\",\n";
                                expectedValue << "                \"" << swp.playback << "\"\n";
                                expectedValue << "            ]";
                            }
                            expectedValue << "\n        ]";
                        }
                        expectedValue << "\n    }";

                        std::string cleanCurrent = currentValue;
                        std::string cleanExpected = expectedValue.str();

                        auto cleanWhitespace = [](std::string& str) {
                            str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());
                        };

                        cleanWhitespace(cleanCurrent);
                        cleanWhitespace(cleanExpected);

                        if (cleanCurrent != cleanExpected) {
                            return true;
                        }
                    }
                } else {
                    return true;
                }
            } else {
                // Key has data but does NOT exist in JSON - change needed to insert it
                return true;
            }
        }
    }


    return false;
}

// ===== PARSE EXISTING DATA FROM JSON WITH PLAYBACK =====

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
            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos >= len) break;

            if (str[pos] != '"') {
                ++pos;
                continue;
            }

            size_t keyStart = pos + 1;
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

            std::string animationKey = content.substr(keyStart, pos - keyStart);
            ++pos;

            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos >= len || str[pos] != ':') {
                ++pos;
                continue;
            }

            ++pos;
            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos >= len || str[pos] != '[') {
                ++pos;
                continue;
            }

            ++pos;

            std::vector<SoundWithPlayback> sounds;
            sounds.reserve(50);
            size_t soundIter = 0;

            while (pos < len && soundIter++ < maxIters) {
                while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                if (pos >= len) break;

                if (str[pos] == ']') {
                    ++pos;
                    break;
                }

                if (str[pos] == '[') {
                    ++pos;
                    while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;

                    std::string soundFile = "";
                    std::string listIndex = "list-1";
                    std::string playback = "0";

                    if (pos < len && str[pos] == '"') {
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

                        if (pos < len) {
                            soundFile = content.substr(soundStart, pos - soundStart);
                            ++pos;
                        }
                    }

                    while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                    if (pos < len && str[pos] == ',') {
                        ++pos;
                        while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                    }

                    if (pos < len && str[pos] == '"') {
                        size_t listStart = pos + 1;
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
                            listIndex = content.substr(listStart, pos - listStart);
                            ++pos;
                        }
                    }

                    while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                    if (pos < len && str[pos] == ',') {
                        ++pos;
                        while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                    }

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
                            ++pos;
                        }
                    }

                    while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                    if (pos < len && str[pos] == ']') {
                        ++pos;
                    }

                    if (!soundFile.empty()) {
                        int extractedPista = 0;
                        size_t listPos = listIndex.find("list");
                        if (listPos != std::string::npos) {
                            size_t dashPos = listIndex.find('-', listPos);
                            if (dashPos != std::string::npos && dashPos > listPos + 4) {
                                std::string pistaStr = listIndex.substr(listPos + 4, dashPos - listPos - 4);
                                try {
                                    extractedPista = std::stoi(pistaStr);
                                } catch (...) {
                                    extractedPista = 0;
                                }
                            }
                        }
                        sounds.emplace_back(soundFile, listIndex, playback, extractedPista);
                    }

                    while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                    if (pos < len && str[pos] == ',') {
                        ++pos;
                        while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                    }

                    continue;
                }

                if (str[pos] == ',') {
                    ++pos;
                    continue;
                }

                ++pos;
            }

            while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos < len && str[pos] == ',') ++pos;

            if (!animationKey.empty()) {
                result.emplace_back(std::move(animationKey), std::move(sounds));
            }
        }
    } catch (...) {
    }

    return result;
}

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

        const size_t maxFileSize = 50 * 1024 * 1024;
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

// ===== ULTRA-SAFE ATOMIC WRITE =====

bool WriteJsonAtomically(const fs::path& jsonPath, const std::string& content, const fs::path& analysisDir,
                         std::ofstream& logFile) {
    try {
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

        if (!PerformTripleValidation(tempPath, fs::path(), logFile)) {
            logFile << "ERROR: Temporary JSON file failed integrity check!" << std::endl;
            MoveCorruptedJsonToAnalysis(tempPath, analysisDir, logFile);
            try {
                fs::remove(tempPath);
            } catch (...) {
            }
            return false;
        }

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

        if (PerformTripleValidation(jsonPath, fs::path(), logFile)) {
            logFile << "SUCCESS: JSON file written atomically and verified!" << std::endl;
            return true;
        } else {
            logFile << "ERROR: Final JSON file failed integrity check!" << std::endl;
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

bool UpdateIniRuleCount(const fs::path& iniPath, const std::string& originalLine, int newCount,
                        std::ofstream& logFile) {
    return true;
}

// ===== MODIFIED MAIN FUNCTION WITH IMPROVED DETECTION FOR WABBAJACK/MO2 =====

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    try {
        SKSE::Init(skse);

        SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
            try {
                if (message->type == SKSE::MessagingInterface::kDataLoaded) {
                    std::string documentsPath;
                    std::string gamePath;

                    try {
                        documentsPath = GetDocumentsPath();
                        gamePath = GetGamePath();
                    } catch (...) {
                        RE::ConsoleLog::GetSingleton()->Print(
                            "OSoundtracks Assistant: Error getting paths - using defaults");
                        documentsPath = "C:\\Users\\Default\\Documents";
                        gamePath = "";
                    }

                    fs::path logFilePath = fs::path(documentsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" /
                                           "OSoundtracks_SA_Expansion_Sounds_NG.log";
                    CreateDirectoryIfNotExists(logFilePath.parent_path());

                    std::ofstream logFile(logFilePath, std::ios::out | std::ios::trunc);

                    auto now = std::chrono::system_clock::now();
                    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
                    std::tm buf;
                    localtime_s(&buf, &in_time_t);

                    logFile << "====================================================" << std::endl;
                    logFile << "OSoundtracks SA Expansion Sounds NG v" << PLUGIN_VERSION << std::endl;
                    logFile << "REPLACEMENT MODE WITH CLEANUP" << std::endl;
                    logFile << "ENHANCED PATH DETECTION - Wabbajack/MO2 Compatible" << std::endl;
                    logFile << "PISTA SUPPORT - Multi-layer sound system enabled" << std::endl;
                    logFile << "Log created on: " << std::put_time(&buf, "%Y-%m-%d %H:%M:%S") << std::endl;
                    logFile << "====================================================" << std::endl << std::endl;

                    const std::string jsonFilename = "OSoundtracks-SA-Expansion-Sounds-NG.json";
                    fs::path jsonOutputPath;
                    fs::path iniSearchPath;
                    fs::path sksePluginsPath;
                    bool pathDetectionSuccessful = false;

                    logFile << "Searching for game installation with ENHANCED MULTI-ENVIRONMENT detection..." << std::endl;
                    logFile << "----------------------------------------------------" << std::endl;

                    std::string mo2OverwritePath = GetEnvVar("MO_OVERWRITE_PATH");

                    if (!mo2OverwritePath.empty()) {
                        fs::path mo2Path = fs::path(mo2OverwritePath) / "SKSE" / "Plugins";
                        logFile << "METHOD 1: Trying MO2 Overwrite path: " << mo2Path.string() << std::endl;

                        if (fs::exists(mo2Path) && IsValidPluginPath(mo2Path, logFile)) {
                            sksePluginsPath = mo2Path;
                            iniSearchPath = fs::path(mo2OverwritePath);
                            
                            fs::path tempJsonPath;
                            if (FindFileWithFallback(mo2Path, jsonFilename, tempJsonPath, logFile)) {
                                jsonOutputPath = tempJsonPath;
                                pathDetectionSuccessful = true;
                                logFile << "SUCCESS: Valid installation in MO2 Overwrite" << std::endl;
                            }
                        } else {
                            logFile << "MO2 path exists but DLL validation failed" << std::endl;
                        }
                    }

                    if (!pathDetectionSuccessful) {
                        std::string gamePathEnhanced = GetGamePathEnhanced(logFile);
                        
                        if (!gamePathEnhanced.empty()) {
                            fs::path standardPath = BuildPathCaseInsensitive(
                                fs::path(gamePathEnhanced),
                                {"Data", "SKSE", "Plugins"},
                                logFile
                            );
                            
                            logFile << "METHOD 2: Trying standard game path: " << standardPath.string() << std::endl;

                            if (fs::exists(standardPath) && IsValidPluginPath(standardPath, logFile)) {
                                sksePluginsPath = standardPath;
                                iniSearchPath = BuildPathCaseInsensitive(
                                    fs::path(gamePathEnhanced),
                                    {"Data"},
                                    logFile
                                );
                                
                                fs::path tempJsonPath;
                                if (FindFileWithFallback(standardPath, jsonFilename, tempJsonPath, logFile)) {
                                    jsonOutputPath = tempJsonPath;
                                    pathDetectionSuccessful = true;
                                    logFile << "SUCCESS: Valid installation at standard game path" << std::endl;
                                }
                            } else {
                                logFile << "Standard path exists but DLL validation failed" << std::endl;
                            }
                        } else {
                            logFile << "No game path detected from registry or common locations" << std::endl;
                        }
                    }

                    if (!pathDetectionSuccessful) {
                        logFile << std::endl;
                        logFile << "METHOD 3: DLL Directory Detection (Wabbajack/MO2 Portable/Nolvus fallback)" << std::endl;
                        logFile << "This method works for: Wabbajack modpacks, portable installs, network drives" << std::endl;
                        
                        fs::path dllDir = GetDllDirectory(logFile);
                        
                        if (!dllDir.empty()) {
                            fs::path calculatedGamePath = dllDir.parent_path().parent_path().parent_path();
                            
                            logFile << "DLL directory detected: " << dllDir.string() << std::endl;
                            logFile << "Calculated game root: " << calculatedGamePath.string() << std::endl;
                            
                            sksePluginsPath = dllDir;
                            iniSearchPath = BuildPathCaseInsensitive(
                                calculatedGamePath,
                                {"Data"},
                                logFile
                            );
                            
                            if (IsValidPluginPath(sksePluginsPath, logFile)) {
                                fs::path tempJsonPath;
                                if (FindFileWithFallback(sksePluginsPath, jsonFilename, tempJsonPath, logFile)) {
                                    jsonOutputPath = tempJsonPath;
                                    pathDetectionSuccessful = true;
                                    logFile << "SUCCESS: DLL directory method successful (Wabbajack/Portable detected)" << std::endl;
                                } else {
                                    logFile << "DLL path valid but JSON not found" << std::endl;
                                }
                            } else {
                                logFile << "DLL directory validation failed" << std::endl;
                            }
                        } else {
                            logFile << "Could not determine DLL directory" << std::endl;
                        }
                    }

                    if (!pathDetectionSuccessful) {
                        logFile << std::endl;
                        logFile << "=====================================================" << std::endl;
                        logFile << "  CRITICAL ERROR: NO VALID PATH DETECTED!" << std::endl;
                        logFile << "=====================================================" << std::endl;
                        logFile << std::endl;
                        logFile << "All detection methods failed:" << std::endl;
                        logFile << "  METHOD 1 (MO2 Variables): FAILED" << std::endl;
                        logFile << "  METHOD 2 (Registry/Standard): FAILED" << std::endl;
                        logFile << "  METHOD 3 (DLL Directory - Wabbajack fallback): FAILED" << std::endl;
                        logFile << std::endl;
                        logFile << "POSSIBLE SOLUTIONS:" << std::endl;
                        logFile << "1. Reinstall OSoundtracks and this expansion" << std::endl;
                        logFile << "2. Run SKSE through Mod Organizer 2 if using MO2" << std::endl;
                        logFile << "3. For Wabbajack/Nolvus: Ensure the DLL is in Data/SKSE/Plugins/" << std::endl;
                        logFile << "4. Check mod installation in your mod manager" << std::endl;
                        logFile << "5. Verify Skyrim SE is properly installed" << std::endl;
                        logFile << "====================================================" << std::endl;
                        logFile.close();

                        RE::ConsoleLog::GetSingleton()->Print(
                            "CRITICAL: OSoundtracks path detection FAILED! Check log file.");
                        return;
                    }

                    logFile << std::endl;
                    logFile << "SUCCESS: Paths detected successfully" << std::endl;
                    logFile << "JSON file: " << jsonOutputPath.string() << std::endl;
                    logFile << "INI search path: " << iniSearchPath.string() << std::endl;
                    logFile << "SKSE Plugins path: " << sksePluginsPath.string() << std::endl;
                    logFile << std::endl;

                    fs::path backupConfigIniPath = sksePluginsPath / "OSoundtracks-SA-Expansion-Sounds-NG.ini";
                    fs::path backupJsonPath = sksePluginsPath / "Backup_OSoundtracks" / "OSoundtracks-SA-Expansion-Sounds-NG.json";
                    fs::path analysisDir = sksePluginsPath / "Backup_OSoundtracks" / "Analysis";

                    logFile << "Checking backup configuration..." << std::endl;
                    logFile << "----------------------------------------------------" << std::endl;

                    int backupValue = ReadBackupConfigFromIni(backupConfigIniPath, logFile);

                    logFile << std::endl;
                    if (!PerformSimpleJsonIntegrityCheck(jsonOutputPath, logFile)) {
                        logFile << std::endl;
                        logFile << "CRITICAL: JSON failed simple integrity check at startup! Attempting to restore "
                                   "from backup..."
                                << std::endl;

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
                            return;
                        }
                    }

                    logFile << "JSON passed initial integrity check or was restored - proceeding with normal process..."
                            << std::endl;
                    logFile << "Mode: REPLACEMENT (new sounds replace existing ones)" << std::endl;
                    logFile << "Cleanup: AnimationKeys not in INI files will be removed" << std::endl;
                    logFile << std::endl;

                    const std::set<std::string> validKeys = {"SoundKey", "SoundEffectKey", "SoundPositionKey", "SoundTAGKey", "SoundMenuKey"};
                    std::map<std::string, OrderedPluginData> processedData;

                    for (const auto& key : validKeys) {
                        processedData[key] = OrderedPluginData();
                    }

                    std::set<std::string> allProcessedAnimationKeys;
                    bool backupPerformed = false;

                    if (backupValue == 1 || backupValue == 2) {
                        if (backupValue == 2) {
                            logFile << "Backup enabled (Backup = true), performing LITERAL backup always..."
                                    << std::endl;
                        } else {
                            logFile << "Backup enabled (Backup = 1), performing LITERAL backup..." << std::endl;
                        }

                        if (PerformLiteralJsonBackup(jsonOutputPath, backupJsonPath, logFile)) {
                            backupPerformed = true;
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
                            logFile << "Process truncated due to JSON read error. No INI processing or updates "
                                       "performed."
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
                    int totalRulesReplaced = 0;
                    int totalRulesSkipped = 0;
                    int totalRulesAccumulated = 0;
                    int totalFilesProcessed = 0;

                    logFile << "Scanning for OSoundtracks_*.ini files..." << std::endl;
                    logFile << "----------------------------------------------------" << std::endl;

                    std::vector<fs::path> iniSearchPaths;

                    if (!iniSearchPath.empty()) {
                        iniSearchPaths.push_back(iniSearchPath);
                        logFile << "INI search path: " << iniSearchPath.string() << std::endl;
                    }

                    if (!mo2OverwritePath.empty()) {
                        fs::path mo2Path = fs::path(mo2OverwritePath);
                        if (fs::exists(mo2Path) && std::find(iniSearchPaths.begin(), iniSearchPaths.end(), mo2Path) == iniSearchPaths.end()) {
                            iniSearchPaths.push_back(mo2Path);
                            logFile << "Additional search: MO2 Overwrite" << std::endl;
                        }
                    }

                    logFile << std::endl;

                    try {
                        for (const auto& searchPath : iniSearchPaths) {
                            if (!fs::exists(searchPath)) {
                                logFile << "Path does not exist, skipping: " << searchPath.string() << std::endl;
                                continue;
                            }
                            
                            logFile << "Scanning: " << searchPath.string() << std::endl;
                            
                            try {
                                for (const auto& entry : fs::directory_iterator(searchPath)) {
                                    
                                    if (!entry.is_regular_file()) continue;
                                    
                                    try {
                                        std::wstring wPath = entry.path().wstring();
                                        std::wstring wFilename = entry.path().filename().wstring();
                                        
                                        std::string fullPath = SafeWideStringToString(wPath);
                                        std::string filename = SafeWideStringToString(wFilename);
                                        
                                        if (filename.empty() || fullPath.empty()) {
                                            continue;
                                        }
                                        
                                        bool matchesPattern = false;
                                        
                                        if (filename.starts_with("OSoundtracks_") && filename.ends_with(".ini")) {
                                            matchesPattern = true;
                                        }
                                        
                                        if (!matchesPattern && wFilename.starts_with(L"OSoundtracks_") && wFilename.ends_with(L".ini")) {
                                            matchesPattern = true;
                                        }
                                        
                                        if (matchesPattern) {
                                            
                                            logFile << std::endl << "Processing file: " << filename << std::endl;
                                            logFile << "Full path: " << fullPath << std::endl;
                                            totalFilesProcessed++;

                                            std::ifstream iniFile(entry.path());
                                            if (!iniFile.is_open()) {
                                                logFile << "  ERROR: Could not open file!" << std::endl;
                                                continue;
                                            }

                                            std::string line;
                                            int rulesInFile = 0;
                                            int rulesAppliedInFile = 0;
                                            int rulesReplacedInFile = 0;
                                            int rulesSkippedInFile = 0;

                                            while (std::getline(iniFile, line)) {
                                                std::string originalLine = line;

                                                size_t commentPos = line.find(';');
                                                if (commentPos != std::string::npos) {
                                                    line = line.substr(0, commentPos);
                                                }

                                                commentPos = line.find('#');
                                                if (commentPos != std::string::npos) {
                                                    line = line.substr(0, commentPos);
                                                }

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
                                                                rule.animationKey, rule.soundFile, rule.playback, rule.pista);

                                                            switch (result) {
                                                                case SetPresetResult::Added:
                                                                    rulesAppliedInFile++;
                                                                    totalRulesApplied++;
                                                                    {
                                                                        std::string listPrefix = (rule.pista == 0) ? "list" : "list" + std::to_string(rule.pista);
                                                                        logFile << "  Added: " << key
                                                                                << " -> AnimationKey: " << rule.animationKey
                                                                                << " -> Sound: " << rule.soundFile
                                                                                << " -> List: " << listPrefix << "-1"
                                                                                << " -> Playback: " << rule.playback
                                                                                << " -> Pista: " << rule.pista << std::endl;
                                                                    }
                                                                    break;
                                                                case SetPresetResult::Accumulated:
                                                                    {
                                                                        totalRulesAccumulated++;
                                                                        auto& data = processedData[key];
                                                                        auto it = std::find_if(data.orderedData.begin(), data.orderedData.end(),
                                                                            [&rule](const auto& pair) { return pair.first == rule.animationKey; });
                                                                        int listNum = 1;
                                                                        if (it != data.orderedData.end()) {
                                                                            for (const auto& sound : it->second) {
                                                                                if (sound.pista == rule.pista) {
                                                                                    listNum++;
                                                                                }
                                                                            }
                                                                        }
                                                                        std::string listPrefix = (rule.pista == 0) ? "list" : "list" + std::to_string(rule.pista);
                                                                        logFile << "  Accumulated: " << key
                                                                                << " -> AnimationKey: " << rule.animationKey
                                                                                << " -> Sound: " << rule.soundFile
                                                                                << " -> List: " << listPrefix << "-" << listNum
                                                                                << " -> Playback: " << rule.playback
                                                                                << " -> Pista: " << rule.pista << std::endl;
                                                                        break;
                                                                    }
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

                                                            allProcessedAnimationKeys.insert(rule.animationKey);

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
                                    } catch (...) {
                                        continue;
                                    }
                                }
                            } catch (const std::exception& e) {
                                logFile << "ERROR scanning path " << searchPath.string() << ": " << e.what() << std::endl;
                            } catch (...) {
                                logFile << "ERROR scanning path (unknown error)" << std::endl;
                            }
                        }
                    } catch (const std::exception& e) {
                        logFile << "CRITICAL ERROR in INI scanning: " << e.what() << std::endl;
                    } catch (...) {
                        logFile << "CRITICAL ERROR in INI scanning: Unknown error" << std::endl;
                    }

                    logFile << std::endl;
                    logFile << "Cleaning up AnimationKeys not present in INI files..." << std::endl;
                    logFile << "----------------------------------------------------" << std::endl;

                    size_t keysBeforeCleanup = 0;
                    for (const auto& [key, data] : processedData) {
                        keysBeforeCleanup += data.getPluginCount();
                    }

                    for (auto& [key, data] : processedData) {
                        data.cleanUnprocessedKeys(allProcessedAnimationKeys);
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
                    logFile << "Total rules accumulated (multi-sound): " << totalRulesAccumulated << std::endl;
                    logFile << "Total rules replaced (updated): " << totalRulesReplaced << std::endl;
                    logFile << "Total rules skipped (no change): " << totalRulesSkipped << std::endl;
                    logFile << "Total AnimationKeys removed (cleanup): " << keysRemoved << std::endl;

                    logFile << std::endl;
                    logFile << "Applying final sorting (Start and OStimAlignMenu first in SoundKey)..." << std::endl;
                    for (auto& [key, data] : processedData) {
                        if (key == "SoundKey") {
                            data.sortOrderedData();
                            logFile << "  SoundKey sorted with priority keys at top" << std::endl;
                        }
                    }

                    logFile << std::endl << "Final data in JSON:" << std::endl;
                    for (const auto& [key, data] : processedData) {
                        size_t count = data.getTotalPresetCount();
                        if (count > 0) {
                            logFile << "  " << key << ": " << data.getPluginCount() << " animation keys, " << count
                                    << " total sounds with playback settings" << std::endl;
                        }
                    }
                    logFile << "====================================================" << std::endl << std::endl;

                    logFile << "Updating JSON at: " << jsonOutputPath.string() << std::endl;
                    logFile << "Applying proper 4-space indentation format with playback support..." << std::endl;
                    logFile << "Mode: REPLACEMENT (sounds are replaced, not added)" << std::endl;

                    try {
                        std::string updatedJsonContent;
                        
                        bool needsFullRebuild = originalJsonContent.empty() || 
                                                originalJsonContent.length() < 10 ||
                                                originalJsonContent.find("\"SoundKey\"") == std::string::npos;
                        
                        if (needsFullRebuild) {
                            logFile << "JSON requires full rebuild (empty, corrupted, or missing required keys)" << std::endl;
                            updatedJsonContent = RebuildJsonFromScratch(processedData, logFile);
                        } else {
                            updatedJsonContent = PreserveOriginalSections(originalJsonContent, processedData, logFile);
                        }

                        if (CheckIfChangesNeeded(originalJsonContent, processedData) || keysRemoved > 0 || needsFullRebuild) {
                            if (needsFullRebuild) {
                                logFile << "Full rebuild triggered, JSON update required." << std::endl;
                            } else if (keysRemoved > 0) {
                                logFile << "AnimationKeys were removed, JSON update required." << std::endl;
                            }
                            logFile << "Changes from INI rules require updating the master JSON file. Proceeding with atomic write..." << std::endl;

                            if (WriteJsonAtomically(jsonOutputPath, updatedJsonContent, analysisDir, logFile)) {
                                logFile << "SUCCESS: JSON updated successfully with proper 4-space indentation hierarchy and playback support!" << std::endl;

                                logFile << std::endl;
                                if (CorrectJsonIndentation(jsonOutputPath, analysisDir, logFile)) {
                                    logFile << "SUCCESS: JSON indentation verification and correction completed!" << std::endl;
                                } else {
                                    logFile << "WARNING: JSON indentation correction had issues, attempting rebuild..." << std::endl;
                                    
                                    std::string rebuiltJson = RebuildJsonFromScratch(processedData, logFile);
                                    if (WriteJsonAtomically(jsonOutputPath, rebuiltJson, analysisDir, logFile)) {
                                        logFile << "SUCCESS: JSON rebuilt and written successfully!" << std::endl;
                                    } else {
                                        logFile << "ERROR: Rebuild also failed, restoring from backup..." << std::endl;
                                        if (fs::exists(backupJsonPath)) {
                                            RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile);
                                        }
                                    }
                                }
                            } else {
                                logFile << "ERROR: Failed to write JSON safely! Attempting full rebuild..." << std::endl;
                                
                                std::string rebuiltJson = RebuildJsonFromScratch(processedData, logFile);
                                if (WriteJsonAtomically(jsonOutputPath, rebuiltJson, analysisDir, logFile)) {
                                    logFile << "SUCCESS: JSON rebuilt from scratch and written successfully!" << std::endl;
                                } else {
                                    logFile << "ERROR: Rebuild also failed, restoring from backup..." << std::endl;
                                    if (fs::exists(backupJsonPath)) {
                                        RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile);
                                    }
                                }
                            }
                        } else {
                            logFile << "No changes detected between INI rules and master JSON. Skipping redundant atomic write." << std::endl;

                            if (CorrectJsonIndentation(jsonOutputPath, analysisDir, logFile)) {
                                logFile << "JSON indentation is already perfect or has been corrected." << std::endl;
                            } else {
                                logFile << "WARNING: JSON indentation correction had issues, attempting rebuild..." << std::endl;
                                std::string rebuiltJson = RebuildJsonFromScratch(processedData, logFile);
                                if (WriteJsonAtomically(jsonOutputPath, rebuiltJson, analysisDir, logFile)) {
                                    logFile << "SUCCESS: JSON rebuilt and written successfully!" << std::endl;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        logFile << "ERROR in JSON update process: " << e.what() << std::endl;
                        logFile << "Attempting full rebuild from INI data..." << std::endl;
                        
                        std::string rebuiltJson = RebuildJsonFromScratch(processedData, logFile);
                        if (!WriteJsonAtomically(jsonOutputPath, rebuiltJson, analysisDir, logFile)) {
                            logFile << "ERROR: Rebuild failed, restoring from backup..." << std::endl;
                            if (fs::exists(backupJsonPath)) {
                                RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile);
                            }
                        }
                    } catch (...) {
                        logFile << "ERROR in JSON update process: Unknown exception" << std::endl;
                        logFile << "Attempting full rebuild from INI data..." << std::endl;
                        
                        std::string rebuiltJson = RebuildJsonFromScratch(processedData, logFile);
                        if (!WriteJsonAtomically(jsonOutputPath, rebuiltJson, analysisDir, logFile)) {
                            logFile << "ERROR: Rebuild failed, restoring from backup..." << std::endl;
                            if (fs::exists(backupJsonPath)) {
                                RestoreJsonFromBackup(backupJsonPath, jsonOutputPath, analysisDir, logFile);
                            }
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