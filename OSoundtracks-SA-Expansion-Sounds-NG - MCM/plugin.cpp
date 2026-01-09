#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <shlobj.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <windows.h>
#include <shellapi.h>
#include <knownfolders.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
namespace logger = SKSE::log;

static std::string g_documentsPath;
static std::string g_gamePath;
static bool g_isInitialized = false;
static std::mutex g_logMutex;
static std::atomic<bool> g_isShuttingDown(false);

static bool g_usingDllPath = false;
static fs::path g_dllDirectory;
static fs::path g_osoundtracksIniPath;

void WriteToAdvancedLog(const std::string& message, int lineNumber = 0);
void ShowGameNotification(const std::string& message);
fs::path GetDllDirectory();
std::vector<std::string> GetAuthorsFromJSON();
std::vector<std::string> GetAuthorsFromINI();
bool CompareAuthors(const std::vector<std::string>& jsonAuthors, const std::vector<std::string>& iniAuthors);
void CheckAndRegenerateAuthorsINI();
void GenerateAuthorsINI();

static size_t FindCaseInsensitive(const std::string& haystack, const std::string& needle) {
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) {
            return static_cast<unsigned char>(std::tolower(ch1)) == static_cast<unsigned char>(std::tolower(ch2));
        });
    if (it == haystack.end()) {
        return std::string::npos;
    }
    return static_cast<size_t>(it - haystack.begin());
}

void ShowGameNotification(const std::string& message) {
    RE::DebugNotification(message.c_str());
    WriteToAdvancedLog("IN-GAME MESSAGE SHOWN: " + message, __LINE__);
}

std::string SafeWideStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    
    try {
        int size_needed = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr.c_str(),
            static_cast<int>(wstr.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        
        if (size_needed > 0) {
            std::string result(size_needed, 0);
            int converted = WideCharToMultiByte(
                CP_UTF8,
                0,
                wstr.c_str(),
                static_cast<int>(wstr.size()),
                &result[0],
                size_needed,
                nullptr,
                nullptr
            );
            
            if (converted > 0) {
                return result;
            }
        }
        
        size_needed = WideCharToMultiByte(
            CP_ACP,
            0,
            wstr.c_str(),
            static_cast<int>(wstr.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        
        if (size_needed > 0) {
            std::string result(size_needed, 0);
            int converted = WideCharToMultiByte(
                CP_ACP,
                0,
                wstr.c_str(),
                static_cast<int>(wstr.size()),
                &result[0],
                size_needed,
                nullptr,
                nullptr
            );
            
            if (converted > 0) {
                return result;
            }
        }
        
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

void CreateDirectoryIfNotExists(const fs::path& dirPath) {
    try {
        if (!dirPath.empty() && !fs::exists(dirPath)) {
            fs::create_directories(dirPath);
        }
    } catch (const std::exception& e) {
        logger::error("Error creating directory {}: {}", dirPath.string(), e.what());
    } catch (...) {
        logger::error("Unknown error creating directory: {}", dirPath.string());
    }
}

fs::path GetDllDirectory() {
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
                    logger::info("DLL directory detected: {}", dllDir.string());
                    return dllDir;
                }
            }
        }

        logger::warn("Could not determine DLL directory");
        return fs::path();

    } catch (const std::exception& e) {
        logger::error("ERROR in GetDllDirectory: {}", e.what());
        return fs::path();
    } catch (...) {
        logger::error("ERROR in GetDllDirectory: Unknown exception");
        return fs::path();
    }
}

bool IsValidPluginPath(const fs::path& pluginPath) {
    const std::vector<std::string> dllNames = {
        "OSoundtracks-SA-Expansion-Sounds-NG-MCM.dll"
    };
    
    for (const auto& dllName : dllNames) {
        fs::path dllPath = pluginPath / dllName;
        
        try {
            if (fs::exists(dllPath)) {
                logger::info("DLL validation passed: Found {}", dllName);
                WriteToAdvancedLog("DLL found: " + dllName, __LINE__);
                return true;
            }
        } catch (...) {
            continue;
        }
    }
    
    logger::warn("DLL validation failed: No valid DLL found in path");
    return false;
}

bool FindFileWithFallback(const fs::path& basePath, const std::string& filename, fs::path& foundPath) {
    try {
        fs::path normalPath = basePath / filename;
        if (fs::exists(normalPath)) {
            foundPath = normalPath;
            logger::info("Found file (exact match): {}", foundPath.string());
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
                logger::info("Found file (canonical path): {}", foundPath.string());
                return true;
            }
        } catch (...) {}
        
        if (fs::exists(basePath) && fs::is_directory(basePath)) {
            std::string lowerFilename = filename;
            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
            
            for (const auto& entry : fs::directory_iterator(basePath)) {
                try {
                    std::string entryFilename = entry.path().filename().string();
                    std::string lowerEntryFilename = entryFilename;
                    std::transform(lowerEntryFilename.begin(), lowerEntryFilename.end(), 
                                 lowerEntryFilename.begin(), ::tolower);
                    
                    if (lowerEntryFilename == lowerFilename) {
                        foundPath = entry.path();
                        logger::info("Found file (case-insensitive): {}", foundPath.string());
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

fs::path BuildPathCaseInsensitive(const fs::path& basePath, const std::vector<std::string>& components) {
    try {
        fs::path currentPath = basePath;
        
        for (const auto& component : components) {
            fs::path testPath = currentPath / component;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            std::string lowerComponent = component;
            std::transform(lowerComponent.begin(), lowerComponent.end(), lowerComponent.begin(), ::tolower);
            testPath = currentPath / lowerComponent;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            std::string upperComponent = component;
            std::transform(upperComponent.begin(), upperComponent.end(), upperComponent.begin(), ::toupper);
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

std::string GetCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);
    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string GetCurrentTimeStringWithMillis() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);
    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

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
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Skyrim Special Edition", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buffer[MAX_PATH] = {0};
            DWORD bufferSize = MAX_PATH;
            if (RegQueryValueExA(hKey, "Installed Path", nullptr, nullptr, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return std::string(buffer);
            }
            RegCloseKey(hKey);
        }
        
        std::vector<std::string> commonPaths = {
            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "C:\\Program Files\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "D:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "E:\\Steam\\steamapps\\common\\Skyrim Special Edition"
        };
        
        for (const auto& path : commonPaths) {
            if (fs::exists(fs::path(path) / "SkyrimSE.exe")) {
                return path;
            }
        }
        
        return "";
    } catch (...) {
        return "";
    }
}

struct OSoundtracksLogPaths {
    fs::path primary;
    fs::path secondary;
};

OSoundtracksLogPaths GetAllOSoundtracksLogsPaths() {
    static bool loggedOnce = false;
    OSoundtracksLogPaths paths;
    
    try {
        std::string documentsPath = g_documentsPath;
        if (documentsPath.empty()) {
            documentsPath = "C:\\";
        }

        paths.primary = fs::path(documentsPath) / "My Games" / "Skyrim Special Edition" / "SKSE";
        
        paths.secondary = fs::path(documentsPath) / "My Games" / "Skyrim.INI" / "SKSE";

        try {
            fs::create_directories(paths.primary);
            fs::create_directories(paths.secondary);

            if (!loggedOnce) {
                logger::info("DUAL-PATH SYSTEM INITIALIZED");
                logger::info("PRIMARY path: {}", paths.primary.string());
                logger::info("SECONDARY path: {}", paths.secondary.string());
                loggedOnce = true;
            }
        } catch (const std::exception& e) {
            logger::error("Could not create SKSE logs directories: {}", e.what());
        }

    } catch (const std::exception& e) {
        logger::error("Error in GetAllOSoundtracksLogsPaths: {}", e.what());
    }

    return paths;
}

void WriteToAdvancedLog(const std::string& message, int lineNumber) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto paths = GetAllOSoundtracksLogsPaths();
    
    std::vector<fs::path> logPaths = {
        paths.primary / "OSoundtracks-SA-Expansion-Sounds-NG-MCM.log",
        paths.secondary / "OSoundtracks-SA-Expansion-Sounds-NG-MCM.log"
    };

    for (const auto& logPath : logPaths) {
        try {
            std::ofstream logFile(logPath, std::ios::app);
            if (logFile.is_open()) {
                std::stringstream ss;
                ss << "[" << GetCurrentTimeStringWithMillis() << "] ";
                ss << "[log] [info] ";

                if (lineNumber > 0) {
                    ss << "[plugin.cpp:" << lineNumber << "] ";
                } else {
                    ss << "[plugin.cpp:0] ";
                }

                ss << message;

                logFile << ss.str() << std::endl;
                logFile.close();
            }
        } catch (...) {
        }
    }
}

float GetIniFloat(const std::string& section, const std::string& key, float defaultValue) {
    try {
        if (g_osoundtracksIniPath.empty() || !fs::exists(g_osoundtracksIniPath)) {
            WriteToAdvancedLog("INI file not found, returning default for " + key, __LINE__);
            return defaultValue;
        }

        std::ifstream iniFile(g_osoundtracksIniPath);
        if (!iniFile.is_open()) {
            return defaultValue;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(iniFile, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line[0] == '[' && line[line.length() - 1] == ']') {
                currentSection = line.substr(1, line.length() - 2);
                continue;
            }

            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string lineKey = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                lineKey.erase(0, lineKey.find_first_not_of(" \t"));
                lineKey.erase(lineKey.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                std::string sectionLower = currentSection;
                std::string keyLower = lineKey;
                std::string targetSectionLower = section;
                std::string targetKeyLower = key;

                std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);
                std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                std::transform(targetSectionLower.begin(), targetSectionLower.end(), targetSectionLower.begin(), ::tolower);
                std::transform(targetKeyLower.begin(), targetKeyLower.end(), targetKeyLower.begin(), ::tolower);

                if (sectionLower == targetSectionLower && keyLower == targetKeyLower) {
                    try {
                        float result = std::stof(value);
                        iniFile.close();
                        return result;
                    } catch (...) {
                        iniFile.close();
                        return defaultValue;
                    }
                }
            }
        }

        iniFile.close();
        return defaultValue;

    } catch (const std::exception& e) {
        WriteToAdvancedLog("ERROR reading INI float: " + std::string(e.what()), __LINE__);
        return defaultValue;
    }
}

bool GetIniBool(const std::string& section, const std::string& key, bool defaultValue) {
    try {
        if (g_osoundtracksIniPath.empty() || !fs::exists(g_osoundtracksIniPath)) {
            return defaultValue;
        }

        std::ifstream iniFile(g_osoundtracksIniPath);
        if (!iniFile.is_open()) {
            return defaultValue;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(iniFile, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line[0] == '[' && line[line.length() - 1] == ']') {
                currentSection = line.substr(1, line.length() - 2);
                continue;
            }

            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string lineKey = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                lineKey.erase(0, lineKey.find_first_not_of(" \t"));
                lineKey.erase(lineKey.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                std::string sectionLower = currentSection;
                std::string keyLower = lineKey;
                std::string targetSectionLower = section;
                std::string targetKeyLower = key;

                std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);
                std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                std::transform(targetSectionLower.begin(), targetSectionLower.end(), targetSectionLower.begin(), ::tolower);
                std::transform(targetKeyLower.begin(), targetKeyLower.end(), targetKeyLower.begin(), ::tolower);

                if (sectionLower == targetSectionLower && keyLower == targetKeyLower) {
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    bool result = (value == "true" || value == "1" || value == "yes");
                    iniFile.close();
                    return result;
                }
            }
        }

        iniFile.close();
        return defaultValue;

    } catch (const std::exception& e) {
        WriteToAdvancedLog("ERROR reading INI bool: " + std::string(e.what()), __LINE__);
        return defaultValue;
    }
}

std::string GetIniString(const std::string& section, const std::string& key, const std::string& defaultValue) {
    try {
        if (g_osoundtracksIniPath.empty() || !fs::exists(g_osoundtracksIniPath)) {
            return defaultValue;
        }

        std::ifstream iniFile(g_osoundtracksIniPath);
        if (!iniFile.is_open()) {
            return defaultValue;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(iniFile, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line[0] == '[' && line[line.length() - 1] == ']') {
                currentSection = line.substr(1, line.length() - 2);
                continue;
            }

            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string lineKey = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                lineKey.erase(0, lineKey.find_first_not_of(" \t"));
                lineKey.erase(lineKey.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                std::string sectionLower = currentSection;
                std::string keyLower = lineKey;
                std::string targetSectionLower = section;
                std::string targetKeyLower = key;

                std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);
                std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                std::transform(targetSectionLower.begin(), targetSectionLower.end(), targetSectionLower.begin(), ::tolower);
                std::transform(targetKeyLower.begin(), targetKeyLower.end(), targetKeyLower.begin(), ::tolower);

                if (sectionLower == targetSectionLower && keyLower == targetKeyLower) {
                    iniFile.close();
                    return value;
                }
            }
        }

        iniFile.close();
        return defaultValue;

    } catch (const std::exception& e) {
        WriteToAdvancedLog("ERROR reading INI string: " + std::string(e.what()), __LINE__);
        return defaultValue;
    }
}

bool SetIniValue(const std::string& section, const std::string& key, const std::string& newValue) {
    try {
        if (g_osoundtracksIniPath.empty()) {
            WriteToAdvancedLog("INI path not set, cannot write", __LINE__);
            return false;
        }

        std::vector<std::string> lines;
        bool keyFound = false;
        bool inTargetSection = false;
        std::string targetSectionLower = section;
        std::string targetKeyLower = key;
        std::transform(targetSectionLower.begin(), targetSectionLower.end(), targetSectionLower.begin(), ::tolower);
        std::transform(targetKeyLower.begin(), targetKeyLower.end(), targetKeyLower.begin(), ::tolower);

        if (fs::exists(g_osoundtracksIniPath)) {
            std::ifstream iniFile(g_osoundtracksIniPath);
            if (iniFile.is_open()) {
                std::string line;
                while (std::getline(iniFile, line)) {
                    std::string trimmedLine = line;
                    trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t\r\n"));
                    trimmedLine.erase(trimmedLine.find_last_not_of(" \t\r\n") + 1);

                    if (!trimmedLine.empty() && trimmedLine[0] == '[' && trimmedLine.back() == ']') {
                        std::string currentSection = trimmedLine.substr(1, trimmedLine.length() - 2);
                        std::string currentSectionLower = currentSection;
                        std::transform(currentSectionLower.begin(), currentSectionLower.end(), currentSectionLower.begin(), ::tolower);
                        inTargetSection = (currentSectionLower == targetSectionLower);
                    }

                    if (inTargetSection && !keyFound) {
                        size_t equalPos = trimmedLine.find('=');
                        if (equalPos != std::string::npos) {
                            std::string lineKey = trimmedLine.substr(0, equalPos);
                            lineKey.erase(0, lineKey.find_first_not_of(" \t"));
                            lineKey.erase(lineKey.find_last_not_of(" \t") + 1);

                            std::string lineKeyLower = lineKey;
                            std::transform(lineKeyLower.begin(), lineKeyLower.end(), lineKeyLower.begin(), ::tolower);

                            if (lineKeyLower == targetKeyLower) {
                                line = lineKey + " = " + newValue;
                                keyFound = true;
                            }
                        }
                    }

                    lines.push_back(line);
                }
                iniFile.close();
            }
        }

        std::ofstream outFile(g_osoundtracksIniPath, std::ios::trunc);
        if (!outFile.is_open()) {
            WriteToAdvancedLog("Could not open INI for writing", __LINE__);
            return false;
        }

        for (const auto& l : lines) {
            outFile << l << "\n";
        }
        outFile.close();

        WriteToAdvancedLog("INI updated: [" + section + "] " + key + " = " + newValue, __LINE__);

        CheckAndRegenerateAuthorsINI();

        return true;

    } catch (const std::exception& e) {
        WriteToAdvancedLog("ERROR writing INI: " + std::string(e.what()), __LINE__);
        return false;
    }
}

bool SetIniFloat(const std::string& section, const std::string& key, float value) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << value;
    return SetIniValue(section, key, ss.str());
}

bool SetIniBool(const std::string& section, const std::string& key, bool value) {
    return SetIniValue(section, key, value ? "true" : "false");
}

namespace OSoundtracks_Native {

    float GetBaseVolume(RE::StaticFunctionTag*) {
        return GetIniFloat("Volume Control", "BaseVolume", 0.9f);
    }

    float GetEffectVolume(RE::StaticFunctionTag*) {
        return GetIniFloat("Volume Control", "EffectVolume", 1.2f);
    }

    float GetMenuVolume(RE::StaticFunctionTag*) {
        return GetIniFloat("Volume Control", "MenuVolume", 0.6f);
    }

    float GetSpecificVolume(RE::StaticFunctionTag*) {
        return GetIniFloat("Volume Control", "SpecificVolume", 0.7f);
    }

    float GetPositionVolume(RE::StaticFunctionTag*) {
        return GetIniFloat("Volume Control", "PositionVolume", 1.2f);
    }

    bool GetMasterVolumeEnabled(RE::StaticFunctionTag*) {
        return GetIniBool("Volume Control", "MasterVolumeEnabled", true);
    }

    float GetTAGVolume(RE::StaticFunctionTag*) {
        return GetIniFloat("Volume Control", "TAGVolume", 1.2f);
    }

    float GetSoundMenuKeyVolume(RE::StaticFunctionTag*) {
        return GetIniFloat("Volume Control", "SoundMenuKeyVolume", 0.2f);
    }

    bool GetStartup(RE::StaticFunctionTag*) {
        return GetIniBool("Startup Sound", "Startup", true);
    }

    bool GetVisible(RE::StaticFunctionTag*) {
        return GetIniBool("Top Notifications", "Visible", true);
    }

    bool GetBackup(RE::StaticFunctionTag*) {
        return GetIniBool("Original backup", "Backup", true);
    }

    bool GetMuteGameMusic(RE::StaticFunctionTag*) {
        return GetIniBool("Skyrim Audio", "MuteGameMusicDuringOStim", true);
    }

    std::string GetMuteGameMusicValue(RE::StaticFunctionTag*) {
        return GetIniString("Skyrim Audio", "MuteGameMusicDuringOStim", "MUSCombatBoss");
    }

    std::string GetSoundMenuKeyMode(RE::StaticFunctionTag*) {
        return GetIniString("Menu Sound", "SoundMenuKey", "Author_Order");
    }

    std::string GetAuthor(RE::StaticFunctionTag*) {
        return GetIniString("Menu Sound", "Author", "John95acJazz");
    }

    std::string GetAuthorList(RE::StaticFunctionTag*) {
        fs::path authorsIniPath = g_dllDirectory / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini";
        
        if (!fs::exists(authorsIniPath)) {
            WriteToAdvancedLog("Authors INI not found", __LINE__);
            return "";
        }
        
        std::ifstream authorsFile(authorsIniPath);
        if (!authorsFile.is_open()) {
            WriteToAdvancedLog("Cannot open Authors INI", __LINE__);
            return "";
        }
        
        std::string line;
        while (std::getline(authorsFile, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string key = line.substr(0, equalPos);
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                
                std::string keyLower = key;
                std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                
                if (keyLower == "list") {
                    std::string value = line.substr(equalPos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    authorsFile.close();
                    return value;
                }
            }
        }
        
        authorsFile.close();
        return "";
    }

    void SetBaseVolume(RE::StaticFunctionTag*, float value) {
        SetIniFloat("Volume Control", "BaseVolume", value);
    }

    void SetMenuVolume(RE::StaticFunctionTag*, float value) {
        SetIniFloat("Volume Control", "MenuVolume", value);
    }

    void SetSpecificVolume(RE::StaticFunctionTag*, float value) {
        SetIniFloat("Volume Control", "SpecificVolume", value);
    }

    void SetEffectVolume(RE::StaticFunctionTag*, float value) {
        SetIniFloat("Volume Control", "EffectVolume", value);
    }

    void SetPositionVolume(RE::StaticFunctionTag*, float value) {
        SetIniFloat("Volume Control", "PositionVolume", value);
    }

    void SetTAGVolume(RE::StaticFunctionTag*, float value) {
        SetIniFloat("Volume Control", "TAGVolume", value);
    }

    void SetSoundMenuKeyVolume(RE::StaticFunctionTag*, float value) {
        SetIniFloat("Volume Control", "SoundMenuKeyVolume", value);
    }

    void SetMasterVolumeEnabled(RE::StaticFunctionTag*, bool value) {
        SetIniBool("Volume Control", "MasterVolumeEnabled", value);
    }

    void SetStartup(RE::StaticFunctionTag*, bool value) {
        SetIniBool("Startup Sound", "Startup", value);
    }

    void SetVisible(RE::StaticFunctionTag*, bool value) {
        SetIniBool("Top Notifications", "Visible", value);
    }

    void SetBackup(RE::StaticFunctionTag*, bool value) {
        SetIniBool("Original backup", "Backup", value);
    }

    void SetMuteGameMusic(RE::StaticFunctionTag*, bool value) {
        SetIniBool("Skyrim Audio", "MuteGameMusicDuringOStim", value);
    }

    void SetMuteGameMusicValue(RE::StaticFunctionTag*, std::string value) {
        SetIniValue("Skyrim Audio", "MuteGameMusicDuringOStim", value);
    }

    void SetSoundMenuKeyMode(RE::StaticFunctionTag*, std::string value) {
        SetIniValue("Menu Sound", "SoundMenuKey", value);
    }

    void SetAuthor(RE::StaticFunctionTag*, std::string value) {
        SetIniValue("Menu Sound", "Author", value);
    }

    void ActivateAdvancedMCM(RE::StaticFunctionTag*) {
        logger::info("ActivateAdvancedMCM called - Reserved for future use");
        WriteToAdvancedLog("MCM Button pressed - Reserved for future functionality", __LINE__);
    }

    void ActivateStandaloneMode(RE::StaticFunctionTag*) {
        logger::info("ActivateStandaloneMode called");
        WriteToAdvancedLog("Standalone Mode button pressed - Not yet implemented", __LINE__);
    }

    void OpenURL(RE::StaticFunctionTag*, std::string url) {
        if (url.empty()) {
            WriteToAdvancedLog("OpenURL called with empty URL", __LINE__);
            return;
        }
        WriteToAdvancedLog("Opening URL: " + url, __LINE__);
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("GetBaseVolume", "OSoundtracks_NativeScript", GetBaseVolume);
        vm->RegisterFunction("GetMenuVolume", "OSoundtracks_NativeScript", GetMenuVolume);
        vm->RegisterFunction("GetSpecificVolume", "OSoundtracks_NativeScript", GetSpecificVolume);
        vm->RegisterFunction("GetEffectVolume", "OSoundtracks_NativeScript", GetEffectVolume);
        vm->RegisterFunction("GetPositionVolume", "OSoundtracks_NativeScript", GetPositionVolume);
        vm->RegisterFunction("GetTAGVolume", "OSoundtracks_NativeScript", GetTAGVolume);
        vm->RegisterFunction("GetSoundMenuKeyVolume", "OSoundtracks_NativeScript", GetSoundMenuKeyVolume);
        vm->RegisterFunction("GetMasterVolumeEnabled", "OSoundtracks_NativeScript", GetMasterVolumeEnabled);
        vm->RegisterFunction("GetStartup", "OSoundtracks_NativeScript", GetStartup);
        vm->RegisterFunction("GetVisible", "OSoundtracks_NativeScript", GetVisible);
        vm->RegisterFunction("GetBackup", "OSoundtracks_NativeScript", GetBackup);
        vm->RegisterFunction("GetMuteGameMusic", "OSoundtracks_NativeScript", GetMuteGameMusic);
        vm->RegisterFunction("GetMuteGameMusicValue", "OSoundtracks_NativeScript", GetMuteGameMusicValue);
        vm->RegisterFunction("GetSoundMenuKeyMode", "OSoundtracks_NativeScript", GetSoundMenuKeyMode);
        vm->RegisterFunction("GetAuthor", "OSoundtracks_NativeScript", GetAuthor);
        vm->RegisterFunction("GetAuthorList", "OSoundtracks_NativeScript", GetAuthorList);
        vm->RegisterFunction("SetBaseVolume", "OSoundtracks_NativeScript", SetBaseVolume);
        vm->RegisterFunction("SetMenuVolume", "OSoundtracks_NativeScript", SetMenuVolume);
        vm->RegisterFunction("SetSpecificVolume", "OSoundtracks_NativeScript", SetSpecificVolume);
        vm->RegisterFunction("SetEffectVolume", "OSoundtracks_NativeScript", SetEffectVolume);
        vm->RegisterFunction("SetPositionVolume", "OSoundtracks_NativeScript", SetPositionVolume);
        vm->RegisterFunction("SetTAGVolume", "OSoundtracks_NativeScript", SetTAGVolume);
        vm->RegisterFunction("SetSoundMenuKeyVolume", "OSoundtracks_NativeScript", SetSoundMenuKeyVolume);
        vm->RegisterFunction("SetMasterVolumeEnabled", "OSoundtracks_NativeScript", SetMasterVolumeEnabled);
        vm->RegisterFunction("SetStartup", "OSoundtracks_NativeScript", SetStartup);
        vm->RegisterFunction("SetVisible", "OSoundtracks_NativeScript", SetVisible);
        vm->RegisterFunction("SetBackup", "OSoundtracks_NativeScript", SetBackup);
        vm->RegisterFunction("SetMuteGameMusic", "OSoundtracks_NativeScript", SetMuteGameMusic);
        vm->RegisterFunction("SetMuteGameMusicValue", "OSoundtracks_NativeScript", SetMuteGameMusicValue);
        vm->RegisterFunction("SetSoundMenuKeyMode", "OSoundtracks_NativeScript", SetSoundMenuKeyMode);
        vm->RegisterFunction("SetAuthor", "OSoundtracks_NativeScript", SetAuthor);
        vm->RegisterFunction("ActivateAdvancedMCM", "OSoundtracks_NativeScript", ActivateAdvancedMCM);
        vm->RegisterFunction("ActivateStandaloneMode", "OSoundtracks_NativeScript", ActivateStandaloneMode);
        vm->RegisterFunction("OpenURL", "OSoundtracks_NativeScript", OpenURL);
        logger::info("OSoundtracks_NativeScript functions registered successfully");
        WriteToAdvancedLog("Papyrus functions registered successfully (32 functions)", __LINE__);
        return true;
    }
}

std::vector<std::string> GetAuthorsFromJSON() {
    std::vector<std::string> authors;
    
    try {
        fs::path jsonPath = g_dllDirectory / "OSoundtracks-SA-Expansion-Sounds-NG.json";
        
        if (!fs::exists(jsonPath)) {
            return authors;
        }

        std::ifstream jsonFile(jsonPath);
        if (!jsonFile.is_open()) {
            return authors;
        }

        std::string line;
        bool inSoundMenuKey = false;
        int braceDepth = 0;

        while (std::getline(jsonFile, line)) {
            if (line.find("\"SoundMenuKey\"") != std::string::npos) {
                inSoundMenuKey = true;
                for (char c : line) {
                    if (c == '{') braceDepth++;
                    if (c == '}') braceDepth--;
                }
                continue;
            }

            if (inSoundMenuKey) {
                for (char c : line) {
                    if (c == '{') braceDepth++;
                    if (c == '}') braceDepth--;
                }

                size_t quoteStart = line.find('"');
                while (quoteStart != std::string::npos) {
                    size_t quoteEnd = line.find('"', quoteStart + 1);
                    if (quoteEnd == std::string::npos) break;

                    size_t colonPos = line.find(':', quoteEnd + 1);
                    if (colonPos != std::string::npos) {
                        std::string potentialAuthor = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

                        std::string afterColon = line.substr(quoteEnd + 1, colonPos - quoteEnd - 1);
                        bool hasOnlyWhitespace = true;
                        for (char c : afterColon) {
                            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                                hasOnlyWhitespace = false;
                                break;
                            }
                        }

                        if (!potentialAuthor.empty() &&
                            potentialAuthor != "SoundMenuKey" &&
                            hasOnlyWhitespace &&
                            std::find(authors.begin(), authors.end(), potentialAuthor) == authors.end()) {
                            authors.push_back(potentialAuthor);
                        }
                    }

                    quoteStart = line.find('"', quoteEnd + 1);
                }

                if (braceDepth == 0) {
                    break;
                }
            }
        }

        jsonFile.close();

    } catch (...) {
        authors.clear();
    }
    
    return authors;
}

std::vector<std::string> GetAuthorsFromINI() {
    std::vector<std::string> authors;
    
    try {
        fs::path authorsIniPath = g_dllDirectory / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini";
        
        if (!fs::exists(authorsIniPath)) {
            return authors;
        }

        std::ifstream iniFile(authorsIniPath);
        if (!iniFile.is_open()) {
            return authors;
        }

        std::string line;
        while (std::getline(iniFile, line)) {
            std::string trimmedLine = line;
            trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t\r\n"));
            trimmedLine.erase(trimmedLine.find_last_not_of(" \t\r\n") + 1);

            if (trimmedLine.rfind("List =", 0) == 0) {
                size_t equalPos = trimmedLine.find('=');
                if (equalPos != std::string::npos) {
                    std::string authorList = trimmedLine.substr(equalPos + 1);
                    authorList.erase(0, authorList.find_first_not_of(" \t"));
                    authorList.erase(authorList.find_last_not_of(" \t") + 1);

                    size_t pos = 0;
                    std::string delimiter = "|";
                    while ((pos = authorList.find(delimiter)) != std::string::npos) {
                        std::string author = authorList.substr(0, pos);
                        if (!author.empty()) {
                            authors.push_back(author);
                        }
                        authorList.erase(0, pos + delimiter.length());
                    }
                    if (!authorList.empty()) {
                        authors.push_back(authorList);
                    }
                    break;
                }
            }
        }

        iniFile.close();

    } catch (...) {
        authors.clear();
    }
    
    return authors;
}

bool CompareAuthors(const std::vector<std::string>& jsonAuthors, const std::vector<std::string>& iniAuthors) {
    if (jsonAuthors.size() != iniAuthors.size()) {
        return false;
    }

    std::vector<std::string> sortedJson = jsonAuthors;
    std::vector<std::string> sortedIni = iniAuthors;
    std::sort(sortedJson.begin(), sortedJson.end());
    std::sort(sortedIni.begin(), sortedIni.end());

    return sortedJson == sortedIni;
}

void CheckAndRegenerateAuthorsINI() {
    try {
        WriteToAdvancedLog("========================================", __LINE__);
        WriteToAdvancedLog("Checking Authors INI consistency", __LINE__);

        std::vector<std::string> jsonAuthors = GetAuthorsFromJSON();
        std::vector<std::string> iniAuthors = GetAuthorsFromINI();

        if (jsonAuthors.empty()) {
            WriteToAdvancedLog("No authors found in JSON", __LINE__);
            WriteToAdvancedLog("========================================", __LINE__);
            return;
        }

        if (iniAuthors.empty()) {
            WriteToAdvancedLog("Authors INI not found or empty, generating...", __LINE__);
            GenerateAuthorsINI();
            WriteToAdvancedLog("========================================", __LINE__);
            return;
        }

        bool authorsMatch = CompareAuthors(jsonAuthors, iniAuthors);

        if (!authorsMatch) {
            WriteToAdvancedLog("Authors mismatch detected, regenerating Authors INI...", __LINE__);
            GenerateAuthorsINI();
        } else {
            WriteToAdvancedLog("Authors match, no changes needed", __LINE__);
        }

        WriteToAdvancedLog("========================================", __LINE__);

    } catch (const std::exception& e) {
        WriteToAdvancedLog("ERROR checking Authors INI: " + std::string(e.what()), __LINE__);
        WriteToAdvancedLog("========================================", __LINE__);
    }
}

void GenerateAuthorsINI() {
    try {
        WriteToAdvancedLog("========================================", __LINE__);
        WriteToAdvancedLog("Generating Authors INI from JSON", __LINE__);

        fs::path jsonPath = g_dllDirectory / "OSoundtracks-SA-Expansion-Sounds-NG.json";
        
        if (!fs::exists(jsonPath)) {
            WriteToAdvancedLog("JSON file not found, Authors INI will not be generated", __LINE__);
            WriteToAdvancedLog("========================================", __LINE__);
            return;
        }

        std::ifstream jsonFile(jsonPath);
        if (!jsonFile.is_open()) {
            WriteToAdvancedLog("Cannot open JSON file, Authors INI will not be generated", __LINE__);
            WriteToAdvancedLog("========================================", __LINE__);
            return;
        }

        std::string line;
        std::vector<std::string> authors;
        bool inSoundMenuKey = false;
        int braceDepth = 0;

        while (std::getline(jsonFile, line)) {
            if (line.find("\"SoundMenuKey\"") != std::string::npos) {
                inSoundMenuKey = true;
                WriteToAdvancedLog("Found SoundMenuKey section in JSON", __LINE__);
                for (char c : line) {
                    if (c == '{') braceDepth++;
                    if (c == '}') braceDepth--;
                }
                continue;
            }

            if (inSoundMenuKey) {
                for (char c : line) {
                    if (c == '{') braceDepth++;
                    if (c == '}') braceDepth--;
                }

                size_t quoteStart = line.find('"');
                while (quoteStart != std::string::npos) {
                    size_t quoteEnd = line.find('"', quoteStart + 1);
                    if (quoteEnd == std::string::npos) break;

                    size_t colonPos = line.find(':', quoteEnd + 1);
                    if (colonPos != std::string::npos) {
                        std::string potentialAuthor = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

                        std::string afterColon = line.substr(quoteEnd + 1, colonPos - quoteEnd - 1);
                        bool hasOnlyWhitespace = true;
                        for (char c : afterColon) {
                            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                                hasOnlyWhitespace = false;
                                break;
                            }
                        }

                        if (!potentialAuthor.empty() &&
                            potentialAuthor != "SoundMenuKey" &&
                            hasOnlyWhitespace &&
                            std::find(authors.begin(), authors.end(), potentialAuthor) == authors.end()) {
                            authors.push_back(potentialAuthor);
                            WriteToAdvancedLog("Found author: " + potentialAuthor, __LINE__);
                        }
                    }

                    quoteStart = line.find('"', quoteEnd + 1);
                }

                if (braceDepth == 0) {
                    break;
                }
            }
        }

        jsonFile.close();

        if (authors.empty()) {
            WriteToAdvancedLog("No authors found in SoundMenuKey section", __LINE__);
            WriteToAdvancedLog("Authors INI will not be generated", __LINE__);
            WriteToAdvancedLog("========================================", __LINE__);
            return;
        }

        std::string authorList;
        for (size_t i = 0; i < authors.size(); ++i) {
            authorList += authors[i];
            if (i < authors.size() - 1) {
                authorList += "|";
            }
        }

        fs::path authorsIniPath = g_dllDirectory / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini";
        
        std::ofstream authorsIni(authorsIniPath, std::ios::trunc);
        if (!authorsIni.is_open()) {
            WriteToAdvancedLog("Cannot create Authors INI file", __LINE__);
            WriteToAdvancedLog("========================================", __LINE__);
            return;
        }

        authorsIni << "[Authors]\n";
        authorsIni << "List = " << authorList << "\n";
        authorsIni.close();

        WriteToAdvancedLog("Authors INI generated successfully", __LINE__);
        WriteToAdvancedLog("Total authors found: " + std::to_string(authors.size()), __LINE__);
        WriteToAdvancedLog("Author list: " + authorList, __LINE__);
        WriteToAdvancedLog("File saved to: " + authorsIniPath.string(), __LINE__);
        WriteToAdvancedLog("========================================", __LINE__);

    } catch (const std::exception& e) {
        WriteToAdvancedLog("ERROR generating Authors INI: " + std::string(e.what()), __LINE__);
        WriteToAdvancedLog("========================================", __LINE__);
    }
}

void InitializePlugin() {
    try {
        g_documentsPath = GetDocumentsPath();
        
        auto paths = GetAllOSoundtracksLogsPaths();
        if (!paths.primary.empty()) {
            std::vector<fs::path> logFolders = { paths.primary, paths.secondary };
            
            for (const auto& folder : logFolders) {
                try {
                    auto advancedLogPath = folder / "OSoundtracks-SA-Expansion-Sounds-NG-MCM.log";
                    std::ofstream clearLog(advancedLogPath, std::ios::trunc);
                    clearLog.close();
                } catch (...) {}
            }
        }

        WriteToAdvancedLog("OSoundtracks MCM Plugin - Starting...", __LINE__);
        WriteToAdvancedLog("========================================", __LINE__);
        WriteToAdvancedLog("OSoundtracks MCM Plugin - v17.3.0", __LINE__);
        WriteToAdvancedLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToAdvancedLog("========================================", __LINE__);
        
        fs::path dllDir = GetDllDirectory();
        
        if (!dllDir.empty()) {
            g_dllDirectory = dllDir;
            g_usingDllPath = true;
            WriteToAdvancedLog("DLL Directory: " + dllDir.string(), __LINE__);
            
            fs::path iniPath = dllDir / "OSoundtracks-SA-Expansion-Sounds-NG.ini";
            if (fs::exists(iniPath)) {
                g_osoundtracksIniPath = iniPath;
                WriteToAdvancedLog("INI file found: " + iniPath.string(), __LINE__);
            } else {
                WriteToAdvancedLog("WARNING: INI file not found at: " + iniPath.string(), __LINE__);
            }
            
            WriteToAdvancedLog("Generating Authors INI (Initialization trigger)", __LINE__);
            GenerateAuthorsINI();
        } else {
            WriteToAdvancedLog("WARNING: Could not detect DLL directory", __LINE__);
        }

        g_isInitialized = true;
        WriteToAdvancedLog("PLUGIN INITIALIZATION COMPLETE", __LINE__);
        WriteToAdvancedLog("========================================", __LINE__);

    } catch (const std::exception& e) {
        logger::error("CRITICAL ERROR in Initialize: {}", e.what());
        WriteToAdvancedLog("CRITICAL ERROR: " + std::string(e.what()), __LINE__);
    }
}

void ShutdownPlugin() {
    logger::info("OSOUNDTRACKS MCM PLUGIN SHUTTING DOWN");
    WriteToAdvancedLog("PLUGIN SHUTTING DOWN", __LINE__);

    g_isShuttingDown = true;

    WriteToAdvancedLog("========================================", __LINE__);
    WriteToAdvancedLog("Plugin shutdown complete at: " + GetCurrentTimeString(), __LINE__);
    WriteToAdvancedLog("========================================", __LINE__);

    logger::info("Plugin shutdown complete");
}

void MessageListener(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("kDataLoaded: Game fully loaded");
            WriteToAdvancedLog("Game data loaded - MCM ready", __LINE__);
            WriteToAdvancedLog("Generating Authors INI (kDataLoaded trigger)", __LINE__);
            GenerateAuthorsINI();
            break;

        default:
            break;
    }
}

void SetupLog() {
    auto loggerPtr = std::make_shared<spdlog::logger>("log");
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::off);
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SetupLog();

    logger::info("OSoundtracks MCM Plugin v17.3.0 - Starting...");
    
    auto paths = GetAllOSoundtracksLogsPaths();
    try {
        std::ofstream clearLog(paths.primary / "OSoundtracks-SA-Expansion-Sounds-NG-MCM.log", std::ios::trunc);
        clearLog.close();
    } catch (...) {}

    WriteToAdvancedLog("========================================", __LINE__);
    WriteToAdvancedLog("OSoundtracks MCM Plugin v17.3.0", __LINE__);
    WriteToAdvancedLog("Started: " + GetCurrentTimeString(), __LINE__);
    WriteToAdvancedLog("========================================", __LINE__);

    InitializePlugin();

    SKSE::GetPapyrusInterface()->Register(OSoundtracks_Native::RegisterPapyrusFunctions);
    
    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

    logger::info("OSoundtracks MCM Plugin loaded successfully");
    return true;
}

constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({17, 3, 0});
    v.PluginName("OSoundtracks MCM");
    v.AuthorName("John95AC");
    v.UsesAddressLibrary();
    v.UsesSigScanning();
    v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST, SKSE::RUNTIME_LATEST_VR});

    return v;
}();
