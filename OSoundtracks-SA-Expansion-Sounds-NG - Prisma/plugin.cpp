#include "PrismaUI_API.h"
#include "bass.h"

#include <windows.h>
#include <shellapi.h>
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <shlobj.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <unordered_map>

// CORE NAMESPACE
namespace fs = std::filesystem;
namespace logger = SKSE::log;

static HMODULE g_bassModule = nullptr;
static bool g_bassInitialized = false;
static std::mutex g_bassMutex;
static HSTREAM g_healingStream = 0;
static HSTREAM g_menuOpenStream = 0;
static HSTREAM g_menuCloseStream = 0;
static HSTREAM g_videoAudioStream = 0;
static HSTREAM g_impactAudioStream = 0;
static HSTREAM g_jackpotAudioStream = 0;
static HSTREAM g_jackpotOutAudioStream = 0;
static HSTREAM g_doomMusicStream = 0;

typedef BOOL(WINAPI *BASS_Init_t)(int, DWORD, DWORD, HWND, void *);
typedef BOOL(WINAPI *BASS_Free_t)();
typedef int(WINAPI *BASS_ErrorGetCode_t)();
typedef HSTREAM(WINAPI *BASS_StreamCreateFile_t)(BOOL, const void *, QWORD, QWORD, DWORD);
typedef BOOL(WINAPI *BASS_ChannelPlay_t)(DWORD, BOOL);
typedef BOOL(WINAPI *BASS_ChannelStop_t)(DWORD);
typedef BOOL(WINAPI *BASS_StreamFree_t)(DWORD);
typedef BOOL(WINAPI *BASS_ChannelSetAttribute_t)(DWORD, DWORD, float);
typedef BOOL(WINAPI *BASS_ChannelPause_t)(DWORD);
typedef QWORD(WINAPI *BASS_ChannelSeconds2Bytes_t)(DWORD, double);
typedef BOOL(WINAPI *BASS_ChannelSetPosition_t)(DWORD, QWORD, DWORD);
typedef QWORD(WINAPI *BASS_ChannelGetLength_t)(DWORD, DWORD);
typedef double(WINAPI *BASS_ChannelBytes2Seconds_t)(DWORD, QWORD);

static BASS_Init_t pBASS_Init = nullptr;
static BASS_Free_t pBASS_Free = nullptr;
static BASS_ErrorGetCode_t pBASS_ErrorGetCode = nullptr;
static BASS_StreamCreateFile_t pBASS_StreamCreateFile = nullptr;
static BASS_ChannelPlay_t pBASS_ChannelPlay = nullptr;
static BASS_ChannelStop_t pBASS_ChannelStop = nullptr;
static BASS_StreamFree_t pBASS_StreamFree = nullptr;
static BASS_ChannelSetAttribute_t pBASS_ChannelSetAttribute = nullptr;
static BASS_ChannelPause_t pBASS_ChannelPause = nullptr;
static BASS_ChannelSeconds2Bytes_t pBASS_ChannelSeconds2Bytes = nullptr;
static BASS_ChannelSetPosition_t pBASS_ChannelSetPosition = nullptr;
static BASS_ChannelGetLength_t pBASS_ChannelGetLength = nullptr;
static BASS_ChannelBytes2Seconds_t pBASS_ChannelBytes2Seconds = nullptr;

void SetupLog();
fs::path GetPluginDllPath();
bool InitializeBASSLibrary();
void ShutdownBASSLibrary();
HSTREAM PlayBASSSound(const std::string &soundFile, float volume);
void StopBASSStream(HSTREAM &stream);

void OnPlayVideoAudio(const char *data);
void OnPauseVideoAudio(const char *data);
void OnStopVideoAudio(const char *data);
void OnSeekVideoAudio(const char *data);
void OnSetVideoVolume(const char *data);

void OnPlayImpactSound(const char *data);
void OnStopImpactSound(const char *data);

void OnPlayMeowSound(const char *data);
void OnPlayRobotTalkSound(const char *data);
void OnPlayJackpotSound(const char *data);
void OnStopJackpotSound(const char *data);
void OnPlayJackpotOutSound(const char *data);

PRISMA_UI_API::IVPrismaUI1 *PrismaUI = nullptr;

typedef const char* (*FnGetMenuTrack)();
typedef const char* (*FnGetMenuAuthor)();
typedef int         (*FnGetMenuStatus)();
typedef float       (*FnGetMenuProgress)();
typedef const char* (*FnGetBaseTrack)();
typedef void        (*FnPauseMusic)();
typedef void        (*FnResumeMusic)();
typedef void        (*FnNextTrack)();
typedef void        (*FnAuthorPreview)(const char*, int);
typedef const char* (*FnGetSpecificTrack)();
typedef int         (*FnGetSpecificStatus)();
typedef const char* (*FnGetActiveSystem)();

static FnGetMenuTrack    g_fnGetMenuTrack    = nullptr;
static FnGetMenuAuthor   g_fnGetMenuAuthor   = nullptr;
static FnGetMenuStatus   g_fnGetMenuStatus   = nullptr;
static FnGetMenuProgress g_fnGetMenuProgress = nullptr;
static FnGetBaseTrack    g_fnGetBaseTrack    = nullptr;
static FnPauseMusic      g_fnPauseMusic      = nullptr;
static FnResumeMusic     g_fnResumeMusic     = nullptr;
static FnNextTrack       g_fnNextTrack       = nullptr;
static FnAuthorPreview   g_fnAuthorPreview   = nullptr;
static FnGetSpecificTrack g_fnGetSpecificTrack = nullptr;
static FnGetSpecificStatus g_fnGetSpecificStatus = nullptr;
static FnGetActiveSystem g_fnGetActiveSystem = nullptr;
static bool              g_playerFunctionsLoaded = false;

static PrismaView g_HealingView = 0;
static bool g_ViewCreated = false;
static bool g_ViewFocused = false;

struct TrackInfo
{
    std::string name;           
    std::string duration;       
    std::string format;         
    std::string imageFile;      
    std::string albumOrAnim;    
    bool isSpecial;             
};

static std::map<std::string, std::vector<TrackInfo>> g_soundMenuKeyTracks;
static std::map<std::string, std::vector<TrackInfo>> g_soundKeyTracks;
static std::string g_lastJsonModTime;

// AUDIO ENGINE BINDINGS
void LoadSoundPlayerFunctions() {
    if (g_playerFunctionsLoaded) return;
    HMODULE dll = GetModuleHandleA("OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.dll");
    if (!dll) { logger::warn("Sound-Player DLL not found"); return; }
    g_fnGetMenuTrack    = (FnGetMenuTrack)   GetProcAddress(dll, "OSoundtracks_GetCurrentMenuTrack");
    g_fnGetMenuAuthor   = (FnGetMenuAuthor)  GetProcAddress(dll, "OSoundtracks_GetCurrentMenuAuthor");
    g_fnGetMenuStatus   = (FnGetMenuStatus)  GetProcAddress(dll, "OSoundtracks_GetMenuStatus");
    g_fnGetMenuProgress = (FnGetMenuProgress)GetProcAddress(dll, "OSoundtracks_GetMenuProgress");
    g_fnGetBaseTrack    = (FnGetBaseTrack)   GetProcAddress(dll, "OSoundtracks_GetCurrentBaseTrack");
    g_fnPauseMusic      = (FnPauseMusic)     GetProcAddress(dll, "OSoundtracks_PauseMusic");
    g_fnResumeMusic     = (FnResumeMusic)    GetProcAddress(dll, "OSoundtracks_ResumeMusic");
    g_fnNextTrack       = (FnNextTrack)      GetProcAddress(dll, "OSoundtracks_NextTrack");
    g_fnAuthorPreview   = (FnAuthorPreview)  GetProcAddress(dll, "OSoundtracks_PlayAuthorPreview");
    g_fnGetSpecificTrack = (FnGetSpecificTrack)GetProcAddress(dll, "OSoundtracks_GetSpecificTrack");
    g_fnGetSpecificStatus = (FnGetSpecificStatus)GetProcAddress(dll, "OSoundtracks_GetSpecificStatus");
    g_fnGetActiveSystem = (FnGetActiveSystem)GetProcAddress(dll, "OSoundtracks_GetActiveSystem");
    g_playerFunctionsLoaded = true;
    logger::info("Sound-Player functions loaded");
}

static std::atomic<bool> g_nowPlayingTimerActive{false};
static std::thread g_nowPlayingThread;
static bool g_isShuttingDown = false;

static std::string g_lastTrack, g_lastAuthor, g_lastSystem;
static int g_lastStatus = -1;

// NOW PLAYING LOGIC
void SendNowPlayingToJS() {
    if (!g_HealingView || !g_playerFunctionsLoaded) return;

    const char* system = g_fnGetActiveSystem ? g_fnGetActiveSystem() : "none";
    std::string systemStr(system ? system : "none");

    std::string trackStr, authorStr;
    int status = 0;
    std::string statusText = "stopped";

    if (systemStr == "SoundMenuKey") {
        const char* t = g_fnGetMenuTrack ? g_fnGetMenuTrack() : "";
        const char* a = g_fnGetMenuAuthor ? g_fnGetMenuAuthor() : "";
        int s = g_fnGetMenuStatus ? g_fnGetMenuStatus() : 0;
        trackStr = t ? t : "";
        authorStr = a ? a : "";
        status = s;
        statusText = (status == 1) ? "playing" : "paused";
    } else if (systemStr == "SoundKey") {
        const char* t = g_fnGetSpecificTrack ? g_fnGetSpecificTrack() : "";
        int s = g_fnGetSpecificStatus ? g_fnGetSpecificStatus() : 0;
        trackStr = t ? t : "";
        status = s;
        statusText = (status == 1) ? "playing" : "paused";
    }

    if (trackStr != g_lastTrack || authorStr != g_lastAuthor || 
        systemStr != g_lastSystem || status != g_lastStatus) {
        logger::info("[NowPlaying] {}, track='{}', author='{}', system='{}'",
            statusText, trackStr, authorStr, systemStr);
        g_lastTrack = trackStr;
        g_lastAuthor = authorStr;
        g_lastSystem = systemStr;
        g_lastStatus = status;
    }

    std::stringstream ss;
    ss << "updateNowPlaying({\"track\":\"" << trackStr
       << "\",\"author\":\"" << authorStr
       << "\",\"status\":\"" << statusText
       << "\",\"system\":\"" << systemStr << "\"})";
    PrismaUI->Invoke(g_HealingView, ss.str().c_str());
}

void StartNowPlayingTimer() {
    if (g_nowPlayingTimerActive.load()) return;
    g_nowPlayingTimerActive = true;
    g_nowPlayingThread = std::thread([]() {
        while (g_nowPlayingTimerActive.load() && !g_isShuttingDown) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (g_HealingView && g_playerFunctionsLoaded) SendNowPlayingToJS();
        }
    });
    g_nowPlayingThread.detach();
    logger::info("Now Playing timer started (500ms)");
}

void StopNowPlayingTimer() { g_nowPlayingTimerActive = false; }

// PLUGIN CONFIGURATION
struct PluginConfig
{
    uint32_t firstKey = 0x2A;              
    uint32_t secondKey = 0x11;             
    bool singleKeyMode = false;            
    int volume = 70;                       
    bool muteHubSound = false;             
    std::string hubSound = "miau-PDA.wav"; 
    int posX = 50;                         
    int posY = 50;                         
    int hubSize = 100;                     

    
    bool gifVisible = false; 
    int gifPosX = 60;        
    int gifPosY = 50;        
    int gifZoom = 100;       
    int gifWidth = 180;      
    int gifHeight = 180;     

    
    bool videoVisible = false;    
    int videoPosX = 70;           
    int videoPosY = 50;           
    int videoWidth = 320;         
    int videoHeight = 240;        
    int videoOpacity = 100;       
    int videoVolume = 70;         
    bool videoFullscreen = false; 

    
    bool logsVisible = false;    
    int logsPosX = 50;           
    int logsPosY = 50;           
    int logsWidth = 400;         
    int logsHeight = 300;        
    int logsFontSize = 10;       
    bool logsAutoRefresh = true; 

    
    bool liveVisible = false;   
    int livePosX = 80;          
    int livePosY = 50;          
    int liveSize = 100;         
    bool liveAutoStart = false; 
    std::string liveNowPlayingMode = "timed";
    int liveNowPlayingSeconds = 10;
    std::string liveNowPlayingGif = "viaductk-music-7683.gif";

    
    int bootPosX = 10; 
    int bootPosY = 10; 

    
    int globalAlpha = 97;                
    std::string globalColor = "default"; 

    
    std::string liveBackground = "default"; 

    
    int menusSize = 100;
    int itemsSize = 100;
    int toastSize = 14;

    // Settings-js toggles
    bool hubVisibleSettings = true;
    bool proximitySettings = false;
};

static PluginConfig g_config;
static std::string g_configPath;

struct ItemsConfig
{
    struct
    {
        
        std::string showNotification = "game";
    } notification;

    struct
    {
        bool enabled = true;
        std::string formID = "00000F";
        std::string plugin = "Skyrim.esm";
        int amount = 9;
        std::string itemName = "Gold";
    } gold;

    struct
    {
        bool enabled = false;
        std::string formID = "XXXXXX";
        std::string plugin = "None";
        int amount = 1;
        std::string itemName = "None";
    } item1;

    struct
    {
        bool enabled = false;
        std::string formID = "XXXXXX";
        std::string plugin = "None";
        int amount = 1;
        std::string itemName = "None";
    } item2;
};

static ItemsConfig g_itemsConfig;

std::string GetConfigPath();
std::string GetItemsConfigPath();
std::string GetBackupItemsConfigPath();

bool LoadItemsConfig()
{
    std::string configPath = GetItemsConfigPath();
    std::string backupPath = GetBackupItemsConfigPath();

    if (!fs::exists(configPath))
    {
        logger::info("Items config not found, will restore from backup or use defaults");
        if (fs::exists(backupPath))
        {
            try
            {
                fs::copy_file(backupPath, configPath, fs::copy_options::overwrite_existing);
                logger::info("Restored items config from backup");
            }
            catch (const std::exception &e)
            {
                logger::error("Failed to restore items config from backup: {}", e.what());
            }
        }
        else
        {
            logger::info("No backup found, using default values");
        }
        return true;
    }

    std::ifstream file(configPath);
    if (!file.is_open())
    {
        logger::error("Failed to open items config: {}", configPath);
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(file, line))
    {
        
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        
        if (line[0] == '[' && line.back() == ']')
        {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos)
            continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        
        if (currentSection == "Notification")
        {
            if (key == "ShowNotification")
            {
                g_itemsConfig.notification.showNotification = value;
            }
        }
        else if (currentSection == "Gold")
        {
            if (key == "Enabled")
            {
                g_itemsConfig.gold.enabled = (value == "true" || value == "1" || value == "yes");
            }
            else if (key == "FormID")
            {
                g_itemsConfig.gold.formID = value;
            }
            else if (key == "Plugin")
            {
                g_itemsConfig.gold.plugin = value;
            }
            else if (key == "Amount")
            {
                try
                {
                    g_itemsConfig.gold.amount = std::stoi(value);
                }
                catch (...)
                {
                    g_itemsConfig.gold.amount = 9;
                }
            }
            else if (key == "ItemName")
            {
                g_itemsConfig.gold.itemName = value;
            }
        }
        else if (currentSection == "Item1")
        {
            if (key == "Enabled")
            {
                g_itemsConfig.item1.enabled = (value == "true" || value == "1" || value == "yes");
            }
            else if (key == "FormID")
            {
                g_itemsConfig.item1.formID = value;
            }
            else if (key == "Plugin")
            {
                g_itemsConfig.item1.plugin = value;
            }
            else if (key == "Amount")
            {
                try
                {
                    g_itemsConfig.item1.amount = std::stoi(value);
                }
                catch (...)
                {
                    g_itemsConfig.item1.amount = 1;
                }
            }
            else if (key == "ItemName")
            {
                g_itemsConfig.item1.itemName = value;
            }
        }
        else if (currentSection == "Item2")
        {
            if (key == "Enabled")
            {
                g_itemsConfig.item2.enabled = (value == "true" || value == "1" || value == "yes");
            }
            else if (key == "FormID")
            {
                g_itemsConfig.item2.formID = value;
            }
            else if (key == "Plugin")
            {
                g_itemsConfig.item2.plugin = value;
            }
            else if (key == "Amount")
            {
                try
                {
                    g_itemsConfig.item2.amount = std::stoi(value);
                }
                catch (...)
                {
                    g_itemsConfig.item2.amount = 1;
                }
            }
            else if (key == "ItemName")
            {
                g_itemsConfig.item2.itemName = value;
            }
        }
    }

    logger::info("Loaded items config - Notification: {}, Gold enabled: {}, Amount: {}",
                 g_itemsConfig.notification.showNotification,
                 g_itemsConfig.gold.enabled,
                 g_itemsConfig.gold.amount);

    return true;
}

std::string GetConfigPath()
{
    if (!g_configPath.empty())
        return g_configPath;

    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        g_configPath = (dllDir / "OSoundtracks-Prisma.ini").string();
        return g_configPath;
    }
    return "OSoundtracks-Prisma.ini";
}
std::string GetBackupConfigPath()
{
    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        fs::path backupDir = dllDir / "Backup";
        return (backupDir / "OSoundtracks-Prisma.ini").string();
    }
    return "Backup/OSoundtracks-Prisma.ini";
}

std::string GetMenusConfigPath()
{
    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        return (dllDir / "OSoundtracks-Prisma-menus.ini").string();
    }
    return "OSoundtracks-Prisma-menus.ini";
}

std::string GetBackupMenusConfigPath()
{
    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        fs::path backupDir = dllDir / "Backup";
        return (backupDir / "OSoundtracks-Prisma-menus.ini").string();
    }
    return "Backup/OSoundtracks-Prisma-menus.ini";
}

std::string GetItemsConfigPath()
{
    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        return (dllDir / "OSoundtracks-Prisma-Items.ini").string();
    }
    return "OSoundtracks-Prisma-Items.ini";
}

std::string GetBackupItemsConfigPath()
{
    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        fs::path backupDir = dllDir / "Backup";
        return (backupDir / "OSoundtracks-Prisma-Items.ini").string();
    }
    return "Backup/OSoundtracks-Prisma-Items.ini";
}

std::string GetMemoryJsPath()
{
    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        return (dllDir / "Prisma-John95AC-Memory.js").string();
    }
    return "Prisma-John95AC-Memory.js";
}

fs::path GetAssetsIniDir()
{
    return fs::path(GetPluginDllPath()).parent_path().parent_path() /
           "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini";
}

std::map<std::string, int> g_detectedMenus;

std::set<std::string> g_openTrackedMenus;

std::vector<std::string> g_authorsList;

void SaveMenusConfig()
{
    std::string configPath = GetMenusConfigPath();

    std::ofstream file(configPath);
    if (!file.is_open())
    {
        logger::error("Failed to save menus config: {}", configPath);
        return;
    }

    file << "; ============================================\n";
    file << "; OSOUNDTRACKS PRISMA - MENU TRACKING SYSTEM\n";
    file << "; ============================================\n";
    file << "; This file tracks all menus detected in the game\n";
    file << "; New menus are added automatically when detected in log\n";
    file << "; ============================================\n\n";

    file << "[DetectedMenus]\n";
    file << "; MenuName = enabled (1 = tracked, 0 = ignored)\n";

    for (const auto &[menuName, enabled] : g_detectedMenus)
    {
        file << menuName << " = " << enabled << "\n";
    }

    file.close();

    
    std::string backupPath = GetBackupMenusConfigPath();
    fs::path backupDir = fs::path(backupPath).parent_path();
    try
    {
        if (!fs::exists(backupDir))
        {
            fs::create_directories(backupDir);
        }
        fs::copy_file(configPath, backupPath, fs::copy_options::overwrite_existing);
        logger::info("Menus backup saved to: {}", backupPath);
    }
    catch (const std::exception &e)
    {
        logger::error("Failed to save menus backup: {}", e.what());
    }

    logger::info("Saved {} menus to config", g_detectedMenus.size());
}

void SaveItemsConfig()
{
    std::string configPath = GetItemsConfigPath();

    
    std::vector<std::string> lines;
    std::ifstream inFile(configPath);
    bool fileExists = inFile.is_open();

    if (fileExists)
    {
        std::string line;
        while (std::getline(inFile, line))
        {
            lines.push_back(line);
        }
        inFile.close();
    }

    
    std::ofstream outFile(configPath);
    if (!outFile.is_open())
    {
        logger::error("Failed to save items config: {}", configPath);
        return;
    }

    
    std::string currentSection = "";

    if (fileExists)
    {
        for (const auto &line : lines)
        {
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

            
            if (!trimmed.empty() && trimmed[0] == '[')
            {
                size_t endBracket = trimmed.find(']');
                if (endBracket != std::string::npos)
                {
                    currentSection = trimmed.substr(1, endBracket - 1);
                }
                outFile << line << "\n";
                continue;
            }

            
            size_t pos = trimmed.find('=');
            if (pos != std::string::npos && trimmed[0] != ';' && trimmed[0] != '#')
            {
                std::string key = trimmed.substr(0, pos);
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);

                
                if (currentSection == "Notification" && key == "ShowNotification")
                {
                    outFile << "ShowNotification = " << g_itemsConfig.notification.showNotification << "\n";
                }
                else if (currentSection == "Gold")
                {
                    if (key == "Enabled")
                        outFile << "Enabled = " << (g_itemsConfig.gold.enabled ? "true" : "false") << "\n";
                    else if (key == "FormID")
                        outFile << "FormID = " << g_itemsConfig.gold.formID << "\n";
                    else if (key == "Plugin")
                        outFile << "Plugin = " << g_itemsConfig.gold.plugin << "\n";
                    else if (key == "Amount")
                        outFile << "Amount = " << g_itemsConfig.gold.amount << "\n";
                    else if (key == "ItemName")
                        outFile << "ItemName = " << g_itemsConfig.gold.itemName << "\n";
                    else
                        outFile << line << "\n";
                }
                else if (currentSection == "Item1")
                {
                    if (key == "Enabled")
                        outFile << "Enabled = " << (g_itemsConfig.item1.enabled ? "true" : "false") << "\n";
                    else if (key == "FormID")
                        outFile << "FormID = " << g_itemsConfig.item1.formID << "\n";
                    else if (key == "Plugin")
                        outFile << "Plugin = " << g_itemsConfig.item1.plugin << "\n";
                    else if (key == "Amount")
                        outFile << "Amount = " << g_itemsConfig.item1.amount << "\n";
                    else if (key == "ItemName")
                        outFile << "ItemName = " << g_itemsConfig.item1.itemName << "\n";
                    else
                        outFile << line << "\n";
                }
                else if (currentSection == "Item2")
                {
                    if (key == "Enabled")
                        outFile << "Enabled = " << (g_itemsConfig.item2.enabled ? "true" : "false") << "\n";
                    else if (key == "FormID")
                        outFile << "FormID = " << g_itemsConfig.item2.formID << "\n";
                    else if (key == "Plugin")
                        outFile << "Plugin = " << g_itemsConfig.item2.plugin << "\n";
                    else if (key == "Amount")
                        outFile << "Amount = " << g_itemsConfig.item2.amount << "\n";
                    else if (key == "ItemName")
                        outFile << "ItemName = " << g_itemsConfig.item2.itemName << "\n";
                    else
                        outFile << line << "\n";
                }
                else
                {
                    
                    outFile << line << "\n";
                }
            }
            else
            {
                
                outFile << line << "\n";
            }
        }
    }
    else
    {
        
        outFile << "; =====================================================\n";
        outFile << "; OSoundtracks Prisma - Items Configuration\n";
        outFile << "; =====================================================\n";
        outFile << "; This file configures items that can be given to the player\n";
        outFile << "; via UI buttons or console commands.\n";
        outFile << ";\n";
        outFile << "; Format:\n";
        outFile << ";   FormID = 6 hex digits (without 0x prefix)\n";
        outFile << ";   Plugin = mod name that contains the item\n";
        outFile << ";   Amount = quantity to give\n";
        outFile << ";   ItemName = name for notification (translatable)\n";
        outFile << "; =====================================================\n\n";

        outFile << "[Notification]\n";
        outFile << "; ShowNotification: \"game\" (plugin), \"hub\" (HTML/JS), \"false\" (disabled)\n";
        outFile << "ShowNotification = " << g_itemsConfig.notification.showNotification << "\n\n";

        outFile << "[Gold]\n";
        outFile << "; Gold item from Skyrim base game\n";
        outFile << "Enabled = " << (g_itemsConfig.gold.enabled ? "true" : "false") << "\n";
        outFile << "FormID = " << g_itemsConfig.gold.formID << "\n";
        outFile << "Plugin = " << g_itemsConfig.gold.plugin << "\n";
        outFile << "Amount = " << g_itemsConfig.gold.amount << "\n";
        outFile << "ItemName = " << g_itemsConfig.gold.itemName << "\n\n";

        outFile << "[Item1]\n";
        outFile << "; Custom item slot 1\n";
        outFile << "Enabled = " << (g_itemsConfig.item1.enabled ? "true" : "false") << "\n";
        outFile << "FormID = " << g_itemsConfig.item1.formID << "\n";
        outFile << "Plugin = " << g_itemsConfig.item1.plugin << "\n";
        outFile << "Amount = " << g_itemsConfig.item1.amount << "\n";
        outFile << "ItemName = " << g_itemsConfig.item1.itemName << "\n\n";

        outFile << "[Item2]\n";
        outFile << "; Custom item slot 2\n";
        outFile << "Enabled = " << (g_itemsConfig.item2.enabled ? "true" : "false") << "\n";
        outFile << "FormID = " << g_itemsConfig.item2.formID << "\n";
        outFile << "Plugin = " << g_itemsConfig.item2.plugin << "\n";
        outFile << "Amount = " << g_itemsConfig.item2.amount << "\n";
        outFile << "ItemName = " << g_itemsConfig.item2.itemName << "\n";
    }

    outFile.close();

    
    std::string backupPath = GetBackupItemsConfigPath();
    fs::path backupDir = fs::path(backupPath).parent_path();
    try
    {
        if (!fs::exists(backupDir))
        {
            fs::create_directories(backupDir);
        }
        fs::copy_file(configPath, backupPath, fs::copy_options::overwrite_existing);
        logger::info("Items backup saved to: {}", backupPath);
    }
    catch (const std::exception &e)
    {
        logger::error("Failed to save items backup: {}", e.what());
    }

    logger::info("Saved items config");
}

void LoadMenusConfig()
{
    std::string configPath = GetMenusConfigPath();

    if (!fs::exists(configPath))
    {
        
        g_detectedMenus["Console"] = 1;
        g_detectedMenus["PrismaUI_FocusMenu"] = 1;
        g_detectedMenus["Cursor Menu"] = 1;
        g_detectedMenus["Journal Menu"] = 1;
        g_detectedMenus["TweenMenu"] = 1;
        g_detectedMenus["Fader Menu"] = 1;
        g_detectedMenus["OstimSceneMenu"] = 1;
        g_detectedMenus["LoadWaitSpinner"] = 1;
        SaveMenusConfig();
        logger::info("Created default menus config: {}", configPath);
        return;
    }

    std::ifstream file(configPath);
    if (!file.is_open())
        return;

    std::string line;
    bool inDetectedMenus = false;

    while (std::getline(file, line))
    {
        if (line.find("[DetectedMenus]") != std::string::npos)
        {
            inDetectedMenus = true;
            continue;
        }

        if (inDetectedMenus && line.find("[") != std::string::npos && line.find("]") != std::string::npos)
        {
            break;
        }

        if (inDetectedMenus && line.find("=") != std::string::npos)
        {
            
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                continue;

            size_t eqPos = line.find("=");
            std::string menuName = line.substr(0, eqPos);
            menuName.erase(0, menuName.find_first_not_of(" \t"));
            menuName.erase(menuName.find_last_not_of(" \t") + 1);

            int enabled = 1;
            if (eqPos + 1 < line.length())
            {
                std::string value = line.substr(eqPos + 1);
                
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                try
                {
                    enabled = std::stoi(value);
                }
                catch (...)
                {
                    enabled = 1;
                }
            }

            g_detectedMenus[menuName] = enabled;
        }
    }

    file.close();
    logger::info("Loaded {} menus from config", g_detectedMenus.size());
}

void LoadConfig()
{
    std::string configPath = GetConfigPath();
    std::string backupPath = GetBackupConfigPath();

    
    if (fs::exists(backupPath) && fs::exists(configPath))
    {
        std::map<std::string, std::string> backupValues;
        std::map<std::string, std::string> originalValues;

        
        std::ifstream backupFile(backupPath);
        if (backupFile.is_open())
        {
            std::string line;
            while (std::getline(backupFile, line))
            {
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
                if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
                    continue;
                size_t pos = line.find('=');
                if (pos != std::string::npos)
                {
                    std::string key = line.substr(0, pos);
                    std::string value = line.substr(pos + 1);
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    backupValues[key] = value;
                }
            }
            backupFile.close();
        }

        
        std::ifstream originalFile(configPath);
        if (originalFile.is_open())
        {
            std::string line;
            while (std::getline(originalFile, line))
            {
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
                if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
                    continue;
                size_t pos = line.find('=');
                if (pos != std::string::npos)
                {
                    std::string key = line.substr(0, pos);
                    std::string value = line.substr(pos + 1);
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    originalValues[key] = value;
                }
            }
            originalFile.close();
        }

        
        bool needsUpdate = false;
        for (const auto &[key, backupValue] : backupValues)
        {
            if (originalValues.find(key) != originalValues.end())
            {
                if (originalValues[key] != backupValue)
                {
                    logger::info("Restoring from backup: {} = {} (was {})", key, backupValue, originalValues[key]);
                    originalValues[key] = backupValue;
                    needsUpdate = true;
                }
            }
        }

        
        if (needsUpdate)
        {
            std::vector<std::string> lines;
            std::ifstream inFile(configPath);
            if (inFile.is_open())
            {
                std::string line;
                while (std::getline(inFile, line))
                {
                    lines.push_back(line);
                }
                inFile.close();
            }

            std::ofstream outFile(configPath);
            if (outFile.is_open())
            {
                for (const auto &line : lines)
                {
                    std::string trimmed = line;
                    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
                    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

                    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' || trimmed[0] == '[')
                    {
                        outFile << line << "\n";
                    }
                    else
                    {
                        size_t pos = trimmed.find('=');
                        if (pos != std::string::npos)
                        {
                            std::string key = trimmed.substr(0, pos);
                            key.erase(0, key.find_first_not_of(" \t"));
                            key.erase(key.find_last_not_of(" \t") + 1);
                            if (originalValues.find(key) != originalValues.end())
                            {
                                outFile << key << " = " << originalValues[key] << "\n";
                            }
                    else
                    {
                        outFile << line << "\n";
                    }
                }

                else
                        {
                            outFile << line << "\n";
                        }
                    }
                }
                outFile.close();
                logger::info("Original config updated from backup");
            }
        }
    }

    std::ifstream file(configPath);
    if (!file.is_open())
    {
        logger::info("Config file not found, using defaults: {}", configPath);
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
            continue;

        
        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        
        std::string keyLower = key;
        std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);

        if (keyLower == "firstkey")
        {
            try
            {
                g_config.firstKey = std::stoul(value, nullptr, 0);
                logger::info("Config: firstKey = 0x{:X}", g_config.firstKey);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "secondkey")
        {
            try
            {
                g_config.secondKey = std::stoul(value, nullptr, 0);
                logger::info("Config: secondKey = 0x{:X}", g_config.secondKey);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "singlekeymode")
        {
            g_config.singleKeyMode = (value == "1" || value == "true");
            logger::info("Config: singleKeyMode = {}", g_config.singleKeyMode);
        }
        else if (keyLower == "volume")
        {
            try
            {
                g_config.volume = std::stoi(value);
                g_config.volume = std::max(0, std::min(100, g_config.volume));
                logger::info("Config: volume = {}", g_config.volume);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "posx")
        {
            try
            {
                g_config.posX = std::stoi(value);
                g_config.posX = std::max(0, std::min(100, g_config.posX));
                logger::info("Config: posX = {}", g_config.posX);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "posy")
        {
            try
            {
                g_config.posY = std::stoi(value);
                g_config.posY = std::max(0, std::min(100, g_config.posY));
                logger::info("Config: posY = {}", g_config.posY);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "hubsize")
        {
            try
            {
                g_config.hubSize = std::stoi(value);
                g_config.hubSize = std::max(50, std::min(200, g_config.hubSize));
                logger::info("Config: hubSize = {}", g_config.hubSize);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "mutehubsound")
        {
            g_config.muteHubSound = (value == "1" || value == "true");
            logger::info("Config: muteHubSound = {}", g_config.muteHubSound);
        }
        else if (keyLower == "hubsound")
        {
            g_config.hubSound = value;
            logger::info("Config: hubSound = {}", g_config.hubSound);
        }
        else if (keyLower == "gifvisible")
        {
            g_config.gifVisible = (value == "1" || value == "true");
            logger::info("Config: gifVisible = {}", g_config.gifVisible);
        }
        else if (keyLower == "gifposx")
        {
            try
            {
                g_config.gifPosX = std::stoi(value);
                g_config.gifPosX = std::max(0, std::min(100, g_config.gifPosX));
                logger::info("Config: gifPosX = {}", g_config.gifPosX);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "gifposy")
        {
            try
            {
                g_config.gifPosY = std::stoi(value);
                g_config.gifPosY = std::max(0, std::min(100, g_config.gifPosY));
                logger::info("Config: gifPosY = {}", g_config.gifPosY);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "gifzoom")
        {
            try
            {
                g_config.gifZoom = std::stoi(value);
                g_config.gifZoom = std::max(50, std::min(200, g_config.gifZoom));
                logger::info("Config: gifZoom = {}", g_config.gifZoom);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "gifwidth")
        {
            try
            {
                g_config.gifWidth = std::stoi(value);
                g_config.gifWidth = std::max(120, g_config.gifWidth);
                logger::info("Config: gifWidth = {}", g_config.gifWidth);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "gifheight")
        {
            try
            {
                g_config.gifHeight = std::stoi(value);
                g_config.gifHeight = std::max(120, g_config.gifHeight);
                logger::info("Config: gifHeight = {}", g_config.gifHeight);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "videovisible")
        {
            g_config.videoVisible = (value == "1" || value == "true");
            logger::info("Config: videoVisible = {}", g_config.videoVisible);
        }
        else if (keyLower == "videoposx")
        {
            try
            {
                g_config.videoPosX = std::stoi(value);
                g_config.videoPosX = std::max(0, std::min(100, g_config.videoPosX));
                logger::info("Config: videoPosX = {}", g_config.videoPosX);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "videoposy")
        {
            try
            {
                g_config.videoPosY = std::stoi(value);
                g_config.videoPosY = std::max(0, std::min(100, g_config.videoPosY));
                logger::info("Config: videoPosY = {}", g_config.videoPosY);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "videowidth")
        {
            try
            {
                g_config.videoWidth = std::stoi(value);
                g_config.videoWidth = std::max(200, g_config.videoWidth);
                logger::info("Config: videoWidth = {}", g_config.videoWidth);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "videoheight")
        {
            try
            {
                g_config.videoHeight = std::stoi(value);
                g_config.videoHeight = std::max(150, g_config.videoHeight);
                logger::info("Config: videoHeight = {}", g_config.videoHeight);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "videoopacity")
        {
            try
            {
                g_config.videoOpacity = std::stoi(value);
                g_config.videoOpacity = std::max(20, std::min(100, g_config.videoOpacity));
                logger::info("Config: videoOpacity = {}", g_config.videoOpacity);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "videovolume")
        {
            try
            {
                g_config.videoVolume = std::stoi(value);
                g_config.videoVolume = std::max(0, std::min(100, g_config.videoVolume));
                logger::info("Config: videoVolume = {}", g_config.videoVolume);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "videofullscreen")
        {
            g_config.videoFullscreen = (value == "1" || value == "true");
            logger::info("Config: videoFullscreen = {}", g_config.videoFullscreen);
        }
        else if (keyLower == "logsvisible")
        {
            g_config.logsVisible = (value == "1" || value == "true");
            logger::info("Config: logsVisible = {}", g_config.logsVisible);
        }
        else if (keyLower == "logsposx")
        {
            try
            {
                g_config.logsPosX = std::stoi(value);
                g_config.logsPosX = std::max(0, std::min(100, g_config.logsPosX));
                logger::info("Config: logsPosX = {}", g_config.logsPosX);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "logsposy")
        {
            try
            {
                g_config.logsPosY = std::stoi(value);
                g_config.logsPosY = std::max(0, std::min(100, g_config.logsPosY));
                logger::info("Config: logsPosY = {}", g_config.logsPosY);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "logswidth")
        {
            try
            {
                g_config.logsWidth = std::stoi(value);
                g_config.logsWidth = std::max(300, g_config.logsWidth);
                logger::info("Config: logsWidth = {}", g_config.logsWidth);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "logsheight")
        {
            try
            {
                g_config.logsHeight = std::stoi(value);
                g_config.logsHeight = std::max(150, g_config.logsHeight);
                logger::info("Config: logsHeight = {}", g_config.logsHeight);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "logsfontsize")
        {
            try
            {
                g_config.logsFontSize = std::stoi(value);
                g_config.logsFontSize = std::max(8, std::min(18, g_config.logsFontSize));
                logger::info("Config: logsFontSize = {}", g_config.logsFontSize);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "logsautorefresh")
        {
            g_config.logsAutoRefresh = (value == "1" || value == "true");
            logger::info("Config: logsAutoRefresh = {}", g_config.logsAutoRefresh);
        }
        else if (keyLower == "livevisible")
        {
            g_config.liveVisible = (value == "1" || value == "true");
            logger::info("Config: liveVisible = {}", g_config.liveVisible);
        }
        else if (keyLower == "liveposx")
        {
            try
            {
                g_config.livePosX = std::stoi(value);
                g_config.livePosX = std::max(0, std::min(100, g_config.livePosX));
                logger::info("Config: livePosX = {}", g_config.livePosX);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "liveposy")
        {
            try
            {
                g_config.livePosY = std::stoi(value);
                g_config.livePosY = std::max(0, std::min(100, g_config.livePosY));
                logger::info("Config: livePosY = {}", g_config.livePosY);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "globalalpha")
        {
            try
            {
                g_config.globalAlpha = std::stoi(value);
                g_config.globalAlpha = std::max(20, std::min(100, g_config.globalAlpha));
                logger::info("Config: globalAlpha = {}", g_config.globalAlpha);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "globalcolor")
        {
            g_config.globalColor = value;
            logger::info("Config: globalColor = {}", g_config.globalColor);
        }
        else if (keyLower == "livebackground")
        {
            g_config.liveBackground = value;
            logger::info("Config: liveBackground = {}", g_config.liveBackground);
        }
        else if (keyLower == "menussize")
        {
            try
            {
                g_config.menusSize = std::stoi(value);
                g_config.menusSize = std::max(50, std::min(200, g_config.menusSize));
                logger::info("Config: menusSize = {}", g_config.menusSize);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "itemssize")
        {
            try
            {
                g_config.itemsSize = std::stoi(value);
                g_config.itemsSize = std::max(50, std::min(200, g_config.itemsSize));
                logger::info("Config: itemsSize = {}", g_config.itemsSize);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "toastsize")
        {
            try
            {
                g_config.toastSize = std::stoi(value);
                g_config.toastSize = std::max(10, std::min(24, g_config.toastSize));
                logger::info("Config: toastSize = {}", g_config.toastSize);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "livesize")
        {
            try
            {
                g_config.liveSize = std::stoi(value);
                g_config.liveSize = std::max(50, std::min(200, g_config.liveSize));
                logger::info("Config: liveSize = {}", g_config.liveSize);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "liveautostart")
        {
            g_config.liveAutoStart = (value == "true" || value == "1");
            logger::info("Config: liveAutoStart = {}", g_config.liveAutoStart);
        }
        else if (keyLower == "livenowplayingmode")
        {
            g_config.liveNowPlayingMode = value;
            logger::info("Config: liveNowPlayingMode = {}", g_config.liveNowPlayingMode);
        }
        else if (keyLower == "livenowplayingseconds")
        {
            try
            {
                g_config.liveNowPlayingSeconds = std::stoi(value);
                g_config.liveNowPlayingSeconds = std::max(3, std::min(120, g_config.liveNowPlayingSeconds));
                logger::info("Config: liveNowPlayingSeconds = {}", g_config.liveNowPlayingSeconds);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "livenowplayinggif")
        {
            g_config.liveNowPlayingGif = value;
            logger::info("Config: liveNowPlayingGif = {}", g_config.liveNowPlayingGif);
        }
        else if (keyLower == "bootposx")
        {
            try
            {
                g_config.bootPosX = std::stoi(value);
                g_config.bootPosX = std::max(0, std::min(100, g_config.bootPosX));
                logger::info("Config: bootPosX = {}", g_config.bootPosX);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "bootposy")
        {
            try
            {
                g_config.bootPosY = std::stoi(value);
                g_config.bootPosY = std::max(0, std::min(100, g_config.bootPosY));
                logger::info("Config: bootPosY = {}", g_config.bootPosY);
            }
            catch (...)
            {
            }
        }
        else if (keyLower == "hubvisiblesettings")
        {
            g_config.hubVisibleSettings = (value == "1" || value == "true");
            logger::info("Config: hubVisibleSettings = {}", g_config.hubVisibleSettings);
        }
        else if (keyLower == "proximitysettings")
        {
            g_config.proximitySettings = (value == "1" || value == "true");
            logger::info("Config: proximitySettings = {}", g_config.proximitySettings);
        }
    }
    file.close();

    
    logger::info("========================================");
    logger::info("Configuration loaded successfully from: {}", configPath);
    logger::info("firstKey = 0x{:X}, secondKey = 0x{:X}", g_config.firstKey, g_config.secondKey);
    logger::info("singleKeyMode = {}, volume = {}", g_config.singleKeyMode, g_config.volume);
    logger::info("posX = {}, posY = {}, hubSize = {}", g_config.posX, g_config.posY, g_config.hubSize);
    logger::info("muteHubSound = {}, hubSound = {}", g_config.muteHubSound, g_config.hubSound);
    logger::info("gifVisible = {}, gifPosX = {}, gifPosY = {}, gifZoom = {}",
                 g_config.gifVisible, g_config.gifPosX, g_config.gifPosY, g_config.gifZoom);
    logger::info("gifWidth = {}, gifHeight = {}", g_config.gifWidth, g_config.gifHeight);
    logger::info("videoVisible = {}, videoPosX = {}, videoPosY = {}",
                 g_config.videoVisible, g_config.videoPosX, g_config.videoPosY);
    logger::info("videoWidth = {}, videoHeight = {}", g_config.videoWidth, g_config.videoHeight);
    logger::info("logsVisible = {}, logsPosX = {}, logsPosY = {}, logsWidth = {}, logsHeight = {}",
                 g_config.logsVisible, g_config.logsPosX, g_config.logsPosY, g_config.logsWidth, g_config.logsHeight);
    logger::info("logsFontSize = {}, logsAutoRefresh = {}", g_config.logsFontSize, g_config.logsAutoRefresh);
    logger::info("liveVisible = {}, livePosX = {}, livePosY = {}",
                 g_config.liveVisible, g_config.livePosX, g_config.livePosY);
    logger::info("========================================");

    
    
}

void SaveConfig()
{
    std::string configPath = GetConfigPath();

    
    std::vector<std::string> lines;
    std::ifstream inFile(configPath);
    bool fileExists = inFile.is_open();

    if (fileExists)
    {
        std::string line;
        while (std::getline(inFile, line))
        {
            lines.push_back(line);
        }
        inFile.close();
    }

    
    std::ofstream outFile(configPath);
    if (!outFile.is_open())
    {
        logger::error("Failed to save config: {}", configPath);
        return;
    }

    
    std::map<std::string, bool> keysWritten;

    
    std::string currentSection = "";

    if (fileExists)
    {
        for (const auto &line : lines)
        {
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

            
            if (!trimmed.empty() && trimmed[0] == '[')
            {
                size_t endBracket = trimmed.find(']');
                if (endBracket != std::string::npos)
                {
                    currentSection = trimmed.substr(1, endBracket - 1);
                }
                outFile << line << "\n";
                continue;
            }

            
            size_t pos = trimmed.find('=');
            if (pos != std::string::npos && trimmed[0] != ';' && trimmed[0] != '#')
            {
                std::string key = trimmed.substr(0, pos);
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);

                
                if (currentSection == "Activation")
                {
                    if (key == "FirstKey")
                    {
                        outFile << "FirstKey = 0x" << std::hex << g_config.firstKey << std::dec << "\n";
                        keysWritten["FirstKey"] = true;
                    }
                    else if (key == "SecondKey")
                    {
                        outFile << "SecondKey = 0x" << std::hex << g_config.secondKey << std::dec << "\n";
                        keysWritten["SecondKey"] = true;
                    }
                    else if (key == "SingleKeyMode")
                    {
                        outFile << "SingleKeyMode = " << (g_config.singleKeyMode ? "true" : "false") << "\n";
                        keysWritten["SingleKeyMode"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "Audio")
                {
                    if (key == "Volume")
                    {
                        outFile << "Volume = " << g_config.volume << "\n";
                        keysWritten["Volume"] = true;
                    }
                    else if (key == "MuteHubSound")
                    {
                        outFile << "MuteHubSound = " << (g_config.muteHubSound ? "true" : "false") << "\n";
                        keysWritten["MuteHubSound"] = true;
                    }
                    else if (key == "HubSound")
                    {
                        outFile << "HubSound = " << g_config.hubSound << "\n";
                        keysWritten["HubSound"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "Position")
                {
                    if (key == "PosX")
                    {
                        outFile << "PosX = " << g_config.posX << "\n";
                        keysWritten["PosX"] = true;
                    }
                    else if (key == "PosY")
                    {
                        outFile << "PosY = " << g_config.posY << "\n";
                        keysWritten["PosY"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "Display")
                {
                    if (key == "HubSize")
                    {
                        outFile << "HubSize = " << g_config.hubSize << "\n";
                        keysWritten["HubSize"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "PanelGif")
                {
                    if (key == "gifVisible")
                    {
                        outFile << "gifVisible = " << (g_config.gifVisible ? "true" : "false") << "\n";
                        keysWritten["GifVisible"] = true;
                    }
                    else if (key == "gifPosX")
                    {
                        outFile << "gifPosX = " << g_config.gifPosX << "\n";
                        keysWritten["GifPosX"] = true;
                    }
                    else if (key == "gifPosY")
                    {
                        outFile << "gifPosY = " << g_config.gifPosY << "\n";
                        keysWritten["GifPosY"] = true;
                    }
                    else if (key == "gifZoom")
                    {
                        outFile << "gifZoom = " << g_config.gifZoom << "\n";
                        keysWritten["GifZoom"] = true;
                    }
                    else if (key == "gifWidth")
                    {
                        outFile << "gifWidth = " << g_config.gifWidth << "\n";
                        keysWritten["GifWidth"] = true;
                    }
                    else if (key == "gifHeight")
                    {
                        outFile << "gifHeight = " << g_config.gifHeight << "\n";
                        keysWritten["GifHeight"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "PanelVideo")
                {
                    if (key == "videoVisible")
                    {
                        outFile << "videoVisible = " << (g_config.videoVisible ? "true" : "false") << "\n";
                        keysWritten["VideoVisible"] = true;
                    }
                    else if (key == "videoPosX")
                    {
                        outFile << "videoPosX = " << g_config.videoPosX << "\n";
                        keysWritten["VideoPosX"] = true;
                    }
                    else if (key == "videoPosY")
                    {
                        outFile << "videoPosY = " << g_config.videoPosY << "\n";
                        keysWritten["VideoPosY"] = true;
                    }
                    else if (key == "videoWidth")
                    {
                        outFile << "videoWidth = " << g_config.videoWidth << "\n";
                        keysWritten["VideoWidth"] = true;
                    }
                    else if (key == "videoHeight")
                    {
                        outFile << "videoHeight = " << g_config.videoHeight << "\n";
                        keysWritten["VideoHeight"] = true;
                    }
                    else if (key == "videoOpacity")
                    {
                        outFile << "videoOpacity = " << g_config.videoOpacity << "\n";
                        keysWritten["VideoOpacity"] = true;
                    }
                    else if (key == "videoVolume")
                    {
                        outFile << "videoVolume = " << g_config.videoVolume << "\n";
                        keysWritten["VideoVolume"] = true;
                    }
                    else if (key == "videoFullscreen")
                    {
                        outFile << "videoFullscreen = " << (g_config.videoFullscreen ? "true" : "false") << "\n";
                        keysWritten["VideoFullscreen"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "PanelLogs")
                {
                    if (key == "logsVisible")
                    {
                        outFile << "logsVisible = " << (g_config.logsVisible ? "true" : "false") << "\n";
                        keysWritten["LogsVisible"] = true;
                    }
                    else if (key == "logsPosX")
                    {
                        outFile << "logsPosX = " << g_config.logsPosX << "\n";
                        keysWritten["LogsPosX"] = true;
                    }
                    else if (key == "logsPosY")
                    {
                        outFile << "logsPosY = " << g_config.logsPosY << "\n";
                        keysWritten["LogsPosY"] = true;
                    }
                    else if (key == "logsWidth")
                    {
                        outFile << "logsWidth = " << g_config.logsWidth << "\n";
                        keysWritten["LogsWidth"] = true;
                    }
                    else if (key == "logsHeight")
                    {
                        outFile << "logsHeight = " << g_config.logsHeight << "\n";
                        keysWritten["LogsHeight"] = true;
                    }
                    else if (key == "logsFontSize")
                    {
                        outFile << "logsFontSize = " << g_config.logsFontSize << "\n";
                        keysWritten["LogsFontSize"] = true;
                    }
                    else if (key == "logsAutoRefresh")
                    {
                        outFile << "logsAutoRefresh = " << (g_config.logsAutoRefresh ? "true" : "false") << "\n";
                        keysWritten["LogsAutoRefresh"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "PanelLive")
                {
                    if (key == "liveVisible")
                    {
                        outFile << "liveVisible = " << (g_config.liveVisible ? "true" : "false") << "\n";
                        keysWritten["LiveVisible"] = true;
                    }
                    else if (key == "livePosX")
                    {
                        outFile << "livePosX = " << g_config.livePosX << "\n";
                        keysWritten["LivePosX"] = true;
                    }
                    else if (key == "livePosY")
                    {
                        outFile << "livePosY = " << g_config.livePosY << "\n";
                        keysWritten["LivePosY"] = true;
                    }
                    else if (key == "liveSize")
                    {
                        outFile << "liveSize = " << g_config.liveSize << "\n";
                        keysWritten["LiveSize"] = true;
                    }
                    else if (key == "liveAutoStart")
                    {
                        outFile << "liveAutoStart = " << (g_config.liveAutoStart ? "true" : "false") << "\n";
                        keysWritten["LiveAutoStart"] = true;
                    }
                    else if (key == "liveNowPlayingMode")
                    {
                        outFile << "liveNowPlayingMode = " << g_config.liveNowPlayingMode << "\n";
                        keysWritten["LiveNowPlayingMode"] = true;
                    }
                    else if (key == "liveNowPlayingSeconds")
                    {
                        outFile << "liveNowPlayingSeconds = " << g_config.liveNowPlayingSeconds << "\n";
                        keysWritten["LiveNowPlayingSeconds"] = true;
                    }
                    else if (key == "liveNowPlayingGif")
                    {
                        outFile << "liveNowPlayingGif = " << g_config.liveNowPlayingGif << "\n";
                        keysWritten["LiveNowPlayingGif"] = true;
                    }
                    else if (key == "bootPosX")
                    {
                        outFile << "bootPosX = " << g_config.bootPosX << "\n";
                        keysWritten["BootPosX"] = true;
                    }
                    else if (key == "bootPosY")
                    {
                        outFile << "bootPosY = " << g_config.bootPosY << "\n";
                        keysWritten["BootPosY"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "GlobalStyle")
                {
                    if (key == "globalAlpha")
                    {
                        outFile << "globalAlpha = " << g_config.globalAlpha << "\n";
                        keysWritten["GlobalAlpha"] = true;
                    }
                    else if (key == "globalColor")
                    {
                        outFile << "globalColor = " << g_config.globalColor << "\n";
                        keysWritten["GlobalColor"] = true;
                    }
                    else if (key == "liveBackground")
                    {
                        outFile << "liveBackground = " << g_config.liveBackground << "\n";
                        keysWritten["LiveBackground"] = true;
                    }
                    else if (key == "menusSize")
                    {
                        outFile << "menusSize = " << g_config.menusSize << "\n";
                        keysWritten["MenusSize"] = true;
                    }
                    else if (key == "itemsSize")
                    {
                        outFile << "itemsSize = " << g_config.itemsSize << "\n";
                        keysWritten["ItemsSize"] = true;
                    }
                    else if (key == "toastSize")
                    {
                        outFile << "toastSize = " << g_config.toastSize << "\n";
                        keysWritten["ToastSize"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else if (currentSection == "Settings-js")
                {
                    if (key == "hubVisibleSettings")
                    {
                        outFile << "hubVisibleSettings = " << (g_config.hubVisibleSettings ? "true" : "false") << "\n";
                        keysWritten["HubVisibleSettings"] = true;
                    }
                    else if (key == "proximitySettings")
                    {
                        outFile << "proximitySettings = " << (g_config.proximitySettings ? "true" : "false") << "\n";
                        keysWritten["ProximitySettings"] = true;
                    }
                    else
                    {
                        outFile << line << "\n";
                    }
                }
                else
                {
                    
                    outFile << line << "\n";
                }
            }
            else
            {
                
                outFile << line << "\n";
            }
        }
    }

    
    if (!fileExists)
    {
        outFile << "[Activation]\n";
        outFile << "; First Button - The key you HOLD before pressing Second Button\n";
        outFile << "; Scan codes: 0x2A = Left Shift, 0x36 = Right Shift\n";
        outFile << ";             0x1D = Left Ctrl, 0x1C = Right Ctrl, 0x38 = Left Alt\n";
        outFile << "FirstKey = 0x" << std::hex << g_config.firstKey << std::dec << "\n";
        outFile << "\n";
        outFile << "; Second Button - The key you PRESS while holding First Button\n";
        outFile << "; Scan codes: 0x1E = A, 0x30 = B, 0x2E = C, 0x20 = D, 0x12 = E\n";
        outFile << ";             0x21 = F, 0x22 = G, 0x23 = H, 0x17 = I, 0x24 = J\n";
        outFile << ";             0x25 = K, 0x26 = L, 0x32 = M, 0x31 = N, 0x18 = O\n";
        outFile << ";             0x19 = P, 0x10 = Q, 0x13 = R, 0x1F = S, 0x14 = T\n";
        outFile << ";             0x16 = U, 0x2F = V, 0x11 = W, 0x2D = X, 0x15 = Y, 0x2C = Z\n";
        outFile << "SecondKey = 0x" << std::hex << g_config.secondKey << std::dec << "\n";
        outFile << "\n";
        outFile << "; Single Key Mode - Activate hub with just Second Button\n";
        outFile << "; Set to true to use only SecondKey, false to require FirstKey+SecondKey\n";
        outFile << "SingleKeyMode = " << (g_config.singleKeyMode ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "[Audio]\n";
        outFile << "; Volume for hub sounds (bell, swoosh, heal, open/close)\n";
        outFile << "; Range: 0 to 100 (0 = mute, 100 = max)\n";
        outFile << "Volume = " << g_config.volume << "\n";
        outFile << "\n";
        outFile << "; Mute hub sound (open/close)\n";
        outFile << "; Set to true to mute the hub sound, false to hear it\n";
        outFile << "MuteHubSound = " << (g_config.muteHubSound ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "; Hub sound file (miau-PDA.wav, swoosh-07.wav, or metal-pipe.wav)\n";
        outFile << "HubSound = " << g_config.hubSound << "\n";
        outFile << "\n";
        outFile << "[Position]\n";
        outFile << "; Hub screen position (percentage)\n";
        outFile << "; Range: 0 to 100 (50 = center)\n";
        outFile << "; Drag the hub header to move it, position saves automatically\n";
        outFile << "PosX = " << g_config.posX << "\n";
        outFile << "PosY = " << g_config.posY << "\n";
        outFile << "\n";
        outFile << "[Display]\n";
        outFile << "; Hub size multiplier (percentage)\n";
        outFile << "; Range: 50 to 200 (100 = normal size)\n";
        outFile << "; Use +/- buttons in hub to adjust\n";
        outFile << "HubSize = " << g_config.hubSize << "\n";
        outFile << "\n";
        outFile << "[PanelGif]\n";
        outFile << "; GIF Panel visibility\n";
        outFile << "; Set to true to show on startup, false to hide\n";
        outFile << "gifVisible = " << (g_config.gifVisible ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "; GIF Panel screen position (percentage)\n";
        outFile << "; Range: 0 to 100\n";
        outFile << "gifPosX = " << g_config.gifPosX << "\n";
        outFile << "gifPosY = " << g_config.gifPosY << "\n";
        outFile << "\n";
        outFile << "; GIF zoom multiplier (percentage)\n";
        outFile << "; Range: 50 to 200 (100 = normal size)\n";
        outFile << "gifZoom = " << g_config.gifZoom << "\n";
        outFile << "\n";
        outFile << "; GIF Panel size (pixels)\n";
        outFile << "gifWidth = " << g_config.gifWidth << "\n";
        outFile << "gifHeight = " << g_config.gifHeight << "\n";
        outFile << "\n";
        outFile << "[PanelVideo]\n";
        outFile << "; Video Panel visibility\n";
        outFile << "; Set to true to show on startup, false to hide\n";
        outFile << "videoVisible = " << (g_config.videoVisible ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "; Video Panel screen position (percentage)\n";
        outFile << "; Range: 0 to 100\n";
        outFile << "videoPosX = " << g_config.videoPosX << "\n";
        outFile << "videoPosY = " << g_config.videoPosY << "\n";
        outFile << "\n";
        outFile << "; Video Panel size (pixels)\n";
        outFile << "videoWidth = " << g_config.videoWidth << "\n";
        outFile << "videoHeight = " << g_config.videoHeight << "\n";
        outFile << "\n";
        outFile << "; Video panel opacity (20-100%)\n";
        outFile << "videoOpacity = " << g_config.videoOpacity << "\n";
        outFile << "\n";
        outFile << "; Video audio volume (0-100%)\n";
        outFile << "videoVolume = " << g_config.videoVolume << "\n";
        outFile << "\n";
        outFile << "; Video fullscreen state\n";
        outFile << "videoFullscreen = " << (g_config.videoFullscreen ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "[PanelLogs]\n";
        outFile << "; Logs Panel visibility\n";
        outFile << "logsVisible = " << (g_config.logsVisible ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "; Logs Panel screen position (percentage)\n";
        outFile << "logsPosX = " << g_config.logsPosX << "\n";
        outFile << "logsPosY = " << g_config.logsPosY << "\n";
        outFile << "\n";
        outFile << "; Logs Panel size (pixels)\n";
        outFile << "logsWidth = " << g_config.logsWidth << "\n";
        outFile << "logsHeight = " << g_config.logsHeight << "\n";
        outFile << "\n";
        outFile << "; Logs font size (8-18)\n";
        outFile << "logsFontSize = " << g_config.logsFontSize << "\n";
        outFile << "\n";
        outFile << "; Logs auto-refresh (every 2 seconds)\n";
        outFile << "logsAutoRefresh = " << (g_config.logsAutoRefresh ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "[PanelLive]\n";
        outFile << "; Live Stats Panel visibility\n";
        outFile << "liveVisible = " << (g_config.liveVisible ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "; Live Panel screen position (percentage)\n";
        outFile << "livePosX = " << g_config.livePosX << "\n";
        outFile << "livePosY = " << g_config.livePosY << "\n";
        outFile << "\n";
        outFile << "; Live panel size multiplier (50-200%)\n";
        outFile << "liveSize = " << g_config.liveSize << "\n";
        outFile << "[GlobalStyle]\n";
        outFile << "; Background opacity (20-100%)\n";
        outFile << "globalAlpha = " << g_config.globalAlpha << "\n";
        outFile << "\n";
        outFile << "; Color theme (default, gold, dark, blood, nature)\n";
        outFile << "globalColor = " << g_config.globalColor << "\n";
        outFile << "\n";
        outFile << "; Panel Live background image\n";
        outFile << "liveBackground = " << g_config.liveBackground << "\n";
        outFile << "\n";
        outFile << "; Menu Tracking submenu scale (50-200%)\n";
        outFile << "menusSize = " << g_config.menusSize << "\n";
        outFile << "\n";
        outFile << "; Items Config submenu scale (50-200%)\n";
        outFile << "itemsSize = " << g_config.itemsSize << "\n";
        outFile << "\n";
        outFile << "; Notification toast font-size (10-24px)\n";
        outFile << "toastSize = " << g_config.toastSize << "\n";
        outFile << "\n";
        outFile << "[Settings-js]\n";
        outFile << "; Hub Toggle Settings (from hub-toggle-panel.js)\n";
        outFile << "; true = Hub responds to hotkey, false = Hub locked\n";
        outFile << "hubVisibleSettings = " << (g_config.hubVisibleSettings ? "true" : "false") << "\n";
        outFile << "\n";
        outFile << "; Balls proximity mode\n";
        outFile << "; true = Balls hide on cursor near, false = always visible\n";
        outFile << "proximitySettings = " << (g_config.proximitySettings ? "true" : "false") << "\n";
    }

    outFile.close();
    logger::info("Config saved to: {}", configPath);

    
    std::string backupPath = GetBackupConfigPath();
    fs::path backupDir = fs::path(backupPath).parent_path();
    try
    {
        if (!fs::exists(backupDir))
        {
            fs::create_directories(backupDir);
        }
        fs::copy_file(configPath, backupPath, fs::copy_options::overwrite_existing);
        logger::info("Backup saved to: {}", backupPath);
    }
    catch (const std::exception &e)
    {
        logger::error("Failed to save backup: {}", e.what());
    }
}

std::string SafeWideStringToString(const std::wstring &wstr)
{
    if (wstr.empty())
        return std::string();
    try
    {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size_needed <= 0)
        {
            size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
            if (size_needed <= 0)
                return std::string();
            std::string result(size_needed, 0);
            int converted = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
            if (converted <= 0)
                return std::string();
            return result;
        }
        std::string result(size_needed, 0);
        int converted = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
        if (converted <= 0)
            return std::string();
        return result;
    }
    catch (...)
    {
        std::string result;
        result.reserve(wstr.size());
        for (wchar_t wc : wstr)
        {
            if (wc <= 127)
            {
                result.push_back(static_cast<char>(wc));
            }
            else
            {
                result.push_back('?');
            }
        }
        return result;
    }
}

std::string GetEnvVar(const std::string &key)
{
    char *buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, key.c_str()) == 0 && buf != nullptr)
    {
        std::string value(buf);
        free(buf);
        return value;
    }
    return "";
}

std::string NormalizeName(const std::string &name)
{
    std::string normalized = name;
    normalized.erase(0, normalized.find_first_not_of(" \t\r\n"));
    normalized.erase(normalized.find_last_not_of(" \t\r\n") + 1);
    return normalized;
}

std::string GetCurrentTimeString()
{
    auto now = std::chrono::system_clock::now();
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);
    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string GetCurrentTimeStringWithMillis()
{
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

std::string GetDocumentsPath()
{
    try
    {
        wchar_t path[MAX_PATH] = {0};
        HRESULT result = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, path);
        if (SUCCEEDED(result))
        {
            std::wstring ws(path);
            std::string converted = SafeWideStringToString(ws);
            if (!converted.empty())
            {
                return converted;
            }
        }
        std::string userProfile = GetEnvVar("USERPROFILE");
        if (!userProfile.empty())
        {
            return userProfile + "\\Documents";
        }
        return "C:\\Users\\Default\\Documents";
    }
    catch (...)
    {
        return "C:\\Users\\Default\\Documents";
    }
}

void CreateDirectoryIfNotExists(const fs::path &path)
{
    try
    {
        if (!fs::exists(path))
        {
            fs::create_directories(path);
        }
    }
    catch (...)
    {
    }
}

struct DualLogger
{
    std::ofstream primaryLog;
    std::ofstream secondaryLog;
    std::ofstream assetsLog;
    bool secondaryEnabled = false;
    bool assetsEnabled = false;
    std::mutex logMutex;

    DualLogger(const fs::path &primaryPath, const fs::path &secondaryPath, const fs::path &assetsPath)
    {
        try
        {
            CreateDirectoryIfNotExists(primaryPath.parent_path());
            primaryLog.open(primaryPath, std::ios::out | std::ios::trunc);

            try
            {
                CreateDirectoryIfNotExists(secondaryPath.parent_path());
                secondaryLog.open(secondaryPath, std::ios::out | std::ios::trunc);
                secondaryEnabled = secondaryLog.is_open();
            }
            catch (...)
            {
                secondaryEnabled = false;
            }

            try
            {
                CreateDirectoryIfNotExists(assetsPath.parent_path());
                assetsLog.open(assetsPath, std::ios::out | std::ios::trunc);
                assetsEnabled = assetsLog.is_open();
            }
            catch (...)
            {
                assetsEnabled = false;
            }
        }
        catch (...)
        {
        }
    }

    ~DualLogger()
    {
        if (primaryLog.is_open())
            primaryLog.close();
        if (secondaryLog.is_open())
            secondaryLog.close();
        if (assetsLog.is_open())
            assetsLog.close();
    }

    void write(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(logMutex);

        std::string timestamped = "[" + GetCurrentTimeStringWithMillis() + "] " + message;

        if (primaryLog.is_open())
        {
            primaryLog << timestamped << std::endl;
            primaryLog.flush();
        }
        if (secondaryEnabled && secondaryLog.is_open())
        {
            secondaryLog << timestamped << std::endl;
            secondaryLog.flush();
        }
        if (assetsEnabled && assetsLog.is_open())
        {
            assetsLog << timestamped << std::endl;
            assetsLog.flush();
        }
    }

    bool isOpen() const
    {
        return primaryLog.is_open();
    }
};

static DualLogger *g_dualLogger = nullptr;

void LogToAll(const std::string &message)
{
    logger::info("{}", message);
    if (g_dualLogger && g_dualLogger->isOpen())
    {
        g_dualLogger->write(message);
    }
}

static std::ofstream g_menusLog;
static std::deque<std::string> g_menusLogLines;
static std::mutex g_menusLogMutex;

std::string GetGamePath();

void WriteToMenusLog(const std::string &message)
{
    std::lock_guard<std::mutex> lock(g_menusLogMutex);

    std::string docsPath = GetDocumentsPath();
    std::string gamePath = GetGamePath();

    
    std::vector<fs::path> logPaths = {
        
        fs::path(docsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" / "OSoundtracks-Prisma-Menus.log",
        
        fs::path(docsPath) / "My Games" / "Skyrim.INI" / "SKSE" / "OSoundtracks-Prisma-Menus.log",
        
        fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Logs" / "OSoundtracks-Prisma-Menus.log"};

    
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);

    std::stringstream ss;
    ss << "[" << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << message;

    std::string newLine = ss.str();
    g_menusLogLines.push_back(newLine);

    
    if (g_menusLogLines.size() > 200)
    {
        g_menusLogLines.pop_front();
    }

    
    for (const auto &logPath : logPaths)
    {
        try
        {
            fs::path dirPath = logPath.parent_path();
            if (!fs::exists(dirPath))
            {
                fs::create_directories(dirPath);
            }
            std::ofstream logFile(logPath, std::ios::trunc);
            if (logFile.is_open())
            {
                for (const auto &line : g_menusLogLines)
                {
                    logFile << line << std::endl;
                }
                logFile.close();
            }
        }
        catch (...)
        {
        }
    }
}

bool IsValidPluginPath(const fs::path &pluginPath)
{
    const std::vector<std::string> dllNames = {
        "OSoundtracks-Prisma.dll"};

    for (const auto &dllName : dllNames)
    {
        fs::path dllPath = pluginPath / dllName;
        try
        {
            if (fs::exists(dllPath))
            {
                return true;
            }
        }
        catch (...)
        {
            continue;
        }
    }
    return false;
}

fs::path BuildPathCaseInsensitive(const fs::path &basePath, const std::vector<std::string> &components)
{
    try
    {
        fs::path currentPath = basePath;

        for (const auto &component : components)
        {
            fs::path testPath = currentPath / component;
            if (fs::exists(testPath))
            {
                currentPath = testPath;
                continue;
            }

            std::string lowerComponent = component;
            std::transform(lowerComponent.begin(), lowerComponent.end(),
                           lowerComponent.begin(), ::tolower);
            testPath = currentPath / lowerComponent;
            if (fs::exists(testPath))
            {
                currentPath = testPath;
                continue;
            }

            std::string upperComponent = component;
            std::transform(upperComponent.begin(), upperComponent.end(),
                           upperComponent.begin(), ::toupper);
            testPath = currentPath / upperComponent;
            if (fs::exists(testPath))
            {
                currentPath = testPath;
                continue;
            }

            bool found = false;
            if (fs::exists(currentPath) && fs::is_directory(currentPath))
            {
                for (const auto &entry : fs::directory_iterator(currentPath))
                {
                    try
                    {
                        std::string entryName = entry.path().filename().string();
                        std::string lowerEntryName = entryName;
                        std::transform(lowerEntryName.begin(), lowerEntryName.end(),
                                       lowerEntryName.begin(), ::tolower);

                        if (lowerEntryName == lowerComponent)
                        {
                            currentPath = entry.path();
                            found = true;
                            break;
                        }
                    }
                    catch (...)
                    {
                        continue;
                    }
                }
            }

            if (!found)
            {
                currentPath = currentPath / component;
            }
        }

        return currentPath;
    }
    catch (...)
    {
        return basePath;
    }
}

fs::path GetPluginDllPath()
{
    try
    {
        HMODULE hModule = nullptr;
        static int dummyVariable = 0;

        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&dummyVariable),
                &hModule) &&
            hModule != nullptr)
        {

            wchar_t dllPath[MAX_PATH] = {0};
            DWORD size = GetModuleFileNameW(hModule, dllPath, MAX_PATH);

            if (size > 0)
            {
                std::wstring wsDllPath(dllPath);
                std::string dllPathStr = SafeWideStringToString(wsDllPath);

                if (!dllPathStr.empty())
                {
                    fs::path dllDir = fs::path(dllPathStr).parent_path();
                    return dllDir;
                }
            }
        }

        return fs::path();
    }
    catch (...)
    {
        return fs::path();
    }
}

std::string GetGamePath()
{
    try
    {
        
        std::string mo2Path = GetEnvVar("MO2_MODS_PATH");
        if (!mo2Path.empty())
        {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(mo2Path), {"Data", "SKSE", "Plugins"});
            if (IsValidPluginPath(testPath))
            {
                logger::info("Game path detected: MO2 Environment Variable");
                return mo2Path;
            }
        }

        
        std::string mo2Overwrite = GetEnvVar("MO_OVERWRITE_PATH");
        if (!mo2Overwrite.empty())
        {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(mo2Overwrite), {"SKSE", "Plugins"});
            if (IsValidPluginPath(testPath))
            {
                logger::info("Game path detected: MO2 Overwrite Path");
                return mo2Overwrite;
            }
        }

        
        std::string vortexPath = GetEnvVar("VORTEX_MODS_PATH");
        if (!vortexPath.empty())
        {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(vortexPath), {"Data", "SKSE", "Plugins"});
            if (IsValidPluginPath(testPath))
            {
                logger::info("Game path detected: Vortex Environment Variable");
                return vortexPath;
            }
        }

        
        std::string skyrimMods = GetEnvVar("SKYRIM_MODS_FOLDER");
        if (!skyrimMods.empty())
        {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(skyrimMods), {"Data", "SKSE", "Plugins"});
            if (IsValidPluginPath(testPath))
            {
                logger::info("Game path detected: SKYRIM_MODS_FOLDER Variable");
                return skyrimMods;
            }
        }

        
        std::vector<std::string> registryKeys = {
            "SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Skyrim Special Edition",
            "SOFTWARE\\Bethesda Softworks\\Skyrim Special Edition",
            "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1457087920",
            "SOFTWARE\\GOG.com\\Games\\1457087920",
            "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\489830",
            "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\611670"};

        HKEY hKey;
        char pathBuffer[MAX_PATH] = {0};
        DWORD pathSize = sizeof(pathBuffer);

        for (const auto &key : registryKeys)
        {
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                if (RegQueryValueExA(hKey, "Installed Path", NULL, NULL, (LPBYTE)pathBuffer, &pathSize) == ERROR_SUCCESS)
                {
                    RegCloseKey(hKey);
                    std::string result(pathBuffer);
                    if (!result.empty())
                    {
                        fs::path testPath = BuildPathCaseInsensitive(
                            fs::path(result), {"Data", "SKSE", "Plugins"});
                        if (IsValidPluginPath(testPath))
                        {
                            logger::info("Game path detected: Windows Registry");
                            return result;
                        }
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
            "D:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "E:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "E:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "F:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "F:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "G:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "C:\\GOG Games\\Skyrim Special Edition",
            "D:\\GOG Games\\Skyrim Special Edition",
            "E:\\GOG Games\\Skyrim Special Edition",
            "C:\\Games\\Skyrim Special Edition",
            "D:\\Games\\Skyrim Special Edition"};

        for (const auto &pathCandidate : commonPaths)
        {
            try
            {
                if (fs::exists(pathCandidate) && fs::is_directory(pathCandidate))
                {
                    fs::path testPath = BuildPathCaseInsensitive(
                        fs::path(pathCandidate), {"Data", "SKSE", "Plugins"});
                    if (IsValidPluginPath(testPath))
                    {
                        logger::info("Game path detected: Common Installation Path");
                        return pathCandidate;
                    }
                }
            }
            catch (...)
            {
                continue;
            }
        }

        
        logger::info("Attempting DLL Directory Detection (Wabbajack/MO2/Portable fallback)...");
        fs::path dllDir = GetPluginDllPath();

        if (!dllDir.empty())
        {
            if (IsValidPluginPath(dllDir))
            {
                fs::path calculatedGamePath = dllDir.parent_path().parent_path().parent_path();
                logger::info("Game path detected: DLL Directory Method (Wabbajack/Portable)");
                return calculatedGamePath.string();
            }
        }

        logger::warn("No valid game path detected, using default fallback");
        return "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition";
    }
    catch (...)
    {
        return "";
    }
}

// LOGGING SETUP
void SetupLog()
{
    std::string docsPath = GetDocumentsPath();

    
    fs::path primaryLogPath = fs::path(docsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" / "OSoundtracks-Prisma.log";

    
    fs::path secondaryLogPath = fs::path(docsPath) / "My Games" / "Skyrim.INI" / "SKSE" / "OSoundtracks-Prisma.log";

    
    std::string gamePathForLog = GetGamePath();
    fs::path assetsLogPath = fs::path(gamePathForLog) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Logs" / "OSoundtracks-Prisma.log";

    
    fs::path dllDir = GetPluginDllPath();

    
    g_dualLogger = new DualLogger(primaryLogPath, secondaryLogPath, assetsLogPath);

    
    fs::path logDir = fs::path(docsPath) / "My Games" / "Skyrim Special Edition" / "SKSE";
    if (!fs::exists(logDir))
    {
        fs::create_directories(logDir);
    }

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(primaryLogPath.string(), true);
    auto logger = std::make_shared<spdlog::logger>("global log", sink);
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    
    if (g_dualLogger && g_dualLogger->isOpen())
    {
        g_dualLogger->write("========================================");
        g_dualLogger->write("OSoundtracks-Prisma v6.1.0 - Log Initialized");
        g_dualLogger->write("========================================");
        g_dualLogger->write("Primary Log: " + primaryLogPath.string());
        g_dualLogger->write("Secondary Log: " + secondaryLogPath.string());
        g_dualLogger->write("Assets Log: " + assetsLogPath.string());
        g_dualLogger->write("Game Path: " + GetGamePath());
        g_dualLogger->write("Documents Path: " + docsPath);
        g_dualLogger->write("DLL Path: " + dllDir.string());
        g_dualLogger->write("");
    }

    logger::info("OSoundtracks-Prisma Log initialized (3-log system active)");
}

bool InitializeBASSLibrary()
{
    std::lock_guard<std::mutex> lock(g_bassMutex);

    if (g_bassInitialized)
        return true;

    fs::path dllDir = GetPluginDllPath();
    fs::path bassPath;

    if (!dllDir.empty())
    {
        bassPath = dllDir / "DJ_library" / "bass.dll";
    }

    if (bassPath.empty())
    {
        logger::error("Failed to determine bass.dll path");
        return false;
    }

    logger::info("Attempting to load BASS from: {}", bassPath.string());

    g_bassModule = LoadLibraryW(bassPath.wstring().c_str());
    if (!g_bassModule)
    {
        DWORD error = GetLastError();
        logger::error("Failed to load bass.dll from: {} (error: {})", bassPath.string(), error);
        return false;
    }

    pBASS_Init = (BASS_Init_t)GetProcAddress(g_bassModule, "BASS_Init");
    pBASS_Free = (BASS_Free_t)GetProcAddress(g_bassModule, "BASS_Free");
    pBASS_ErrorGetCode = (BASS_ErrorGetCode_t)GetProcAddress(g_bassModule, "BASS_ErrorGetCode");
    pBASS_StreamCreateFile = (BASS_StreamCreateFile_t)GetProcAddress(g_bassModule, "BASS_StreamCreateFile");
    pBASS_ChannelPlay = (BASS_ChannelPlay_t)GetProcAddress(g_bassModule, "BASS_ChannelPlay");
    pBASS_ChannelStop = (BASS_ChannelStop_t)GetProcAddress(g_bassModule, "BASS_ChannelStop");
    pBASS_StreamFree = (BASS_StreamFree_t)GetProcAddress(g_bassModule, "BASS_StreamFree");
    pBASS_ChannelSetAttribute = (BASS_ChannelSetAttribute_t)GetProcAddress(g_bassModule, "BASS_ChannelSetAttribute");
    pBASS_ChannelPause = (BASS_ChannelPause_t)GetProcAddress(g_bassModule, "BASS_ChannelPause");
    pBASS_ChannelSeconds2Bytes = (BASS_ChannelSeconds2Bytes_t)GetProcAddress(g_bassModule, "BASS_ChannelSeconds2Bytes");
    pBASS_ChannelSetPosition = (BASS_ChannelSetPosition_t)GetProcAddress(g_bassModule, "BASS_ChannelSetPosition");
    pBASS_ChannelGetLength = (BASS_ChannelGetLength_t)GetProcAddress(g_bassModule, "BASS_ChannelGetLength");
    pBASS_ChannelBytes2Seconds = (BASS_ChannelBytes2Seconds_t)GetProcAddress(g_bassModule, "BASS_ChannelBytes2Seconds");

    if (!pBASS_Init || !pBASS_StreamCreateFile || !pBASS_ChannelPlay)
    {
        logger::error("Failed to get BASS function pointers");
        FreeLibrary(g_bassModule);
        g_bassModule = nullptr;
        return false;
    }

    if (!pBASS_Init(-1, 44100, 0, nullptr, nullptr))
    {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        if (error != BASS_ERROR_ALREADY)
        {
            logger::error("BASS_Init failed with error: {}", error);
            FreeLibrary(g_bassModule);
            g_bassModule = nullptr;
            return false;
        }
    }

    g_bassInitialized = true;
    logger::info("BASS Audio Library initialized successfully from: {}", bassPath.string());
    return true;
}

void ShutdownBASSLibrary()
{
    std::lock_guard<std::mutex> lock(g_bassMutex);

    if (g_healingStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_healingStream);
        g_healingStream = 0;
    }
    if (g_menuOpenStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_menuOpenStream);
        g_menuOpenStream = 0;
    }
    if (g_menuCloseStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_menuCloseStream);
        g_menuCloseStream = 0;
    }
    if (g_videoAudioStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_videoAudioStream);
        g_videoAudioStream = 0;
    }
    if (g_impactAudioStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_impactAudioStream);
        g_impactAudioStream = 0;
    }
    if (g_jackpotAudioStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_jackpotAudioStream);
        g_jackpotAudioStream = 0;
    }
    if (g_jackpotOutAudioStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_jackpotOutAudioStream);
        g_jackpotOutAudioStream = 0;
    }
    if (g_doomMusicStream)
    {
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_doomMusicStream);
        g_doomMusicStream = 0;
    }

    if (g_bassInitialized && pBASS_Free)
    {
        pBASS_Free();
    }

    if (g_bassModule)
    {
        FreeLibrary(g_bassModule);
        g_bassModule = nullptr;
    }

    g_bassInitialized = false;
    logger::info("BASS Audio Library shutdown complete");
}

fs::path FindSoundFile(const std::string &baseName)
{
    if (baseName.empty())
        return fs::path();

    
    std::string gamePath = GetGamePath();
    fs::path soundsDir;

    if (!gamePath.empty())
    {
        soundsDir = fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Sound";
    }
    else
    {
        
        fs::path dllDir = GetPluginDllPath();
        soundsDir = dllDir / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Sound";
    }

    std::string subFolder;
    std::string fileName = baseName;

    size_t slashPos = baseName.find('/');
    if (slashPos != std::string::npos)
    {
        subFolder = baseName.substr(0, slashPos);
        fileName = baseName.substr(slashPos + 1);
        soundsDir = soundsDir / subFolder;
    }

    std::string nameWithoutExt = fileName;
    std::string originalExt = "";

    size_t lastDot = fileName.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        originalExt = fileName.substr(lastDot);
        nameWithoutExt = fileName.substr(0, lastDot);
    }

    
    fs::path testPath = soundsDir / fileName;
    if (!originalExt.empty() && fs::exists(testPath))
    {
        logger::info("Sound found: {}", testPath.string());
        return testPath;
    }

    
    fs::path wavPath = soundsDir / (nameWithoutExt + ".wav");
    if (fs::exists(wavPath))
    {
        logger::info("Sound found: {}", wavPath.string());
        return wavPath;
    }

    fs::path mp3Path = soundsDir / (nameWithoutExt + ".mp3");
    if (fs::exists(mp3Path))
    {
        logger::info("Sound found: {}", mp3Path.string());
        return mp3Path;
    }

    fs::path oggPath = soundsDir / (nameWithoutExt + ".ogg");
    if (fs::exists(oggPath))
    {
        logger::info("Sound found: {}", oggPath.string());
        return oggPath;
    }

    logger::warn("Sound file not found: {} (searched in: {}, tried .wav, .mp3, .ogg)", baseName, soundsDir.string());
    return fs::path();
}

std::string GetAudioDuration(const fs::path &audioPath)
{
    if (!pBASS_StreamCreateFile || !pBASS_ChannelGetLength || !pBASS_ChannelBytes2Seconds)
    {
        return "00:00";
    }

    if (!fs::exists(audioPath))
    {
        return "00:00";
    }

    
    HSTREAM stream = pBASS_StreamCreateFile(FALSE, audioPath.wstring().c_str(), 0, 0, BASS_UNICODE | BASS_STREAM_DECODE);

    if (!stream)
    {
        return "00:00";
    }

    QWORD bytes = pBASS_ChannelGetLength(stream, BASS_POS_BYTE);
    double seconds = pBASS_ChannelBytes2Seconds(stream, bytes);

    if (pBASS_StreamFree)
    {
        pBASS_StreamFree(stream);
    }

    if (seconds <= 0)
    {
        return "00:00";
    }

    int mins = (int)seconds / 60;
    int secs = (int)seconds % 60;

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", mins, secs);

    return std::string(buffer);
}

std::string GetAudioFormat(const fs::path &audioPath)
{
    std::string ext = audioPath.extension().string();

    
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    
    if (!ext.empty() && ext[0] == '.')
    {
        ext = ext.substr(1);
    }

    
    if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac" || ext == "aac")
    {
        return ext;
    }

    return "unknown";
}

std::string FindAlbumImage(const std::string &albumName, const fs::path &soundsDir)
{
    if (albumName.empty())
    {
        return "";
    }

    
    std::string albumLower = albumName;
    std::transform(albumLower.begin(), albumLower.end(), albumLower.begin(), ::tolower);

    
    std::vector<std::string> imageExts = {"png", "jpg", "jpeg", "bmp", "tga", "dds"};

    
    
    
    

    std::vector<std::string> patterns = {
        albumLower,                          
        albumLower + "_cover",               
        albumLower + "-album"                
    };

    for (const auto &pattern : patterns)
    {
        for (const auto &ext : imageExts)
        {
            fs::path testPath = soundsDir / (pattern + "." + ext);
            if (fs::exists(testPath))
            {
                return pattern + "." + ext;
            }
        }
    }

    
    if (albumLower == "basicmenu")
    {
        return "basicmenu.png";
    }

    return "";
}

std::string GetPrismaListIniPath()
{
    fs::path dllDir = GetPluginDllPath();
    if (!dllDir.empty())
    {
        return (dllDir / "OSoundtracks-SA-Prisma-List.ini").string();
    }
    return "OSoundtracks-SA-Prisma-List.ini";
}

bool ScanMusicJson()
{
    fs::path jsonPath = fs::path(GetGamePath()) / "Data" / "SKSE" / "Plugins" / "OSoundtracks-SA-Expansion-Sounds-NG.json";

    if (!fs::exists(jsonPath))
    {
        logger::warn("Music JSON not found: {}", jsonPath.string());
        return false;
    }

    
    auto ftime = fs::last_write_time(jsonPath);
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t modTime = std::chrono::system_clock::to_time_t(sctp);

    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&modTime));
    std::string currentTime = timeStr;

    if (currentTime == g_lastJsonModTime)
    {
        logger::info("Music JSON unchanged since last scan");
        return true;  
    }

    g_lastJsonModTime = currentTime;
    logger::info("Music JSON found, scanning: {}", jsonPath.string());

    
    std::ifstream file(jsonPath);
    if (!file.is_open())
    {
        logger::error("Failed to open Music JSON: {}", jsonPath.string());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    
    g_soundMenuKeyTracks.clear();
    g_soundKeyTracks.clear();

    
    fs::path soundsDir = fs::path(GetGamePath()) / "Data" / "sound" / "OSoundtracks";

    
    
    
    
    size_t soundMenuKeyPos = content.find("\"SoundMenuKey\"");
    if (soundMenuKeyPos != std::string::npos)
    {
        size_t sectionStart = content.find("{", soundMenuKeyPos);
        size_t sectionEnd = content.find("}", sectionStart);

        if (sectionStart != std::string::npos && sectionEnd != std::string::npos)
        {
            size_t pos = sectionStart + 1;

            while (pos < sectionEnd)
            {
                
                
                size_t quoteStart = content.find("\"", pos);
                if (quoteStart == std::string::npos || quoteStart >= sectionEnd) break;

                size_t quoteEnd = content.find("\"", quoteStart + 1);
                if (quoteEnd == std::string::npos || quoteEnd >= sectionEnd) break;

                std::string albumName = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

                
                size_t colonPos = content.find(":", quoteEnd);
                if (colonPos == std::string::npos || colonPos >= sectionEnd) break;

                
                size_t arrayStart = content.find("[", colonPos);
                if (arrayStart == std::string::npos || arrayStart >= sectionEnd) break;

                
                int bracketCount = 1;
                size_t arrayEnd = arrayStart + 1;
                while (arrayEnd < content.size() && bracketCount > 0)
                {
                    if (content[arrayEnd] == '[') bracketCount++;
                    else if (content[arrayEnd] == ']') bracketCount--;
                    arrayEnd++;
                }

                if (arrayEnd > arrayStart)
                {
                    std::string arrayContent = content.substr(arrayStart, arrayEnd - arrayStart);

                    
                    
                    std::vector<TrackInfo> albumTracks;

                    
                    size_t trackPos = arrayContent.find("[");
                    if (trackPos != std::string::npos) {
                        trackPos++; 
                    }

                    while (trackPos < arrayContent.size())
                    {
                        
                        size_t trackArrayStart = arrayContent.find("[", trackPos);
                        if (trackArrayStart == std::string::npos) break;

                        
                        int tbCount = 1;
                        size_t trackArrayEnd = trackArrayStart + 1;
                        while (trackArrayEnd < arrayContent.size() && tbCount > 0)
                        {
                            if (arrayContent[trackArrayEnd] == '[') tbCount++;
                            else if (arrayContent[trackArrayEnd] == ']') tbCount--;
                            trackArrayEnd++;
                        }

                        if (trackArrayEnd > trackArrayStart)
                        {
                            std::string trackStr = arrayContent.substr(trackArrayStart, trackArrayEnd - trackArrayStart);

                            
                            
                            size_t firstQuote = trackStr.find("\"");
                            if (firstQuote != std::string::npos)
                            {
                                size_t secondQuote = trackStr.find("\"", firstQuote + 1);
                                if (secondQuote != std::string::npos)
                                {
                                    std::string trackName = trackStr.substr(firstQuote + 1, secondQuote - firstQuote - 1);

                                    
                                    std::string foundExt;
                                    fs::path audioPath;

                                    for (const auto &ext : {"mp3", "wav", "ogg", "flac"})
                                    {
                                        fs::path testPath = soundsDir / (trackName + "." + ext);
                                        if (fs::exists(testPath))
                                        {
                                            audioPath = testPath;
                                            foundExt = ext;
                                            break;
                                        }
                                    }

                                    if (!audioPath.empty())
                                    {
                                        TrackInfo info;
                                        info.name = trackName;
                                        info.duration = GetAudioDuration(audioPath);
                                        info.format = foundExt;
                                        info.imageFile = albumName;  
                                        info.albumOrAnim = albumName;
                                        info.isSpecial = false;

                                        albumTracks.push_back(info);
                                        logger::info("Found track: {} ({}), album: {}", trackName, foundExt, albumName);
                                    }
                                    else
                                    {
                                        logger::warn("Audio file not found for track: {}", trackName);
                                    }
                                }
                            }
                        }

                        trackPos = trackArrayEnd;
                    }

                    if (!albumTracks.empty())
                    {
                        g_soundMenuKeyTracks[albumName] = albumTracks;
                        logger::info("Album '{}' has {} tracks", albumName, albumTracks.size());
                    }
                }

                
                pos = arrayEnd;
                size_t nextComma = content.find(",", pos);
                if (nextComma != std::string::npos && nextComma < sectionEnd)
                {
                    pos = nextComma + 1;
                }
                else
                {
                    break;  
                }
            }
        }
    }

    
    
    
    
    size_t soundKeyPos = content.find("\"SoundKey\"");
    if (soundKeyPos != std::string::npos)
    {
        size_t sectionStart = content.find("{", soundKeyPos);
        size_t sectionEnd = content.find("}", sectionStart);

        if (sectionStart != std::string::npos && sectionEnd != std::string::npos)
        {
            size_t pos = sectionStart + 1;

            while (pos < sectionEnd)
            {
                
                size_t quoteStart = content.find("\"", pos);
                if (quoteStart == std::string::npos || quoteStart >= sectionEnd) break;

                size_t quoteEnd = content.find("\"", quoteStart + 1);
                if (quoteEnd == std::string::npos || quoteEnd >= sectionEnd) break;

                std::string animName = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

                
                size_t colonPos = content.find(":", quoteEnd);
                if (colonPos == std::string::npos || colonPos >= sectionEnd) break;

                
                size_t arrayStart = content.find("[", colonPos);
                if (arrayStart == std::string::npos || arrayStart >= sectionEnd) break;

                
                int bracketCount = 1;
                size_t arrayEnd = arrayStart + 1;
                while (arrayEnd < content.size() && bracketCount > 0)
                {
                    if (content[arrayEnd] == '[') bracketCount++;
                    else if (content[arrayEnd] == ']') bracketCount--;
                    arrayEnd++;
                }

                if (arrayEnd > arrayStart)
                {
                    std::string arrayContent = content.substr(arrayStart, arrayEnd - arrayStart);

                    
                    std::vector<TrackInfo> animTracks;
                    size_t trackPos = 0;

                    while (trackPos < arrayContent.size())
                    {
                        
                        size_t trackArrayStart = arrayContent.find("[", trackPos);
                        if (trackArrayStart == std::string::npos) break;

                        
                        int tbCount = 1;
                        size_t trackArrayEnd = trackArrayStart + 1;
                        while (trackArrayEnd < arrayContent.size() && tbCount > 0)
                        {
                            if (arrayContent[trackArrayEnd] == '[') tbCount++;
                            else if (arrayContent[trackArrayEnd] == ']') tbCount--;
                            trackArrayEnd++;
                        }

                        if (trackArrayEnd > trackArrayStart)
                        {
                            std::string trackStr = arrayContent.substr(trackArrayStart, trackArrayEnd - trackArrayStart);

                            
                            size_t firstQuote = trackStr.find("\"");
                            if (firstQuote != std::string::npos)
                            {
                                size_t secondQuote = trackStr.find("\"", firstQuote + 1);
                                if (secondQuote != std::string::npos)
                                {
                                    std::string soundName = trackStr.substr(firstQuote + 1, secondQuote - firstQuote - 1);

                                    
                                    std::string foundExt;
                                    fs::path audioPath;

                                    for (const auto &ext : {"mp3", "wav", "ogg", "flac"})
                                    {
                                        fs::path testPath = soundsDir / (soundName + "." + ext);
                                        if (fs::exists(testPath))
                                        {
                                            audioPath = testPath;
                                            foundExt = ext;
                                            break;
                                        }
                                    }

                                    if (!audioPath.empty())
                                    {
                                        TrackInfo info;
                                        info.name = soundName;
                                        info.duration = GetAudioDuration(audioPath);
                                        info.format = foundExt;
                                        info.imageFile = animName;  
                                        info.albumOrAnim = animName;
                                        info.isSpecial = (soundName == "miau-03");  

                                        animTracks.push_back(info);
                                        logger::info("Found sound: {} ({}), animation: {}", soundName, foundExt, animName);
                                    }
                                    else
                                    {
                                        logger::warn("Audio file not found for sound: {}", soundName);
                                    }
                                }
                            }
                        }

                        trackPos = trackArrayEnd;
                    }

                    if (!animTracks.empty())
                    {
                        g_soundKeyTracks[animName] = animTracks;
                        logger::info("Animation '{}' has {} sounds", animName, animTracks.size());
                    }
                }

                
                pos = arrayEnd;
                size_t nextComma = content.find(",", pos);
                if (nextComma != std::string::npos && nextComma < sectionEnd)
                {
                    pos = nextComma + 1;
                }
                else
                {
                    break;  
                }
            }
        }
    }

    logger::info("Scan complete: {} SoundMenuKey albums, {} SoundKey animations",
                 g_soundMenuKeyTracks.size(), g_soundKeyTracks.size());

    return true;
}

void WritePrismaListIni()
{
    std::string iniPath = GetPrismaListIniPath();
    
    std::ofstream file(iniPath);
    if (!file.is_open())
    {
        logger::error("Failed to create Prisma List INI: {}", iniPath);
        return;
    }

    
    size_t totalTracks = 0;
    for (const auto &[album, tracks] : g_soundMenuKeyTracks)
    {
        totalTracks += tracks.size();
    }
    
    size_t totalSounds = 0;
    for (const auto &[anim, tracks] : g_soundKeyTracks)
    {
        totalSounds += tracks.size();
    }

    
    
    
    file << "[SoundMenuKey]\n";
    file << "; Format: TrackName = Duration | Format | ImageFile | Album\n";
    file << "; This file is auto-generated by Prisma plugin\n";
    file << "; ImageFile is the album name (no extension needed)\n\n";

    for (const auto &[album, tracks] : g_soundMenuKeyTracks)
    {
        file << "\n; Album: " << album << "\n";
        
        for (const auto &track : tracks)
        {
            
            std::string imageFile = track.imageFile.empty() ? album : track.imageFile;
            file << track.name << " = " << track.duration
                 << " | " << track.format
                 << " | " << imageFile
                 << " | " << track.albumOrAnim << "\n";
        }
    }

    
    
    
    file << "\n[SoundKey]\n";
    file << "; Format: SoundName = Duration | Format | ImageFile | Animation\n";
    file << "; ImageFile is the animation name (no extension needed)\n\n";

    for (const auto &[anim, tracks] : g_soundKeyTracks)
    {
        file << "\n; Animation: " << anim << "\n";
        
        for (const auto &track : tracks)
        {
            std::string imageFile;
            
            
            if (track.name == "miau-03")
            {
                file << "; SPECIAL: miau-03 plays at game start before Prisma is loaded\n";
                file << "; No image - this sound will NOT appear in Hub\n";
                imageFile = "SPECIAL_NO_IMAGE";
            }
            else
            {
                
                imageFile = track.imageFile.empty() ? anim : track.imageFile;
            }

            file << track.name << " = " << track.duration
                 << " | " << track.format
                 << " | " << imageFile
                 << " | " << track.albumOrAnim << "\n";
        }
    }

    
    
    
    file << "\n[Metadata]\n";
    file << "LastScan = " << g_lastJsonModTime << "\n";
    file << "TotalTracks = " << totalTracks << "\n";
    file << "TotalSounds = " << totalSounds << "\n";

    file.close();

    logger::info("Prisma List INI written to: {} ({} tracks, {} sounds)", iniPath, totalTracks, totalSounds);

    
    fs::path backupDir = fs::path(GetPluginDllPath()) / "Backup";
    try
    {
        if (!fs::exists(backupDir))
        {
            fs::create_directories(backupDir);
        }
        fs::copy_file(iniPath, backupDir / "OSoundtracks-SA-Prisma-List.ini", fs::copy_options::overwrite_existing);
        logger::info("Prisma List INI backup saved");
    }
    catch (const std::exception &e)
    {
        logger::error("Failed to backup Prisma List INI: {}", e.what());
    }

    
    
    fs::path assetsIniDir = fs::path(GetPluginDllPath()).parent_path().parent_path() / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini";
    try
    {
        if (!fs::exists(assetsIniDir))
        {
            fs::create_directories(assetsIniDir);
        }
        fs::copy_file(iniPath, assetsIniDir / "OSoundtracks-SA-Prisma-List.ini", fs::copy_options::overwrite_existing);
        logger::info("Prisma List INI copied to Assets/ini for web access");
    }
    catch (const std::exception &e)
    {
        logger::error("Failed to copy Prisma List INI to Assets/ini: {}", e.what());
    }
}

HSTREAM PlayBASSSound(const std::string &soundFile, float volume)
{
    if (!g_bassInitialized)
    {
        if (!InitializeBASSLibrary())
            return 0;
    }

    
    if (volume <= 0.0f)
    {
        logger::info("Sound muted: {}", soundFile);
        return 0;
    }

    fs::path soundPath = FindSoundFile(soundFile);
    if (soundPath.empty())
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_bassMutex);

    HSTREAM stream = pBASS_StreamCreateFile(
        FALSE,
        soundPath.wstring().c_str(),
        0, 0,
        BASS_UNICODE);

    if (!stream)
    {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("BASS: Failed to create stream for {}: error {}", soundPath.string(), error);
        return 0;
    }

    if (pBASS_ChannelSetAttribute)
    {
        pBASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, volume);
    }

    if (!pBASS_ChannelPlay(stream, FALSE))
    {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("BASS: Failed to play stream: error {}", error);
        pBASS_StreamFree(stream);
        return 0;
    }

    logger::info("BASS: Playing {} (volume={}%)", soundFile, static_cast<int>(volume * 100));
    return stream;
}

void StopBASSStream(HSTREAM &stream)
{
    std::lock_guard<std::mutex> lock(g_bassMutex);

    if (stream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(stream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(stream);
        stream = 0;
    }
}

void RestoreHealth(RE::Actor *actor, float amount)
{
    if (!actor)
        return;

    auto *actorValueOwner = actor->AsActorValueOwner();
    if (actorValueOwner)
    {
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kHealth, amount);
        logger::info("Restored {} health to actor", amount);
    }
}

void RestoreMagicka(RE::Actor *actor, float amount)
{
    if (!actor)
        return;

    auto *actorValueOwner = actor->AsActorValueOwner();
    if (actorValueOwner)
    {
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kMagicka, amount);
        logger::info("Restored {} magicka to actor", amount);
    }
}

void RestoreStamina(RE::Actor *actor, float amount)
{
    if (!actor)
        return;

    auto *actorValueOwner = actor->AsActorValueOwner();
    if (actorValueOwner)
    {
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kStamina, amount);
        logger::info("Restored {} stamina to actor", amount);
    }
}

RE::Actor *GetPlayerActor()
{
    return RE::PlayerCharacter::GetSingleton();
}

std::string GetPlayerStatsJSON()
{
    auto *player = GetPlayerActor();
    if (!player)
        return "{}";

    auto *avOwner = player->AsActorValueOwner();
    if (!avOwner)
        return "{}";

    float healthCurrent = avOwner->GetActorValue(RE::ActorValue::kHealth);
    float healthMax = avOwner->GetPermanentActorValue(RE::ActorValue::kHealth);

    float magickaCurrent = avOwner->GetActorValue(RE::ActorValue::kMagicka);
    float magickaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kMagicka);

    float staminaCurrent = avOwner->GetActorValue(RE::ActorValue::kStamina);
    float staminaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);

    std::stringstream ss;
    ss << "{";
    ss << "\"health\":{\"current\":" << (int)healthCurrent << ",\"max\":" << (int)healthMax << "},";
    ss << "\"magicka\":{\"current\":" << (int)magickaCurrent << ",\"max\":" << (int)magickaMax << "},";
    ss << "\"stamina\":{\"current\":" << (int)staminaCurrent << ",\"max\":" << (int)staminaMax << "}";
    ss << "}";

    return ss.str();
}

void UpdateUIStats()
{
    if (!PrismaUI || !g_HealingView)
        return;

    std::string json = GetPlayerStatsJSON();
    std::string script = "updatePlayerStats('" + json + "')";
    PrismaUI->Invoke(g_HealingView, script.c_str());
}

void OnRequestPlayerStats(const char *data)
{
    UpdateUIStats();
}

void OnGetMenusConfig(const char *data)
{
    if (!PrismaUI || !g_HealingView)
        return;

    std::stringstream ss;
    ss << "[";
    bool first = true;
    for (const auto &[menuName, enabled] : g_detectedMenus)
    {
        if (!first)
            ss << ",";
        ss << "{\"name\":\"" << menuName << "\",\"enabled\":" << enabled << "}";
        first = false;
    }
    ss << "]";

    std::string script = "updateMenusList('" + ss.str() + "')";
    PrismaUI->Invoke(g_HealingView, script.c_str());
    logger::info("Sent menus list to JS: {} menus", g_detectedMenus.size());
}

void OnGetAuthorsConfig(const char *data)
{
    if (!PrismaUI || !g_HealingView)
        return;

    std::stringstream ss;
    ss << "[";
    bool first = true;
    for (const auto &author : g_authorsList)
    {
        if (!first)
            ss << ",";
        ss << "\"" << author << "\"";
        first = false;
    }
    ss << "]";

    std::string script = "updateAuthorsList('" + ss.str() + "')";
    PrismaUI->Invoke(g_HealingView, script.c_str());
    logger::info("Sent authors list to JS: {} authors", g_authorsList.size());
}

void UpdatePanelLiveVisibility();

void OnToggleMenu(const char *data)
{
    if (!data)
        return;

    std::string menuName(data);
    if (g_detectedMenus.find(menuName) != g_detectedMenus.end())
    {
        int oldValue = g_detectedMenus[menuName];
        g_detectedMenus[menuName] = oldValue == 1 ? 0 : 1;
        SaveMenusConfig();
        logger::info("Toggled menu: {} = {}", menuName, g_detectedMenus[menuName]);

        
        
        if (oldValue == 1 && g_detectedMenus[menuName] == 0)
        {
            g_openTrackedMenus.erase(menuName);
            UpdatePanelLiveVisibility();
            logger::info("Menu {} removed from tracking, Panel Live visibility updated", menuName);
        }
    }
}

void SaveItemsConfig();

void OnGetItemsConfig(const char *data)
{
    if (!PrismaUI || !g_HealingView)
        return;

    std::stringstream ss;
    ss << "{";
    ss << "\"showNotification\":\"" << g_itemsConfig.notification.showNotification << "\",";
    ss << "\"goldEnabled\":" << (g_itemsConfig.gold.enabled ? "true" : "false") << ",";
    ss << "\"goldAmount\":" << g_itemsConfig.gold.amount;
    ss << "}";

    std::string script = "updateItemsConfigUI('" + ss.str() + "')";
    PrismaUI->Invoke(g_HealingView, script.c_str());
    logger::info("Sent items config to JS");
}

void OnSaveItemsConfig(const char *data)
{
    if (!data || strlen(data) == 0)
        return;

    
    std::string json(data);

    
    size_t notifPos = json.find("\"showNotification\":\"");
    if (notifPos != std::string::npos)
    {
        size_t start = notifPos + strlen("\"showNotification\":\"");
        size_t end = json.find("\"", start);
        if (end != std::string::npos)
        {
            g_itemsConfig.notification.showNotification = json.substr(start, end - start);
        }
    }

    
    size_t goldEnabledPos = json.find("\"goldEnabled\":");
    if (goldEnabledPos != std::string::npos)
    {
        size_t start = goldEnabledPos + strlen("\"goldEnabled\":");
        std::string value = json.substr(start, 4);
        g_itemsConfig.gold.enabled = (value.substr(0, 4) == "true");
    }

    
    size_t goldAmountPos = json.find("\"goldAmount\":");
    if (goldAmountPos != std::string::npos)
    {
        size_t start = goldAmountPos + strlen("\"goldAmount\":");
        size_t end = start;
        while (end < json.size() && isdigit(json[end]))
            end++;
        if (end > start)
        {
            try
            {
                g_itemsConfig.gold.amount = std::stoi(json.substr(start, end - start));
            }
            catch (...)
            {
                g_itemsConfig.gold.amount = 9;
            }
        }
    }

    
    size_t menusSizePos = json.find("\"menusSize\":");
    if (menusSizePos != std::string::npos)
    {
        size_t start = menusSizePos + strlen("\"menusSize\":");
        size_t end = start;
        while (end < json.size() && isdigit(json[end]))
            end++;
        if (end > start)
        {
            try
            {
                g_config.menusSize = std::stoi(json.substr(start, end - start));
                g_config.menusSize = std::max(50, std::min(200, g_config.menusSize));
            }
            catch (...)
            {
            }
        }
    }

    
    size_t itemsSizePos = json.find("\"itemsSize\":");
    if (itemsSizePos != std::string::npos)
    {
        size_t start = itemsSizePos + strlen("\"itemsSize\":");
        size_t end = start;
        while (end < json.size() && isdigit(json[end]))
            end++;
        if (end > start)
        {
            try
            {
                g_config.itemsSize = std::stoi(json.substr(start, end - start));
                g_config.itemsSize = std::max(50, std::min(200, g_config.itemsSize));
            }
            catch (...)
            {
            }
        }
    }

    
    size_t toastSizePos = json.find("\"toastSize\":");
    if (toastSizePos != std::string::npos)
    {
        size_t start = toastSizePos + strlen("\"toastSize\":");
        size_t end = start;
        while (end < json.size() && isdigit(json[end]))
            end++;
        if (end > start)
        {
            try
            {
                g_config.toastSize = std::stoi(json.substr(start, end - start));
                g_config.toastSize = std::max(10, std::min(24, g_config.toastSize));
            }
            catch (...)
            {
            }
        }
    }

    
    SaveItemsConfig();

    
    SaveConfig();

    logger::info("Saved items config and sizes from JS");
}

void UpdatePanelLiveVisibility()
{
    if (!PrismaUI || !g_HealingView)
        return;

    
    
    bool shouldHide = !g_openTrackedMenus.empty();

    
    std::string script = std::string("setPanelLiveVisibility(") + (shouldHide ? "true" : "false") + ")";
    PrismaUI->Invoke(g_HealingView, script.c_str());

    logger::info("Panel Live visibility updated: {} ({} tracked menus open)",
                 shouldHide ? "HIDDEN" : "VISIBLE", g_openTrackedMenus.size());
}

void ToggleHealingMenu();

void OnGiveGold(const char *data)
{
    logger::info("OnGiveGold called with data: {}", data);

    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!player)
    {
        logger::error("OnGiveGold: Player not found");
        return;
    }

    auto *gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
    if (!gold)
    {
        logger::error("OnGiveGold: Gold item not found");
        return;
    }

    int amount = g_itemsConfig.gold.amount;
    if (data && strlen(data) > 0)
    {
        amount = std::atoi(data);
    }

    player->AddObjectToContainer(gold, nullptr, amount, nullptr);

    
    float volume = g_config.volume / 100.0f;
    PlayBASSSound("coins27.wav", volume);

    
    if (g_itemsConfig.notification.showNotification == "game")
    {
        std::string msg = "Received " + std::to_string(amount) + " gold";
        RE::DebugNotification(msg.c_str());
    }
    else if (g_itemsConfig.notification.showNotification == "hub")
    {
        
        std::string script = "showHubNotification('Received " + std::to_string(amount) + " gold', 'gold')";
        PrismaUI->Invoke(g_HealingView, script.c_str());
    }
    

    logger::info("Player received {} gold via UI button", amount);
}

void OnHealAll(const char *data)
{
    logger::info("OnHealAll called with data: {}", data);

    
    float volume = g_config.volume / 100.0f;
    PlayBASSSound("gameplay-recover-hp.wav", volume);

    auto *player = GetPlayerActor();
    if (!player)
        return;

    auto *avOwner = player->AsActorValueOwner();
    if (!avOwner)
        return;

    float healthMax = avOwner->GetPermanentActorValue(RE::ActorValue::kHealth);
    float magickaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kMagicka);
    float staminaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);

    RestoreHealth(player, healthMax);
    RestoreMagicka(player, magickaMax);
    RestoreStamina(player, staminaMax);

    UpdateUIStats();

    if (PrismaUI && g_HealingView)
    {
        PrismaUI->Invoke(g_HealingView, "showMessage('All stats restored!')");
    }
}

void OnHealHealth(const char *data)
{
    logger::info("OnHealHealth called with data: {}", data);

    
    float volume = g_config.volume / 100.0f;
    PlayBASSSound("gameplay-recover-hp.wav", volume);

    auto *player = GetPlayerActor();
    if (!player)
        return;

    auto *avOwner = player->AsActorValueOwner();
    if (!avOwner)
        return;

    float healthMax = avOwner->GetPermanentActorValue(RE::ActorValue::kHealth);
    RestoreHealth(player, healthMax);

    UpdateUIStats();

    if (PrismaUI && g_HealingView)
    {
        PrismaUI->Invoke(g_HealingView, "showMessage('Health restored!')");
    }
}

void OnHealMagicka(const char *data)
{
    logger::info("OnHealMagicka called with data: {}", data);

    
    float volume = g_config.volume / 100.0f;
    PlayBASSSound("gameplay-recover-hp.wav", volume);

    auto *player = GetPlayerActor();
    if (!player)
        return;

    auto *avOwner = player->AsActorValueOwner();
    if (!avOwner)
        return;

    float magickaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kMagicka);
    RestoreMagicka(player, magickaMax);

    UpdateUIStats();

    if (PrismaUI && g_HealingView)
    {
        PrismaUI->Invoke(g_HealingView, "showMessage('Magicka restored!')");
    }
}

void OnHealStamina(const char *data)
{
    logger::info("OnHealStamina called with data: {}", data);

    
    float volume = g_config.volume / 100.0f;
    PlayBASSSound("gameplay-recover-hp.wav", volume);

    auto *player = GetPlayerActor();
    if (!player)
        return;

    auto *avOwner = player->AsActorValueOwner();
    if (!avOwner)
        return;

    float staminaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);
    RestoreStamina(player, staminaMax);

    UpdateUIStats();

    if (PrismaUI && g_HealingView)
    {
        PrismaUI->Invoke(g_HealingView, "showMessage('Stamina restored!')");
    }
}

void OnRestorePercent(const char *data)
{
    logger::info("OnRestorePercent called with data: {}", data);

    
    float volume = g_config.volume / 100.0f;
    PlayBASSSound("gameplay-recover-hp.wav", volume);

    auto *player = GetPlayerActor();
    if (!player)
        return;

    
    int percent = 50; 
    if (data && strlen(data) > 0)
    {
        try
        {
            percent = std::stoi(data);
        }
        catch (...)
        {
            percent = 50;
        }
    }

    auto *avOwner = player->AsActorValueOwner();
    if (!avOwner)
        return;

    float healthMax = avOwner->GetPermanentActorValue(RE::ActorValue::kHealth);
    float magickaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kMagicka);
    float staminaMax = avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);

    float multiplier = percent / 100.0f;

    RestoreHealth(player, healthMax * multiplier);
    RestoreMagicka(player, magickaMax * multiplier);
    RestoreStamina(player, staminaMax * multiplier);

    UpdateUIStats();

    if (PrismaUI && g_HealingView)
    {
        std::string msg = "Restored " + std::to_string(percent) + "% of all stats!";
        PrismaUI->Invoke(g_HealingView, ("showMessage('" + msg + "')").c_str());
    }
}

void OnPlayVideoAudio(const char *data)
{
    if (!g_bassInitialized)
        return;

    
    double startSeconds = 0.0;
    if (data && strlen(data) > 0)
    {
        try
        {
            startSeconds = std::stod(data);
        }
        catch (...)
        {
            startSeconds = 0.0;
        }
    }

    
    if (!g_videoAudioStream)
    {
        std::string gamePath = GetGamePath();
        fs::path audioPath = fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Video" / "corto-v1" / "corto-v1.mp3";

        g_videoAudioStream = pBASS_StreamCreateFile(
            FALSE,
            audioPath.wstring().c_str(),
            0, 0,
            BASS_UNICODE);

        if (g_videoAudioStream)
        {
            logger::info("Video audio stream created: {}", audioPath.string());
        }
        else
        {
            int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
            logger::error("Video audio: stream creation failed (BASS error: {})", error);
            return;
        }
    }

    
    if (g_videoAudioStream)
    {
        
        if (pBASS_ChannelSetAttribute)
        {
            pBASS_ChannelSetAttribute(g_videoAudioStream, BASS_ATTRIB_VOL, (float)g_config.videoVolume / 100.0f);
        }

        
        if (startSeconds > 0.0 && pBASS_ChannelSeconds2Bytes && pBASS_ChannelSetPosition)
        {
            QWORD pos = pBASS_ChannelSeconds2Bytes(g_videoAudioStream, startSeconds);
            pBASS_ChannelSetPosition(g_videoAudioStream, pos, BASS_POS_BYTE);
        }

        
        if (pBASS_ChannelPlay)
        {
            pBASS_ChannelPlay(g_videoAudioStream, FALSE);
        }

        logger::info("Video audio PLAY from {} seconds", startSeconds);
    }
}

void OnPauseVideoAudio(const char *data)
{
    if (g_videoAudioStream && pBASS_ChannelPause)
    {
        pBASS_ChannelPause(g_videoAudioStream);
    }
    logger::info("Video audio PAUSE");
}

void OnStopVideoAudio(const char *data)
{
    if (g_videoAudioStream)
    {
        pBASS_ChannelStop(g_videoAudioStream);
        pBASS_StreamFree(g_videoAudioStream);
        g_videoAudioStream = 0;
    }
    logger::info("Video audio STOP");
}

void OnSeekVideoAudio(const char *data)
{
    if (!data || !g_videoAudioStream)
        return;

    double segundos = std::stod(data);

    if (pBASS_ChannelSeconds2Bytes && pBASS_ChannelSetPosition)
    {
        QWORD pos = pBASS_ChannelSeconds2Bytes(g_videoAudioStream, segundos);
        pBASS_ChannelSetPosition(g_videoAudioStream, pos, BASS_POS_BYTE);
    }
    logger::info("Video audio SEEK to {} seconds", segundos);
}

void OnSetVideoVolume(const char *data)
{
    if (!data)
        return;

    int volume = std::stoi(data);
    volume = std::max(0, std::min(100, volume));

    g_config.videoVolume = volume;

    
    if (g_videoAudioStream && pBASS_ChannelSetAttribute)
    {
        pBASS_ChannelSetAttribute(g_videoAudioStream, BASS_ATTRIB_VOL, (float)volume / 100.0f);
    }

    logger::info("Video volume set to: {}%", volume);
}

void OnPlayImpactSound(const char *data)
{
    if (!g_bassInitialized || !data)
        return;

    std::string type = data;
    std::string gamePath = GetGamePath();

    
    std::vector<std::string> sounds;

    if (type == "boom")
    {
        sounds = {
            "Boom-big-cine-boom-sound-effect_convertido.wav",
            "Boom-dragon-studio-cinematic-boom_convertido.wav",
            "Boom-foxwagen-boom-3b-muffled_convertido.wav",
            "Boom-imgmidi-subsonic-boom-one_convertido.wav"};
    }
    else if (type == "blood")
    {
        sounds = {
            "sword_Blood-freesound_community-beheading-sfx_convertido.wav",
            "sword_Blood-ragecore29-htf-head-or-body-rips-apart_convertido.wav",
            "sword_Blood-u_xjrmmgxfru-flesh-impact_convertido.wav",
            "sword_Blood-universfield-sword-blade-slicing-flesh_convertido.wav",
            "sword_Blood_dragon-studio-violent-sword-slice-2_convertido.wav"};
    }
    else if (type == "parry")
    {
        sounds = {
            "sword_Parry-dragon-studio-sword-slice_convertido.wav",
            "sword_Parry-freesound_community-sword-sound-2_convertido.wav"};
    }

    if (sounds.empty())
    {
        logger::warn("OnPlayImpactSound: Unknown type: {}", type);
        return;
    }

    
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    std::string soundFile = sounds[std::rand() % sounds.size()];

    
    fs::path audioPath = fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Sound" / "impact_frames" / soundFile;

    
    if (g_impactAudioStream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(g_impactAudioStream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_impactAudioStream);
        g_impactAudioStream = 0;
    }

    
    g_impactAudioStream = pBASS_StreamCreateFile(
        FALSE,
        audioPath.wstring().c_str(),
        0, 0,
        BASS_UNICODE);

    if (g_impactAudioStream)
    {
        pBASS_ChannelSetAttribute(g_impactAudioStream, BASS_ATTRIB_VOL, (float)g_config.volume / 100.0f);
        pBASS_ChannelPlay(g_impactAudioStream, TRUE);
        logger::info("Impact sound PLAY: {} ({})", type, soundFile);
    }
    else
    {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("Impact sound failed: {} (BASS error: {})", audioPath.string(), error);
    }
}

void OnStopImpactSound(const char *data)
{
    if (g_impactAudioStream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(g_impactAudioStream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_impactAudioStream);
        g_impactAudioStream = 0;
    }
    logger::info("Impact sound STOP");
}

void OnPlayDoomMusic(const char *data)
{
    if (!g_bassInitialized)
        return;

    std::string gamePath = GetGamePath();

    fs::path audioPath = fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Media" / "return-of-the-demon-slayer.mp3";

    if (g_doomMusicStream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(g_doomMusicStream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_doomMusicStream);
        g_doomMusicStream = 0;
    }

    g_doomMusicStream = pBASS_StreamCreateFile(
        FALSE,
        audioPath.wstring().c_str(),
        0, 0,
        BASS_UNICODE);

    if (g_doomMusicStream)
    {
        pBASS_ChannelSetAttribute(g_doomMusicStream, BASS_ATTRIB_VOL, (float)0.65);
        pBASS_ChannelPlay(g_doomMusicStream, TRUE);
        logger::info("Doom music PLAY: return-of-the-demon-slayer.mp3");
    }
    else
    {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("Doom music failed: {} (BASS error: {})", audioPath.string(), error);
    }
}

void OnStopDoomMusic(const char *data)
{
    if (g_doomMusicStream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(g_doomMusicStream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_doomMusicStream);
        g_doomMusicStream = 0;
    }
    logger::info("Doom music STOP");
}

void OnPlayMeowSound(const char *data)
{
    if (!g_bassInitialized)
        return;

    float volume = g_config.volume / 100.0f;
    PlayBASSSound("miau-PDA.wav", volume);
}

void OnPlayRobotTalkSound(const char *data)
{
    if (!g_bassInitialized)
        return;

    float volume = g_config.volume / 100.0f;
    PlayBASSSound("robot_talk.wav", volume);
}

void OnPlayJackpotSound(const char *data)
{
    if (!g_bassInitialized || !data)
        return;

    std::string type = data;
    std::string baseName;
    if (type.find("en") != std::string::npos)
    {
        baseName = "jackpot_EN_v";
    }
    else
    {
        baseName = "jackpot_ES_v";
    }

    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    int version = (std::rand() % 2) + 1;
    std::string fileName = baseName + std::to_string(version) + ".mp3";

    fs::path soundPath = FindSoundFile("JACKPOT/" + fileName);
    if (soundPath.empty())
    {
        logger::error("Jackpot sound not found: {}", fileName);
        return;
    }

    if (g_jackpotAudioStream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(g_jackpotAudioStream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_jackpotAudioStream);
        g_jackpotAudioStream = 0;
    }

    g_jackpotAudioStream = pBASS_StreamCreateFile(
        FALSE,
        soundPath.wstring().c_str(),
        0, 0,
        BASS_UNICODE);

    if (g_jackpotAudioStream)
    {
        pBASS_ChannelSetAttribute(g_jackpotAudioStream, BASS_ATTRIB_VOL, (float)g_config.volume / 100.0f);
        pBASS_ChannelPlay(g_jackpotAudioStream, TRUE);
        logger::info("Jackpot sound PLAY: {}", fileName);
    }
    else
    {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("Jackpot sound failed: {} (BASS error: {})", soundPath.string(), error);
    }
}

void OnStopJackpotSound(const char *data)
{
    if (g_jackpotAudioStream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(g_jackpotAudioStream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_jackpotAudioStream);
        g_jackpotAudioStream = 0;
    }
    logger::info("Jackpot sound STOP");
}

void OnPlayJackpotOutSound(const char *data)
{
    if (!g_bassInitialized)
        return;

    fs::path soundPath = FindSoundFile("JACKPOT/alexzavesa-action-and-epic-the-end-drums.mp3");
    if (soundPath.empty())
    {
        logger::error("Jackpot OUT sound not found");
        return;
    }

    if (g_jackpotOutAudioStream)
    {
        if (pBASS_ChannelStop)
            pBASS_ChannelStop(g_jackpotOutAudioStream);
        if (pBASS_StreamFree)
            pBASS_StreamFree(g_jackpotOutAudioStream);
        g_jackpotOutAudioStream = 0;
    }

    g_jackpotOutAudioStream = pBASS_StreamCreateFile(
        FALSE,
        soundPath.wstring().c_str(),
        0, 0,
        BASS_UNICODE);

    if (g_jackpotOutAudioStream)
    {
        pBASS_ChannelSetAttribute(g_jackpotOutAudioStream, BASS_ATTRIB_VOL, (float)g_config.volume / 100.0f);
        pBASS_ChannelPlay(g_jackpotOutAudioStream, TRUE);
        logger::info("Jackpot OUT sound PLAY");
    }
    else
    {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("Jackpot OUT sound failed: {} (BASS error: {})", soundPath.string(), error);
    }
}

void OnCloseMenu(const char *data)
{
    logger::info("OnCloseMenu called");

    
    float volume = g_config.volume / 100.0f;
    PlayBASSSound("swoosh-07.wav", volume);

    if (PrismaUI && g_HealingView)
    {
        PrismaUI->Unfocus(g_HealingView);

        
        if (g_config.liveVisible)
        {
            PrismaUI->Invoke(g_HealingView, "hideHubKeepLive()");
        }
        else
        {
            PrismaUI->Hide(g_HealingView);
        }

        
        auto *msgQ = RE::UIMessageQueue::GetSingleton();
        if (msgQ)
        {
            msgQ->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    }
}

void OnOpenURL(const char *data)
{
    if (!data || strlen(data) == 0)
    {
        logger::warn("OnOpenURL called with empty URL");
        return;
    }
    std::string url(data);
    logger::info("OnOpenURL called: {}", url);
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void OnGetConfig(const char *data)
{
    logger::info("OnGetConfig called");

    if (!PrismaUI || !g_HealingView)
    {
        logger::warn("OnGetConfig: PrismaUI or view not ready");
        return;
    }

    
    std::stringstream ss;
    ss << "{\"firstKey\":" << g_config.firstKey
       << ",\"secondKey\":" << g_config.secondKey
       << ",\"singleKeyMode\":" << (g_config.singleKeyMode ? "true" : "false")
       << ",\"volume\":" << g_config.volume
       << ",\"muteHubSound\":" << (g_config.muteHubSound ? "true" : "false")
       << ",\"hubSound\":\"" << g_config.hubSound << "\""
       << ",\"posX\":" << g_config.posX
       << ",\"posY\":" << g_config.posY
       << ",\"hubSize\":" << g_config.hubSize
       
       << ",\"gifVisible\":" << (g_config.gifVisible ? "true" : "false")
       << ",\"gifPosX\":" << g_config.gifPosX
       << ",\"gifPosY\":" << g_config.gifPosY
       << ",\"gifZoom\":" << g_config.gifZoom
       << ",\"gifWidth\":" << g_config.gifWidth
       << ",\"gifHeight\":" << g_config.gifHeight
       
       << ",\"videoVisible\":" << (g_config.videoVisible ? "true" : "false")
       << ",\"videoPosX\":" << g_config.videoPosX
       << ",\"videoPosY\":" << g_config.videoPosY
       << ",\"videoWidth\":" << g_config.videoWidth
       << ",\"videoHeight\":" << g_config.videoHeight
       << ",\"videoOpacity\":" << g_config.videoOpacity
       << ",\"videoVolume\":" << g_config.videoVolume
       << ",\"videoFullscreen\":" << (g_config.videoFullscreen ? "true" : "false")
       
       << ",\"logsVisible\":" << (g_config.logsVisible ? "true" : "false")
       << ",\"logsPosX\":" << g_config.logsPosX
       << ",\"logsPosY\":" << g_config.logsPosY
       << ",\"logsWidth\":" << g_config.logsWidth
       << ",\"logsHeight\":" << g_config.logsHeight
       << ",\"logsFontSize\":" << g_config.logsFontSize
       << ",\"logsAutoRefresh\":" << (g_config.logsAutoRefresh ? "true" : "false")
       
       << ",\"liveVisible\":" << (g_config.liveVisible ? "true" : "false")
       << ",\"livePosX\":" << g_config.livePosX
       << ",\"livePosY\":" << g_config.livePosY
       << ",\"liveSize\":" << g_config.liveSize
       << ",\"liveAutoStart\":" << (g_config.liveAutoStart ? "true" : "false")
       << ",\"liveNowPlayingMode\":\"" << g_config.liveNowPlayingMode << "\""
       << ",\"liveNowPlayingSeconds\":" << g_config.liveNowPlayingSeconds
       << ",\"liveNowPlayingGif\":\"" << g_config.liveNowPlayingGif << "\""
       
       << ",\"bootPosX\":" << g_config.bootPosX
       << ",\"bootPosY\":" << g_config.bootPosY
       
       << ",\"globalAlpha\":" << g_config.globalAlpha
       << ",\"globalColor\":\"" << g_config.globalColor << "\""
        << ",\"liveBackground\":\"" << g_config.liveBackground << "\""
        << ",\"hubVisibleSettings\":" << (g_config.hubVisibleSettings ? "true" : "false")
        << ",\"proximitySettings\":" << (g_config.proximitySettings ? "true" : "false") << "}";

    std::string script = "updateConfig('" + ss.str() + "')";
    logger::info("OnGetConfig sending: {}", ss.str());
    PrismaUI->Invoke(g_HealingView, script.c_str());

    
    if (g_config.liveVisible)
    {
        PrismaUI->Invoke(g_HealingView, "showPanelLive");
        logger::info("Panel Live shown at startup (persistent mode)");
    }

    
    
    
    UpdatePanelLiveVisibility();
}

void LoadAndSendConfig()
{
    if (!g_playerFunctionsLoaded) LoadSoundPlayerFunctions(); 
    std::string iniPath = GetGamePath() + "/Data/SKSE/Plugins/OSoundtracks-SA-Expansion-Sounds-NG.ini";
    std::string prismaIniPath = GetConfigPath();
    char buf[256];

    auto ReadVal = [&](const char *section, const char *key, const char *def) -> std::string
    {
        GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), iniPath.c_str());
        return std::string(buf);
    };

    
    auto ReadPrismaVal = [&](const char *section, const char *key, const char *def) -> std::string
    {
        GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), prismaIniPath.c_str());
        return std::string(buf);
    };

    std::string json = "{";
    json += "\"BaseVolume\":\"" + ReadVal("Volume Control", "BaseVolume", "1.2") + "\",";
    json += "\"MenuVolume\":\"" + ReadVal("Volume Control", "MenuVolume", "0.8") + "\",";
    json += "\"SpecificVolume\":\"" + ReadVal("Volume Control", "SpecificVolume", "1.2") + "\",";
    json += "\"EffectVolume\":\"" + ReadVal("Volume Control", "EffectVolume", "0.4") + "\",";
    json += "\"PositionVolume\":\"" + ReadVal("Volume Control", "PositionVolume", "1.3") + "\",";
    json += "\"TAGVolume\":\"" + ReadVal("Volume Control", "TAGVolume", "1.2") + "\",";
    json += "\"SoundMenuKeyVolume\":\"" + ReadVal("Volume Control", "SoundMenuKeyVolume", "0.1") + "\",";
    json += "\"MasterVolumeEnabled\":\"" + ReadVal("Volume Control", "MasterVolumeEnabled", "true") + "\",";
    json += "\"Startup\":\"" + ReadVal("Startup Sound", "Startup", "true") + "\",";
    json += "\"MuteGameMusicDuringOStim\":\"" + ReadVal("Skyrim Audio", "MuteGameMusicDuringOStim", "MUSSpecialDeath") + "\",";
    json += "\"Visible\":\"" + ReadVal("Top Notifications", "Visible", "true") + "\",";
    json += "\"SoundMenuKey\":\"" + ReadVal("Menu Sound", "SoundMenuKey", "Author_Random") + "\",";
    json += "\"Author\":\"" + ReadVal("Menu Sound", "Author", "John95acNord") + "\",";
    json += "\"Toast\":\"" + ReadPrismaVal("OSoundtracks", "Toast", "true") + "\"";  
    json += "}";

    std::string script = "loadOsoundtracksConfig(" + json + ");";
    PrismaUI->Invoke(g_HealingView, script.c_str());
}

void LoadAndSendAuthors()
{
    std::string gamePath = GetGamePath();
    std::string iniPath;

    if (!gamePath.empty())
    {
        iniPath = (fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini" / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini").string();
    }
    else
    {
        fs::path dllDir = GetPluginDllPath();
        iniPath = (dllDir / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini" / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini").string();
    }

    char buf[1024];
    GetPrivateProfileStringA("Authors", "List", "", buf, sizeof(buf), iniPath.c_str());

    std::string listStr(buf);

    
    g_authorsList.clear();

    size_t pos = 0;
    while ((pos = listStr.find('|')) != std::string::npos)
    {
        g_authorsList.push_back(listStr.substr(0, pos));
        listStr.erase(0, pos + 1);
    }
    if (!listStr.empty())
    {
        g_authorsList.push_back(listStr);
    }

    logger::info("Loaded {} authors from INI", g_authorsList.size());

    
    OnGetAuthorsConfig("");
}

void OnUpdateIniValue(const char *data)
{
    if (!data || strlen(data) == 0)
        return;
    try
    {
        std::string json(data);
        auto parseVal = [&json](const std::string &key) -> std::string
        {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos)
                return "";
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
                pos++;
            size_t end = json.find_first_of(",}", pos);
            if (end == std::string::npos)
                end = json.length();
            std::string value = json.substr(pos, end - pos);
            if (!value.empty() && value.front() == '"' && value.back() == '"')
            {
                value = value.substr(1, value.length() - 2);
            }
            return value;
        };

        std::string key = parseVal("key");
        std::string val = parseVal("value");
        if (key.empty())
            return;

        
        if (key == "Toast")
        {
            std::string prismaIniPath = GetConfigPath();
            std::string valWithSpace = " " + val;
            WritePrivateProfileStringA("OSoundtracks", key.c_str(), valWithSpace.c_str(), prismaIniPath.c_str());
            logger::info("Hub Config Updated Prisma INI: [OSoundtracks] {} = {}", key, val);
            return;
        }

        std::string iniPath = GetGamePath() + "/Data/SKSE/Plugins/OSoundtracks-SA-Expansion-Sounds-NG.ini";

        std::string section = "Volume Control";
        if (key == "Startup")
            section = "Startup Sound";
        else if (key == "MuteGameMusicDuringOStim")
            section = "Skyrim Audio";
        else if (key == "Visible")
            section = "Top Notifications";
        else if (key == "SoundMenuKey" || key == "Author")
            section = "Menu Sound";

        std::string valWithSpace = " " + val;
        WritePrivateProfileStringA(section.c_str(), key.c_str(), valWithSpace.c_str(), iniPath.c_str());
        logger::info("Hub Config Updated INI: [{}] {} = {}", section, key, val);
    }
    catch (...)
    {
    }
}

void CopyAuthorsIniToAssets()
{
    std::string masterPath = GetDocumentsPath() + "/My Games/Skyrim Special Edition/SKSE/OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini";

    std::string gamePath = GetGamePath();
    std::string assetsPath;

    if (!gamePath.empty())
    {
        assetsPath = (fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini" / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini").string();
    }
    else
    {
        fs::path dllDir = GetPluginDllPath();
        assetsPath = (dllDir / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini" / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini").string();
    }

    try
    {
        if (std::filesystem::exists(masterPath))
        {
            std::filesystem::copy_file(masterPath, assetsPath, std::filesystem::copy_options::overwrite_existing);
        }
    }
    catch (...)
    {
    }
}

void MonitorAuthorsIni()
{
    std::string masterPath = GetDocumentsPath() + "/My Games/Skyrim Special Edition/SKSE/OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini";

    std::string gamePath = GetGamePath();
    std::string assetsPath;

    if (!gamePath.empty())
    {
        assetsPath = (fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini" / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini").string();
    }
    else
    {
        fs::path dllDir = GetPluginDllPath();
        assetsPath = (dllDir / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "ini" / "OSoundtracks-SA-Expansion-Sounds-NG-Autors.ini").string();
    }

    static auto lastModTime = std::filesystem::file_time_type::min();
    while (true)
    {
        try
        {
            if (std::filesystem::exists(masterPath))
            {
                auto currentModTime = std::filesystem::last_write_time(masterPath);
                if (currentModTime > lastModTime)
                {
                    std::filesystem::copy_file(masterPath, assetsPath, std::filesystem::copy_options::overwrite_existing);
                    lastModTime = currentModTime;
                    LoadAndSendAuthors();
                }
            }
        }
        catch (...)
        {
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void StartAuthorsIniMonitoring()
{
    std::thread monitorThread(MonitorAuthorsIni);
    monitorThread.detach();
}

std::string ToHexString(uint32_t val);
void WriteKeyMemoryJS();
void CleanupUninstalledMods();
void CopyMemoryJsToAssets();
void AutoAdjustSecondKey();
std::string ScanCodeToName(uint32_t scanCode);
void OnKeyConflict(const char* data);
void OnGetKeyTrackerConfig(const char *data);

void OnSaveConfig(const char *data)
{
    logger::info("OnSaveConfig called with: {}", data);

    if (!data || strlen(data) == 0)
    {
        logger::warn("Empty config data");
        return;
    }

    try
    {
        static uint32_t previousSecondKey = g_config.secondKey;

        
        std::string json(data);

        
        auto parseValue = [&json](const std::string &key) -> std::string
        {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos)
                return "";
            pos += search.length();

            
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
                pos++;

            
            size_t end = json.find_first_of(",}", pos);
            if (end == std::string::npos)
                end = json.length();

            std::string value = json.substr(pos, end - pos);
            
            if (!value.empty() && value.front() == '"' && value.back() == '"')
            {
                value = value.substr(1, value.length() - 2);
            }
            return value;
        };

        std::string firstKeyStr = parseValue("firstKey");
        std::string secondKeyStr = parseValue("secondKey");
        std::string singleKeyModeStr = parseValue("singleKeyMode");
        std::string volumeStr = parseValue("volume");
        std::string posXStr = parseValue("posX");
        std::string posYStr = parseValue("posY");
        std::string hubSizeStr = parseValue("hubSize");
        std::string muteHubSoundStr = parseValue("muteHubSound");
        std::string hubSoundStr = parseValue("hubSound");
        
        std::string gifVisibleStr = parseValue("gifVisible");
        std::string gifPosXStr = parseValue("gifPosX");
        std::string gifPosYStr = parseValue("gifPosY");
        std::string gifZoomStr = parseValue("gifZoom");
        std::string gifWidthStr = parseValue("gifWidth");
        std::string gifHeightStr = parseValue("gifHeight");
        
        std::string videoVisibleStr = parseValue("videoVisible");
        std::string videoPosXStr = parseValue("videoPosX");
        std::string videoPosYStr = parseValue("videoPosY");
        std::string videoWidthStr = parseValue("videoWidth");
        std::string videoHeightStr = parseValue("videoHeight");
        std::string videoOpacityStr = parseValue("videoOpacity");
        std::string videoVolumeStr = parseValue("videoVolume");
        std::string videoFullscreenStr = parseValue("videoFullscreen");
        
        std::string logsVisibleStr = parseValue("logsVisible");
        std::string logsPosXStr = parseValue("logsPosX");
        std::string logsPosYStr = parseValue("logsPosY");
        std::string logsWidthStr = parseValue("logsWidth");
        std::string logsHeightStr = parseValue("logsHeight");
        std::string logsAutoRefreshStr = parseValue("logsAutoRefresh");
        
        std::string liveVisibleStr = parseValue("liveVisible");
        std::string livePosXStr = parseValue("livePosX");
        std::string livePosYStr = parseValue("livePosY");
        std::string liveSizeStr = parseValue("liveSize");
        std::string liveAutoStartStr = parseValue("liveAutoStart");
        
        std::string bootPosXStr = parseValue("bootPosX");
        std::string bootPosYStr = parseValue("bootPosY");
        
        std::string globalAlphaStr = parseValue("globalAlpha");
        std::string globalColorStr = parseValue("globalColor");
        std::string liveBackgroundStr = parseValue("liveBackground");
        std::string menusSizeStr = parseValue("menusSize");
        std::string itemsSizeStr = parseValue("itemsSize");
        std::string toastSizeStr = parseValue("toastSize");
        std::string hubVisibleSettingsStr = parseValue("hubVisibleSettings");
        std::string proximitySettingsStr = parseValue("proximitySettings");

        if (!firstKeyStr.empty())
        {
            g_config.firstKey = std::stoul(firstKeyStr);
        }
        if (!secondKeyStr.empty())
        {
            previousSecondKey = g_config.secondKey;
            g_config.secondKey = std::stoul(secondKeyStr);
        }
        if (!singleKeyModeStr.empty())
        {
            g_config.singleKeyMode = (singleKeyModeStr == "true" || singleKeyModeStr == "1");
        }
        if (!volumeStr.empty())
        {
            g_config.volume = std::stoi(volumeStr);
            g_config.volume = std::max(0, std::min(100, g_config.volume));
        }
        if (!muteHubSoundStr.empty())
        {
            g_config.muteHubSound = (muteHubSoundStr == "true" || muteHubSoundStr == "1");
        }
        if (!hubSoundStr.empty())
        {
            g_config.hubSound = hubSoundStr;
        }
        if (!posXStr.empty())
        {
            g_config.posX = std::stoi(posXStr);
            g_config.posX = std::max(0, std::min(100, g_config.posX));
        }
        if (!posYStr.empty())
        {
            g_config.posY = std::stoi(posYStr);
            g_config.posY = std::max(0, std::min(100, g_config.posY));
        }
        if (!hubSizeStr.empty())
        {
            g_config.hubSize = std::stoi(hubSizeStr);
            g_config.hubSize = std::max(50, std::min(200, g_config.hubSize));
        }

        
        if (!gifVisibleStr.empty())
        {
            g_config.gifVisible = (gifVisibleStr == "true" || gifVisibleStr == "1");
        }
        if (!gifPosXStr.empty())
        {
            g_config.gifPosX = std::stoi(gifPosXStr);
            g_config.gifPosX = std::max(0, std::min(100, g_config.gifPosX));
        }
        if (!gifPosYStr.empty())
        {
            g_config.gifPosY = std::stoi(gifPosYStr);
            g_config.gifPosY = std::max(0, std::min(100, g_config.gifPosY));
        }
        if (!gifZoomStr.empty())
        {
            g_config.gifZoom = std::stoi(gifZoomStr);
            g_config.gifZoom = std::max(50, std::min(200, g_config.gifZoom));
        }
        if (!gifWidthStr.empty())
        {
            g_config.gifWidth = std::stoi(gifWidthStr);
            g_config.gifWidth = std::max(120, g_config.gifWidth);
        }
        if (!gifHeightStr.empty())
        {
            g_config.gifHeight = std::stoi(gifHeightStr);
            g_config.gifHeight = std::max(120, g_config.gifHeight);
        }

        
        if (!videoVisibleStr.empty())
        {
            g_config.videoVisible = (videoVisibleStr == "true" || videoVisibleStr == "1");
        }
        if (!videoPosXStr.empty())
        {
            g_config.videoPosX = std::stoi(videoPosXStr);
            g_config.videoPosX = std::max(0, std::min(100, g_config.videoPosX));
        }
        if (!videoPosYStr.empty())
        {
            g_config.videoPosY = std::stoi(videoPosYStr);
            g_config.videoPosY = std::max(0, std::min(100, g_config.videoPosY));
        }
        if (!videoWidthStr.empty())
        {
            g_config.videoWidth = std::stoi(videoWidthStr);
            g_config.videoWidth = std::max(200, g_config.videoWidth);
        }
        if (!videoHeightStr.empty())
        {
            g_config.videoHeight = std::stoi(videoHeightStr);
            g_config.videoHeight = std::max(150, g_config.videoHeight);
        }
        if (!videoOpacityStr.empty())
        {
            g_config.videoOpacity = std::stoi(videoOpacityStr);
            g_config.videoOpacity = std::max(20, std::min(100, g_config.videoOpacity));
        }
        if (!videoVolumeStr.empty())
        {
            g_config.videoVolume = std::stoi(videoVolumeStr);
            g_config.videoVolume = std::max(0, std::min(100, g_config.videoVolume));
        }
        if (!videoFullscreenStr.empty())
        {
            g_config.videoFullscreen = (videoFullscreenStr == "true" || videoFullscreenStr == "1");
        }

        
        if (!logsVisibleStr.empty())
        {
            g_config.logsVisible = (logsVisibleStr == "true" || logsVisibleStr == "1");
        }
        if (!logsPosXStr.empty())
        {
            g_config.logsPosX = std::stoi(logsPosXStr);
            g_config.logsPosX = std::max(0, std::min(100, g_config.logsPosX));
        }
        if (!logsPosYStr.empty())
        {
            g_config.logsPosY = std::stoi(logsPosYStr);
            g_config.logsPosY = std::max(0, std::min(100, g_config.logsPosY));
        }
        if (!logsWidthStr.empty())
        {
            g_config.logsWidth = std::stoi(logsWidthStr);
            g_config.logsWidth = std::max(200, g_config.logsWidth);
        }
        if (!logsHeightStr.empty())
        {
            g_config.logsHeight = std::stoi(logsHeightStr);
            g_config.logsHeight = std::max(150, g_config.logsHeight);
        }
        if (!logsAutoRefreshStr.empty())
        {
            g_config.logsAutoRefresh = (logsAutoRefreshStr == "true" || logsAutoRefreshStr == "1");
        }
        
        if (!liveVisibleStr.empty())
        {
            g_config.liveVisible = (liveVisibleStr == "true" || liveVisibleStr == "1");
        }
        if (!livePosXStr.empty())
        {
            g_config.livePosX = std::stoi(livePosXStr);
            g_config.livePosX = std::max(0, std::min(100, g_config.livePosX));
        }
        if (!livePosYStr.empty())
        {
            g_config.livePosY = std::stoi(livePosYStr);
            g_config.livePosY = std::max(0, std::min(100, g_config.livePosY));
        }
        if (!liveSizeStr.empty())
        {
            g_config.liveSize = std::stoi(liveSizeStr);
            g_config.liveSize = std::max(50, std::min(200, g_config.liveSize));
        }
        if (!liveAutoStartStr.empty())
        {
            g_config.liveAutoStart = (liveAutoStartStr == "true" || liveAutoStartStr == "1");
        }
        
        if (!bootPosXStr.empty())
        {
            g_config.bootPosX = std::stoi(bootPosXStr);
            g_config.bootPosX = std::max(0, std::min(100, g_config.bootPosX));
        }
        if (!bootPosYStr.empty())
        {
            g_config.bootPosY = std::stoi(bootPosYStr);
            g_config.bootPosY = std::max(0, std::min(100, g_config.bootPosY));
        }
        
        if (!globalAlphaStr.empty())
        {
            g_config.globalAlpha = std::stoi(globalAlphaStr);
            g_config.globalAlpha = std::max(20, std::min(100, g_config.globalAlpha));
        }
        if (!globalColorStr.empty())
        {
            g_config.globalColor = globalColorStr;
        }
        if (!liveBackgroundStr.empty())
        {
            g_config.liveBackground = liveBackgroundStr;
        }
        if (!menusSizeStr.empty())
        {
            g_config.menusSize = std::stoi(menusSizeStr);
            g_config.menusSize = std::max(50, std::min(200, g_config.menusSize));
        }
        if (!itemsSizeStr.empty())
        {
            g_config.itemsSize = std::stoi(itemsSizeStr);
            g_config.itemsSize = std::max(50, std::min(200, g_config.itemsSize));
        }
        if (!toastSizeStr.empty())
        {
            g_config.toastSize = std::stoi(toastSizeStr);
            g_config.toastSize = std::max(10, std::min(24, g_config.toastSize));
        }
        if (!hubVisibleSettingsStr.empty())
        {
            g_config.hubVisibleSettings = (hubVisibleSettingsStr == "true" || hubVisibleSettingsStr == "1");
        }
        if (!proximitySettingsStr.empty())
        {
            g_config.proximitySettings = (proximitySettingsStr == "true" || proximitySettingsStr == "1");
        }

        SaveConfig();

        bool keyConflict = false;
        std::string conflictModName;
        std::string jsPath = GetMemoryJsPath();
        std::ifstream readJsCheck(jsPath);
        if (readJsCheck.is_open())
        {
            std::string jsContent;
            std::getline(readJsCheck, jsContent, '\0');
            readJsCheck.close();

            std::string newSecondKey = ToHexString(g_config.secondKey);
            for (char &c : newSecondKey) c = tolower(c);

            std::string myDllName = "OSoundtracks-Prisma.dll";
            size_t searchPos = 0;
            while ((searchPos = jsContent.find("\"secondKey\":", searchPos)) != std::string::npos)
            {
                size_t skQuoteStart = jsContent.find("\"", searchPos + 12) + 1;
                size_t skQuoteEnd = jsContent.find("\"", skQuoteStart);
                std::string sk = jsContent.substr(skQuoteStart, skQuoteEnd - skQuoteStart);
                std::string skLower = sk;
                for (char &c : skLower) c = tolower(c);

                if (skLower == newSecondKey)
                {
                    size_t nameSearchBack = jsContent.rfind("\"name\":", searchPos);
                    if (nameSearchBack != std::string::npos)
                    {
                        size_t nStart = jsContent.find("\"", nameSearchBack + 7) + 1;
                        size_t nEnd = jsContent.find("\"", nStart);
                        std::string foundModName = jsContent.substr(nStart, nEnd - nStart);
                        if (foundModName != myDllName)
                        {
                            keyConflict = true;
                            conflictModName = foundModName;
                            break;
                        }
                    }
                }
                searchPos = skQuoteEnd;
            }
        }

        if (keyConflict)
        {
            g_config.secondKey = previousSecondKey;
            SaveConfig();
            OnKeyConflict(conflictModName.c_str());
        }
        else
        {
            previousSecondKey = g_config.secondKey;
            WriteKeyMemoryJS();
            CopyMemoryJsToAssets();
            OnGetKeyTrackerConfig(nullptr);

            if (PrismaUI && g_HealingView)
            {
                PrismaUI->Invoke(g_HealingView, "showMessage('Configuration saved!')");
            }
        }
    }
    catch (const std::exception &e)
    {
        logger::error("Error parsing config: {}", e.what());
    }
}

// MUSIC CONTROL CALLBACKS
void OnPauseMusic(const char*)  { if (g_fnPauseMusic)  g_fnPauseMusic();  }
void OnResumeMusic(const char*) { if (g_fnResumeMusic) g_fnResumeMusic(); }
void OnNextTrack(const char*)   { if (g_fnNextTrack)   g_fnNextTrack();   }

void OnAuthorPreview(const char* data) {
    if (!data || !g_fnAuthorPreview) return;
    std::string author(data);
    if (!author.empty()) g_fnAuthorPreview(author.c_str(), 7);
}

void OnSaveNowPlayingConfig(const char* data) {
    if (!data) return;
    std::string s(data);
    auto extractStr = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        size_t pos = s.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        size_t end = s.find("\"", pos);
        return (end != std::string::npos) ? s.substr(pos, end - pos) : "";
    };
    auto extractInt = [&](const std::string& key) -> int {
        std::string search = "\"" + key + "\":";
        size_t pos = s.find(search);
        if (pos == std::string::npos) return -1;
        pos += search.size();
        try { return std::stoi(s.substr(pos)); } catch (...) { return -1; }
    };
    std::string mode = extractStr("liveNowPlayingMode");
    int seconds      = extractInt("liveNowPlayingSeconds");
    std::string gif  = extractStr("liveNowPlayingGif");
    if (!mode.empty()) g_config.liveNowPlayingMode = mode;
    if (seconds > 0)   g_config.liveNowPlayingSeconds = std::max(3, std::min(120, seconds));
    if (!gif.empty())  g_config.liveNowPlayingGif = gif;
    SaveConfig();
}

void OnGetLogs(const char *data)
{
    
    std::string logType = data ? data : "main";

    logger::info("OnGetLogs called for: {}", logType);

    if (!PrismaUI || !g_HealingView)
    {
        logger::warn("OnGetLogs: PrismaUI or view not ready");
        return;
    }

    
    std::string docsPath = GetDocumentsPath();
    fs::path logPath;

    if (logType == "menus")
    {
        
        logPath = fs::path(docsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" / "OSoundtracks-Prisma-Menus.log";
        
        if (!fs::exists(logPath))
        {
            logPath = fs::path(docsPath) / "My Games" / "Skyrim.INI" / "SKSE" / "OSoundtracks-Prisma-Menus.log";
        }
    }
    else
    {
        
        logPath = fs::path(docsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" / "OSoundtracks-Prisma.log";
        
        if (!fs::exists(logPath))
        {
            logPath = fs::path(docsPath) / "My Games" / "Skyrim.INI" / "SKSE" / "OSoundtracks-Prisma.log";
        }
    }

    
    std::string logContent;
    std::ifstream logFile(logPath);

    if (!logFile.is_open())
    {
        logger::warn("OnGetLogs: Could not open log file: {}", logPath.string());
        
        std::string gamePath = GetGamePath();
        fs::path assetsLogPath;
        if (logType == "menus")
        {
            assetsLogPath = fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Logs" / "OSoundtracks-Prisma-Menus.log";
        }
        else
        {
            assetsLogPath = fs::path(gamePath) / "Data" / "PrismaUI" / "views" / "OSoundtracks-Prisma" / "Assets" / "Logs" / "OSoundtracks-Prisma.log";
        }
        logFile.open(assetsLogPath);
    }

    if (logFile.is_open())
    {
        
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(logFile, line))
        {
            lines.push_back(line);
        }
        logFile.close();

        
        size_t startIdx = lines.size() > 500 ? lines.size() - 500 : 0;
        std::stringstream ss;
        for (size_t i = startIdx; i < lines.size(); i++)
        {
            ss << lines[i] << "\n";
        }
        logContent = ss.str();

        logger::info("OnGetLogs: Read {} lines from log file", lines.size());
    }
    else
    {
        logContent = "[INFO] Log file not found. The log will appear here when available.\n";
        logContent += "[INFO] Expected path: " + logPath.string();
    }

    
    std::string escapedContent;
    for (char c : logContent)
    {
        switch (c)
        {
        case '\\':
            escapedContent += "\\\\";
            break;
        case '"':
            escapedContent += "\\\"";
            break;
        case '\n':
            escapedContent += "\\n";
            break;
        case '\r':
            escapedContent += "\\r";
            break;
        case '\t':
            escapedContent += "\\t";
            break;
        default:
            escapedContent += c;
        }
    }

    
    std::string script = "window.updateLogsContent(\"" + escapedContent + "\")";
    PrismaUI->Invoke(g_HealingView, script.c_str());
}

class InputEventHandler : public RE::BSTEventSink<RE::InputEvent *>
{
public:
    static InputEventHandler *GetSingleton()
    {
        static InputEventHandler singleton;
        return &singleton;
    }

    static void RegisterSink()
    {
        auto *inputMgr = RE::BSInputDeviceManager::GetSingleton();
        if (inputMgr)
        {
            inputMgr->AddEventSink(GetSingleton());
            logger::info("Input event handler registered (Press Shift+A to toggle menu)");
        }
    }

    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent *const *a_event, RE::BSTEventSource<RE::InputEvent *> *) override
    {
        if (!a_event)
            return RE::BSEventNotifyControl::kContinue;

        
        static bool modifierHeld = false;

        for (RE::InputEvent *event = *a_event; event; event = event->next)
        {
            if (event->eventType != RE::INPUT_EVENT_TYPE::kButton)
                continue;

            auto *buttonEvent = event->AsButtonEvent();
            if (!buttonEvent || buttonEvent->GetDevice() != RE::INPUT_DEVICE::kKeyboard)
                continue;

            uint32_t idCode = buttonEvent->GetIDCode();
            bool isDown = buttonEvent->IsDown();
            bool isPressed = buttonEvent->IsPressed();

            
            if (idCode == g_config.firstKey)
            {
                modifierHeld = isPressed;
            }

            
            if (!g_config.singleKeyMode)
            {
                
                
                if (idCode == g_config.secondKey && isDown && modifierHeld)
                {
                    logger::info("Activation combo detected: first key + second key (modifierHeld={})", modifierHeld);
                    ToggleHealingMenu();
                }
            }
            else
            {
                
                if (idCode == g_config.secondKey && isDown)
                {
                    logger::info("Activation key pressed");
                    ToggleHealingMenu();
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

private:
    InputEventHandler() = default;
};

void ToggleHealingMenu()
{
    if (!PrismaUI || !g_HealingView)
    {
        logger::warn("PrismaUI not initialized or view not created");
        return;
    }

    float volume = g_config.volume / 100.0f;

    if (g_ViewFocused)
    {
        
        if (!g_config.muteHubSound)
        {
            PlayBASSSound(g_config.hubSound, volume);
        }

        PrismaUI->Unfocus(g_HealingView);

        
        if (g_config.liveVisible)
        {
            
            PrismaUI->Invoke(g_HealingView, "hideHubKeepLive()");
            logger::info("Healing menu closed (persistent mode - Panel Live visible)");
        }
        else
        {
            
            PrismaUI->Hide(g_HealingView);
            logger::info("Healing menu closed");
        }

        g_ViewFocused = false;

        
        auto *msgQ = RE::UIMessageQueue::GetSingleton();
        if (msgQ)
        {
            msgQ->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    }
    else
    {
        
        if (!g_config.muteHubSound)
        {
            PlayBASSSound(g_config.hubSound, volume);
        }

        
        PrismaUI->Invoke(g_HealingView, "showHub()");

        UpdateUIStats();
        PrismaUI->Show(g_HealingView);
        PrismaUI->Focus(g_HealingView);
        g_ViewFocused = true;

        
        auto *msgQ = RE::UIMessageQueue::GetSingleton();
        if (msgQ)
        {
            msgQ->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
        }

        logger::info("Healing menu opened");
    }
}

class MenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static MenuEventHandler *GetSingleton()
    {
        static MenuEventHandler singleton;
        return &singleton;
    }

    static void RegisterSink()
    {
        auto *ui = RE::UI::GetSingleton();
        if (ui)
        {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton());
            logger::info("Menu event handler registered");
            WriteToMenusLog("[INIT] Menu event monitoring started");
        }
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent *event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override
    {
        if (!event)
        {
            return RE::BSEventNotifyControl::kContinue;
        }

        std::string menuName = event->menuName.c_str();
        bool isOpening = event->opening;

        
        std::stringstream msg;
        msg << "[MENU] " << menuName << " " << (isOpening ? "OPENED" : "CLOSED");
        WriteToMenusLog(msg.str());

        
        logger::info("Menu event: {} {}", menuName, isOpening ? "opened" : "closed");

        
        if (g_detectedMenus.find(menuName) == g_detectedMenus.end())
        {
            g_detectedMenus[menuName] = 1;
            SaveMenusConfig();
            logger::info("New menu detected and added: {}", menuName);
        }

        
        
        bool isTracked = (g_detectedMenus.find(menuName) != g_detectedMenus.end() &&
                          g_detectedMenus[menuName] == 1);

        if (isTracked)
        {
            if (isOpening)
            {
                g_openTrackedMenus.insert(menuName);
                logger::info("Tracked menu OPENED: {} ({} total open)", menuName, g_openTrackedMenus.size());
            }
            else
            {
                g_openTrackedMenus.erase(menuName);
                logger::info("Tracked menu CLOSED: {} ({} total open)", menuName, g_openTrackedMenus.size());
            }
            
            UpdatePanelLiveVisibility();
            
            std::string statusScript = std::string("updateMenuStatus('") + menuName + "', " + (isOpening ? "true" : "false") + ")";
            PrismaUI->Invoke(g_HealingView, statusScript.c_str());
        }

        return RE::BSEventNotifyControl::kContinue;
    }

 private:
    MenuEventHandler() = default;
};

void OnKeyConflict(const char* data)
{
    if (!PrismaUI || !g_HealingView) return;
    std::string script = "showKeyConflictToast('" + std::string(data) + "')";
    PrismaUI->Invoke(g_HealingView, script.c_str());
}

void InitializePrismaUI()
{
    logger::info("Initializing Prisma UI...");

    
    PrismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1 *>(
        PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));

    if (!PrismaUI)
    {
        logger::error("Failed to get PrismaUI API! Make sure PrismaUI.dll is loaded.");
        return;
    }

    logger::info("PrismaUI API acquired successfully");

    
    g_HealingView = PrismaUI->CreateView("OSoundtracks-Prisma/index.html", [](PrismaView view)
                                         {
                                             logger::info("Healing view DOM ready: {}", view);

                                              UpdateUIStats();
                                              LoadAndSendAuthors();
                                             LoadAndSendConfig();

                                              // PRISMA UI EVENT HANDLERS
                                              PrismaUI->RegisterJSListener(view, "onHealAll", OnHealAll);
                                             PrismaUI->RegisterJSListener(view, "onHealHealth", OnHealHealth);
                                             PrismaUI->RegisterJSListener(view, "onHealMagicka", OnHealMagicka);
                                             PrismaUI->RegisterJSListener(view, "onHealStamina", OnHealStamina);
                                             PrismaUI->RegisterJSListener(view, "onRestorePercent", OnRestorePercent);

                                              
                                              PrismaUI->RegisterJSListener(view, "onGiveGold", OnGiveGold);

                                              PrismaUI->RegisterJSListener(view, "onCloseMenu", OnCloseMenu);
                                              PrismaUI->RegisterJSListener(view, "onOpenURL", OnOpenURL);

                                             
                                             PrismaUI->RegisterJSListener(view, "onPlayVideoAudio", OnPlayVideoAudio);
                                             PrismaUI->RegisterJSListener(view, "onPauseVideoAudio", OnPauseVideoAudio);
                                             PrismaUI->RegisterJSListener(view, "onStopVideoAudio", OnStopVideoAudio);
                                             PrismaUI->RegisterJSListener(view, "onSeekVideoAudio", OnSeekVideoAudio);
                                             PrismaUI->RegisterJSListener(view, "onSetVideoVolume", OnSetVideoVolume);

                                             
                                             PrismaUI->RegisterJSListener(view, "onPlayImpactSound", OnPlayImpactSound);
                                             PrismaUI->RegisterJSListener(view, "onStopImpactSound", OnStopImpactSound);

                                             PrismaUI->RegisterJSListener(view, "onPlayMeowSound", OnPlayMeowSound);
                                             PrismaUI->RegisterJSListener(view, "onPlayRobotTalkSound", OnPlayRobotTalkSound);
                                             PrismaUI->RegisterJSListener(view, "onPlayJackpotSound", OnPlayJackpotSound);
                                             PrismaUI->RegisterJSListener(view, "onStopJackpotSound", OnStopJackpotSound);
                                             PrismaUI->RegisterJSListener(view, "onPlayJackpotOutSound", OnPlayJackpotOutSound);

                                             PrismaUI->RegisterJSListener(view, "onPlayDoomMusic", OnPlayDoomMusic);
                                             PrismaUI->RegisterJSListener(view, "onStopDoomMusic", OnStopDoomMusic);


                                             PrismaUI->RegisterJSListener(view, "onGetConfig", OnGetConfig);
                                             PrismaUI->RegisterJSListener(view, "onSaveConfig", OnSaveConfig);
PrismaUI->RegisterJSListener(view, "onGetKeyTrackerConfig", OnGetKeyTrackerConfig);
                                             PrismaUI->RegisterJSListener(view, "onUpdateIniValue", OnUpdateIniValue);

     
     PrismaUI->RegisterJSListener(view, "onPauseMusic",          OnPauseMusic);
     PrismaUI->RegisterJSListener(view, "onResumeMusic",         OnResumeMusic);
     PrismaUI->RegisterJSListener(view, "onNextTrack",           OnNextTrack);
     PrismaUI->RegisterJSListener(view, "onAuthorPreview",       OnAuthorPreview);
     PrismaUI->RegisterJSListener(view, "onSaveNowPlayingConfig",OnSaveNowPlayingConfig);

                                              
                                              PrismaUI->RegisterJSListener(view, "onGetLogs", OnGetLogs);

                                              
                                              PrismaUI->RegisterJSListener(view, "onRequestPlayerStats", OnRequestPlayerStats);

                                               PrismaUI->RegisterJSListener(view, "onGetMenusConfig", OnGetMenusConfig);
                                               PrismaUI->RegisterJSListener(view, "onToggleMenu", OnToggleMenu);
                                               PrismaUI->RegisterJSListener(view, "onGetAuthorsConfig", OnGetAuthorsConfig);

                                               
PrismaUI->RegisterJSListener(view, "onGetItemsConfig", OnGetItemsConfig);
                                                PrismaUI->RegisterJSListener(view, "onSaveItemsConfig", OnSaveItemsConfig);

                                                PrismaUI->RegisterJSListener(view, "onKeyConflict", OnKeyConflict);

                                                logger::info("JavaScript listeners registered");

                                             
                                             OnGetConfig(nullptr);

                                             
                                             
                                             
                                             PrismaUI->Invoke(g_HealingView, "startBootAnimation()");
                                             logger::info("Boot animation sequence started"); });

    
    
    
    
    LoadSoundPlayerFunctions();
    StartNowPlayingTimer();

    if (g_HealingView)
    {
        g_ViewCreated = true;
        logger::info("Healing view created successfully: {}", g_HealingView);

        
        PrismaUI->Show(g_HealingView);
    }
    else
    {
        logger::error("Failed to create healing view!");
    }
}

std::string ToHexString(uint32_t val)
{
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << val;
    return ss.str();
}

std::string ScanCodeToName(uint32_t scanCode)
{
    static const std::unordered_map<uint32_t, std::string> keyNames = {
        {0x2A, "Left Shift"}, {0x36, "Right Shift"},
        {0x1D, "Left Ctrl"}, {0x1C, "Right Ctrl"},
        {0x38, "Left Alt"},
        {0x1E, "A"}, {0x30, "B"}, {0x2E, "C"}, {0x20, "D"}, {0x12, "E"},
        {0x21, "F"}, {0x22, "G"}, {0x23, "H"}, {0x17, "I"}, {0x24, "J"},
        {0x25, "K"}, {0x26, "L"}, {0x32, "M"}, {0x31, "N"}, {0x18, "O"},
        {0x19, "P"}, {0x10, "Q"}, {0x13, "R"}, {0x1F, "S"}, {0x14, "T"},
        {0x16, "U"}, {0x2F, "V"}, {0x11, "W"}, {0x2D, "X"}, {0x15, "Y"}, {0x2C, "Z"},
        {0x02, "0"}, {0x03, "1"}, {0x04, "2"}, {0x05, "3"}, {0x06, "4"},
        {0x07, "5"}, {0x08, "6"}, {0x09, "7"}, {0x0A, "8"}, {0x0B, "9"}
    };
    auto it = keyNames.find(scanCode);
    if (it != keyNames.end()) return it->second;
    return ToHexString(scanCode);
}

void CleanupUninstalledMods()
{
    fs::path dllDir = GetPluginDllPath();
    if (dllDir.empty()) return;

    std::set<std::string> existingDlls;
    try {
        for (const auto& entry : fs::directory_iterator(dllDir)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();
            if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".dll") {
                existingDlls.insert(filename);
            }
        }
    } catch (...) {
        logger::info("CleanupUninstalledMods: Could not scan DLL directory");
        return;
    }

    std::string jsPath = GetMemoryJsPath();
    std::ifstream readJs(jsPath);
    if (!readJs.is_open()) return;

    std::string content;
    std::getline(readJs, content, '\0');
    readJs.close();

    struct ModData {
        std::string name;
        std::string firstKeyStr;
        std::string secondKeyStr;
        bool singleKeyMode;
    };

    std::vector<ModData> mods;

    size_t modsPos = content.find("\"mods\":");
    if (modsPos != std::string::npos)
    {
        size_t arrayStart = content.find("[", modsPos);
        size_t arrayEnd = content.find("]", arrayStart);
        if (arrayStart != std::string::npos && arrayEnd != std::string::npos)
        {
            std::string modsArray = content.substr(arrayStart, arrayEnd - arrayStart + 1);

            size_t pos = 0;
            while ((pos = modsArray.find("\"name\":", pos)) != std::string::npos)
            {
                ModData mod;

                size_t nameStart = modsArray.find("\"", pos + 8) + 1;
                size_t nameEnd = modsArray.find("\"", nameStart);
                if (nameStart == std::string::npos || nameEnd == std::string::npos) break;
                mod.name = modsArray.substr(nameStart, nameEnd - nameStart);

                size_t firstKeyPos = modsArray.find("\"firstKey\":", nameEnd);
                size_t fkStart = modsArray.find("\"", firstKeyPos + 11) + 1;
                size_t fkEnd = modsArray.find("\"", fkStart);
                mod.firstKeyStr = modsArray.substr(fkStart, fkEnd - fkStart);

                size_t secondKeyPos = modsArray.find("\"secondKey\":", fkEnd);
                size_t skStart = modsArray.find("\"", secondKeyPos + 12) + 1;
                size_t skEnd = modsArray.find("\"", skStart);
                mod.secondKeyStr = modsArray.substr(skStart, skEnd - skStart);

                size_t singleKeyPos = modsArray.find("\"singleKeyMode\":", skEnd);
                size_t smStart = singleKeyPos + 16;
                size_t smEnd = modsArray.find("}", smStart);
                std::string smStr = modsArray.substr(smStart, smEnd - smStart);
                mod.singleKeyMode = (smStr.find("true") != std::string::npos);

                mods.push_back(mod);
                pos = smEnd;
            }
        }
    }

    int removedCount = 0;
    std::vector<ModData> cleanMods;
    for (auto& mod : mods)
    {
        if (existingDlls.find(mod.name) != existingDlls.end())
        {
            cleanMods.push_back(mod);
        }
        else
        {
            logger::info("CleanupUninstalledMods: {} was uninstalled, removing from memory", mod.name);
            removedCount++;
        }
    }

    if (removedCount > 0)
    {
        std::ofstream outFile(jsPath);
        if (outFile.is_open())
        {
            outFile << "var prismaKeyMemory = {\n";
            outFile << "  \"mods\": [\n";
            for (size_t i = 0; i < cleanMods.size(); i++)
            {
                uint32_t fkCode = 0, skCode = 0;
                try { fkCode = std::stoul(cleanMods[i].firstKeyStr, nullptr, 0); } catch (...) {}
                try { skCode = std::stoul(cleanMods[i].secondKeyStr, nullptr, 0); } catch (...) {}

                outFile << "    {\n";
                outFile << "      \"name\": \"" << cleanMods[i].name << "\",\n";
                outFile << "      \"activation\": {\n";
                outFile << "        \"firstKey\": \"" << cleanMods[i].firstKeyStr << "\",\n";
                outFile << "        \"firstKeyName\": \"" << ScanCodeToName(fkCode) << "\",\n";
                outFile << "        \"secondKey\": \"" << cleanMods[i].secondKeyStr << "\",\n";
                outFile << "        \"secondKeyName\": \"" << ScanCodeToName(skCode) << "\",\n";
                outFile << "        \"singleKeyMode\": " << (cleanMods[i].singleKeyMode ? "true" : "false") << "\n";
                outFile << "      }\n";
                outFile << "    }";
                if (i < cleanMods.size() - 1) outFile << ",";
                outFile << "\n";
            }
            outFile << "  ]\n";
            outFile << "};\n";
            outFile.close();
        }
        logger::info("CleanupUninstalledMods: {} uninstalled mod(s) removed from memory", removedCount);
    }
}

void WriteKeyMemoryJS()
{
    std::string jsPath = GetMemoryJsPath();

    uint32_t firstKey = g_config.firstKey;
    uint32_t secondKey = g_config.secondKey;
    bool singleKeyMode = g_config.singleKeyMode;

    std::string dllName = "OSoundtracks-Prisma.dll";

    struct ModData {
        std::string name;
        std::string firstKeyStr;
        std::string secondKeyStr;
        bool singleKeyMode;
    };

    std::vector<ModData> mods;

    std::ifstream readJs(jsPath);
    std::string content;
    if (readJs.is_open())
    {
        std::getline(readJs, content, '\0');
        readJs.close();

        size_t modsPos = content.find("\"mods\":");
        if (modsPos != std::string::npos)
        {
            size_t arrayStart = content.find("[", modsPos);
            size_t arrayEnd = content.find("]", arrayStart);
            if (arrayStart != std::string::npos && arrayEnd != std::string::npos)
            {
                std::string modsArray = content.substr(arrayStart, arrayEnd - arrayStart + 1);

                size_t pos = 0;
                while ((pos = modsArray.find("\"name\":", pos)) != std::string::npos)
                {
                    ModData mod;

                    size_t nameStart = modsArray.find("\"", pos + 8) + 1;
                    size_t nameEnd = modsArray.find("\"", nameStart);
                    if (nameStart == std::string::npos || nameEnd == std::string::npos) break;
                    mod.name = modsArray.substr(nameStart, nameEnd - nameStart);

                    size_t firstKeyPos = modsArray.find("\"firstKey\":", nameEnd);
                    size_t fkStart = modsArray.find("\"", firstKeyPos + 11) + 1;
                    size_t fkEnd = modsArray.find("\"", fkStart);
                    mod.firstKeyStr = modsArray.substr(fkStart, fkEnd - fkStart);

                    size_t secondKeyPos = modsArray.find("\"secondKey\":", fkEnd);
                    size_t skStart = modsArray.find("\"", secondKeyPos + 12) + 1;
                    size_t skEnd = modsArray.find("\"", skStart);
                    mod.secondKeyStr = modsArray.substr(skStart, skEnd - skStart);

                    size_t singleKeyPos = modsArray.find("\"singleKeyMode\":", skEnd);
                    size_t smStart = singleKeyPos + 16;
                    size_t smEnd = modsArray.find("}", smStart);
                    std::string smStr = modsArray.substr(smStart, smEnd - smStart);
                    mod.singleKeyMode = (smStr.find("true") != std::string::npos);

                    mods.push_back(mod);
                    pos = smEnd;
                }
            }
        }
    }

    bool found = false;
    for (auto& mod : mods)
    {
        if (mod.name == dllName)
        {
            found = true;
            mod.firstKeyStr = ToHexString(firstKey);
            mod.secondKeyStr = ToHexString(secondKey);
            mod.singleKeyMode = singleKeyMode;
            break;
        }
    }

    if (!found)
    {
        ModData newMod;
        newMod.name = dllName;
        newMod.firstKeyStr = ToHexString(firstKey);
        newMod.secondKeyStr = ToHexString(secondKey);
        newMod.singleKeyMode = singleKeyMode;
        mods.push_back(newMod);
    }

    std::ofstream outFile(jsPath);
    if (outFile.is_open())
    {
        outFile << "var prismaKeyMemory = {\n";
        outFile << "  \"mods\": [\n";
        for (size_t i = 0; i < mods.size(); i++)
        {
            uint32_t fkCode = 0, skCode = 0;
            try { fkCode = std::stoul(mods[i].firstKeyStr, nullptr, 0); } catch (...) {}
            try { skCode = std::stoul(mods[i].secondKeyStr, nullptr, 0); } catch (...) {}

            outFile << "    {\n";
            outFile << "      \"name\": \"" << mods[i].name << "\",\n";
            outFile << "      \"activation\": {\n";
            outFile << "        \"firstKey\": \"" << mods[i].firstKeyStr << "\",\n";
            outFile << "        \"firstKeyName\": \"" << ScanCodeToName(fkCode) << "\",\n";
            outFile << "        \"secondKey\": \"" << mods[i].secondKeyStr << "\",\n";
            outFile << "        \"secondKeyName\": \"" << ScanCodeToName(skCode) << "\",\n";
            outFile << "        \"singleKeyMode\": " << (mods[i].singleKeyMode ? "true" : "false") << "\n";
            outFile << "      }\n";
            outFile << "    }";
            if (i < mods.size() - 1) outFile << ",";
            outFile << "\n";
        }
        outFile << "  ]\n";
        outFile << "};\n";
        outFile.close();
    }
}

void AutoAdjustSecondKey()
{
    std::string jsPath = GetMemoryJsPath();
    std::ifstream readJs(jsPath);
    std::string content;
    
    if (!readJs.is_open()) return;
    
    std::getline(readJs, content, '\0');
    readJs.close();
    
    std::vector<std::string> usedSecondKeys;
    std::string dllName = "OSoundtracks-Prisma.dll";
    
    size_t pos = 0;
    while ((pos = content.find("\"name\":", pos)) != std::string::npos)
    {
        size_t nameStart = content.find("\"", pos + 8) + 1;
        size_t nameEnd = content.find("\"", nameStart);
        if (nameStart == std::string::npos || nameEnd == std::string::npos) break;
        std::string modName = content.substr(nameStart, nameEnd - nameStart);
        
        size_t secondKeyPos = content.find("\"secondKey\":", nameEnd);
        if (secondKeyPos == std::string::npos) break;
        size_t skStart = content.find("\"", secondKeyPos + 12) + 1;
        size_t skEnd = content.find("\"", skStart);
        if (skStart == std::string::npos || skEnd == std::string::npos) break;
        std::string sk = content.substr(skStart, skEnd - skStart);
        
        if (modName != dllName)
        {
            usedSecondKeys.push_back(sk);
        }
        
        pos = skEnd;
    }
    
    std::vector<std::string> letterKeys = {
        "0x1e", "0x30", "0x2e", "0x20", "0x12", "0x21", "0x22", "0x23", "0x17", "0x24",
        "0x25", "0x26", "0x32", "0x31", "0x18", "0x19", "0x10", "0x13", "0x1f", "0x14",
        "0x16", "0x2f", "0x11", "0x2d", "0x15", "0x2c"
    };
    
    std::string currentKey = ToHexString(g_config.secondKey);
    for (char &c : currentKey) c = tolower(c);
    
    for (const auto &used : usedSecondKeys)
    {
        std::string usedLower = used;
        for (char &c : usedLower) c = tolower(c);
        if (usedLower == currentKey)
        {
            for (const auto &letter : letterKeys)
            {
                bool available = true;
                for (const auto &u : usedSecondKeys)
                {
                    std::string uLower = u;
                    for (char &c : uLower) c = tolower(c);
                    if (uLower == letter)
                    {
                        available = false;
                        break;
                    }
                }
                if (available)
                {
                    g_config.secondKey = std::stoul(letter, nullptr, 0);
                    logger::info("SecondKey auto-adjusted to {} due to conflict", letter);
                    SaveConfig();
                    return;
                }
            }
            break;
        }
    }
}

void CopyMemoryJsToAssets()
{
    fs::path assetsDir = GetAssetsIniDir();
    std::string jsPath = GetMemoryJsPath();
    
    try
    {
        if (!fs::exists(assetsDir))
        {
            fs::create_directories(assetsDir);
        }
        fs::copy_file(jsPath, assetsDir / "Prisma-John95AC-Memory.js", fs::copy_options::overwrite_existing);
        logger::info("Memory JS copied to Assets/ini");
    }
    catch (const std::exception &e)
    {
        logger::error("Failed to copy Memory JS: {}", e.what());
    }
}

void OnGetKeyTrackerConfig(const char *data)
{
    if (!PrismaUI || !g_HealingView)
        return;

    std::string jsPath = GetMemoryJsPath();
    logger::info("Reading Key Tracker data from {}", jsPath);

    std::ifstream inFile(jsPath);
    if (!inFile.is_open())
    {
        logger::warn("Cannot read Memory.js from {}", jsPath);
        return;
    }

    std::stringstream buffer;
    buffer << inFile.rdbuf();
    std::string content = buffer.str();
    inFile.close();

    logger::info("Content length: {} bytes", content.length());

    std::string escaped;
    for (char c : content)
    {
        if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else if (c == '\'') escaped += "\\'";
        else if (c == '\\') escaped += "\\\\";
        else escaped += c;
    }

    std::string script = "updateKeyTrackerData('" + escaped + "')";
    PrismaUI->Invoke(g_HealingView, script.c_str());
    logger::info("Sent Key Tracker data to JS");
}

void MessageListener(SKSE::MessagingInterface::Message *message)
{
    switch (message->type)
    {
    case SKSE::MessagingInterface::kDataLoaded:
    {
        logger::info("Data loaded, initializing OSoundtracks Prisma v6.1.0...");

        
        std::string gamePath = GetGamePath();
        logger::info("Game path: {}", gamePath);
        logger::info("Documents path: {}", GetDocumentsPath());

        
        LoadConfig();

        
        LoadMenusConfig();

        
        LoadItemsConfig();

        
        InitializeBASSLibrary();

        
        InitializePrismaUI();

        
        ScanMusicJson();
        WritePrismaListIni();

        
        AutoAdjustSecondKey();
        CleanupUninstalledMods();
        WriteKeyMemoryJS();
        CopyMemoryJsToAssets();
        OnGetKeyTrackerConfig(nullptr);

        
        InputEventHandler::RegisterSink();

        
        MenuEventHandler::RegisterSink();

        break;
    }

    default:
        if (message->type == 0x4F535052) {
            logger::info("Prisma open signal received from MCM");
            ToggleHealingMenu();
        }
        break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *a_skse)
{
    SKSE::Init(a_skse);
    SetupLog();
    SKSE::AllocTrampoline(1 << 10);
    auto *messaging = reinterpret_cast<SKSE::MessagingInterface *>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));
    if (!messaging)
    {
        logger::critical("Failed to get messaging interface!");
        return false;
    }

    messaging->RegisterListener("SKSE", MessageListener);

    CopyAuthorsIniToAssets();
    StartAuthorsIniMonitoring();

    logger::info("OSoundtracks Prisma plugin loaded successfully");
    return true;
}

// SKSE PLUGIN MAIN
constinit auto SKSEPlugin_Version = []()
{
    SKSE::PluginVersionData v;
    v.PluginVersion({6, 1, 0});
    v.PluginName("OSoundtracks-Prisma");
    v.AuthorName("John95AC");
    v.UsesAddressLibrary();
    v.UsesSigScanning();
    v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST, SKSE::RUNTIME_LATEST_VR});
    return v;
}();
