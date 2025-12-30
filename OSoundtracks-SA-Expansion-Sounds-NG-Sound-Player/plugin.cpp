#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <RE/G/GameSettingCollection.h> 
#include <shlobj.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <Psapi.h>
#include "bass.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;
namespace logger = SKSE::log;

struct SKSELogsPaths {
    fs::path primary;
    fs::path secondary;
};

struct SoundOption {
    std::string soundFile;
    int listNumber;
    int layerNumber;
    
    SoundOption() : soundFile(""), listNumber(1), layerNumber(0) {}
    SoundOption(const std::string& file, int list) : soundFile(file), listNumber(list), layerNumber(0) {}
    SoundOption(const std::string& file, int list, int layer) : soundFile(file), listNumber(list), layerNumber(layer) {}
};

struct SoundConfigMultiple {
    std::vector<SoundOption> soundOptions;
    std::map<int, std::vector<SoundOption>> layers;
    int repeatDelaySeconds;

    SoundConfigMultiple() : repeatDelaySeconds(0) {}
};

enum ScriptType { SCRIPT_BASE, SCRIPT_SPECIFIC, SCRIPT_MENU, SCRIPT_CHECK, SCRIPT_EFFECT, SCRIPT_POSITION, SCRIPT_TAG };

struct ScriptState {
    std::string name;
    fs::path scriptPath;
    fs::path stopFilePath;
    fs::path trackFilePath;
    PROCESS_INFORMATION processInfo;
    bool isRunning;
    bool isPaused;
    std::string currentTrack;

    ScriptState()
        : name(""),
          scriptPath(""),
          stopFilePath(""),
          trackFilePath(""),
          processInfo({0}),
          isRunning(false),
          isPaused(false),
          currentTrack("") {}
};

static std::ofstream g_soundPlayerLog;
static std::ofstream g_actionsLog;
static std::deque<std::string> g_heartbeatLines;
static std::deque<std::string> g_actionLines;
static std::string g_documentsPath;
static std::string g_gamePath;
static bool g_isInitialized = false;
static std::mutex g_logMutex;
static std::streampos g_lastOStimLogPosition = 0;
static bool g_monitoringActive = false;
static std::thread g_monitorThread;
static int g_monitorCycles = 0;
static std::unordered_set<std::string> g_processedLines;
static size_t g_lastFileSize = 0;
static std::string g_lastAnimation = "";

static std::unordered_map<std::string, SoundConfigMultiple> g_animationSoundMap;
static std::unordered_map<std::string, SoundConfigMultiple> g_effectSoundMap;
static std::unordered_map<std::string, SoundConfigMultiple> g_positionSoundMap;
static std::unordered_map<std::string, SoundConfigMultiple> g_tagSoundMap;

static ScriptState g_baseScript;
static ScriptState g_menuScript;
static ScriptState g_specificScript;
static ScriptState g_checkScript;

static bool g_scriptsInitialized = false;
static bool g_firstAnimationDetected = false;
static std::chrono::steady_clock::time_point g_monitoringStartTime;
static bool g_initialDelayComplete = false;
static std::atomic<bool> g_isShuttingDown(false);

static std::atomic<bool> g_pauseMonitoring(false);
static bool g_activationMessageShown = false;

static std::thread g_heartbeatThread;
static std::atomic<bool> g_heartbeatActive(false);

static std::string g_currentBaseAnimation = "";
static std::string g_currentSpecificAnimation = "";
static std::string g_currentPositionFragment = "";

static std::atomic<bool> g_startupSoundEnabled(true);
static std::atomic<bool> g_topNotificationsVisible(true);
static std::time_t g_lastIniCheckTime = 0;
static fs::path g_iniPath;
static std::thread g_iniMonitorThread;
static std::atomic<bool> g_monitoringIni(false);

static std::atomic<bool> g_soundsPaused(false);
static std::mutex g_pauseMutex;

static bool g_baseWasActiveBeforePause = false;
static bool g_menuWasActiveBeforePause = false;
static bool g_specificWasActiveBeforePause = false;
static bool g_positionWasActiveBeforePause = false;
static bool g_effectWasActiveBeforePause = false;
static bool g_tagWasActiveBeforePause = false;

static std::unordered_map<std::string, std::string> g_activeMenuSounds;
static std::unordered_map<std::string, PROCESS_INFORMATION> g_menuSoundProcesses;
static std::mutex g_menuSoundMutex;

static std::vector<std::string> g_baseTracks;
static std::vector<std::string> g_specificTracks;
static std::vector<std::string> g_menuTracks;

static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_lastPlayTime;
static std::mutex g_throttleMutex;

static bool g_usingDllPath = false;
static fs::path g_dllDirectory;
static fs::path g_soundsDirectory;
static fs::path g_scriptsDirectory;

static std::atomic<float> g_baseVolume(0.8f);
static std::atomic<float> g_menuVolume(0.6f);
static std::atomic<float> g_specificVolume(0.9f);
static std::atomic<float> g_effectVolume(1.0f);
static std::atomic<float> g_positionVolume(1.0f);
static std::atomic<float> g_tagVolume(1.0f);
static std::atomic<float> g_soundMenuKeyVolume(1.0f);
static std::atomic<bool> g_volumeControlEnabled(true);

enum class SoundMenuKeyMode {
    DISABLED,
    ALL_ORDER,
    ALL_RANDOM,
    AUTHOR_ORDER,
    AUTHOR_RANDOM
};

static std::unordered_map<std::string, std::vector<SoundOption>> g_soundMenuKeyMap;
static SoundMenuKeyMode g_soundMenuKeyMode = SoundMenuKeyMode::DISABLED;
static std::string g_soundMenuKeyAuthor = "";
static std::vector<std::string> g_soundMenuKeyPlaylist;
static size_t g_soundMenuKeyCurrentIndex = 0;
static std::atomic<bool> g_soundMenuKeyActive(false);
static std::atomic<bool> g_soundMenuKeyPaused(false);
static std::string g_currentSoundMenuKeyTrack = "";
static std::mutex g_soundMenuKeyMutex;
static std::thread g_soundMenuKeyMonitorThread;
static std::atomic<bool> g_soundMenuKeyMonitorActive(false);
static bool g_soundMenuKeyWasActiveBeforePause = false;

static HSTREAM g_authorPreviewStream = 0;
static std::string g_lastAuthorName = "";
static std::atomic<bool> g_previewPlaying(false);
static std::thread g_previewTimerThread;
static std::atomic<bool> g_previewTimerActive(false);

static bool g_iniFirstLoad = true;

static fs::path g_backupDirectory;

static std::atomic<bool> g_backupUpdateEnabled(false);

static std::atomic<bool> g_muteGameMusicDuringOStim(true);
static float g_originalMusicVolume = 1.0f;
static bool g_gameMusicMuted = false;

// ========================================
// BASS Audio Library - Global Variables
// ========================================
static HMODULE g_bassModule = nullptr;
static bool g_bassInitialized = false;
static std::mutex g_bassMutex;

static HSTREAM g_baseStream = 0;
static HSTREAM g_menuStream = 0;
static HSTREAM g_specificStream = 0;
static HSTREAM g_effectStream = 0;
static HSTREAM g_positionStream = 0;
static HSTREAM g_tagStream = 0;
static HSTREAM g_soundMenuKeyStream = 0;

static std::map<std::string, std::map<int, HSTREAM>> g_positionStreams;
static std::set<std::string> g_activePositionFragments;
static std::map<std::string, std::map<int, bool>> g_positionLayersActiveBeforePause;

// BASS Function Pointers
typedef BOOL(WINAPI* BASS_Init_t)(int, DWORD, DWORD, HWND, void*);
typedef BOOL(WINAPI* BASS_Free_t)();
typedef int(WINAPI* BASS_ErrorGetCode_t)();
typedef HSTREAM(WINAPI* BASS_StreamCreateFile_t)(BOOL, const void*, QWORD, QWORD, DWORD);
typedef BOOL(WINAPI* BASS_ChannelPlay_t)(DWORD, BOOL);
typedef BOOL(WINAPI* BASS_ChannelStop_t)(DWORD);
typedef BOOL(WINAPI* BASS_ChannelPause_t)(DWORD);
typedef BOOL(WINAPI* BASS_StreamFree_t)(DWORD);
typedef BOOL(WINAPI* BASS_ChannelSetAttribute_t)(DWORD, DWORD, float);
typedef DWORD(WINAPI* BASS_ChannelIsActive_t)(DWORD);

static BASS_Init_t pBASS_Init = nullptr;
static BASS_Free_t pBASS_Free = nullptr;
static BASS_ErrorGetCode_t pBASS_ErrorGetCode = nullptr;
static BASS_StreamCreateFile_t pBASS_StreamCreateFile = nullptr;
static BASS_ChannelPlay_t pBASS_ChannelPlay = nullptr;
static BASS_ChannelStop_t pBASS_ChannelStop = nullptr;
static BASS_ChannelPause_t pBASS_ChannelPause = nullptr;
static BASS_StreamFree_t pBASS_StreamFree = nullptr;
static BASS_ChannelSetAttribute_t pBASS_ChannelSetAttribute = nullptr;
static BASS_ChannelIsActive_t pBASS_ChannelIsActive = nullptr;
// ========================================

void CheckAndPlaySound(const std::string& animationName);
void CheckPositionSound(const std::string& animationName);
void PlaySound(const std::string& soundFileName, bool waitForCompletion = true);
void StartMonitoringThread();
void StopMonitoringThread();
bool LoadSoundMappings();
std::string GetAnimationBase(const std::string& animationName);
bool LoadIniSettings();
void StartIniMonitoring();
void StopIniMonitoring();
void PlayStartupSound();
void StopAllSounds();
void StartHeartbeatThread();
void StopHeartbeatThread();
void WriteHeartbeat();
SKSELogsPaths GetAllSKSELogsPaths();
fs::path GetHeartbeatLogPath();
void WriteToActionsLog(const std::string& message, int lineNumber = 0);
void PauseAllSounds();
void ResumeAllSounds();
void PlayMenuSound(const std::string& menuName);
void StopMenuSound(const std::string& menuName);
void StopAllMenuSounds();
void WriteToSoundPlayerLog(const std::string& message, int lineNumber = 0, bool isAnimationEntry = false);
void ShowGameNotification(const std::string& message);

void BuildSoundMenuKeyPlaylist();
void StartSoundMenuKey();
void StopSoundMenuKey();
void PauseSoundMenuKey();
void ResumeSoundMenuKey();
void PlayNextSoundMenuKeyTrack();
void SoundMenuKeyMonitorThreadFunction();
void StartSoundMenuKeyMonitor();
void StopSoundMenuKeyMonitor();
void MuteGameMusic();
void RestoreGameMusic();
std::string ToLowerCase(const std::string& str);
void PlayAuthorPreview(const std::string& authorName);
void StartPreviewTimer(const std::string& authorName, const std::string& songName);
void StopAuthorPreview();
void CreateIniBackup();
void ProcessBackupUpdate();

void CleanOldScripts();
void GenerateStaticScripts();
void ClassifyAnimations();
bool IsMenuSound(const std::string& animationName);
bool IsSpecificSound(const std::string& animationName);
bool IsBaseSound(const std::string& animationName);
void StartAllScriptsFrozen();
void StopAllScripts();
void SendTrackCommand(ScriptType type, const std::string& trackFile);
void PlaySoundInScript(ScriptType type, const std::string& soundFile);
void StopScript(ScriptState& scriptState);
void SuspendProcess(HANDLE hProcess);
void ResumeProcess(HANDLE hProcess);
PROCESS_INFORMATION LaunchPowerShellScript(const std::string& scriptPath);
void CleanStopFiles();
void GenerateBaseScript();
void GenerateSpecificScript();
void GenerateMenuScript();
void GenerateCheckScript();
void SendDebugSoundBeforeFreeze(ScriptType type, ScriptState& scriptState);
fs::path GetDllDirectory();
bool SetProcessVolume(DWORD processID, float volume);

void ShowGameNotification(const std::string& message) {
    if (g_topNotificationsVisible.load()) {
        RE::DebugNotification(message.c_str());
        WriteToSoundPlayerLog("IN-GAME MESSAGE SHOWN: " + message, __LINE__);
    } else {
        WriteToSoundPlayerLog("IN-GAME MESSAGE SUPPRESSED: " + message, __LINE__);
    }
}

std::string ToLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// ========================================
// BASS Audio Library - Core Functions
// ========================================

bool InitializeBASSLibrary() {
    std::lock_guard<std::mutex> lock(g_bassMutex);
    
    if (g_bassInitialized) return true;
    
    fs::path bassPath;
    
    if (g_dllDirectory.empty()) {
        HMODULE hModule = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&InitializeBASSLibrary, &hModule)) {
            wchar_t dllPath[MAX_PATH];
            if (GetModuleFileNameW(hModule, dllPath, MAX_PATH)) {
                fs::path dllFilePath(dllPath);
                bassPath = dllFilePath.parent_path() / "DJ_library" / "bass.dll";
            }
        }
    } else {
        bassPath = g_dllDirectory / "DJ_library" / "bass.dll";
    }
    
    if (bassPath.empty()) {
        logger::error("Failed to determine bass.dll path");
        WriteToSoundPlayerLog("BASS ERROR: Failed to determine bass.dll path", __LINE__);
        return false;
    }
    
    logger::info("Attempting to load BASS from: {}", bassPath.string());
    WriteToSoundPlayerLog("BASS: Loading from " + bassPath.string(), __LINE__);
    
    g_bassModule = LoadLibraryW(bassPath.wstring().c_str());
    if (!g_bassModule) {
        DWORD error = GetLastError();
        logger::error("Failed to load bass.dll from: {} (error: {})", bassPath.string(), error);
        WriteToSoundPlayerLog("BASS ERROR: Failed to load bass.dll from " + bassPath.string() + " (error: " + std::to_string(error) + ")", __LINE__);
        return false;
    }
    
    pBASS_Init = (BASS_Init_t)GetProcAddress(g_bassModule, "BASS_Init");
    pBASS_Free = (BASS_Free_t)GetProcAddress(g_bassModule, "BASS_Free");
    pBASS_ErrorGetCode = (BASS_ErrorGetCode_t)GetProcAddress(g_bassModule, "BASS_ErrorGetCode");
    pBASS_StreamCreateFile = (BASS_StreamCreateFile_t)GetProcAddress(g_bassModule, "BASS_StreamCreateFile");
    pBASS_ChannelPlay = (BASS_ChannelPlay_t)GetProcAddress(g_bassModule, "BASS_ChannelPlay");
    pBASS_ChannelStop = (BASS_ChannelStop_t)GetProcAddress(g_bassModule, "BASS_ChannelStop");
    pBASS_ChannelPause = (BASS_ChannelPause_t)GetProcAddress(g_bassModule, "BASS_ChannelPause");
    pBASS_StreamFree = (BASS_StreamFree_t)GetProcAddress(g_bassModule, "BASS_StreamFree");
    pBASS_ChannelSetAttribute = (BASS_ChannelSetAttribute_t)GetProcAddress(g_bassModule, "BASS_ChannelSetAttribute");
    pBASS_ChannelIsActive = (BASS_ChannelIsActive_t)GetProcAddress(g_bassModule, "BASS_ChannelIsActive");
    
    if (!pBASS_Init || !pBASS_StreamCreateFile || !pBASS_ChannelPlay) {
        logger::error("Failed to get BASS function pointers");
        WriteToSoundPlayerLog("BASS ERROR: Failed to get function pointers", __LINE__);
        FreeLibrary(g_bassModule);
        g_bassModule = nullptr;
        return false;
    }
    
    if (!pBASS_Init(-1, 44100, 0, nullptr, nullptr)) {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        if (error != BASS_ERROR_ALREADY) {
            logger::error("BASS_Init failed with error: {}", error);
            WriteToSoundPlayerLog("BASS ERROR: BASS_Init failed with error " + std::to_string(error), __LINE__);
            FreeLibrary(g_bassModule);
            g_bassModule = nullptr;
            return false;
        }
    }
    
    g_bassInitialized = true;
    logger::info("BASS Audio Library initialized successfully from: {}", bassPath.string());
    WriteToSoundPlayerLog("BASS Audio Library initialized successfully", __LINE__);
    return true;
}

void ShutdownBASSLibrary() {
    std::lock_guard<std::mutex> lock(g_bassMutex);
    
    if (g_baseStream) { if (pBASS_StreamFree) pBASS_StreamFree(g_baseStream); g_baseStream = 0; }
    if (g_menuStream) { if (pBASS_StreamFree) pBASS_StreamFree(g_menuStream); g_menuStream = 0; }
    if (g_specificStream) { if (pBASS_StreamFree) pBASS_StreamFree(g_specificStream); g_specificStream = 0; }
    if (g_effectStream) { if (pBASS_StreamFree) pBASS_StreamFree(g_effectStream); g_effectStream = 0; }
    if (g_tagStream) { if (pBASS_StreamFree) pBASS_StreamFree(g_tagStream); g_tagStream = 0; }
    if (g_soundMenuKeyStream) { if (pBASS_StreamFree) pBASS_StreamFree(g_soundMenuKeyStream); g_soundMenuKeyStream = 0; }
    if (g_authorPreviewStream) { if (pBASS_StreamFree) pBASS_StreamFree(g_authorPreviewStream); g_authorPreviewStream = 0; }
    
    for (auto& [fragment, layers] : g_positionStreams) {
        for (auto& [layerNum, stream] : layers) {
            if (stream && pBASS_StreamFree) {
                pBASS_StreamFree(stream);
                stream = 0;
            }
        }
    }
    g_positionStreams.clear();
    g_activePositionFragments.clear();
    g_positionLayersActiveBeforePause.clear();
    
    if (g_bassInitialized && pBASS_Free) {
        pBASS_Free();
    }
    
    if (g_bassModule) {
        FreeLibrary(g_bassModule);
        g_bassModule = nullptr;
    }
    
    g_bassInitialized = false;
    WriteToSoundPlayerLog("BASS Audio Library shutdown complete (multi-layer position cleaned)", __LINE__);
}

fs::path FindSoundFile(const std::string& baseName) {
    if (baseName.empty()) return fs::path();
    
    std::string nameWithoutExt = baseName;
    std::string originalExt = "";
    
    size_t lastDot = baseName.find_last_of('.');
    if (lastDot != std::string::npos) {
        originalExt = baseName.substr(lastDot);
        nameWithoutExt = baseName.substr(0, lastDot);
    }
    
    fs::path testPath = g_soundsDirectory / baseName;
    if (!originalExt.empty() && fs::exists(testPath)) {
        return testPath;
    }
    
    fs::path wavPath = g_soundsDirectory / (nameWithoutExt + ".wav");
    if (fs::exists(wavPath)) {
        return wavPath;
    }
    
    fs::path mp3Path = g_soundsDirectory / (nameWithoutExt + ".mp3");
    if (fs::exists(mp3Path)) {
        return mp3Path;
    }
    
    fs::path oggPath = g_soundsDirectory / (nameWithoutExt + ".ogg");
    if (fs::exists(oggPath)) {
        return oggPath;
    }
    
    logger::warn("Sound file not found: {} (tried .wav, .mp3, .ogg)", baseName);
    WriteToSoundPlayerLog("BASS WARNING: Sound file not found: " + baseName, __LINE__);
    return fs::path();
}

HSTREAM PlayBASSSound(const std::string& soundFile, ScriptType type, bool loop = true) {
    if (!g_bassInitialized) {
        if (!InitializeBASSLibrary()) return 0;
    }
    
    fs::path soundPath = FindSoundFile(soundFile);
    if (soundPath.empty()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(g_bassMutex);
    
    HSTREAM* targetStream = nullptr;
    float volume = 1.0f;
    std::string typeName;
    
    switch (type) {
        case SCRIPT_BASE:
            targetStream = &g_baseStream;
            volume = g_volumeControlEnabled.load() ? g_baseVolume.load() : 1.0f;
            typeName = "BASE";
            break;
        case SCRIPT_MENU:
            targetStream = &g_menuStream;
            volume = g_volumeControlEnabled.load() ? g_menuVolume.load() : 1.0f;
            typeName = "MENU";
            break;
        case SCRIPT_SPECIFIC:
            targetStream = &g_specificStream;
            volume = g_volumeControlEnabled.load() ? g_specificVolume.load() : 1.0f;
            typeName = "SPECIFIC";
            break;
        case SCRIPT_EFFECT:
            targetStream = &g_effectStream;
            volume = g_volumeControlEnabled.load() ? g_effectVolume.load() : 1.0f;
            typeName = "EFFECT";
            break;
        case SCRIPT_POSITION:
            targetStream = &g_positionStream;
            volume = g_volumeControlEnabled.load() ? g_positionVolume.load() : 1.0f;
            typeName = "POSITION";
            break;
        case SCRIPT_TAG:
            targetStream = &g_tagStream;
            volume = g_volumeControlEnabled.load() ? g_tagVolume.load() : 1.0f;
            typeName = "TAG";
            break;
        case SCRIPT_CHECK:
            targetStream = &g_soundMenuKeyStream;
            volume = g_volumeControlEnabled.load() ? g_soundMenuKeyVolume.load() : 1.0f;
            typeName = "SOUNDMENUKEY";
            break;
        default:
            return 0;
    }
    
    if (*targetStream) {
        pBASS_ChannelStop(*targetStream);
        pBASS_StreamFree(*targetStream);
        *targetStream = 0;
    }
    
    DWORD flags = BASS_UNICODE;
    if (loop) flags |= BASS_SAMPLE_LOOP;
    
    *targetStream = pBASS_StreamCreateFile(
        FALSE,
        soundPath.wstring().c_str(),
        0, 0,
        flags
    );
    
    if (!*targetStream) {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("BASS: Failed to create stream for {}: error {}", soundPath.string(), error);
        WriteToSoundPlayerLog("BASS ERROR: Failed to create stream, error " + std::to_string(error), __LINE__);
        return 0;
    }
    
    if (pBASS_ChannelSetAttribute) {
        pBASS_ChannelSetAttribute(*targetStream, BASS_ATTRIB_VOL, volume);
    }
    
    if (!pBASS_ChannelPlay(*targetStream, FALSE)) {
        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
        logger::error("BASS: Failed to play stream: error {}", error);
        return 0;
    }
    
    std::string displayName = soundFile;
    if (displayName.size() >= 4 && displayName.substr(displayName.size() - 4) == ".wav") {
        displayName = displayName.substr(0, displayName.size() - 4);
    }
    
    WriteToSoundPlayerLog("BASS: Playing " + displayName + " [" + typeName + "] (loop=" + (loop ? "true" : "false") + ", vol=" + std::to_string(static_cast<int>(volume * 100)) + "%)", __LINE__);
    
    if (soundFile != "Debug.wav") {
        std::string notificationMsg = "OSoundtracks - \"" + displayName + "\" is played";
        ShowGameNotification(notificationMsg);
    }
    
    return *targetStream;
}

void StopBASSStream(ScriptType type) {
    std::lock_guard<std::mutex> lock(g_bassMutex);
    
    HSTREAM* targetStream = nullptr;
    
    switch (type) {
        case SCRIPT_BASE: targetStream = &g_baseStream; break;
        case SCRIPT_MENU: targetStream = &g_menuStream; break;
        case SCRIPT_SPECIFIC: targetStream = &g_specificStream; break;
        case SCRIPT_EFFECT: targetStream = &g_effectStream; break;
        case SCRIPT_POSITION: targetStream = &g_positionStream; break;
        case SCRIPT_TAG: targetStream = &g_tagStream; break;
        case SCRIPT_CHECK: targetStream = &g_soundMenuKeyStream; break;
        default: return;
    }
    
    if (*targetStream) {
        if (pBASS_ChannelStop) pBASS_ChannelStop(*targetStream);
        if (pBASS_StreamFree) pBASS_StreamFree(*targetStream);
        *targetStream = 0;
    }
}

void StopAllBASSStreams() {
    StopBASSStream(SCRIPT_BASE);
    StopBASSStream(SCRIPT_MENU);
    StopBASSStream(SCRIPT_SPECIFIC);
    StopBASSStream(SCRIPT_EFFECT);
    StopBASSStream(SCRIPT_POSITION);
    StopBASSStream(SCRIPT_TAG);
    StopBASSStream(SCRIPT_CHECK);
    
    for (auto& [fragment, layers] : g_positionStreams) {
        for (auto& [layerNum, stream] : layers) {
            if (stream) {
                if (pBASS_ChannelStop) pBASS_ChannelStop(stream);
                if (pBASS_StreamFree) pBASS_StreamFree(stream);
                stream = 0;
            }
        }
    }
    g_positionStreams.clear();
    g_activePositionFragments.clear();
    
    g_soundMenuKeyActive = false;
    g_currentSoundMenuKeyTrack = "";
    WriteToSoundPlayerLog("BASS: All streams stopped (including SoundMenuKey and multi-layer Position)", __LINE__);
}

void PauseBASSStream(ScriptType type) {
    std::lock_guard<std::mutex> lock(g_bassMutex);
    
    HSTREAM stream = 0;
    switch (type) {
        case SCRIPT_BASE: stream = g_baseStream; break;
        case SCRIPT_MENU: stream = g_menuStream; break;
        case SCRIPT_SPECIFIC: stream = g_specificStream; break;
        case SCRIPT_EFFECT: stream = g_effectStream; break;
        case SCRIPT_POSITION: stream = g_positionStream; break;
        case SCRIPT_TAG: stream = g_tagStream; break;
        case SCRIPT_CHECK: stream = g_soundMenuKeyStream; break;
        default: return;
    }
    
    if (stream && pBASS_ChannelPause) {
        pBASS_ChannelPause(stream);
    }
}

void ResumeBASSStream(ScriptType type) {
    std::lock_guard<std::mutex> lock(g_bassMutex);
    
    HSTREAM stream = 0;
    switch (type) {
        case SCRIPT_BASE: stream = g_baseStream; break;
        case SCRIPT_MENU: stream = g_menuStream; break;
        case SCRIPT_SPECIFIC: stream = g_specificStream; break;
        case SCRIPT_EFFECT: stream = g_effectStream; break;
        case SCRIPT_POSITION: stream = g_positionStream; break;
        case SCRIPT_TAG: stream = g_tagStream; break;
        case SCRIPT_CHECK: stream = g_soundMenuKeyStream; break;
        default: return;
    }
    
    if (stream && pBASS_ChannelPlay) {
        pBASS_ChannelPlay(stream, FALSE);
    }
}

void SetBASSVolume(ScriptType type, float volume) {
    std::lock_guard<std::mutex> lock(g_bassMutex);
    
    HSTREAM stream = 0;
    switch (type) {
        case SCRIPT_BASE: stream = g_baseStream; break;
        case SCRIPT_MENU: stream = g_menuStream; break;
        case SCRIPT_SPECIFIC: stream = g_specificStream; break;
        case SCRIPT_EFFECT: stream = g_effectStream; break;
        case SCRIPT_POSITION: stream = g_positionStream; break;
        case SCRIPT_TAG: stream = g_tagStream; break;
        case SCRIPT_CHECK: stream = g_soundMenuKeyStream; break;
        default: return;
    }
    
    if (stream && pBASS_ChannelSetAttribute) {
        pBASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, volume);
    }
}

void UpdateAllBASSVolumes() {
    if (!g_bassInitialized) return;
    
    float baseVol = g_volumeControlEnabled.load() ? g_baseVolume.load() : 1.0f;
    float menuVol = g_volumeControlEnabled.load() ? g_menuVolume.load() : 1.0f;
    float specificVol = g_volumeControlEnabled.load() ? g_specificVolume.load() : 1.0f;
    float effectVol = g_volumeControlEnabled.load() ? g_effectVolume.load() : 1.0f;
    float positionVol = g_volumeControlEnabled.load() ? g_positionVolume.load() : 1.0f;
    float tagVol = g_volumeControlEnabled.load() ? g_tagVolume.load() : 1.0f;
    float soundMenuKeyVol = g_volumeControlEnabled.load() ? g_soundMenuKeyVolume.load() : 1.0f;
    
    {
        std::lock_guard<std::mutex> lock(g_bassMutex);
        if (g_baseStream && pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_baseStream, BASS_ATTRIB_VOL, baseVol);
        }
        if (g_menuStream && pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_menuStream, BASS_ATTRIB_VOL, menuVol);
        }
        if (g_specificStream && pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_specificStream, BASS_ATTRIB_VOL, specificVol);
        }
        if (g_effectStream && pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_effectStream, BASS_ATTRIB_VOL, effectVol);
        }
        if (g_positionStream && pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_positionStream, BASS_ATTRIB_VOL, positionVol);
        }
        if (g_tagStream && pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_tagStream, BASS_ATTRIB_VOL, tagVol);
        }
        if (g_soundMenuKeyStream && pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_soundMenuKeyStream, BASS_ATTRIB_VOL, soundMenuKeyVol);
        }
    }
    
    WriteToSoundPlayerLog("BASS: Volumes updated - Base: " + std::to_string(static_cast<int>(baseVol * 100)) + 
                         "%, Menu: " + std::to_string(static_cast<int>(menuVol * 100)) + 
                         "%, Specific: " + std::to_string(static_cast<int>(specificVol * 100)) +
                         "%, Effect: " + std::to_string(static_cast<int>(effectVol * 100)) +
                         "%, Position: " + std::to_string(static_cast<int>(positionVol * 100)) +
                         "%, Tag: " + std::to_string(static_cast<int>(tagVol * 100)) + 
                         "%, SoundMenuKey: " + std::to_string(static_cast<int>(soundMenuKeyVol * 100)) + "%", __LINE__);
}

// ========================================
// End BASS Audio Library Functions
// ========================================

std::string GetTrackDirectory() {
    fs::path trackDir;
    
    if (g_usingDllPath) {
        trackDir = g_scriptsDirectory / "Track";
        logger::info("Using Wabbajack/MO2 Track directory: {}", trackDir.string());
    } else {
        trackDir = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks" / "Track";
    }
    
    try {
        fs::create_directories(trackDir);
    } catch (const std::exception& e) {
        logger::error("Error creating Track directory: {}", e.what());
    }
    
    std::string trackDirStr = trackDir.string();
    std::replace(trackDirStr.begin(), trackDirStr.end(), '/', '\\');
    
    return trackDirStr;
}

void ForceKillProcess(PROCESS_INFORMATION& pi) {
    if (pi.hProcess != 0 && pi.hProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 100);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    ZeroMemory(&pi, sizeof(pi));
}

std::string SafeWideStringToString(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }

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

        if (size_needed <= 0) {
            logger::error("WideCharToMultiByte size calculation failed. Error: {}", GetLastError());
            return std::string();
        }

        std::string result(size_needed, 0);

        int bytes_converted = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr.c_str(),
            static_cast<int>(wstr.size()),
            &result[0],
            size_needed,
            nullptr,
            nullptr
        );

        if (bytes_converted <= 0) {
            logger::error("WideCharToMultiByte conversion failed. Error: {}", GetLastError());
            return std::string();
        }

        return result;

    } catch (const std::exception& e) {
        logger::error("Exception in SafeWideStringToString: {}", e.what());
        return std::string();
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
        "OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.dll"
    };
    
    for (const auto& dllName : dllNames) {
        fs::path dllPath = pluginPath / dllName;
        
        try {
            if (fs::exists(dllPath)) {
                logger::info("DLL validation passed: Found {}", dllName);
                WriteToSoundPlayerLog("DLL found: " + dllName, __LINE__);
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

SKSELogsPaths GetAllSKSELogsPaths() {
    static bool loggedOnce = false;
    SKSELogsPaths paths;
    
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
        logger::error("Error in GetAllSKSELogsPaths: {}", e.what());
    }

    return paths;
}

fs::path GetHeartbeatLogPath() {
    auto paths = GetAllSKSELogsPaths();
    if (paths.primary.empty()) {
        return fs::path();
    }
    return paths.primary / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log";
}

void WriteToSoundPlayerLog(const std::string& message, int lineNumber, bool isAnimationEntry) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto paths = GetAllSKSELogsPaths();
    
    std::vector<fs::path> logPaths = {
        paths.primary / "OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.log",
        paths.secondary / "OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.log"
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

                if (isAnimationEntry) {
                    ss << message;
                } else {
                    ss << message;
                }

                logFile << ss.str() << std::endl;
                logFile.close();
            }
        } catch (...) {
        }
    }
}

void WriteToActionsLog(const std::string& message, int lineNumber) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto paths = GetAllSKSELogsPaths();

    std::vector<fs::path> logPaths = {
        paths.primary / "OSoundtracks-SA-Expansion-Sounds-NG-Actions.log",
        paths.secondary / "OSoundtracks-SA-Expansion-Sounds-NG-Actions.log"
    };

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);

    std::stringstream ss;
    ss << "[" << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << "[log] [info] ";
    ss << "[plugin.cpp:" << lineNumber << "] ";
    ss << message;

    std::string newLine = ss.str();
    g_actionLines.push_back(newLine);

    if (g_actionLines.size() > 200) {
        g_actionLines.pop_front();
    }

    for (const auto& logPath : logPaths) {
        try {
            std::ofstream actionsFile(logPath, std::ios::trunc);
            if (actionsFile.is_open()) {
                for (const auto& line : g_actionLines) {
                    actionsFile << line << std::endl;
                }
                actionsFile.close();
            }
        } catch (...) {
        }
    }
}

void WriteHeartbeat() {
    std::lock_guard<std::mutex> lock(g_logMutex);

    std::stringstream ss;
    ss << "[" << GetCurrentTimeString() << "] [log] [info] the game is on";
    std::string newLine = ss.str();

    g_heartbeatLines.push_back(newLine);

    if (g_heartbeatLines.size() > 20) {
        g_heartbeatLines.pop_front();
    }

    auto paths = GetAllSKSELogsPaths();
    
    std::vector<fs::path> heartbeatPaths = {
        paths.primary / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log",
        paths.secondary / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log"
    };

    for (const auto& heartbeatPath : heartbeatPaths) {
        try {
            std::ofstream heartbeatFile(heartbeatPath, std::ios::trunc);
            if (heartbeatFile.is_open()) {
                for (const auto& line : g_heartbeatLines) {
                    heartbeatFile << line << std::endl;
                }
                heartbeatFile.close();
            }
        } catch (...) {
        }
    }
}

void HeartbeatThreadFunction() {
    logger::info("Heartbeat thread started");
    g_heartbeatLines.clear();

    while (g_heartbeatActive.load() && !g_isShuttingDown.load()) {
        WriteHeartbeat();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    logger::info("Heartbeat thread stopped");
}

void StartHeartbeatThread() {
    if (!g_heartbeatActive.load()) {
        g_heartbeatActive = true;
        g_heartbeatThread = std::thread(HeartbeatThreadFunction);
        logger::info("Heartbeat monitoring started");
    }
}

void StopHeartbeatThread() {
    if (g_heartbeatActive.load()) {
        g_heartbeatActive = false;
        if (g_heartbeatThread.joinable()) {
            g_heartbeatThread.join();
        }
        logger::info("Heartbeat monitoring stopped");
    }
}

void SuspendProcess(HANDLE hProcess) {
    if (hProcess != 0 && hProcess != INVALID_HANDLE_VALUE) {
        typedef LONG(NTAPI * NtSuspendProcess)(IN HANDLE ProcessHandle);
        NtSuspendProcess pfnNtSuspendProcess =
            (NtSuspendProcess)GetProcAddress(GetModuleHandleA("ntdll"), "NtSuspendProcess");
        if (pfnNtSuspendProcess) {
            pfnNtSuspendProcess(hProcess);
        }
    }
}

void ResumeProcess(HANDLE hProcess) {
    if (hProcess != 0 && hProcess != INVALID_HANDLE_VALUE) {
        typedef LONG(NTAPI * NtResumeProcess)(IN HANDLE ProcessHandle);
        NtResumeProcess pfnNtResumeProcess =
            (NtResumeProcess)GetProcAddress(GetModuleHandleA("ntdll"), "NtResumeProcess");
        if (pfnNtResumeProcess) {
            pfnNtResumeProcess(hProcess);
        }
    }
}

void CleanStopFiles() {
    std::string trackDir = GetTrackDirectory();

    std::vector<std::string> stopFiles = {
        trackDir + "\\stop_OSoundtracks_Base.tmp",
        trackDir + "\\stop_OSoundtracks_Menu.tmp",
        trackDir + "\\stop_OSoundtracks_Specific.tmp"
    };

    for (const auto& stopFile : stopFiles) {
        if (fs::exists(stopFile)) {
            try {
                fs::remove(stopFile);
            } catch (...) {
            }
        }
    }
}

void SendDebugSoundBeforeFreeze(ScriptType type, ScriptState& scriptState) {
    if (!scriptState.isRunning) {
        return;
    }
    
    WriteToSoundPlayerLog("Sending Debug.wav to " + scriptState.name + " before freeze (500ms active playback)", __LINE__);
    
    bool wasFrozen = scriptState.isPaused;
    if (wasFrozen) {
        ResumeProcess(scriptState.processInfo.hProcess);
        scriptState.isPaused = false;
        WriteToSoundPlayerLog("Script " + scriptState.name + " temporarily unfrozen for Debug.wav", __LINE__);
    }
    
    SendTrackCommand(type, "Debug.wav");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    scriptState.currentTrack = "Debug.wav";
    
    WriteToSoundPlayerLog("Debug.wav 500ms playback completed for " + scriptState.name, __LINE__);
}

bool IsMenuSound(const std::string& animationName) {
    return (animationName == "Start" || animationName == "OStimAlignMenu");
}

bool IsSpecificSound(const std::string& animationName) {
    size_t lastDash = animationName.rfind('-');
    if (lastDash != std::string::npos) {
        std::string suffix = animationName.substr(lastDash + 1);
        bool isNumber = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
        return isNumber;
    }
    return false;
}

bool IsBaseSound(const std::string& animationName) {
    return !IsMenuSound(animationName) && !IsSpecificSound(animationName);
}

void ClassifyAnimations() {
    g_baseTracks.clear();
    g_specificTracks.clear();
    g_menuTracks.clear();

    for (const auto& [animName, config] : g_animationSoundMap) {
        for (const auto& option : config.soundOptions) {
            if (IsMenuSound(animName)) {
                g_menuTracks.push_back(option.soundFile);
            } else if (IsSpecificSound(animName)) {
                g_specificTracks.push_back(option.soundFile);
            } else {
                g_baseTracks.push_back(option.soundFile);
            }
        }
    }

    WriteToSoundPlayerLog("Classified animations - Base: " + std::to_string(g_baseTracks.size()) +
                              ", Specific: " + std::to_string(g_specificTracks.size()) +
                              ", Menu: " + std::to_string(g_menuTracks.size()),
                          __LINE__);
}

void CleanOldScripts() {
    fs::path scriptsPath = g_scriptsDirectory;

    if (!fs::exists(scriptsPath)) {
        logger::info("Scripts directory does not exist yet, will be created");
        return;
    }

    try {
        int deletedCount = 0;
        for (auto& entry : fs::directory_iterator(scriptsPath)) {
            if (entry.path().extension() == ".ps1") {
                fs::remove(entry.path());
                deletedCount++;
            }
        }

        WriteToSoundPlayerLog("Cleaned " + std::to_string(deletedCount) + " old scripts from Key_OSoundtracks",
                              __LINE__);

    } catch (const std::exception& e) {
        logger::error("Error cleaning old scripts: {}", e.what());
        WriteToSoundPlayerLog("ERROR cleaning old scripts: " + std::string(e.what()), __LINE__);
    }
}

void CreateIniBackup() {
    try {
        if (!fs::exists(g_iniPath)) {
            WriteToSoundPlayerLog("BACKUP: INI file does not exist, skipping backup", __LINE__);
            return;
        }
        
        fs::path pluginsPath = g_iniPath.parent_path();
        g_backupDirectory = pluginsPath / "OSoundtracks_MCM_Backup";
        
        if (!fs::exists(g_backupDirectory)) {
            fs::create_directories(g_backupDirectory);
            WriteToSoundPlayerLog("BACKUP: Created backup directory: " + g_backupDirectory.string(), __LINE__);
        }
        
        fs::path backupPath = g_backupDirectory / g_iniPath.filename();
        
        std::ifstream sourceFile(g_iniPath);
        if (!sourceFile.is_open()) {
            WriteToSoundPlayerLog("BACKUP ERROR: Could not open source INI", __LINE__);
            return;
        }
        
        std::ofstream backupFile(backupPath, std::ios::trunc);
        if (!backupFile.is_open()) {
            WriteToSoundPlayerLog("BACKUP ERROR: Could not create backup file", __LINE__);
            sourceFile.close();
            return;
        }
        
        std::string line;
        std::string currentSection;
        
        while (std::getline(sourceFile, line)) {
            std::string trimmedLine = line;
            trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t\r\n"));
            trimmedLine.erase(trimmedLine.find_last_not_of(" \t\r\n") + 1);
            
            if (trimmedLine.empty()) {
                continue;
            }
            
            if (trimmedLine[0] == ';' || trimmedLine[0] == '#') {
                continue;
            }
            
            if (trimmedLine[0] == '[' && trimmedLine.back() == ']') {
                currentSection = trimmedLine;
                backupFile << "\n" << trimmedLine << "\n";
                continue;
            }
            
            size_t equalPos = trimmedLine.find('=');
            if (equalPos != std::string::npos) {
                backupFile << trimmedLine << "\n";
            }
        }
        
        sourceFile.close();
        backupFile.close();
        
        WriteToSoundPlayerLog("BACKUP: INI backup created at: " + backupPath.string(), __LINE__);
        
    } catch (const std::exception& e) {
        WriteToSoundPlayerLog("BACKUP ERROR: " + std::string(e.what()), __LINE__);
    }
}

void ProcessBackupUpdate() {
    if (!g_backupUpdateEnabled.load()) {
        return;
    }
    
    try {
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("BACKUP UPDATE: Processing enabled", __LINE__);
        
        if (g_backupDirectory.empty()) {
            fs::path pluginsPath = g_iniPath.parent_path();
            g_backupDirectory = pluginsPath / "OSoundtracks_MCM_Backup";
        }
        
        fs::path backupPath = g_backupDirectory / g_iniPath.filename();
        
        if (!fs::exists(backupPath)) {
            WriteToSoundPlayerLog("BACKUP UPDATE: No backup file found, disabling flag", __LINE__);
            
            std::ifstream masterFile(g_iniPath);
            std::stringstream buffer;
            std::string line;
            bool inBackupSection = false;
            
            if (masterFile.is_open()) {
                while (std::getline(masterFile, line)) {
                    if (line.find("[Backup update]") != std::string::npos) {
                        inBackupSection = true;
                    }
                    
                    if (inBackupSection && line.find("BackupINI") != std::string::npos) {
                        buffer << "BackupINI = false\n";
                    } else {
                        buffer << line << "\n";
                    }
                }
                masterFile.close();
                
                std::ofstream outFile(g_iniPath, std::ios::trunc);
                if (outFile.is_open()) {
                    outFile << buffer.str();
                    outFile.close();
                    WriteToSoundPlayerLog("BACKUP UPDATE: BackupINI set to false in master INI", __LINE__);
                }
            }
            
            g_backupUpdateEnabled = false;
            WriteToSoundPlayerLog("========================================", __LINE__);
            return;
        }
        
        WriteToSoundPlayerLog("BACKUP UPDATE: Backup found at: " + backupPath.string(), __LINE__);
        
        std::map<std::string, std::map<std::string, std::string>> backupConfigs;
        std::ifstream backupFile(backupPath);
        std::string currentSection;
        
        if (backupFile.is_open()) {
            std::string line;
            while (std::getline(backupFile, line)) {
                std::string trimmedLine = line;
                trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t\r\n"));
                trimmedLine.erase(trimmedLine.find_last_not_of(" \t\r\n") + 1);
                
                if (trimmedLine.empty() || trimmedLine[0] == ';' || trimmedLine[0] == '#') {
                    continue;
                }
                
                if (trimmedLine[0] == '[' && trimmedLine.back() == ']') {
                    currentSection = trimmedLine;
                    continue;
                }
                
                size_t equalPos = trimmedLine.find('=');
                if (equalPos != std::string::npos && !currentSection.empty()) {
                    std::string key = trimmedLine.substr(0, equalPos);
                    std::string value = trimmedLine.substr(equalPos + 1);
                    
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    
                    backupConfigs[currentSection][key] = value;
                }
            }
            backupFile.close();
            WriteToSoundPlayerLog("BACKUP UPDATE: Loaded " + std::to_string(backupConfigs.size()) + " sections from backup", __LINE__);
        }
        
        std::ifstream masterFile(g_iniPath);
        std::stringstream buffer;
        std::string line;
        currentSection = "";
        bool inBackupSection = false;
        
        if (masterFile.is_open()) {
            while (std::getline(masterFile, line)) {
                std::string trimmedLine = line;
                trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t\r\n"));
                trimmedLine.erase(trimmedLine.find_last_not_of(" \t\r\n") + 1);
                
                if (trimmedLine.empty() || trimmedLine[0] == ';' || trimmedLine[0] == '#') {
                    buffer << line << "\n";
                    continue;
                }
                
                if (trimmedLine[0] == '[' && trimmedLine.back() == ']') {
                    currentSection = trimmedLine;
                    inBackupSection = (currentSection == "[Backup update]");
                    buffer << line << "\n";
                    continue;
                }
                
                size_t equalPos = trimmedLine.find('=');
                if (equalPos != std::string::npos && !currentSection.empty()) {
                    std::string key = trimmedLine.substr(0, equalPos);
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    
                    if (inBackupSection && key == "BackupINI") {
                        buffer << key << " = false\n";
                        WriteToSoundPlayerLog("BACKUP UPDATE: Set BackupINI to false", __LINE__);
                    } else if (backupConfigs.find(currentSection) != backupConfigs.end() &&
                             backupConfigs[currentSection].find(key) != backupConfigs[currentSection].end()) {
                        buffer << key << " = " << backupConfigs[currentSection][key] << "\n";
                        WriteToSoundPlayerLog("BACKUP UPDATE: Restored " + currentSection + " -> " + key + " = " + backupConfigs[currentSection][key], __LINE__);
                    } else {
                        buffer << line << "\n";
                    }
                } else {
                    buffer << line << "\n";
                }
            }
            masterFile.close();
            
            std::ofstream outFile(g_iniPath, std::ios::trunc);
            if (outFile.is_open()) {
                outFile << buffer.str();
                outFile.close();
                WriteToSoundPlayerLog("BACKUP UPDATE: Master INI updated successfully", __LINE__);
            }
        }
        
        g_backupUpdateEnabled = false;
        WriteToSoundPlayerLog("BACKUP UPDATE: Process completed", __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        
    } catch (const std::exception& e) {
        WriteToSoundPlayerLog("BACKUP UPDATE ERROR: " + std::string(e.what()), __LINE__);
    }
}

bool LoadIniSettings() {
    try {
        if (!fs::exists(g_iniPath)) {
            logger::warn("INI file not found: {}", g_iniPath.string());
            WriteToSoundPlayerLog("WARNING: INI file not found at: " + g_iniPath.string(), __LINE__);
            return false;
        }

        std::ifstream iniFile(g_iniPath);
        if (!iniFile.is_open()) {
            logger::error("Could not open INI file");
            return false;
        }

        std::string line;
        std::string currentSection;
        bool newStartupSound = g_startupSoundEnabled.load();
        bool newTopNotifications = g_topNotificationsVisible.load();
        bool newMuteGameMusic = g_muteGameMusicDuringOStim.load();
        float newBaseVolume = g_baseVolume.load();
        float newMenuVolume = g_menuVolume.load();
        float newSpecificVolume = g_specificVolume.load();
        float newEffectVolume = g_effectVolume.load();
        float newPositionVolume = g_positionVolume.load();
        float newTagVolume = g_tagVolume.load();
        float newSoundMenuKeyVolume = g_soundMenuKeyVolume.load();
        bool newVolumeEnabled = g_volumeControlEnabled.load();
        SoundMenuKeyMode newSoundMenuKeyMode = g_soundMenuKeyMode;
        std::string newSoundMenuKeyAuthor = g_soundMenuKeyAuthor;
        
        if (g_lastAuthorName.empty()) {
            g_lastAuthorName = g_soundMenuKeyAuthor;
        }

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
                std::string key = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                if (currentSection == "Startup Sound" && key == "Startup") {
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    newStartupSound = (value == "true" || value == "1" || value == "yes");
                } else if (currentSection == "Top Notifications" && key == "Visible") {
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    newTopNotifications = (value == "true" || value == "1" || value == "yes");
                } else if (currentSection == "Skyrim Audio" && key == "MuteGameMusicDuringOStim") {
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    newMuteGameMusic = (value == "true" || value == "1" || value == "yes");
                } else if (currentSection == "Menu Sound") {
                    if (key == "SoundMenuKey") {
                        std::string lowerValue = value;
                        std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
                        if (lowerValue == "false" || lowerValue == "0" || lowerValue == "disabled") {
                            newSoundMenuKeyMode = SoundMenuKeyMode::DISABLED;
                        } else if (lowerValue == "all_order") {
                            newSoundMenuKeyMode = SoundMenuKeyMode::ALL_ORDER;
                        } else if (lowerValue == "all_random") {
                            newSoundMenuKeyMode = SoundMenuKeyMode::ALL_RANDOM;
                        } else if (lowerValue == "author_order") {
                            newSoundMenuKeyMode = SoundMenuKeyMode::AUTHOR_ORDER;
                        } else if (lowerValue == "author_random") {
                            newSoundMenuKeyMode = SoundMenuKeyMode::AUTHOR_RANDOM;
                        } else {
                            logger::warn("Unknown SoundMenuKey mode: {}", value);
                        }
                    } else if (key == "Author") {
                        newSoundMenuKeyAuthor = value;
                    }
                } else if (currentSection == "Backup update") {
                    if (key == "BackupINI") {
                        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                        g_backupUpdateEnabled = (value == "true" || value == "1" || value == "yes");
                    }
                } else if (currentSection == "Volume Control") {
                    if (key == "BaseVolume") {
                        try {
                            float vol = std::stof(value);
                            if (vol >= 0.0f && vol <= 2.0f) {
                                newBaseVolume = vol;
                            } else {
                                logger::warn("BaseVolume out of range (0.0-2.0): {}", vol);
                            }
                        } catch (...) {
                            logger::warn("Invalid BaseVolume value: {}", value);
                        }
                    } else if (key == "MenuVolume") {
                        try {
                            float vol = std::stof(value);
                            if (vol >= 0.0f && vol <= 2.0f) {
                                newMenuVolume = vol;
                            } else {
                                logger::warn("MenuVolume out of range (0.0-2.0): {}", vol);
                            }
                        } catch (...) {
                            logger::warn("Invalid MenuVolume value: {}", value);
                        }
                    } else if (key == "SpecificVolume") {
                        try {
                            float vol = std::stof(value);
                            if (vol >= 0.0f && vol <= 2.0f) {
                                newSpecificVolume = vol;
                            } else {
                                logger::warn("SpecificVolume out of range (0.0-2.0): {}", vol);
                            }
                        } catch (...) {
                            logger::warn("Invalid SpecificVolume value: {}", value);
                        }
                    } else if (key == "EffectVolume") {
                        try {
                            float vol = std::stof(value);
                            if (vol >= 0.0f && vol <= 2.0f) {
                                newEffectVolume = vol;
                            } else {
                                logger::warn("EffectVolume out of range (0.0-2.0): {}", vol);
                            }
                        } catch (...) {
                            logger::warn("Invalid EffectVolume value: {}", value);
                        }
                    } else if (key == "PositionVolume") {
                        try {
                            float vol = std::stof(value);
                            if (vol >= 0.0f && vol <= 2.0f) {
                                newPositionVolume = vol;
                            } else {
                                logger::warn("PositionVolume out of range (0.0-2.0): {}", vol);
                            }
                        } catch (...) {
                            logger::warn("Invalid PositionVolume value: {}", value);
                        }
                    } else if (key == "TAGVolume") {
                        try {
                            float vol = std::stof(value);
                            if (vol >= 0.0f && vol <= 2.0f) {
                                newTagVolume = vol;
                            } else {
                                logger::warn("TAGVolume out of range (0.0-2.0): {}", vol);
                            }
                        } catch (...) {
                            logger::warn("Invalid TAGVolume value: {}", value);
                        }
                    } else if (key == "SoundMenuKeyVolume") {
                        try {
                            float vol = std::stof(value);
                            if (vol >= 0.0f && vol <= 2.0f) {
                                newSoundMenuKeyVolume = vol;
                            } else {
                                logger::warn("SoundMenuKeyVolume out of range (0.0-2.0): {}", vol);
                            }
                        } catch (...) {
                            logger::warn("Invalid SoundMenuKeyVolume value: {}", value);
                        }
                    } else if (key == "MasterVolumeEnabled") {
                        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                        newVolumeEnabled = (value == "true" || value == "1" || value == "yes");
                    }
                }
            }
        }

        iniFile.close();

        bool startupChanged = (newStartupSound != g_startupSoundEnabled.load());
        bool notificationsChanged = (newTopNotifications != g_topNotificationsVisible.load());
        bool muteGameMusicChanged = (newMuteGameMusic != g_muteGameMusicDuringOStim.load());
        bool volumeChanged = (newBaseVolume != g_baseVolume.load() || 
                             newMenuVolume != g_menuVolume.load() || 
                             newSpecificVolume != g_specificVolume.load() ||
                             newEffectVolume != g_effectVolume.load() ||
                             newPositionVolume != g_positionVolume.load() ||
                             newTagVolume != g_tagVolume.load() ||
                             newSoundMenuKeyVolume != g_soundMenuKeyVolume.load() ||
                             newVolumeEnabled != g_volumeControlEnabled.load());

        bool soundMenuKeyChanged = (newSoundMenuKeyMode != g_soundMenuKeyMode ||
                                   newSoundMenuKeyAuthor != g_soundMenuKeyAuthor);
        bool authorChanged = (newSoundMenuKeyAuthor != g_soundMenuKeyAuthor);

        g_startupSoundEnabled = newStartupSound;
        g_topNotificationsVisible = newTopNotifications;
        g_muteGameMusicDuringOStim = newMuteGameMusic;
        g_baseVolume = newBaseVolume;
        g_menuVolume = newMenuVolume;
        g_specificVolume = newSpecificVolume;
        g_effectVolume = newEffectVolume;
        g_positionVolume = newPositionVolume;
        g_tagVolume = newTagVolume;
        g_soundMenuKeyVolume = newSoundMenuKeyVolume;
        g_volumeControlEnabled = newVolumeEnabled;
        
        if (authorChanged && !newSoundMenuKeyAuthor.empty() && !g_iniFirstLoad) {
            WriteToSoundPlayerLog("AUTHOR CHANGED: '" + g_soundMenuKeyAuthor + "' -> '" + newSoundMenuKeyAuthor + "'", __LINE__);
            g_soundMenuKeyAuthor = newSoundMenuKeyAuthor;
            PlayAuthorPreview(newSoundMenuKeyAuthor);
        } else if (authorChanged && !newSoundMenuKeyAuthor.empty() && g_iniFirstLoad) {
            g_soundMenuKeyAuthor = newSoundMenuKeyAuthor;
            WriteToSoundPlayerLog("INITIAL LOAD: Author set to '" + newSoundMenuKeyAuthor + "' (no preview)", __LINE__);
        }
        
        if (g_iniFirstLoad) {
            g_iniFirstLoad = false;
        }
        
        if (soundMenuKeyChanged || (authorChanged && !g_iniFirstLoad)) {
            g_soundMenuKeyMode = newSoundMenuKeyMode;
            if (!authorChanged) {
                g_soundMenuKeyAuthor = newSoundMenuKeyAuthor;
            }
            
            if (newSoundMenuKeyMode == SoundMenuKeyMode::DISABLED) {
                WriteToSoundPlayerLog("SoundMenuKey DISABLED via INI change", __LINE__);
                StopSoundMenuKey();
            }
            else if (g_soundMenuKeyActive.load()) {
                WriteToSoundPlayerLog("SoundMenuKey config/author changed - RESTARTING AUDIO STREAM", __LINE__);
                
                BuildSoundMenuKeyPlaylist();
                
                StopBASSStream(SCRIPT_CHECK);
                g_currentSoundMenuKeyTrack = "";
                
                PlayNextSoundMenuKeyTrack();
                
                if (g_soundsPaused.load()) {
                    PauseBASSStream(SCRIPT_CHECK);
                    g_soundMenuKeyPaused = true;
                    g_soundMenuKeyWasActiveBeforePause = true;
                    WriteToSoundPlayerLog("SoundMenuKey updated and immediately PAUSED (Menu open detected)", __LINE__);
                }
            }
        }

        if (startupChanged) {
            logger::info("Startup sound {}", newStartupSound ? "enabled" : "disabled");
            WriteToSoundPlayerLog("Startup sound " + std::string(newStartupSound ? "enabled" : "disabled"), __LINE__);
        }

        if (notificationsChanged) {
            logger::info("Top notifications {}", newTopNotifications ? "enabled" : "disabled");
            WriteToSoundPlayerLog("Top notifications " + std::string(newTopNotifications ? "enabled" : "disabled"), __LINE__);
        }

        if (volumeChanged) {
            logger::info("Volume settings changed - Base: {}, Menu: {}, Specific: {}, Enabled: {}", 
                        newBaseVolume, newMenuVolume, newSpecificVolume, newVolumeEnabled);
            WriteToSoundPlayerLog("Volume settings - Base: " + std::to_string(newBaseVolume) + 
                                ", Menu: " + std::to_string(newMenuVolume) + 
                                ", Specific: " + std::to_string(newSpecificVolume) + 
                                ", Control: " + std::string(newVolumeEnabled ? "enabled" : "disabled"), __LINE__);
            UpdateAllBASSVolumes();
        }

        WriteToSoundPlayerLog(
            "INI settings loaded - Startup Sound: " + std::string(g_startupSoundEnabled.load() ? "enabled" : "disabled") +
                ", Top Notifications: " + std::string(g_topNotificationsVisible.load() ? "enabled" : "disabled") +
                ", Volume Control: " + std::string(g_volumeControlEnabled.load() ? "enabled" : "disabled"),
            __LINE__);

        return true;

    } catch (const std::exception& e) {
        logger::error("Error loading INI settings: {}", e.what());
        return false;
    }
}

void IniMonitorThreadFunction() {
    logger::info("INI monitoring thread started");

    while (g_monitoringIni.load() && !g_isShuttingDown.load()) {
        try {
            if (fs::exists(g_iniPath)) {
                auto currentModTime = fs::last_write_time(g_iniPath);
                auto currentModTimeT = std::chrono::system_clock::to_time_t(
                    std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        currentModTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()));

                if (currentModTimeT > g_lastIniCheckTime) {
                    WriteToSoundPlayerLog("INI file changed, reloading settings...", __LINE__);
                    CreateIniBackup();
                    LoadIniSettings();
                    g_lastIniCheckTime = currentModTimeT;
                }
            }
        } catch (...) {
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    logger::info("INI monitoring thread stopped");
}

void StartIniMonitoring() {
    if (!g_monitoringIni.load()) {
        g_monitoringIni = true;
        g_iniMonitorThread = std::thread(IniMonitorThreadFunction);
    }
}

void StopIniMonitoring() {
    if (g_monitoringIni.load()) {
        g_monitoringIni = false;
        if (g_iniMonitorThread.joinable()) {
            g_iniMonitorThread.join();
        }
    }
}

std::string GetAnimationBase(const std::string& animationName) {
    size_t lastDash = animationName.rfind('-');
    if (lastDash != std::string::npos) {
        std::string suffix = animationName.substr(lastDash + 1);
        bool isNumber = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
        if (isNumber) {
            return animationName.substr(0, lastDash);
        }
    }
    return animationName;
}

bool SetProcessVolume(DWORD processID, float volume) {
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        logger::error("CoInitialize failed in SetProcessVolume");
        return false;
    }
    
    IMMDeviceEnumerator* deviceEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
    
    if (FAILED(hr)) {
        logger::error("Failed to create device enumerator");
        CoUninitialize();
        return false;
    }
    
    IMMDevice* defaultDevice = nullptr;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    
    if (FAILED(hr)) {
        logger::error("Failed to get default audio endpoint");
        deviceEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    IAudioSessionManager2* sessionManager = nullptr;
    hr = defaultDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER,
                                nullptr, (LPVOID*)&sessionManager);
    
    if (FAILED(hr)) {
        logger::error("Failed to activate session manager");
        defaultDevice->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    IAudioSessionEnumerator* sessionEnum = nullptr;
    hr = sessionManager->GetSessionEnumerator(&sessionEnum);
    
    if (FAILED(hr)) {
        logger::error("Failed to get session enumerator");
        sessionManager->Release();
        defaultDevice->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    int sessionCount = 0;
    sessionEnum->GetCount(&sessionCount);
    
    bool volumeSet = false;
    
    for (int i = 0; i < sessionCount; i++) {
        IAudioSessionControl* sessionControl = nullptr;
        sessionEnum->GetSession(i, &sessionControl);
        
        if (sessionControl == nullptr) {
            continue;
        }
        
        IAudioSessionControl2* sessionControl2 = nullptr;
        hr = sessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (LPVOID*)&sessionControl2);
        
        if (FAILED(hr)) {
            sessionControl->Release();
            continue;
        }
        
        DWORD sessionPID = 0;
        sessionControl2->GetProcessId(&sessionPID);
        
        if (sessionPID == processID) {
            ISimpleAudioVolume* audioVolume = nullptr;
            hr = sessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume), (LPVOID*)&audioVolume);
            
            if (SUCCEEDED(hr) && audioVolume != nullptr) {
                hr = audioVolume->SetMasterVolume(volume, nullptr);
                
                if (SUCCEEDED(hr)) {
                    volumeSet = true;
                    logger::info("Volume set to {} for process {}", volume, processID);
                } else {
                    logger::error("Failed to set volume for process {}", processID);
                }
                
                audioVolume->Release();
            }
            
            sessionControl2->Release();
            sessionControl->Release();
            break;
        }
        
        sessionControl2->Release();
        sessionControl->Release();
    }
    
    sessionEnum->Release();
    sessionManager->Release();
    defaultDevice->Release();
    deviceEnumerator->Release();
    CoUninitialize();
    
    return volumeSet;
}

void GenerateBaseScript() {
    fs::path scriptFile = g_scriptsDirectory / "OSoundtracks_Base.ps1";

    auto heartbeatPath = GetHeartbeatLogPath().string();
    std::string soundsFolder = g_soundsDirectory.string();

    std::ofstream script(scriptFile, std::ios::binary);
    if (!script.is_open()) {
        logger::error("Failed to create Base script");
        return;
    }
    script << "\xEF\xBB\xBF";

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Base.ps1\n";
    script << "# Base animation sounds handler\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
    if (g_usingDllPath) {
        script << "# MODE: Wabbajack/MO2 (DLL-relative paths)\n";
    } else {
        script << "# MODE: Standard installation\n";
    }
    script << "# ===================================================================\n\n";

    std::string trackDir = GetTrackDirectory();
    script << "$carpetaSonidos = '" << soundsFolder << "'\n";
    script << "$archivoLog = '" << heartbeatPath << "'\n";
    script << "$stopFile = '" << trackDir << "\\stop_OSoundtracks_Base.tmp'\n";
    script << "$trackFile = '" << trackDir << "\\track_OSoundtracks_Base.tmp'\n";
    script << "$tiempoEsperaSegundos = 6\n\n";

    script << "# Available tracks\n";
    script << "$archivosWAV = @(\n";
    script << "    'Debug.wav'";
    if (!g_baseTracks.empty())
        script << ",\n";
    else
        script << "\n";

    for (size_t i = 0; i < g_baseTracks.size(); i++) {
        script << "    '" << g_baseTracks[i] << "'";
        if (i < g_baseTracks.size() - 1) script << ",";
        script << "\n";
    }
    script << ")\n\n";

    script << "# Verify WAV files exist\n";
    script << "foreach ($wav in $archivosWAV) {\n";
    script << "    $rutaCompleta = Join-Path $carpetaSonidos $wav\n";
    script << "    if (-not (Test-Path $rutaCompleta)) {\n";
    script << "        Write-Host \"ERROR: Sound file not found: $rutaCompleta\"\n";
    script << "        exit 1\n";
    script << "    }\n";
    script << "}\n\n";

    script << "# Initialize player\n";
    script << "$script:reproductor = New-Object System.Media.SoundPlayer\n";
    script << "$indiceActual = 0\n";
    script << "$ultimoTrackSeleccionado = 'Debug.wav'\n";
    script << "$ultimaModificacionTrack = [DateTime]::MinValue\n\n";

    script << "# Read initial track if exists\n";
    script << "if (Test-Path $trackFile) {\n";
    script << "    try {\n";
    script << "        $trackInicial = Get-Content $trackFile -Raw -ErrorAction SilentlyContinue\n";
    script << "        $trackInicial = $trackInicial.Trim()\n";
    script << "        $parts = $trackInicial -split '\\|'\n";
    script << "        $soundFile = $parts[0]\n";
    script << "        for ($i = 0; $i -lt $archivosWAV.Count; $i++) {\n";
    script << "            if ($archivosWAV[$i] -eq $soundFile) {\n";
    script << "                $indiceActual = $i\n";
    script << "                $ultimoTrackSeleccionado = $soundFile\n";
    script << "                Write-Host \"Starting with pre-selected track: $soundFile\"\n";
    script << "                break\n";
    script << "            }\n";
    script << "        }\n";
    script << "    } catch { }\n";
    script << "}\n\n";

    script << "# Function to play track\n";
    script << "function Play-Track {\n";
    script << "    param($index)\n";
    script << "    $trackName = $archivosWAV[$index]\n";
    script << "    $rutaWAV = Join-Path $carpetaSonidos $trackName\n";
    script << "    try {\n";
    script << "        if ($script:reproductor -ne $null) {\n";
    script << "            $script:reproductor.Stop()\n";
    script << "            Start-Sleep -Milliseconds 100\n";
    script << "            $script:reproductor.Dispose()\n";
    script << "            $script:reproductor = $null\n";
    script << "        }\n";
    script << "        $script:reproductor = New-Object System.Media.SoundPlayer\n";
    script << "        $script:reproductor.SoundLocation = $rutaWAV\n";
    script << "        $script:reproductor.Load()\n";
    script << "        $script:reproductor.PlayLooping()\n";
    script << "        Write-Host \"NOW PLAYING (BASE): $trackName (Index: $index)\"\n";
    script << "    } catch {\n";
    script << "        Write-Host \"ERROR: Cannot play sound: $_\"\n";
    script << "        exit 1\n";
    script << "    }\n";
    script << "}\n\n";

    script << "# Play initial track\n";
    script << "Play-Track -index $indiceActual\n\n";

    script << "# Get initial heartbeat time\n";
    script << "try {\n";
    script << "    $ultimaModificacion = (Get-Item $archivoLog -ErrorAction Stop).LastWriteTime\n";
    script << "} catch {\n";
    script << "    if ($script:reproductor -ne $null) {\n";
    script << "        $script:reproductor.Stop()\n";
    script << "        $script:reproductor.Dispose()\n";
    script << "    }\n";
    script << "    Write-Host \"ERROR: Heartbeat log not found\"\n";
    script << "    exit 1\n";
    script << "}\n\n";

    script << "# MAIN LOOP\n";
    script << "try {\n";
    script << "    while ($true) {\n";
    script << "        Start-Sleep -Milliseconds 100\n\n";

    script << "        # Check 1: STOP command\n";
    script << "        if (Test-Path $stopFile) {\n";
    script << "            Remove-Item $stopFile -Force -ErrorAction SilentlyContinue\n";
    script << "            Write-Host \"STOP command received\"\n";
    script << "            break\n";
    script << "        }\n\n";

    script << "        # Check 2: Track change command\n";
    script << "        if (Test-Path $trackFile) {\n";
    script << "            try {\n";
    script << "                $currentModTime = (Get-Item $trackFile).LastWriteTime\n";
    script << "                if ($currentModTime -gt $ultimaModificacionTrack) {\n";
    script << "                    $trackContent = Get-Content $trackFile -Raw -ErrorAction SilentlyContinue\n";
    script << "                    $trackContent = $trackContent.Trim()\n";
    script << "                    $parts = $trackContent -split '\\|'\n";
    script << "                    $soundFile = $parts[0]\n";
    script << "                    if ($soundFile -ne '' -and $soundFile -ne $ultimoTrackSeleccionado) {\n";
    script << "                        $nuevoIndice = -1\n";
    script << "                        for ($i = 0; $i -lt $archivosWAV.Count; $i++) {\n";
    script << "                            if ($archivosWAV[$i] -eq $soundFile) {\n";
    script << "                                $nuevoIndice = $i\n";
    script << "                                break\n";
    script << "                            }\n";
    script << "                        }\n";
    script << "                        if ($nuevoIndice -ge 0) {\n";
    script << "                            $indiceActual = $nuevoIndice\n";
    script << "                            $ultimoTrackSeleccionado = $soundFile\n";
    script << "                            $ultimaModificacionTrack = $currentModTime\n";
    script << "                            Play-Track -index $indiceActual\n";
    script << "                            Write-Host \">>> TRACK CHANGE: $soundFile <<<\"\n";
    script << "                        }\n";
    script << "                    } elseif ($soundFile -ne '' -and $soundFile -eq $ultimoTrackSeleccionado) {\n";
    script << "                        $ultimaModificacionTrack = $currentModTime\n";
    script << "                        Play-Track -index $indiceActual\n";
    script << "                        Write-Host \">>> TRACK RESTART (forced): $soundFile <<<\"\n";
    script << "                    }\n";
    script << "                }\n";
    script << "            } catch { }\n";
    script << "        }\n\n";

    script << "        # Check 3: Heartbeat monitoring\n";
    script << "        try {\n";
    script << "            $modificacionActual = (Get-Item $archivoLog -ErrorAction Stop).LastWriteTime\n";
    script << "            if ($modificacionActual -gt $ultimaModificacion) {\n";
    script << "                $ultimaModificacion = $modificacionActual\n";
    script << "            }\n";
    script << "            $tiempoTranscurrido = (Get-Date) - $ultimaModificacion\n";
    script << "            if ($tiempoTranscurrido.TotalSeconds -gt $tiempoEsperaSegundos) {\n";
    script << "                Write-Host \"HEARTBEAT TIMEOUT - GAME CRASHED - AUTO TERMINATE\"\n";
    script << "                break\n";
    script << "            }\n";
    script << "        } catch {\n";
    script << "            Write-Host \"ERROR reading heartbeat log - probably CTD\"\n";
    script << "            break\n";
    script << "        }\n";
    script << "    }\n";
    script << "}\n";
    script << "finally {\n";
    script << "    try {\n";
    script << "        if ($script:reproductor -ne $null) {\n";
    script << "            $script:reproductor.Stop()\n";
    script << "            $script:reproductor.Dispose()\n";
    script << "        }\n";
    script << "        Write-Host \"Base script stopped cleanly\"\n";
    script << "    } catch { }\n";
    script << "}\n";

    script.close();

    g_baseScript.name = "Base";
    g_baseScript.scriptPath = scriptFile;
    std::string trackDirStr = trackDir;
    g_baseScript.stopFilePath = trackDirStr + "\\stop_OSoundtracks_Base.tmp";
    g_baseScript.trackFilePath = trackDirStr + "\\track_OSoundtracks_Base.tmp";

    WriteToSoundPlayerLog("Generated OSoundtracks_Base.ps1" + std::string(g_usingDllPath ? " (Wabbajack mode)" : ""), __LINE__);
}

void GenerateSpecificScript() {
    fs::path scriptFile = g_scriptsDirectory / "OSoundtracks_Specific.ps1";

    auto heartbeatPath = GetHeartbeatLogPath().string();
    std::string soundsFolder = g_soundsDirectory.string();

    std::ofstream script(scriptFile, std::ios::binary);
    if (!script.is_open()) {
        logger::error("Failed to create Specific script");
        return;
    }
    script << "\xEF\xBB\xBF";

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Specific.ps1\n";
    script << "# Specific animation sounds handler\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
    if (g_usingDllPath) {
        script << "# MODE: Wabbajack/MO2 (DLL-relative paths)\n";
    } else {
        script << "# MODE: Standard installation\n";
    }
    script << "# ===================================================================\n\n";

    std::string trackDir = GetTrackDirectory();
    script << "$carpetaSonidos = '" << soundsFolder << "'\n";
    script << "$archivoLog = '" << heartbeatPath << "'\n";
    script << "$stopFile = '" << trackDir << "\\stop_OSoundtracks_Specific.tmp'\n";
    script << "$trackFile = '" << trackDir << "\\track_OSoundtracks_Specific.tmp'\n";
    script << "$tiempoEsperaSegundos = 6\n\n";

    script << "# Available tracks\n";
    script << "$archivosWAV = @(\n";
    script << "    'Debug.wav'";
    if (!g_specificTracks.empty())
        script << ",\n";
    else
        script << "\n";

    for (size_t i = 0; i < g_specificTracks.size(); i++) {
        script << "    '" << g_specificTracks[i] << "'";
        if (i < g_specificTracks.size() - 1) script << ",";
        script << "\n";
    }
    script << ")\n\n";

    script << "# Verify WAV files exist\n";
    script << "foreach ($wav in $archivosWAV) {\n";
    script << "    $rutaCompleta = Join-Path $carpetaSonidos $wav\n";
    script << "    if (-not (Test-Path $rutaCompleta)) {\n";
    script << "        Write-Host \"ERROR: Sound file not found: $rutaCompleta\"\n";
    script << "        exit 1\n";
    script << "    }\n";
    script << "}\n\n";

    script << "# Initialize player\n";
    script << "$script:reproductor = New-Object System.Media.SoundPlayer\n";
    script << "$indiceActual = 0\n";
    script << "$ultimoTrackSeleccionado = 'Debug.wav'\n";
    script << "$ultimaModificacionTrack = [DateTime]::MinValue\n\n";

    script << "# Read initial track if exists\n";
    script << "if (Test-Path $trackFile) {\n";
    script << "    try {\n";
    script << "        $trackInicial = Get-Content $trackFile -Raw -ErrorAction SilentlyContinue\n";
    script << "        $trackInicial = $trackInicial.Trim()\n";
    script << "        $parts = $trackInicial -split '\\|'\n";
    script << "        $soundFile = $parts[0]\n";
    script << "        for ($i = 0; $i -lt $archivosWAV.Count; $i++) {\n";
    script << "            if ($archivosWAV[$i] -eq $soundFile) {\n";
    script << "                $indiceActual = $i\n";
    script << "                $ultimoTrackSeleccionado = $soundFile\n";
    script << "                Write-Host \"Starting with pre-selected track: $soundFile\"\n";
    script << "                break\n";
    script << "            }\n";
    script << "        }\n";
    script << "    } catch { }\n";
    script << "}\n\n";

    script << "# Function to play track\n";
    script << "function Play-Track {\n";
    script << "    param($index)\n";
    script << "    $trackName = $archivosWAV[$index]\n";
    script << "    $rutaWAV = Join-Path $carpetaSonidos $trackName\n";
    script << "    try {\n";
    script << "        if ($script:reproductor -ne $null) {\n";
    script << "            $script:reproductor.Stop()\n";
    script << "            Start-Sleep -Milliseconds 100\n";
    script << "            $script:reproductor.Dispose()\n";
    script << "            $script:reproductor = $null\n";
    script << "        }\n";
    script << "        $script:reproductor = New-Object System.Media.SoundPlayer\n";
    script << "        $script:reproductor.SoundLocation = $rutaWAV\n";
    script << "        $script:reproductor.Load()\n";
    script << "        $script:reproductor.PlayLooping()\n";
    script << "        Write-Host \"NOW PLAYING (SPECIFIC): $trackName (Index: $index)\"\n";
    script << "    } catch {\n";
    script << "        Write-Host \"ERROR: Cannot play sound: $_\"\n";
    script << "        exit 1\n";
    script << "    }\n";
    script << "}\n\n";

    script << "# Play initial track\n";
    script << "Play-Track -index $indiceActual\n\n";

    script << "# Get initial heartbeat time\n";
    script << "try {\n";
    script << "    $ultimaModificacion = (Get-Item $archivoLog -ErrorAction Stop).LastWriteTime\n";
    script << "} catch {\n";
    script << "    if ($script:reproductor -ne $null) {\n";
    script << "        $script:reproductor.Stop()\n";
    script << "        $script:reproductor.Dispose()\n";
    script << "    }\n";
    script << "    Write-Host \"ERROR: Heartbeat log not found\"\n";
    script << "    exit 1\n";
    script << "}\n\n";

    script << "# MAIN LOOP\n";
    script << "try {\n";
    script << "    while ($true) {\n";
    script << "        Start-Sleep -Milliseconds 100\n\n";

    script << "        # Check 1: STOP command\n";
    script << "        if (Test-Path $stopFile) {\n";
    script << "            Remove-Item $stopFile -Force -ErrorAction SilentlyContinue\n";
    script << "            Write-Host \"STOP command received\"\n";
    script << "            break\n";
    script << "        }\n\n";

    script << "        # Check 2: Track change command\n";
    script << "        if (Test-Path $trackFile) {\n";
    script << "            try {\n";
    script << "                $currentModTime = (Get-Item $trackFile).LastWriteTime\n";
    script << "                if ($currentModTime -gt $ultimaModificacionTrack) {\n";
    script << "                    $trackContent = Get-Content $trackFile -Raw -ErrorAction SilentlyContinue\n";
    script << "                    $trackContent = $trackContent.Trim()\n";
    script << "                    $parts = $trackContent -split '\\|'\n";
    script << "                    $soundFile = $parts[0]\n";
    script << "                    if ($soundFile -ne '' -and $soundFile -ne $ultimoTrackSeleccionado) {\n";
    script << "                        $nuevoIndice = -1\n";
    script << "                        for ($i = 0; $i -lt $archivosWAV.Count; $i++) {\n";
    script << "                            if ($archivosWAV[$i] -eq $soundFile) {\n";
    script << "                                $nuevoIndice = $i\n";
    script << "                                break\n";
    script << "                            }\n";
    script << "                        }\n";
    script << "                        if ($nuevoIndice -ge 0) {\n";
    script << "                            $indiceActual = $nuevoIndice\n";
    script << "                            $ultimoTrackSeleccionado = $soundFile\n";
    script << "                            $ultimaModificacionTrack = $currentModTime\n";
    script << "                            Play-Track -index $indiceActual\n";
    script << "                            Write-Host \">>> TRACK CHANGE: $soundFile <<<\"\n";
    script << "                        }\n";
    script << "                    } elseif ($soundFile -ne '' -and $soundFile -eq $ultimoTrackSeleccionado) {\n";
    script << "                        $ultimaModificacionTrack = $currentModTime\n";
    script << "                        Play-Track -index $indiceActual\n";
    script << "                        Write-Host \">>> TRACK RESTART (forced): $soundFile <<<\"\n";
    script << "                    }\n";
    script << "                }\n";
    script << "            } catch { }\n";
    script << "        }\n\n";

    script << "        # Check 3: Heartbeat monitoring\n";
    script << "        try {\n";
    script << "            $modificacionActual = (Get-Item $archivoLog -ErrorAction Stop).LastWriteTime\n";
    script << "            if ($modificacionActual -gt $ultimaModificacion) {\n";
    script << "                $ultimaModificacion = $modificacionActual\n";
    script << "            }\n";
    script << "            $tiempoTranscurrido = (Get-Date) - $ultimaModificacion\n";
    script << "            if ($tiempoTranscurrido.TotalSeconds -gt $tiempoEsperaSegundos) {\n";
    script << "                Write-Host \"HEARTBEAT TIMEOUT - GAME CRASHED - AUTO TERMINATE\"\n";
    script << "                break\n";
    script << "            }\n";
    script << "        } catch {\n";
    script << "            Write-Host \"ERROR reading heartbeat log - probably CTD\"\n";
    script << "            break\n";
    script << "        }\n";
    script << "    }\n";
    script << "}\n";
    script << "finally {\n";
    script << "    try {\n";
    script << "        if ($script:reproductor -ne $null) {\n";
    script << "            $script:reproductor.Stop()\n";
    script << "            $script:reproductor.Dispose()\n";
    script << "        }\n";
    script << "        Write-Host \"Specific script stopped cleanly\"\n";
    script << "    } catch { }\n";
    script << "}\n";

    script.close();

    g_specificScript.name = "Specific";
    g_specificScript.scriptPath = scriptFile;
    std::string trackDirStr = trackDir;
    g_specificScript.stopFilePath = trackDirStr + "\\stop_OSoundtracks_Specific.tmp";
    g_specificScript.trackFilePath = trackDirStr + "\\track_OSoundtracks_Specific.tmp";

    WriteToSoundPlayerLog("Generated OSoundtracks_Specific.ps1" + std::string(g_usingDllPath ? " (Wabbajack mode)" : ""), __LINE__);
}

void GenerateMenuScript() {
    fs::path scriptFile = g_scriptsDirectory / "OSoundtracks_Menu.ps1";

    auto heartbeatPath = GetHeartbeatLogPath().string();
    std::string soundsFolder = g_soundsDirectory.string();

    std::ofstream script(scriptFile, std::ios::binary);
    if (!script.is_open()) {
        logger::error("Failed to create Menu script");
        return;
    }
    script << "\xEF\xBB\xBF";

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Menu.ps1\n";
    script << "# Menu sounds handler\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
    if (g_usingDllPath) {
        script << "# MODE: Wabbajack/MO2 (DLL-relative paths)\n";
    } else {
        script << "# MODE: Standard installation\n";
    }
    script << "# ===================================================================\n\n";

    std::string trackDir = GetTrackDirectory();
    script << "$carpetaSonidos = '" << soundsFolder << "'\n";
    script << "$archivoLog = '" << heartbeatPath << "'\n";
    script << "$stopFile = '" << trackDir << "\\stop_OSoundtracks_Menu.tmp'\n";
    script << "$trackFile = '" << trackDir << "\\track_OSoundtracks_Menu.tmp'\n";
    script << "$tiempoEsperaSegundos = 6\n\n";

    script << "# Available tracks\n";
    script << "$archivosWAV = @(\n";
    script << "    'Debug.wav'";
    if (!g_menuTracks.empty())
        script << ",\n";
    else
        script << "\n";

    for (size_t i = 0; i < g_menuTracks.size(); i++) {
        script << "    '" << g_menuTracks[i] << "'";
        if (i < g_menuTracks.size() - 1) script << ",";
        script << "\n";
    }
    script << ")\n\n";

    script << "# Verify WAV files exist\n";
    script << "foreach ($wav in $archivosWAV) {\n";
    script << "    $rutaCompleta = Join-Path $carpetaSonidos $wav\n";
    script << "    if (-not (Test-Path $rutaCompleta)) {\n";
    script << "        Write-Host \"ERROR: Sound file not found: $rutaCompleta\"\n";
    script << "        exit 1\n";
    script << "    }\n";
    script << "}\n\n";

    script << "# Initialize player\n";
    script << "$script:reproductor = New-Object System.Media.SoundPlayer\n";
    script << "$indiceActual = 0\n";
    script << "$ultimoTrackSeleccionado = 'Debug.wav'\n";
    script << "$ultimaModificacionTrack = [DateTime]::MinValue\n\n";

    script << "# Read initial track if exists\n";
    script << "if (Test-Path $trackFile) {\n";
    script << "    try {\n";
    script << "        $trackInicial = Get-Content $trackFile -Raw -ErrorAction SilentlyContinue\n";
    script << "        $trackInicial = $trackInicial.Trim()\n";
    script << "        $parts = $trackInicial -split '\\|'\n";
    script << "        $soundFile = $parts[0]\n";
    script << "        for ($i = 0; $i -lt $archivosWAV.Count; $i++) {\n";
    script << "            if ($archivosWAV[$i] -eq $soundFile) {\n";
    script << "                $indiceActual = $i\n";
    script << "                $ultimoTrackSeleccionado = $soundFile\n";
    script << "                Write-Host \"Starting with pre-selected track: $soundFile\"\n";
    script << "                break\n";
    script << "            }\n";
    script << "        }\n";
    script << "    } catch { }\n";
    script << "}\n\n";

    script << "# Function to play track\n";
    script << "function Play-Track {\n";
    script << "    param($index)\n";
    script << "    $trackName = $archivosWAV[$index]\n";
    script << "    $rutaWAV = Join-Path $carpetaSonidos $trackName\n";
    script << "    try {\n";
    script << "        if ($script:reproductor -ne $null) {\n";
    script << "            $script:reproductor.Stop()\n";
    script << "            Start-Sleep -Milliseconds 100\n";
    script << "            $script:reproductor.Dispose()\n";
    script << "            $script:reproductor = $null\n";
    script << "        }\n";
    script << "        $script:reproductor = New-Object System.Media.SoundPlayer\n";
    script << "        $script:reproductor.SoundLocation = $rutaWAV\n";
    script << "        $script:reproductor.Load()\n";
    script << "        $script:reproductor.PlayLooping()\n";
    script << "        Write-Host \"NOW PLAYING (MENU): $trackName (Index: $index)\"\n";
    script << "    } catch {\n";
    script << "        Write-Host \"ERROR: Cannot play sound: $_\"\n";
    script << "        exit 1\n";
    script << "    }\n";
    script << "}\n\n";

    script << "# Play initial track\n";
    script << "Play-Track -index $indiceActual\n\n";

    script << "# Get initial heartbeat time\n";
    script << "try {\n";
    script << "    $ultimaModificacion = (Get-Item $archivoLog -ErrorAction Stop).LastWriteTime\n";
    script << "} catch {\n";
    script << "    if ($script:reproductor -ne $null) {\n";
    script << "        $script:reproductor.Stop()\n";
    script << "        $script:reproductor.Dispose()\n";
    script << "    }\n";
    script << "    Write-Host \"ERROR: Heartbeat log not found\"\n";
    script << "    exit 1\n";
    script << "}\n\n";

    script << "# MAIN LOOP\n";
    script << "try {\n";
    script << "    while ($true) {\n";
    script << "        Start-Sleep -Milliseconds 100\n\n";

    script << "        # Check 1: STOP command\n";
    script << "        if (Test-Path $stopFile) {\n";
    script << "            Remove-Item $stopFile -Force -ErrorAction SilentlyContinue\n";
    script << "            Write-Host \"STOP command received\"\n";
    script << "            break\n";
    script << "        }\n\n";

    script << "        # Check 2: Track change command\n";
    script << "        if (Test-Path $trackFile) {\n";
    script << "            try {\n";
    script << "                $currentModTime = (Get-Item $trackFile).LastWriteTime\n";
    script << "                if ($currentModTime -gt $ultimaModificacionTrack) {\n";
    script << "                    $trackContent = Get-Content $trackFile -Raw -ErrorAction SilentlyContinue\n";
    script << "                    $trackContent = $trackContent.Trim()\n";
    script << "                    $parts = $trackContent -split '\\|'\n";
    script << "                    $soundFile = $parts[0]\n";
    script << "                    if ($soundFile -ne '' -and $soundFile -ne $ultimoTrackSeleccionado) {\n";
    script << "                        $nuevoIndice = -1\n";
    script << "                        for ($i = 0; $i -lt $archivosWAV.Count; $i++) {\n";
    script << "                            if ($archivosWAV[$i] -eq $soundFile) {\n";
    script << "                                $nuevoIndice = $i\n";
    script << "                                break\n";
    script << "                            }\n";
    script << "                        }\n";
    script << "                        if ($nuevoIndice -ge 0) {\n";
    script << "                            $indiceActual = $nuevoIndice\n";
    script << "                            $ultimoTrackSeleccionado = $soundFile\n";
    script << "                            $ultimaModificacionTrack = $currentModTime\n";
    script << "                            Play-Track -index $indiceActual\n";
    script << "                            Write-Host \">>> TRACK CHANGE: $soundFile <<<\"\n";
    script << "                        }\n";
    script << "                    } elseif ($soundFile -ne '' -and $soundFile -eq $ultimoTrackSeleccionado) {\n";
    script << "                        $ultimaModificacionTrack = $currentModTime\n";
    script << "                        Play-Track -index $indiceActual\n";
    script << "                        Write-Host \">>> TRACK RESTART (forced): $soundFile <<<\"\n";
    script << "                    }\n";
    script << "                }\n";
    script << "            } catch { }\n";
    script << "        }\n\n";

    script << "        # Check 3: Heartbeat monitoring\n";
    script << "        try {\n";
    script << "            $modificacionActual = (Get-Item $archivoLog -ErrorAction Stop).LastWriteTime\n";
    script << "            if ($modificacionActual -gt $ultimaModificacion) {\n";
    script << "                $ultimaModificacion = $modificacionActual\n";
    script << "            }\n";
    script << "            $tiempoTranscurrido = (Get-Date) - $ultimaModificacion\n";
    script << "            if ($tiempoTranscurrido.TotalSeconds -gt $tiempoEsperaSegundos) {\n";
    script << "                Write-Host \"HEARTBEAT TIMEOUT - GAME CRASHED - AUTO TERMINATE\"\n";
    script << "                break\n";
    script << "            }\n";
    script << "        } catch {\n";
    script << "            Write-Host \"ERROR reading heartbeat log - probably CTD\"\n";
    script << "            break\n";
    script << "        }\n";
    script << "    }\n";
    script << "}\n";
    script << "finally {\n";
    script << "    try {\n";
    script << "        if ($script:reproductor -ne $null) {\n";
    script << "            $script:reproductor.Stop()\n";
    script << "            $script:reproductor.Dispose()\n";
    script << "        }\n";
    script << "        Write-Host \"Menu script stopped cleanly\"\n";
    script << "    } catch { }\n";
    script << "}\n";

    script.close();

    g_menuScript.name = "Menu";
    g_menuScript.scriptPath = scriptFile;
    std::string trackDirStr = trackDir;
    g_menuScript.stopFilePath = trackDirStr + "\\stop_OSoundtracks_Menu.tmp";
    g_menuScript.trackFilePath = trackDirStr + "\\track_OSoundtracks_Menu.tmp";

    WriteToSoundPlayerLog("Generated OSoundtracks_Menu.ps1" + std::string(g_usingDllPath ? " (Wabbajack mode)" : ""), __LINE__);
}

void GenerateCheckScript() {
    fs::path scriptFile = g_scriptsDirectory / "OSoundtracks_Check.ps1";

    auto paths = GetAllSKSELogsPaths();
    auto heartbeatPathPrimary = (paths.primary / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log").string();
    auto heartbeatPathSecondary = (paths.secondary / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log").string();

    std::ofstream script(scriptFile, std::ios::binary);
    if (!script.is_open()) {
        logger::error("Failed to create Check script");
        return;
    }
    script << "\xEF\xBB\xBF";

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Check.ps1\n";
    script << "# Watchdog to monitor heartbeat log updates\n";
    script << "# Kills sibling scripts if game crashes\n";
    script << "# DUAL-PATH SYSTEM: Auto-detects PRIMARY or SECONDARY log location\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
    if (g_usingDllPath) {
        script << "# MODE: Wabbajack/MO2 (DLL-relative paths)\n";
    }
    script << "# ===================================================================\n\n";

    script << "# DUAL-PATH SYSTEM: Check both locations\n";
    script << "$logPathPrimary = \"" << heartbeatPathPrimary << "\"\n";
    script << "$logPathSecondary = \"" << heartbeatPathSecondary << "\"\n";
    script << "$timeoutSeconds = 4\n\n";

    script << "$scriptsToKill = @(\n";
    script << "    'OSoundtracks_Base.ps1',\n";
    script << "    'OSoundtracks_Menu.ps1',\n";
    script << "    'OSoundtracks_Specific.ps1'\n";
    script << ")\n\n";

    script << "function WriteLog($message) {\n";
    script << "    $tstamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'\n";
    script << "    Write-Host \"[$tstamp] $message\"\n";
    script << "}\n\n";

    script << "WriteLog 'Watchdog started with DUAL-PATH detection'\n\n";

    script << "# Auto-detect active log path\n";
    script << "if (Test-Path $logPathPrimary) {\n";
    script << "    $logPath = $logPathPrimary\n";
    script << "    WriteLog \"Using PRIMARY heartbeat log: $logPath\"\n";
    script << "} elseif (Test-Path $logPathSecondary) {\n";
    script << "    $logPath = $logPathSecondary\n";
    script << "    WriteLog \"Using SECONDARY heartbeat log: $logPath\"\n";
    script << "} else {\n";
    script << "    WriteLog \"ERROR: Heartbeat log not found in either location!\"\n";
    script << "    WriteLog \"Tried PRIMARY: $logPathPrimary\"\n";
    script << "    WriteLog \"Tried SECONDARY: $logPathSecondary\"\n";
    script << "    WriteLog \"Waiting up to 4s for log creation...\"\n";
    script << "    $waited = 0\n";
    script << "    while ($waited -lt 4) {\n";
    script << "        Start-Sleep -Seconds 1\n";
    script << "        $waited++\n";
    script << "        if (Test-Path $logPathPrimary) {\n";
    script << "            $logPath = $logPathPrimary\n";
    script << "            WriteLog \"PRIMARY log appeared after ${waited}s\"\n";
    script << "            break\n";
    script << "        }\n";
    script << "        if (Test-Path $logPathSecondary) {\n";
    script << "            $logPath = $logPathSecondary\n";
    script << "            WriteLog \"SECONDARY log appeared after ${waited}s\"\n";
    script << "            break\n";
    script << "        }\n";
    script << "    }\n";
    script << "    if (-not (Test-Path $logPath)) {\n";
    script << "        WriteLog \"Timeout waiting for heartbeat log. Exiting...\"\n";
    script << "        exit 1\n";
    script << "    }\n";
    script << "}\n\n";

    script << "try {\n";
    script << "    $lastModified = (Get-Item $logPath).LastWriteTime\n";
    script << "    $lastLines = Get-Content $logPath -Tail 10\n";
    script << "    $lastContent = $lastLines -join \"`n\"\n";
    script << "    WriteLog \"Heartbeat log loaded. Last modification: $lastModified\"\n";
    script << "}\n";
    script << "catch {\n";
    script << "    WriteLog \"Error reading initial heartbeat log: $_\"\n";
    script << "    exit 1\n";
    script << "}\n\n";

    script << "$secondsWithoutChange = 0\n\n";

    script << "while($true) {\n";
    script << "    Start-Sleep -Seconds 1\n\n";

    script << "    try {\n";
    script << "        $currentModified = (Get-Item $logPath).LastWriteTime\n";
    script << "        $currentLines = Get-Content $logPath -Tail 10\n";
    script << "        $currentContent = $currentLines -join \"`n\"\n\n";

    script << "        if($currentModified -le $lastModified -and $currentContent -eq $lastContent) {\n";
    script << "            $secondsWithoutChange++\n";
    script << "            WriteLog \"No heartbeat update, seconds without change: $secondsWithoutChange\"\n";
    script << "        }\n";
    script << "        else {\n";
    script << "            $secondsWithoutChange = 0\n";
    script << "            $lastModified = $currentModified\n";
    script << "            $lastContent = $currentContent\n";
    script << "            WriteLog \"Heartbeat updated at $lastModified\"\n";
    script << "        }\n\n";

    script << "        if($secondsWithoutChange -ge $timeoutSeconds) {\n";
    script << "            WriteLog \"No heartbeat for $timeoutSeconds seconds, killing sibling scripts...\"\n\n";

    script << "            foreach($scriptName in $scriptsToKill) {\n";
    script << "                WriteLog \"Searching processes for: $scriptName\"\n";
    script << "                $procs = Get-Process -Name powershell -ErrorAction SilentlyContinue\n";
    script << "                foreach($proc in $procs) {\n";
    script << "                    try {\n";
    script << "                        $cmdLine = (Get-CimInstance Win32_Process -Filter "
              "\"ProcessId=$($proc.Id)\").CommandLine\n";
    script << "                        if($cmdLine -like \"*$scriptName*\") {\n";
    script << "                            WriteLog \"Killing process PID $($proc.Id) for script $scriptName\"\n";
    script << "                            Stop-Process $proc.Id -Force\n";
    script << "                        }\n";
    script << "                    }\n";
    script << "                    catch {\n";
    script << "                        WriteLog \"Error killing process PID $($proc.Id): $_\"\n";
    script << "                    }\n";
    script << "                }\n";
    script << "            }\n";
    script << "            WriteLog 'Cleanup done. Watchdog closing...'\n";
    script << "            break\n";
    script << "        }\n";
    script << "    }\n";
    script << "    catch {\n";
    script << "        WriteLog \"Error reading heartbeat log: $_\"\n";
    script << "        WriteLog 'Executing emergency cleanup...'\n";
    script << "        foreach($scriptName in $scriptsToKill) {\n";
    script << "            $procs = Get-Process -Name powershell -ErrorAction SilentlyContinue\n";
    script << "            foreach($proc in $procs) {\n";
    script << "                try {\n";
    script << "                    $cmdLine = (Get-CimInstance Win32_Process -Filter "
              "\"ProcessId=$($proc.Id)\").CommandLine\n";
    script << "                    if($cmdLine -like \"*$scriptName*\") {\n";
    script << "                        WriteLog \"Emergency kill PID $($proc.Id) for $scriptName\"\n";
    script << "                        Stop-Process $proc.Id -Force\n";
    script << "                    }\n";
    script << "                }\n";
    script << "                catch {\n";
    script << "                    WriteLog \"Error in emergency kill PID $($proc.Id): $_\"\n";
    script << "                }\n";
    script << "            }\n";
    script << "        }\n";
    script << "        break\n";
    script << "    }\n";
    script << "}\n";
    script << "WriteLog 'Watchdog terminated cleanly.'\n";

    script.close();

    g_checkScript.name = "Check";
    g_checkScript.scriptPath = scriptFile;

    WriteToSoundPlayerLog("Generated OSoundtracks_Check.ps1 (Watchdog) with DUAL-PATH detection" + std::string(g_usingDllPath ? " (Wabbajack mode)" : ""), __LINE__);
}

void GenerateStaticScripts() {
    fs::create_directories(g_scriptsDirectory);

    CleanOldScripts();

    WriteToSoundPlayerLog("GENERATING 4 STATIC POWERSHELL SCRIPTS WITH DUAL-PATH SUPPORT...", __LINE__);
    WriteToSoundPlayerLog("Scripts path: " + g_scriptsDirectory.string(), __LINE__);
    if (g_usingDllPath) {
        WriteToSoundPlayerLog("MODE: Wabbajack/MO2 (DLL-relative paths)", __LINE__);
        WriteToSoundPlayerLog("Sounds path: " + g_soundsDirectory.string(), __LINE__);
    }

    ClassifyAnimations();

    GenerateBaseScript();
    GenerateSpecificScript();
    GenerateMenuScript();
    GenerateCheckScript();

    WriteToSoundPlayerLog("GENERATED 4 STATIC SCRIPTS SUCCESSFULLY WITH DUAL-PATH SYSTEM", __LINE__);
    WriteToSoundPlayerLog("Scripts: Base, Specific, Menu, Check (Watchdog with auto-detection)", __LINE__);
    WriteToSoundPlayerLog("Audio bleed fix applied: Debug.wav on freeze", __LINE__);
}

PROCESS_INFORMATION LaunchPowerShellScript(const std::string& scriptPath) {
    PROCESS_INFORMATION pi = {0};

    std::string command =
        "powershell.exe -WindowStyle Hidden -ExecutionPolicy Bypass -NoProfile -File \"" + scriptPath + "\"";

    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessA(NULL, (LPSTR)command.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                       NULL, NULL, &si, &pi)) {
        logger::info("Launched PowerShell script: {}", scriptPath);
        return pi;
    } else {
        logger::error("Failed to launch PowerShell script: {}", scriptPath);
        return pi;
    }
}

void StartAllScriptsFrozen() {
    if (g_scriptsInitialized) {
        logger::info("Scripts already initialized, skipping");
        return;
    }

    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("PAUSING LOG MONITORING DURING SCRIPT INITIALIZATION", __LINE__);
    WriteToSoundPlayerLog("Reason: Prevent false sound triggers during warmup", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);
    
    g_pauseMonitoring = true;
    
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("INITIALIZING STATIC SCRIPT SYSTEM WITH DEBUG.WAV WARMUP", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    CleanStopFiles();

    WriteToSoundPlayerLog("Step 1/5: Launching Check script (Watchdog)...", __LINE__);
    g_checkScript.processInfo = LaunchPowerShellScript(g_checkScript.scriptPath.string());
    g_checkScript.isRunning = true;
    Sleep(500);
    WriteToSoundPlayerLog("Check script active", __LINE__);

    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 2/5: Launching Base, Menu, Specific scripts (PARALLEL)", __LINE__);
    WriteToSoundPlayerLog("Reason: Faster initialization (2 seconds saved)", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    WriteToSoundPlayerLog("Launching Base script...", __LINE__);
    g_baseScript.processInfo = LaunchPowerShellScript(g_baseScript.scriptPath.string());

    WriteToSoundPlayerLog("Launching Menu script...", __LINE__);
    g_menuScript.processInfo = LaunchPowerShellScript(g_menuScript.scriptPath.string());

    WriteToSoundPlayerLog("Launching Specific script...", __LINE__);
    g_specificScript.processInfo = LaunchPowerShellScript(g_specificScript.scriptPath.string());

    WriteToSoundPlayerLog("Waiting 1000ms for all scripts to initialize...", __LINE__);
    Sleep(1000);
    WriteToSoundPlayerLog("Initialization window complete", __LINE__);

    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 3/5: Verifying script launches...", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    if (g_baseScript.processInfo.hProcess != 0 && g_baseScript.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        g_baseScript.isRunning = true;
        WriteToSoundPlayerLog("Base script launched successfully (PID: " +
                              std::to_string(g_baseScript.processInfo.dwProcessId) + ")", __LINE__);
    } else {
        logger::error("Failed to launch Base script!");
        WriteToSoundPlayerLog("ERROR: Base script failed to launch", __LINE__);
    }

    if (g_menuScript.processInfo.hProcess != 0 && g_menuScript.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        g_menuScript.isRunning = true;
        WriteToSoundPlayerLog("Menu script launched successfully (PID: " +
                              std::to_string(g_menuScript.processInfo.dwProcessId) + ")", __LINE__);
    } else {
        logger::error("Failed to launch Menu script!");
        WriteToSoundPlayerLog("ERROR: Menu script failed to launch", __LINE__);
    }

    if (g_specificScript.processInfo.hProcess != 0 && g_specificScript.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        g_specificScript.isRunning = true;
        WriteToSoundPlayerLog("Specific script launched successfully (PID: " +
                              std::to_string(g_specificScript.processInfo.dwProcessId) + ")", __LINE__);
    } else {
        logger::error("Failed to launch Specific script!");
        WriteToSoundPlayerLog("ERROR: Specific script failed to launch", __LINE__);
    }

    WriteToSoundPlayerLog("All 3 scripts launched in parallel (1000ms total)", __LINE__);

    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 4/5: WARMUP PHASE - Playing Debug.wav 500ms in all scripts", __LINE__);
    WriteToSoundPlayerLog("Purpose: Force .tmp file creation to eliminate first-play delay", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    if (g_baseScript.isRunning) {
        WriteToSoundPlayerLog("Sending Debug.wav to Base script...", __LINE__);
        SendTrackCommand(SCRIPT_BASE, "Debug.wav");
        g_baseScript.currentTrack = "Debug.wav";
    }

    if (g_menuScript.isRunning) {
        WriteToSoundPlayerLog("Sending Debug.wav to Menu script...", __LINE__);
        SendTrackCommand(SCRIPT_MENU, "Debug.wav");
        g_menuScript.currentTrack = "Debug.wav";
    }

    if (g_specificScript.isRunning) {
        WriteToSoundPlayerLog("Sending Debug.wav to Specific script...", __LINE__);
        SendTrackCommand(SCRIPT_SPECIFIC, "Debug.wav");
        g_specificScript.currentTrack = "Debug.wav";
    }

    WriteToSoundPlayerLog("Playing Debug.wav for 500ms (warmup + .tmp creation)...", __LINE__);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    WriteToSoundPlayerLog("Warmup complete - .tmp files created", __LINE__);

    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 5/5: Freezing scripts (Base, Menu, Specific)", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    if (g_baseScript.isRunning) {
        SuspendProcess(g_baseScript.processInfo.hProcess);
        g_baseScript.isPaused = true;
        WriteToSoundPlayerLog("Base script FROZEN (Debug.wav in buffer)", __LINE__);
    }

    if (g_menuScript.isRunning) {
        SuspendProcess(g_menuScript.processInfo.hProcess);
        g_menuScript.isPaused = true;
        WriteToSoundPlayerLog("Menu script FROZEN (Debug.wav in buffer)", __LINE__);
    }

    if (g_specificScript.isRunning) {
        SuspendProcess(g_specificScript.processInfo.hProcess);
        g_specificScript.isPaused = true;
        WriteToSoundPlayerLog("Specific script FROZEN (Debug.wav in buffer)", __LINE__);
    }

    if (g_volumeControlEnabled.load()) {
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("APPLYING VOLUME CONTROL TO POWERSHELL PROCESSES", __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (g_baseScript.isRunning) {
            bool volumeApplied = SetProcessVolume(g_baseScript.processInfo.dwProcessId, g_baseVolume.load());
            if (volumeApplied) {
                WriteToSoundPlayerLog("Base script volume set to " + std::to_string(static_cast<int>(g_baseVolume.load() * 100)) + "% (PID: " + std::to_string(g_baseScript.processInfo.dwProcessId) + ")", __LINE__);
                logger::info("Base script volume applied: {}%", static_cast<int>(g_baseVolume.load() * 100));
            } else {
                WriteToSoundPlayerLog("WARNING: Could not set volume for Base script - user can adjust manually in Windows Volume Mixer", __LINE__);
                logger::warn("Failed to set volume for Base script PID {}", g_baseScript.processInfo.dwProcessId);
            }
        }
        
        if (g_menuScript.isRunning) {
            bool volumeApplied = SetProcessVolume(g_menuScript.processInfo.dwProcessId, g_menuVolume.load());
            if (volumeApplied) {
                WriteToSoundPlayerLog("Menu script volume set to " + std::to_string(static_cast<int>(g_menuVolume.load() * 100)) + "% (PID: " + std::to_string(g_menuScript.processInfo.dwProcessId) + ")", __LINE__);
                logger::info("Menu script volume applied: {}%", static_cast<int>(g_menuVolume.load() * 100));
            } else {
                WriteToSoundPlayerLog("WARNING: Could not set volume for Menu script - user can adjust manually in Windows Volume Mixer", __LINE__);
                logger::warn("Failed to set volume for Menu script PID {}", g_menuScript.processInfo.dwProcessId);
            }
        }
        
        if (g_specificScript.isRunning) {
            bool volumeApplied = SetProcessVolume(g_specificScript.processInfo.dwProcessId, g_specificVolume.load());
            if (volumeApplied) {
                WriteToSoundPlayerLog("Specific script volume set to " + std::to_string(static_cast<int>(g_specificVolume.load() * 100)) + "% (PID: " + std::to_string(g_specificScript.processInfo.dwProcessId) + ")", __LINE__);
                logger::info("Specific script volume applied: {}%", static_cast<int>(g_specificVolume.load() * 100));
            } else {
                WriteToSoundPlayerLog("WARNING: Could not set volume for Specific script - user can adjust manually in Windows Volume Mixer", __LINE__);
                logger::warn("Failed to set volume for Specific script PID {}", g_specificScript.processInfo.dwProcessId);
            }
        }
        
        WriteToSoundPlayerLog("Volume control application complete", __LINE__);
    } else {
        WriteToSoundPlayerLog("Volume control disabled in INI - scripts will use system volume", __LINE__);
    }

    g_scriptsInitialized = true;

    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("RESUMING LOG MONITORING", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);
    
    g_pauseMonitoring = false;
    
    WriteToSoundPlayerLog("ALL SCRIPTS INITIALIZED SUCCESSFULLY", __LINE__);
    WriteToSoundPlayerLog("Status: Base [FROZEN], Menu [FROZEN], Specific [FROZEN], Check [ACTIVE]", __LINE__);
    WriteToSoundPlayerLog("Ready: .tmp files pre-created, zero delay on first sound", __LINE__);
    WriteToSoundPlayerLog("Log monitoring RESUMED - safe to process animations", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    if (!g_activationMessageShown) {
        ShowGameNotification("OSoundtracks - activated");
        g_activationMessageShown = true;
        WriteToSoundPlayerLog("IN-GAME MESSAGE SHOWN: OSoundtracks - activated", __LINE__);
        logger::info("Activation message shown to player");
    }

    logger::info("All scripts started with Debug.wav warmup and frozen successfully");
    logger::info("Total initialization time: ~2 seconds (500 + 1000 + 500) - OPTIMIZED PARALLEL LAUNCH");
}

void SendTrackCommand(ScriptType type, const std::string& trackFile) {
    std::string trackFilePath;

    switch (type) {
        case SCRIPT_BASE:
            trackFilePath = g_baseScript.trackFilePath.string();
            break;
        case SCRIPT_SPECIFIC:
            trackFilePath = g_specificScript.trackFilePath.string();
            break;
        case SCRIPT_MENU:
            trackFilePath = g_menuScript.trackFilePath.string();
            break;
        default:
            return;
    }

    try {
        std::ofstream file(trackFilePath);
        file << trackFile << "|" << GetCurrentTimeStringWithMillis() << std::endl;
        file.close();
    } catch (const std::exception& e) {
        logger::error("Error writing track command: {}", e.what());
    }
}

void PlaySoundInScript(ScriptType type, const std::string& soundFile) {
    ScriptState* scriptState;

    switch (type) {
        case SCRIPT_BASE:
            scriptState = &g_baseScript;
            break;
        case SCRIPT_SPECIFIC:
            scriptState = &g_specificScript;
            break;
        case SCRIPT_MENU:
            scriptState = &g_menuScript;
            break;
        default:
            return;
    }

    if (!scriptState->isRunning) {
        logger::warn("Script {} is not running, cannot play sound", scriptState->name);
        return;
    }

    SendTrackCommand(type, soundFile);

    if (scriptState->isPaused) {
        ResumeProcess(scriptState->processInfo.hProcess);
        scriptState->isPaused = false;
        WriteToSoundPlayerLog("Script " + scriptState->name + " unfrozen to play: " + soundFile, __LINE__);
    }

    scriptState->currentTrack = soundFile;

    if (soundFile != "Debug.wav") {
        std::string displayName = soundFile;
        if (displayName.size() >= 4 && displayName.substr(displayName.size() - 4) == ".wav") {
            displayName = displayName.substr(0, displayName.size() - 4);
        }
        
        std::string notificationMsg = "OSoundtracks - \"" + displayName + "\" is played";
        ShowGameNotification(notificationMsg);
    }
}

void StopScript(ScriptState& scriptState) {
    if (!scriptState.isRunning) return;
    
    WriteToSoundPlayerLog("Stopping script: " + scriptState.name +
                          (scriptState.isPaused ? " [FROZEN]" : " [ACTIVE]"), __LINE__);
    
    if (scriptState.processInfo.hProcess != 0 && scriptState.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(scriptState.processInfo.hProcess, 0);
        WaitForSingleObject(scriptState.processInfo.hProcess, 500);
        WriteToSoundPlayerLog("Script " + scriptState.name + " terminated directly without unfreeze", __LINE__);
    }
    
    CloseHandle(scriptState.processInfo.hProcess);
    CloseHandle(scriptState.processInfo.hThread);
    ZeroMemory(&scriptState.processInfo, sizeof(PROCESS_INFORMATION));
    
    scriptState.isRunning = false;
    scriptState.isPaused = false;
    scriptState.currentTrack = "";
    
    try {
        fs::remove(scriptState.stopFilePath);
    } catch (...) {}
    
    WriteToSoundPlayerLog("Script " + scriptState.name + " stopped cleanly (forced)", __LINE__);
}

void StopAllScripts() {
    WriteToSoundPlayerLog("STOPPING ALL SCRIPTS", __LINE__);

    StopScript(g_baseScript);
    StopScript(g_menuScript);
    StopScript(g_specificScript);

    if (g_checkScript.isRunning && g_checkScript.processInfo.hProcess != 0) {
        try {
            TerminateProcess(g_checkScript.processInfo.hProcess, 1);
            CloseHandle(g_checkScript.processInfo.hProcess);
            CloseHandle(g_checkScript.processInfo.hThread);
            ZeroMemory(&g_checkScript.processInfo, sizeof(PROCESS_INFORMATION));
            g_checkScript.isRunning = false;
            WriteToSoundPlayerLog("Check script (Watchdog) stopped", __LINE__);
        } catch (...) {
        }
    }

    g_scriptsInitialized = false;
    g_currentBaseAnimation = "";
    g_currentSpecificAnimation = "";
    g_currentPositionFragment = "";
    g_lastAnimation = "";

    {
        std::lock_guard<std::mutex> lock(g_throttleMutex);
        g_lastPlayTime.clear();
    }

    g_activationMessageShown = false;
    WriteToSoundPlayerLog("Activation message flag reset - will show on next initialization", __LINE__);

    WriteToSoundPlayerLog("All scripts stopped - tracking variables reset for next event", __LINE__);
}

void PauseAllSounds() {
    std::lock_guard<std::mutex> lock(g_pauseMutex);

    if (!g_soundsPaused.load()) {
        g_soundsPaused = true;

        WriteToSoundPlayerLog("PAUSING ALL SOUNDS (BASS + MULTI-LAYER POSITION)", __LINE__);

        g_baseWasActiveBeforePause = (g_baseStream != 0);
        g_menuWasActiveBeforePause = (g_menuStream != 0);
        g_specificWasActiveBeforePause = (g_specificStream != 0);
        g_soundMenuKeyWasActiveBeforePause = (g_soundMenuKeyActive.load() && !g_soundMenuKeyPaused.load());
        g_effectWasActiveBeforePause = (g_effectStream != 0);
        g_tagWasActiveBeforePause = (g_tagStream != 0);

        if (g_baseWasActiveBeforePause) {
            PauseBASSStream(SCRIPT_BASE);
            WriteToSoundPlayerLog("Base stream paused", __LINE__);
        }

        if (g_menuWasActiveBeforePause) {
            PauseBASSStream(SCRIPT_MENU);
            WriteToSoundPlayerLog("Menu stream paused", __LINE__);
        }

        if (g_specificWasActiveBeforePause) {
            PauseBASSStream(SCRIPT_SPECIFIC);
            WriteToSoundPlayerLog("Specific stream paused", __LINE__);
        }

        if (g_soundMenuKeyWasActiveBeforePause) {
            PauseBASSStream(SCRIPT_CHECK);
            g_soundMenuKeyPaused = true;
            WriteToSoundPlayerLog("SoundMenuKey stream paused", __LINE__);
        }

        if (g_effectWasActiveBeforePause) {
            PauseBASSStream(SCRIPT_EFFECT);
            WriteToSoundPlayerLog("Effect stream paused", __LINE__);
        }

        if (g_tagWasActiveBeforePause) {
            PauseBASSStream(SCRIPT_TAG);
            WriteToSoundPlayerLog("Tag stream paused", __LINE__);
        }

        g_positionLayersActiveBeforePause.clear();
        int totalPositionStreams = 0;
        
        for (auto& [fragment, layers] : g_positionStreams) {
            for (auto& [layerNum, stream] : layers) {
                if (stream && pBASS_ChannelPause) {
                    pBASS_ChannelPause(stream);
                    g_positionLayersActiveBeforePause[fragment][layerNum] = true;
                    totalPositionStreams++;
                }
            }
        }
        
        if (totalPositionStreams > 0) {
            WriteToSoundPlayerLog("Position streams paused: " + std::to_string(totalPositionStreams) + 
                                 " streams across " + std::to_string(g_positionStreams.size()) + " fragments", __LINE__);
        }

        logger::info("BASS: ALL streams paused (Base, Menu, Specific, SoundMenuKey, Effect, Tag, Position x{})", totalPositionStreams);
    }
}

void ResumeAllSounds() {
    std::lock_guard<std::mutex> lock(g_pauseMutex);

    if (g_soundsPaused.load()) {
        g_soundsPaused = false;

        WriteToSoundPlayerLog("RESUMING ALL SOUNDS (BASS + MULTI-LAYER POSITION)", __LINE__);

        if (g_baseWasActiveBeforePause && g_baseStream) {
            ResumeBASSStream(SCRIPT_BASE);
            WriteToSoundPlayerLog("Base stream resumed", __LINE__);
        }

        if (g_menuWasActiveBeforePause && g_menuStream) {
            ResumeBASSStream(SCRIPT_MENU);
            WriteToSoundPlayerLog("Menu stream resumed", __LINE__);
        }

        if (g_specificWasActiveBeforePause && g_specificStream) {
            ResumeBASSStream(SCRIPT_SPECIFIC);
            WriteToSoundPlayerLog("Specific stream resumed", __LINE__);
        }

        if (g_soundMenuKeyWasActiveBeforePause && g_soundMenuKeyStream) {
            if (g_soundMenuKeyMode != SoundMenuKeyMode::DISABLED) {
                ResumeBASSStream(SCRIPT_CHECK);
                g_soundMenuKeyPaused = false;
                WriteToSoundPlayerLog("SoundMenuKey stream resumed", __LINE__);
            } else {
                WriteToSoundPlayerLog("SoundMenuKey NOT resumed (disabled in INI)", __LINE__);
            }
        }

        if (g_effectWasActiveBeforePause && g_effectStream) {
            ResumeBASSStream(SCRIPT_EFFECT);
            WriteToSoundPlayerLog("Effect stream resumed", __LINE__);
        }

        if (g_tagWasActiveBeforePause && g_tagStream) {
            ResumeBASSStream(SCRIPT_TAG);
            WriteToSoundPlayerLog("Tag stream resumed", __LINE__);
        }

        int resumedPositionStreams = 0;
        for (const auto& [fragment, layers] : g_positionLayersActiveBeforePause) {
            for (const auto& [layerNum, wasActive] : layers) {
                if (wasActive) {
                    auto fragIt = g_positionStreams.find(fragment);
                    if (fragIt != g_positionStreams.end()) {
                        auto layerIt = fragIt->second.find(layerNum);
                        if (layerIt != fragIt->second.end() && layerIt->second && pBASS_ChannelPlay) {
                            pBASS_ChannelPlay(layerIt->second, FALSE);
                            resumedPositionStreams++;
                        }
                    }
                }
            }
        }
        
        if (resumedPositionStreams > 0) {
            WriteToSoundPlayerLog("Position streams resumed: " + std::to_string(resumedPositionStreams), __LINE__);
        }

        logger::info("BASS: All streams resumed");
    }
}

void CheckAndPlaySound(const std::string& animationName) {
    if (!g_isInitialized || g_isShuttingDown.load()) return;

    try {
        if (!g_bassInitialized) {
            if (!InitializeBASSLibrary()) {
                logger::error("BASS: Failed to initialize library in CheckAndPlaySound");
                return;
            }
        }

        std::string baseAnimation = GetAnimationBase(animationName);

        if (baseAnimation != g_currentBaseAnimation) {
            logger::info("Base animation changed from '{}' to '{}'", g_currentBaseAnimation, baseAnimation);
            
            auto baseIt = g_animationSoundMap.find(baseAnimation);
            
            if (baseIt != g_animationSoundMap.end()) {
                if (!baseIt->second.soundOptions.empty()) {
                    int randomIndex = rand() % baseIt->second.soundOptions.size();
                    const auto& chosen = baseIt->second.soundOptions[randomIndex];
                    
                    logger::info("BASE SOUND: Starting '{}' for animation family '{}'",
                                 chosen.soundFile, baseAnimation);
                    WriteToSoundPlayerLog("BASE SOUND: " + chosen.soundFile +
                                          " (random " + std::to_string(randomIndex + 1) + 
                                          "/" + std::to_string(baseIt->second.soundOptions.size()) + 
                                          ") for family " + baseAnimation, __LINE__);
                    PlayBASSSound(chosen.soundFile, SCRIPT_BASE, true);
                    g_currentBaseAnimation = baseAnimation;
                    
                    PauseSoundMenuKey();
                }
            } else {
                if (!g_currentBaseAnimation.empty()) {
                    logger::info("No base sound for family '{}', stopping base stream", baseAnimation);
                    WriteToSoundPlayerLog("BASE SOUND STOPPED (no mapping for " + baseAnimation + ")", __LINE__);
                    StopBASSStream(SCRIPT_BASE);
                    
                    ResumeSoundMenuKey();
                }
                g_currentBaseAnimation = baseAnimation;
            }
        }

        auto specificIt = g_animationSoundMap.find(animationName);

        if (specificIt != g_animationSoundMap.end()) {
            if (animationName != g_currentSpecificAnimation) {
                if (!specificIt->second.soundOptions.empty()) {
                    int randomIndex = rand() % specificIt->second.soundOptions.size();
                    const auto& chosen = specificIt->second.soundOptions[randomIndex];
                    
                    WriteToSoundPlayerLog("SPECIFIC SOUND: " + chosen.soundFile +
                                          " (random " + std::to_string(randomIndex + 1) + 
                                          "/" + std::to_string(specificIt->second.soundOptions.size()) + 
                                          ") for " + animationName, __LINE__);
                    PlayBASSSound(chosen.soundFile, SCRIPT_SPECIFIC, true);
                    g_currentSpecificAnimation = animationName;
                }
            } else {
                std::lock_guard<std::mutex> lock(g_throttleMutex);
                auto now = std::chrono::steady_clock::now();
                auto lastPlayIt = g_lastPlayTime.find(animationName);

                if (lastPlayIt != g_lastPlayTime.end()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastPlayIt->second).count();
                    int delayRequired = specificIt->second.repeatDelaySeconds;

                    if (delayRequired > 0 && elapsed < delayRequired) {
                        WriteToSoundPlayerLog("THROTTLED: sound for " + animationName +
                                             " (wait " + std::to_string(delayRequired - elapsed) + "s more)", __LINE__);
                        return;
                    }
                }

                if (!specificIt->second.soundOptions.empty()) {
                    int randomIndex = rand() % specificIt->second.soundOptions.size();
                    const auto& chosen = specificIt->second.soundOptions[randomIndex];
                    
                    WriteToSoundPlayerLog("SPECIFIC SOUND RESTART: " + chosen.soundFile +
                                         " (random " + std::to_string(randomIndex + 1) + 
                                         "/" + std::to_string(specificIt->second.soundOptions.size()) + 
                                         ") for " + animationName + " [delay cleared]", __LINE__);
                    PlayBASSSound(chosen.soundFile, SCRIPT_SPECIFIC, true);
                    g_lastPlayTime[animationName] = now;
                }
            }
            
        } else {
            if (!g_currentSpecificAnimation.empty()) {
                logger::info("No specific sound for '{}', stopping specific stream", animationName);
                WriteToSoundPlayerLog("SPECIFIC SOUND STOPPED (no mapping)", __LINE__);
                StopBASSStream(SCRIPT_SPECIFIC);
            }
            g_currentSpecificAnimation = "";
        }
        
        CheckPositionSound(animationName);
        
    } catch (...) {
        logger::error("Error in CheckAndPlaySound");
    }
}

void CheckPositionSound(const std::string& animationName) {
    if (g_positionSoundMap.empty()) return;
    
    std::string animationLower = ToLowerCase(animationName);
    
    std::set<std::string> matchedFragments;
    
    for (const auto& [fragment, config] : g_positionSoundMap) {
        std::string fragmentLower = ToLowerCase(fragment);
        
        if (animationLower.find(fragmentLower) != std::string::npos) {
            matchedFragments.insert(fragment);
        }
    }
    
    std::vector<std::string> toRemove;
    for (const auto& activeFragment : g_activePositionFragments) {
        if (matchedFragments.find(activeFragment) == matchedFragments.end()) {
            if (g_positionStreams.find(activeFragment) != g_positionStreams.end()) {
                for (auto& [layer, stream] : g_positionStreams[activeFragment]) {
                    if (stream) {
                        if (pBASS_ChannelStop) pBASS_ChannelStop(stream);
                        if (pBASS_StreamFree) pBASS_StreamFree(stream);
                        stream = 0;
                    }
                }
                g_positionStreams.erase(activeFragment);
            }
            toRemove.push_back(activeFragment);
            WriteToSoundPlayerLog("POSITION: Stopped fragment '" + activeFragment + "'", __LINE__);
        }
    }
    
    for (const auto& frag : toRemove) {
        g_activePositionFragments.erase(frag);
    }
    
    for (const auto& fragment : matchedFragments) {
        bool isNewFragment = (g_activePositionFragments.find(fragment) == g_activePositionFragments.end());
        
        if (isNewFragment) {
            const auto& config = g_positionSoundMap[fragment];
            
            if (!config.layers.empty()) {
                for (const auto& [layerNum, sounds] : config.layers) {
                    if (sounds.empty()) continue;
                    
                    int randomIndex = rand() % sounds.size();
                    const auto& chosen = sounds[randomIndex];
                    
                    fs::path soundPath = FindSoundFile(chosen.soundFile);
                    if (soundPath.empty()) {
                        WriteToSoundPlayerLog("POSITION ERROR: Sound not found: " + chosen.soundFile, __LINE__);
                        continue;
                    }
                    
                    if (!g_bassInitialized) {
                        if (!InitializeBASSLibrary()) continue;
                    }
                    
                    std::lock_guard<std::mutex> lock(g_bassMutex);
                    
                    HSTREAM newStream = pBASS_StreamCreateFile(
                        FALSE,
                        soundPath.wstring().c_str(),
                        0, 0,
                        BASS_UNICODE | BASS_SAMPLE_LOOP
                    );
                    
                    if (!newStream) {
                        int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
                        WriteToSoundPlayerLog("POSITION ERROR: Stream creation failed, error " + std::to_string(error), __LINE__);
                        continue;
                    }
                    
                    float volume = g_volumeControlEnabled.load() ? g_positionVolume.load() : 1.0f;
                    if (pBASS_ChannelSetAttribute) {
                        pBASS_ChannelSetAttribute(newStream, BASS_ATTRIB_VOL, volume);
                    }
                    
                    if (!pBASS_ChannelPlay(newStream, FALSE)) {
                        if (pBASS_StreamFree) pBASS_StreamFree(newStream);
                        continue;
                    }
                    
                    g_positionStreams[fragment][layerNum] = newStream;
                    
                    WriteToSoundPlayerLog("POSITION: Playing '" + chosen.soundFile + 
                                         "' [Fragment: '" + fragment + "', Layer: " + std::to_string(layerNum) + "]", __LINE__);
                    
                    if (layerNum == 0) {
                        std::string displayName = chosen.soundFile;
                        size_t dotPos = displayName.find_last_of('.');
                        if (dotPos != std::string::npos) {
                            displayName = displayName.substr(0, dotPos);
                        }
                        ShowGameNotification("OSoundtracks - \"" + displayName + "\" is played");
                    }
                }
            }
            else if (!config.soundOptions.empty()) {
                int randomIndex = rand() % config.soundOptions.size();
                const auto& chosen = config.soundOptions[randomIndex];
                
                fs::path soundPath = FindSoundFile(chosen.soundFile);
                if (!soundPath.empty() && g_bassInitialized) {
                    std::lock_guard<std::mutex> lock(g_bassMutex);
                    
                    HSTREAM newStream = pBASS_StreamCreateFile(
                        FALSE,
                        soundPath.wstring().c_str(),
                        0, 0,
                        BASS_UNICODE | BASS_SAMPLE_LOOP
                    );
                    
                    if (newStream) {
                        float volume = g_volumeControlEnabled.load() ? g_positionVolume.load() : 1.0f;
                        if (pBASS_ChannelSetAttribute) {
                            pBASS_ChannelSetAttribute(newStream, BASS_ATTRIB_VOL, volume);
                        }
                        
                        if (pBASS_ChannelPlay(newStream, FALSE)) {
                            g_positionStreams[fragment][0] = newStream;
                            WriteToSoundPlayerLog("POSITION: Playing '" + chosen.soundFile + 
                                                 "' [Fragment: '" + fragment + "']", __LINE__);
                        }
                    }
                }
            }
            
            g_activePositionFragments.insert(fragment);
        }
    }
}

void PlayMenuSound(const std::string& menuName) {
    auto soundIt = g_animationSoundMap.find(menuName);
    if (soundIt == g_animationSoundMap.end()) {
        return;
    }

    if (!g_bassInitialized) {
        if (!InitializeBASSLibrary()) return;
    }

    PauseSoundMenuKey();

    if (!soundIt->second.soundOptions.empty()) {
        PlayBASSSound(soundIt->second.soundOptions[0].soundFile, SCRIPT_MENU, true);
    }
}

void StopMenuSound(const std::string& menuName) {
    if (g_menuStream) {
        StopBASSStream(SCRIPT_MENU);
        WriteToSoundPlayerLog("BASS: Menu stream stopped", __LINE__);
        logger::info("BASS: Stopped menu sound for: {}", menuName);
    }

    if (!g_baseStream && g_soundMenuKeyActive.load()) {
        ResumeSoundMenuKey();
    }
}

void StopAllMenuSounds() { StopMenuSound(""); }

void StopAllSounds() { StopAllBASSStreams(); }

void StopAuthorPreview() {
    if (!g_previewPlaying.load()) {
        return;
    }
    
    WriteToSoundPlayerLog("PREVIEW: Stopping preview stream", __LINE__);
    
    {
        std::lock_guard<std::mutex> lock(g_bassMutex);
        
        if (g_authorPreviewStream) {
            if (pBASS_ChannelStop) {
                pBASS_ChannelStop(g_authorPreviewStream);
            }
            if (pBASS_StreamFree) {
                pBASS_StreamFree(g_authorPreviewStream);
            }
            g_authorPreviewStream = 0;
        }
    }
    
    g_previewPlaying = false;
    
    if (g_previewTimerActive.load()) {
        g_previewTimerActive = false;
    }
    
    WriteToSoundPlayerLog("PREVIEW: Stream stopped cleanly", __LINE__);
}

void StartPreviewTimer(const std::string& authorName, const std::string& songName) {
    if (g_previewTimerActive.load()) {
        g_previewTimerActive = false;
        if (g_previewTimerThread.joinable()) {
            g_previewTimerThread.join();
        }
    }
    
    g_previewTimerActive = true;
    
    g_previewTimerThread = std::thread([authorName, songName]() {
        WriteToSoundPlayerLog("PREVIEW TIMER: Started for '" + songName + "' (7000ms)", __LINE__);
        
        for (int i = 0; i < 70 && g_previewTimerActive.load() && !g_isShuttingDown.load(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (g_previewTimerActive.load() && !g_isShuttingDown.load()) {
            WriteToSoundPlayerLog("PREVIEW TIMER: 7 seconds elapsed, stopping preview", __LINE__);
            StopAuthorPreview();
        }
        
        g_previewTimerActive = false;
    });
    
    g_previewTimerThread.detach();
}

void PlayAuthorPreview(const std::string& authorName) {
    auto it = g_soundMenuKeyMap.find(authorName);
    if (it == g_soundMenuKeyMap.end()) {
        WriteToSoundPlayerLog("PREVIEW: Author '" + authorName + "' not found in JSON", __LINE__);
        return;
    }
    
    if (it->second.empty()) {
        WriteToSoundPlayerLog("PREVIEW: Author '" + authorName + "' has no songs", __LINE__);
        return;
    }
    
    if (g_previewPlaying.load()) {
        StopAuthorPreview();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, static_cast<int>(it->second.size()) - 1);
    int randomIndex = dis(gen);
    
    std::string previewSong = it->second[randomIndex].soundFile;
    
    WriteToSoundPlayerLog("PREVIEW: Playing '" + previewSong + "' from author '" + authorName + "' (3 seconds)", __LINE__);
    
    fs::path soundPath = FindSoundFile(previewSong);
    if (soundPath.empty()) {
        WriteToSoundPlayerLog("PREVIEW ERROR: Sound file not found: " + previewSong, __LINE__);
        return;
    }
    
    if (!g_bassInitialized) {
        if (!InitializeBASSLibrary()) {
            WriteToSoundPlayerLog("PREVIEW ERROR: BASS not initialized", __LINE__);
            return;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(g_bassMutex);
        
        if (g_authorPreviewStream) {
            if (pBASS_ChannelStop) pBASS_ChannelStop(g_authorPreviewStream);
            if (pBASS_StreamFree) pBASS_StreamFree(g_authorPreviewStream);
            g_authorPreviewStream = 0;
        }
        
        g_authorPreviewStream = pBASS_StreamCreateFile(
            FALSE,
            soundPath.wstring().c_str(),
            0, 0,
            BASS_UNICODE
        );
        
        if (!g_authorPreviewStream) {
            int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
            WriteToSoundPlayerLog("PREVIEW ERROR: Failed to create BASS stream, error " + std::to_string(error), __LINE__);
            return;
        }
        
        float previewVolume = 1.0f;
        
        if (pBASS_ChannelSetAttribute) {
            pBASS_ChannelSetAttribute(g_authorPreviewStream, BASS_ATTRIB_VOL, previewVolume);
        }
        
        if (!pBASS_ChannelPlay(g_authorPreviewStream, FALSE)) {
            int error = pBASS_ErrorGetCode ? pBASS_ErrorGetCode() : -1;
            WriteToSoundPlayerLog("PREVIEW ERROR: Failed to play stream, error " + std::to_string(error), __LINE__);
            return;
        }
    }
    
    g_previewPlaying = true;
    
    StartPreviewTimer(authorName, previewSong);
    
    std::string displayName = previewSong;
    size_t dotPos = displayName.find_last_of('.');
    if (dotPos != std::string::npos) {
        displayName = displayName.substr(0, dotPos);
    }
    
    ShowGameNotification("OSoundtracks PREVIEW - Author: " + authorName);
    ShowGameNotification("Song: \"" + displayName + "\" (7s sample)");
}

void BuildSoundMenuKeyPlaylist() {
    std::lock_guard<std::mutex> lock(g_soundMenuKeyMutex);
    g_soundMenuKeyPlaylist.clear();
    g_soundMenuKeyCurrentIndex = 0;
    
    if (g_soundMenuKeyMode == SoundMenuKeyMode::DISABLED) {
        return;
    }
    
    if (g_soundMenuKeyMode == SoundMenuKeyMode::ALL_ORDER || g_soundMenuKeyMode == SoundMenuKeyMode::ALL_RANDOM) {
        for (const auto& [author, songs] : g_soundMenuKeyMap) {
            for (const auto& song : songs) {
                g_soundMenuKeyPlaylist.push_back(song.soundFile);
            }
        }
    } else if (g_soundMenuKeyMode == SoundMenuKeyMode::AUTHOR_ORDER || g_soundMenuKeyMode == SoundMenuKeyMode::AUTHOR_RANDOM) {
        auto it = g_soundMenuKeyMap.find(g_soundMenuKeyAuthor);
        if (it != g_soundMenuKeyMap.end()) {
            for (const auto& song : it->second) {
                g_soundMenuKeyPlaylist.push_back(song.soundFile);
            }
        } else {
            WriteToSoundPlayerLog("SoundMenuKey: Author '" + g_soundMenuKeyAuthor + "' not found in JSON", __LINE__);
        }
    }
    
    if (g_soundMenuKeyMode == SoundMenuKeyMode::ALL_RANDOM || g_soundMenuKeyMode == SoundMenuKeyMode::AUTHOR_RANDOM) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(g_soundMenuKeyPlaylist.begin(), g_soundMenuKeyPlaylist.end(), g);
    }
    
    WriteToSoundPlayerLog("SoundMenuKey: Playlist built with " + std::to_string(g_soundMenuKeyPlaylist.size()) + " tracks", __LINE__);
}

void PlayNextSoundMenuKeyTrack() {
    std::lock_guard<std::mutex> lock(g_soundMenuKeyMutex);
    
    if (g_soundMenuKeyPlaylist.empty() || !g_soundMenuKeyActive.load()) {
        return;
    }
    
    if (g_soundMenuKeyCurrentIndex >= g_soundMenuKeyPlaylist.size()) {
        g_soundMenuKeyCurrentIndex = 0;
        if (g_soundMenuKeyMode == SoundMenuKeyMode::ALL_RANDOM || g_soundMenuKeyMode == SoundMenuKeyMode::AUTHOR_RANDOM) {
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(g_soundMenuKeyPlaylist.begin(), g_soundMenuKeyPlaylist.end(), g);
        }
    }
    
    std::string trackToPlay = g_soundMenuKeyPlaylist[g_soundMenuKeyCurrentIndex];
    g_soundMenuKeyCurrentIndex++;
    g_currentSoundMenuKeyTrack = trackToPlay;
    
    PlayBASSSound(trackToPlay, SCRIPT_CHECK, false);
    
    WriteToSoundPlayerLog("SoundMenuKey: Playing '" + trackToPlay + "' (" + 
                         std::to_string(g_soundMenuKeyCurrentIndex) + "/" + 
                         std::to_string(g_soundMenuKeyPlaylist.size()) + ")", __LINE__);
}

void StartSoundMenuKey() {
    if (g_soundMenuKeyMode == SoundMenuKeyMode::DISABLED) {
        WriteToSoundPlayerLog("SoundMenuKey: System is disabled", __LINE__);
        return;
    }
    
    if (g_soundMenuKeyActive.load()) {
        WriteToSoundPlayerLog("SoundMenuKey: Already active", __LINE__);
        return;
    }
    
    BuildSoundMenuKeyPlaylist();
    
    if (g_soundMenuKeyPlaylist.empty()) {
        WriteToSoundPlayerLog("SoundMenuKey: No tracks available", __LINE__);
        return;
    }
    
    g_soundMenuKeyActive = true;
    g_soundMenuKeyPaused = false;
    PlayNextSoundMenuKeyTrack();
    StartSoundMenuKeyMonitor();
    
    WriteToSoundPlayerLog("SoundMenuKey: Started", __LINE__);
}

void StopSoundMenuKey() {
    if (!g_soundMenuKeyActive.load()) {
        return;
    }
    
    StopSoundMenuKeyMonitor();
    StopBASSStream(SCRIPT_CHECK);
    g_soundMenuKeyActive = false;
    g_soundMenuKeyPaused = false;
    g_currentSoundMenuKeyTrack = "";
    
    WriteToSoundPlayerLog("SoundMenuKey: Stopped", __LINE__);
}

void PauseSoundMenuKey() {
    if (!g_soundMenuKeyActive.load() || g_soundMenuKeyPaused.load()) {
        return;
    }
    
    PauseBASSStream(SCRIPT_CHECK);
    g_soundMenuKeyPaused = true;
    
    WriteToSoundPlayerLog("SoundMenuKey: Paused", __LINE__);
}

void ResumeSoundMenuKey() {
    if (!g_soundMenuKeyActive.load() || !g_soundMenuKeyPaused.load()) {
        return;
    }
    
    ResumeBASSStream(SCRIPT_CHECK);
    g_soundMenuKeyPaused = false;
    
    WriteToSoundPlayerLog("SoundMenuKey: Resumed", __LINE__);
}

void SoundMenuKeyMonitorThreadFunction() {
    WriteToSoundPlayerLog("SoundMenuKey: Monitor thread started", __LINE__);
    
    while (g_soundMenuKeyMonitorActive.load() && !g_isShuttingDown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (!g_soundMenuKeyActive.load()) {
            continue;
        }
        
        if (g_soundMenuKeyPaused.load()) {
            continue;
        }
        
        if (g_soundMenuKeyStream && pBASS_ChannelIsActive) {
            DWORD status = pBASS_ChannelIsActive(g_soundMenuKeyStream);
            
            if (status == BASS_ACTIVE_STOPPED) {
                WriteToSoundPlayerLog("SoundMenuKey: Track finished, playing next", __LINE__);
                PlayNextSoundMenuKeyTrack();
            }
        }
    }
    
    WriteToSoundPlayerLog("SoundMenuKey: Monitor thread stopped", __LINE__);
}

void StartSoundMenuKeyMonitor() {
    if (g_soundMenuKeyMonitorActive.load()) {
        return;
    }
    
    g_soundMenuKeyMonitorActive = true;
    g_soundMenuKeyMonitorThread = std::thread(SoundMenuKeyMonitorThreadFunction);
    WriteToSoundPlayerLog("SoundMenuKey: Monitor started", __LINE__);
}

void StopSoundMenuKeyMonitor() {
    if (!g_soundMenuKeyMonitorActive.load()) {
        return;
    }
    
    g_soundMenuKeyMonitorActive = false;
    
    if (g_soundMenuKeyMonitorThread.joinable()) {
        g_soundMenuKeyMonitorThread.join();
    }
    
    WriteToSoundPlayerLog("SoundMenuKey: Monitor stopped", __LINE__);
}

void MuteGameMusic() {
    if (!g_muteGameMusicDuringOStim.load()) {
        return;
    }
    
    if (g_gameMusicMuted) {
        return;
    }
    
    try {
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (settings) {
            auto* musicVol = settings->GetSetting("fAudioMusicVolMult");
            if (!musicVol) {
                // Fallback para algunas versiones
                musicVol = settings->GetSetting("fMusicVolume");
            }

            if (musicVol) {
                g_originalMusicVolume = musicVol->GetFloat();
                musicVol->data.f = 0.0f; // MUTE
                g_gameMusicMuted = true;
                
                WriteToSoundPlayerLog("Skyrim music muted via GameSettingCollection (original: " + 
                    std::to_string(static_cast<int>(g_originalMusicVolume * 100)) + "%)", __LINE__);
                logger::info("Game music muted, original saved: {}", g_originalMusicVolume);
            } else {
                WriteToSoundPlayerLog("WARNING: Could not find music volume setting", __LINE__);
            }
        }
    } catch (const std::exception& e) {
        WriteToSoundPlayerLog("ERROR: Failed to mute game music: " + std::string(e.what()), __LINE__);
    } catch (...) {
        WriteToSoundPlayerLog("ERROR: Unknown error muting game music", __LINE__);
    }
}

void RestoreGameMusic() {
    if (!g_gameMusicMuted) {
        return;
    }
    
    try {
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (settings) {
            auto* musicVol = settings->GetSetting("fAudioMusicVolMult");
            if (!musicVol) {
                musicVol = settings->GetSetting("fMusicVolume");
            }

            if (musicVol) {
                musicVol->data.f = g_originalMusicVolume; // RESTORE
                g_gameMusicMuted = false;
                
                WriteToSoundPlayerLog("Skyrim music restored via GameSettingCollection (volume: " + 
                    std::to_string(static_cast<int>(g_originalMusicVolume * 100)) + "%)", __LINE__);
                logger::info("Game music restored to: {}", g_originalMusicVolume);
            }
        }
    } catch (const std::exception& e) {
        WriteToSoundPlayerLog("ERROR: Failed to restore game music: " + std::string(e.what()), __LINE__);
    } catch (...) {
        WriteToSoundPlayerLog("ERROR: Unknown error restoring game music", __LINE__);
    }
}

class GameEventProcessor : public RE::BSTEventSink<RE::TESActivateEvent>,
                           public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                           public RE::BSTEventSink<RE::TESCombatEvent>,
                           public RE::BSTEventSink<RE::TESContainerChangedEvent>,
                           public RE::BSTEventSink<RE::TESEquipEvent>,
                           public RE::BSTEventSink<RE::TESFurnitureEvent>,
                           public RE::BSTEventSink<RE::TESHitEvent>,
                           public RE::BSTEventSink<RE::TESQuestStageEvent>,
                           public RE::BSTEventSink<RE::TESSleepStartEvent>,
                           public RE::BSTEventSink<RE::TESSleepStopEvent>,
                           public RE::BSTEventSink<RE::TESWaitStopEvent> {
    GameEventProcessor() = default;
    ~GameEventProcessor() = default;
    GameEventProcessor(const GameEventProcessor&) = delete;
    GameEventProcessor(GameEventProcessor&&) = delete;
    GameEventProcessor& operator=(const GameEventProcessor&) = delete;
    GameEventProcessor& operator=(GameEventProcessor&&) = delete;

public:
    static GameEventProcessor& GetSingleton() {
        static GameEventProcessor singleton;
        return singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* event,
                                           RE::BSTEventSource<RE::TESActivateEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                           RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (event) {
            std::stringstream msg;
            msg << "Menu " << event->menuName.c_str() << " " << (event->opening ? "opened" : "closed");
            WriteToActionsLog(msg.str(), __LINE__);

            std::string menuName = event->menuName.c_str();

            auto menuSoundIt = g_animationSoundMap.find(menuName);
            if (menuSoundIt != g_animationSoundMap.end()) {
                if (event->opening) {
                    PlayMenuSound(menuName);
                } else {
                    StopMenuSound(menuName);
                }
            }

            if (menuName == "Journal Menu" || menuName == "TweenMenu" || menuName == "Console" ||
                menuName == "FavoritesMenu") {
                if (event->opening) {
                    PauseAllSounds();
                } else {
                    ResumeAllSounds();
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* event,
                                           RE::BSTEventSource<RE::TESCombatEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* event,
                                           RE::BSTEventSource<RE::TESContainerChangedEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,
                                           RE::BSTEventSource<RE::TESEquipEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESFurnitureEvent* event,
                                           RE::BSTEventSource<RE::TESFurnitureEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* event,
                                           RE::BSTEventSource<RE::TESQuestStageEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStartEvent* event,
                                           RE::BSTEventSource<RE::TESSleepStartEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent* event,
                                           RE::BSTEventSource<RE::TESSleepStopEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStopEvent* event,
                                           RE::BSTEventSource<RE::TESWaitStopEvent>*) override {
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

void ProcessOStimLog() {
    try {
        if (g_isShuttingDown.load()) {
            return;
        }

        if (g_pauseMonitoring.load()) {
            return;
        }

        if (!g_initialDelayComplete) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsedSeconds =
                std::chrono::duration_cast<std::chrono::seconds>(currentTime - g_monitoringStartTime).count();
        
            if (elapsedSeconds < 5) {
                return;
            } else {
                g_initialDelayComplete = true;
                WriteToSoundPlayerLog("5-second initial delay complete, starting OStim.log monitoring", __LINE__);
            }
        }

        auto paths = GetAllSKSELogsPaths();
        
        fs::path ostimLogPath = paths.primary / "OStim.log";
        
        if (!fs::exists(ostimLogPath)) {
            ostimLogPath = paths.secondary / "OStim.log";
            
            if (!fs::exists(ostimLogPath)) {
                return;
            }
            
            static bool loggedSecondary = false;
            if (!loggedSecondary) {
                logger::info("OStim.log found in SECONDARY path: {}", ostimLogPath.string());
                WriteToSoundPlayerLog("Using SECONDARY OStim.log path: " + ostimLogPath.string(), __LINE__);
                loggedSecondary = true;
            }
        }

        size_t currentFileSize = fs::file_size(ostimLogPath);

        if (currentFileSize < g_lastFileSize) {
            g_lastOStimLogPosition = 0;
            g_processedLines.clear();
            g_lastAnimation = "";
            g_firstAnimationDetected = false;
            logger::info("OStim.log was truncated, resetting position");
            WriteToSoundPlayerLog("OStim.log reset detected - restarting monitoring", __LINE__);
        } else if (currentFileSize == g_lastFileSize && g_lastOStimLogPosition > 0) {
            return;
        }

        g_lastFileSize = currentFileSize;

        std::ifstream ostimLog(ostimLogPath, std::ios::in);
        if (!ostimLog.is_open()) {
            return;
        }

        if (g_lastOStimLogPosition == 0) {
            ostimLog.seekg(0, std::ios::beg);
        } else {
            ostimLog.seekg(g_lastOStimLogPosition);
        }

        std::string line;

        while (std::getline(ostimLog, line)) {
            if (line.find("[warning]") != std::string::npos) {
                continue;
            }

            size_t lineHash = std::hash<std::string>{}(line);
            std::string hashStr = std::to_string(lineHash);

            if (g_processedLines.find(hashStr) != g_processedLines.end()) {
                continue;
            }

            bool isThreadStop = false;

            if (line.find("[Thread.cpp:634] closing thread") != std::string::npos) {
                isThreadStop = true;
                WriteToSoundPlayerLog("DETECTED: OStim thread closing", __LINE__);
            } else if (line.find("[ThreadManager.cpp:174] trying to stop thread") != std::string::npos) {
                isThreadStop = true;
                WriteToSoundPlayerLog("DETECTED: OStim trying to stop thread", __LINE__);
            }

            if (isThreadStop) {
                g_processedLines.insert(hashStr);
                RestoreGameMusic();
                StopSoundMenuKey();
                StopAllSounds();
                g_firstAnimationDetected = false;
                continue;
            }

            bool isAnimationLine = false;
            std::string animationName;

            if (line.find("[info]") != std::string::npos &&
                line.find("[Thread.cpp:195] thread 0 changed to node") != std::string::npos) {
                size_t nodePos = line.find("changed to node ");
                if (nodePos != std::string::npos) {
                    size_t startPos = nodePos + 16;
                    if (startPos < line.length()) {
                        animationName = line.substr(startPos);
                        isAnimationLine = true;
                    }
                }
            } else if (line.find("[info]") != std::string::npos &&
                       line.find("[OStimMenu.h:48] UI_TransitionRequest") != std::string::npos) {
                size_t lastOpenBrace = line.rfind('{');
                size_t lastCloseBrace = line.rfind('}');

                if (lastOpenBrace != std::string::npos && lastCloseBrace != std::string::npos &&
                    lastCloseBrace > lastOpenBrace) {
                    animationName = line.substr(lastOpenBrace + 1, lastCloseBrace - lastOpenBrace - 1);
                    if (!animationName.empty()) {
                        isAnimationLine = true;
                    }
                }
            }

            if (isAnimationLine && !animationName.empty()) {
                animationName.erase(animationName.find_last_not_of(" \n\r\t") + 1);

                g_processedLines.insert(hashStr);

                if (animationName == g_lastAnimation) {
                    continue;
                }

                g_lastAnimation = animationName;

                if (g_processedLines.size() > 500) {
                    g_processedLines.clear();
                }

                std::string formattedAnimation = "{" + animationName + "}";
                WriteToSoundPlayerLog(formattedAnimation, __LINE__, true);

                if (!g_firstAnimationDetected) {
                    WriteToSoundPlayerLog("First animation detected, reloading sound mappings and regenerating scripts",
                                          __LINE__);
                    LoadSoundMappings();
                    g_firstAnimationDetected = true;
                    
                    MuteGameMusic();
                    StartSoundMenuKey();
                }

                CheckAndPlaySound(animationName);
            }
        }

        g_lastOStimLogPosition = ostimLog.tellg();

        if (ostimLog.eof()) {
            ostimLog.clear();
            ostimLog.seekg(0, std::ios::end);
            g_lastOStimLogPosition = ostimLog.tellg();
        }

        ostimLog.close();

    } catch (const std::exception& e) {
        logger::error("Error processing OStim.log: {}", e.what());
    } catch (...) {
        logger::error("Unknown error processing OStim.log");
    }
}

void MonitoringThreadFunction() {
    WriteToSoundPlayerLog("Monitoring thread started - Watching OStim.log for animations", __LINE__);

    auto paths = GetAllSKSELogsPaths();
    fs::path ostimLogPathPrimary = paths.primary / "OStim.log";
    fs::path ostimLogPathSecondary = paths.secondary / "OStim.log";
    
    WriteToSoundPlayerLog("Monitoring OStim.log with DUAL-PATH detection:", __LINE__);
    WriteToSoundPlayerLog("  PRIMARY: " + ostimLogPathPrimary.string(), __LINE__);
    WriteToSoundPlayerLog("  SECONDARY: " + ostimLogPathSecondary.string(), __LINE__);
    WriteToSoundPlayerLog("Waiting 5 seconds before starting OStim.log analysis...", __LINE__);

    g_monitoringStartTime = std::chrono::steady_clock::now();
    g_initialDelayComplete = false;

    while (g_monitoringActive && !g_isShuttingDown.load()) {
        g_monitorCycles++;
        ProcessOStimLog();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    logger::info("Monitoring thread stopped");
}

void StartMonitoringThread() {
    if (!g_monitoringActive) {
        g_monitoringActive = true;
        g_monitorCycles = 0;
        g_lastOStimLogPosition = 0;
        g_lastFileSize = 0;
        g_processedLines.clear();
        g_lastAnimation = "";
        g_firstAnimationDetected = false;
        g_initialDelayComplete = false;
        g_monitorThread = std::thread(MonitoringThreadFunction);

        WriteToSoundPlayerLog("MONITORING SYSTEM ACTIVATED WITH STATIC SCRIPT SYSTEM AND DUAL-PATH", __LINE__);
    }
}

void StopMonitoringThread() {
    if (g_monitoringActive) {
        g_monitoringActive = false;
        if (g_monitorThread.joinable()) {
            g_monitorThread.join();
        }

        StopAllSounds();

        logger::info("Monitoring system stopped");
    }
}

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
        logger::info("==============================================");
        logger::info("GAME PATH DETECTION - Enhanced Mode");
        logger::info("==============================================");

        logger::info("Priority 1: Environment Variables");
        std::string mo2Path = GetEnvVar("MO2_MODS_PATH");
        if (!mo2Path.empty()) {
            fs::path pluginPath = BuildPathCaseInsensitive(fs::path(mo2Path), {"Data", "SKSE", "Plugins"});
            if (IsValidPluginPath(pluginPath)) {
                logger::info("Found and validated game path via MO2_MODS_PATH: {}", mo2Path);
                return mo2Path;
            } else {
                logger::warn("MO2_MODS_PATH found but DLL validation failed");
            }
        }

        std::string vortexPath = GetEnvVar("VORTEX_MODS_PATH");
        if (!vortexPath.empty()) {
            fs::path pluginPath = BuildPathCaseInsensitive(fs::path(vortexPath), {"Data", "SKSE", "Plugins"});
            if (IsValidPluginPath(pluginPath)) {
                logger::info("Found and validated game path via VORTEX_MODS_PATH: {}", vortexPath);
                return vortexPath;
            } else {
                logger::warn("VORTEX_MODS_PATH found but DLL validation failed");
            }
        }

        std::string skyrimMods = GetEnvVar("SKYRIM_MODS_FOLDER");
        if (!skyrimMods.empty()) {
            fs::path pluginPath = BuildPathCaseInsensitive(fs::path(skyrimMods), {"Data", "SKSE", "Plugins"});
            if (IsValidPluginPath(pluginPath)) {
                logger::info("Found and validated game path via SKYRIM_MODS_FOLDER: {}", skyrimMods);
                return skyrimMods;
            } else {
                logger::warn("SKYRIM_MODS_FOLDER found but DLL validation failed");
            }
        }

        logger::info("Priority 2: Windows Registry");
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
                    if (!result.empty() && fs::exists(result)) {
                        fs::path pluginPath = BuildPathCaseInsensitive(fs::path(result), {"Data", "SKSE", "Plugins"});
                        if (IsValidPluginPath(pluginPath)) {
                            logger::info("Found and validated game path in registry: {}", result);
                            return result;
                        } else {
                            logger::warn("Registry path found but DLL validation failed: {}", result);
                        }
                    }
                }
                RegCloseKey(hKey);
            }
            pathSize = sizeof(pathBuffer);
        }

        logger::info("Priority 3: Common Installation Paths");
        std::vector<std::string> commonPaths = {
            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "C:\\Program Files\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "D:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "E:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "F:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition"};

        for (const auto& pathCandidate : commonPaths) {
            try {
                if (fs::exists(pathCandidate) && fs::is_directory(pathCandidate)) {
                    fs::path exePath = fs::path(pathCandidate) / "SkyrimSE.exe";
                    if (fs::exists(exePath)) {
                        fs::path pluginPath = BuildPathCaseInsensitive(fs::path(pathCandidate), {"Data", "SKSE", "Plugins"});
                        if (IsValidPluginPath(pluginPath)) {
                            logger::info("Found and validated game path via common paths: {}", pathCandidate);
                            return pathCandidate;
                        } else {
                            logger::warn("Common path found but DLL validation failed: {}", pathCandidate);
                        }
                    }
                }
            } catch (...) {
                continue;
            }
        }

        logger::info("Priority 4: Executable Directory (Universal Fallback)");
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
            fs::path fullPath(exePath);
            std::string gamePath = fullPath.parent_path().string();
            
            fs::path pluginPath = BuildPathCaseInsensitive(fs::path(gamePath), {"Data", "SKSE", "Plugins"});
            if (IsValidPluginPath(pluginPath)) {
                logger::info("Using executable directory as game path: {}", gamePath);
                return gamePath;
            } else {
                logger::warn("Executable directory found but DLL validation failed");
            }
        }

        logger::error("All game path detection methods failed");
        logger::info("==============================================");
        return "";
    } catch (const std::exception& e) {
        logger::error("Error in GetGamePath: {}", e.what());
        return "";
    }
}

bool LoadSoundMappings() {
    try {
        g_animationSoundMap.clear();

        logger::info("==============================================");
        logger::info("SOUND MAPPINGS DETECTION - Enhanced Mode");
        logger::info("==============================================");

        fs::path jsonPath;
        bool foundInStandardPath = false;

        logger::info("Priority 1: Standard Installation Path");
        fs::path standardPluginPath = BuildPathCaseInsensitive(
            fs::path(g_gamePath), 
            {"Data", "SKSE", "Plugins"}
        );

        if (IsValidPluginPath(standardPluginPath)) {
            logger::info("DLL validated in standard path: {}", standardPluginPath.string());
            
            if (FindFileWithFallback(standardPluginPath, "OSoundtracks-SA-Expansion-Sounds-NG.json", jsonPath)) {
                foundInStandardPath = true;
                logger::info("JSON found in standard path: {}", jsonPath.string());
            }
        } else {
            logger::warn("DLL validation failed for standard path");
        }

        if (!foundInStandardPath) {
            std::vector<fs::path> altStandardPaths = {
                BuildPathCaseInsensitive(fs::path(g_gamePath), {"Data"}),
                BuildPathCaseInsensitive(fs::path(g_gamePath), {"Data", "SKSE"})
            };

            for (const auto& altPath : altStandardPaths) {
                if (FindFileWithFallback(altPath, "OSoundtracks-SA-Expansion-Sounds-NG.json", jsonPath)) {
                    foundInStandardPath = true;
                    logger::info("JSON found in alternative standard path: {}", jsonPath.string());
                    break;
                }
            }
        }

        if (!foundInStandardPath) {
            logger::info("Priority 2: DLL Directory (Wabbajack/MO2/Portable Mode)");
            
            g_dllDirectory = GetDllDirectory();
            
            if (!g_dllDirectory.empty()) {
                logger::info("DLL directory detected: {}", g_dllDirectory.string());
                
                if (IsValidPluginPath(g_dllDirectory)) {
                    logger::info("DLL validation passed for DLL directory");
                    
                    if (FindFileWithFallback(g_dllDirectory, "OSoundtracks-SA-Expansion-Sounds-NG.json", jsonPath)) {
                        g_usingDllPath = true;
                        
                        g_scriptsDirectory = g_dllDirectory / "Key_OSoundtracks";
                        
                        fs::path soundPath1 = g_dllDirectory.parent_path().parent_path() / "sound" / "OSoundtracks";
                        fs::path soundPath2 = g_dllDirectory / "OSoundtracks_Sounds";
                        fs::path soundPath3 = BuildPathCaseInsensitive(
                            g_dllDirectory.parent_path().parent_path(), 
                            {"Data", "sound", "OSoundtracks"}
                        );
                        
                        fs::path foundSoundPath;
                        if (FindFileWithFallback(soundPath1.parent_path(), "OSoundtracks", foundSoundPath)) {
                            g_soundsDirectory = foundSoundPath;
                            logger::info("Found sounds folder (sibling structure): {}", g_soundsDirectory.string());
                        } else if (FindFileWithFallback(soundPath2.parent_path(), "OSoundtracks_Sounds", foundSoundPath)) {
                            g_soundsDirectory = foundSoundPath;
                            logger::info("Found sounds folder (DLL-relative): {}", g_soundsDirectory.string());
                        } else if (fs::exists(soundPath3) && fs::is_directory(soundPath3)) {
                            g_soundsDirectory = soundPath3;
                            logger::info("Found sounds folder (Data structure): {}", g_soundsDirectory.string());
                        } else {
                            g_soundsDirectory = soundPath1;
                            logger::warn("Sounds folder not found, assuming: {}", g_soundsDirectory.string());
                        }
                        
                        logger::info("==============================================");
                        logger::info("WABBAJACK/MO2 MODE ACTIVATED");
                        logger::info("==============================================");
                        logger::info("JSON path: {}", jsonPath.string());
                        logger::info("Scripts directory: {}", g_scriptsDirectory.string());
                        logger::info("Sounds directory: {}", g_soundsDirectory.string());
                        logger::info("==============================================");
                        
                        WriteToSoundPlayerLog("WABBAJACK/MO2 MODE ACTIVATED", __LINE__);
                        WriteToSoundPlayerLog("Using DLL-relative paths for all resources", __LINE__);
                        WriteToSoundPlayerLog("JSON: " + jsonPath.string(), __LINE__);
                        WriteToSoundPlayerLog("Scripts: " + g_scriptsDirectory.string(), __LINE__);
                        WriteToSoundPlayerLog("Sounds: " + g_soundsDirectory.string(), __LINE__);
                    } else {
                        logger::error("JSON not found in DLL directory: {}", g_dllDirectory.string());
                    }
                } else {
                    logger::warn("DLL validation failed for DLL directory");
                }
            } else {
                logger::error("Could not determine DLL directory");
            }
        } else {
            g_usingDllPath = false;
            g_scriptsDirectory = BuildPathCaseInsensitive(
                fs::path(g_gamePath), 
                {"Data", "SKSE", "Plugins", "Key_OSoundtracks"}
            );
            
            fs::path soundsPath;
            fs::path soundsBasePath = BuildPathCaseInsensitive(
                fs::path(g_gamePath), 
                {"Data", "sound"}
            );
            
            if (FindFileWithFallback(soundsBasePath, "OSoundtracks", soundsPath)) {
                g_soundsDirectory = soundsPath;
                logger::info("Found sounds folder (case-insensitive): {}", g_soundsDirectory.string());
            } else {
                g_soundsDirectory = soundsBasePath / "OSoundtracks";
                logger::info("Using assumed sounds folder: {}", g_soundsDirectory.string());
            }
            
            logger::info("Using STANDARD installation paths");
            logger::info("Scripts directory: {}", g_scriptsDirectory.string());
            logger::info("Sounds directory: {}", g_soundsDirectory.string());
        }

        if (!fs::exists(jsonPath)) {
            logger::error("JSON configuration file not found in any location!");
            WriteToSoundPlayerLog("ERROR: JSON not found in standard or DLL-relative paths", __LINE__);
            return false;
        }

        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            logger::error("Could not open JSON file");
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        size_t soundKeyPos = content.find("\"SoundKey\"");
        if (soundKeyPos == std::string::npos) {
            logger::error("'SoundKey' not found in JSON");
            return false;
        }

        size_t bracePos = content.find('{', soundKeyPos);
        if (bracePos == std::string::npos) {
            logger::error("Invalid JSON structure");
            return false;
        }

        size_t currentPos = bracePos + 1;

        while (currentPos < content.length()) {
            size_t quoteStart = content.find('"', currentPos);
            if (quoteStart == std::string::npos) break;

            size_t quoteEnd = content.find('"', quoteStart + 1);
            if (quoteEnd == std::string::npos) break;

            std::string animationName = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

            size_t arrayStart = content.find('[', quoteEnd);
            if (arrayStart == std::string::npos) break;

            size_t soundQuoteStart = content.find('"', arrayStart);
            if (soundQuoteStart == std::string::npos) break;

            size_t soundQuoteEnd = content.find('"', soundQuoteStart + 1);
            if (soundQuoteEnd == std::string::npos) break;

            std::string soundFile = content.substr(soundQuoteStart + 1, soundQuoteEnd - soundQuoteStart - 1);

            int repeatDelay = 0;
            size_t delayQuoteEnd = std::string::npos;

            size_t commaPos = content.find(',', soundQuoteEnd);
            size_t arrayEnd = content.find(']', soundQuoteEnd);

            if (commaPos != std::string::npos && arrayEnd != std::string::npos && commaPos < arrayEnd) {
                size_t delayQuoteStart = content.find('"', commaPos);

                if (delayQuoteStart != std::string::npos && delayQuoteStart < arrayEnd) {
                    delayQuoteEnd = content.find('"', delayQuoteStart + 1);

                    if (delayQuoteEnd != std::string::npos && delayQuoteEnd < arrayEnd) {
                        std::string delayStr = content.substr(delayQuoteStart + 1, delayQuoteEnd - delayQuoteStart - 1);
                        try {
                            repeatDelay = std::stoi(delayStr);
                            if (repeatDelay < 0) repeatDelay = 0;
                        } catch (...) {
                            logger::warn("Invalid delay '{}' for animation '{}', defaulting to 0", delayStr, animationName);
                            repeatDelay = 0;
                        }
                    }
                }
            }

            std::string listStr = "list-1";
            int listNumber = 1;
            
            size_t listQuoteStart = content.find('"', delayQuoteEnd != std::string::npos ? delayQuoteEnd : soundQuoteEnd);
            if (listQuoteStart != std::string::npos && listQuoteStart < arrayEnd) {
                size_t listQuoteEnd = content.find('"', listQuoteStart + 1);
                if (listQuoteEnd != std::string::npos && listQuoteEnd < arrayEnd) {
                    listStr = content.substr(listQuoteStart + 1, listQuoteEnd - listQuoteStart - 1);
                    if (listStr.find("list-") == 0) {
                        try {
                            listNumber = std::stoi(listStr.substr(5));
                        } catch (...) {
                            listNumber = 1;
                        }
                    }
                }
            }
            
            auto& config = g_animationSoundMap[animationName];
            config.soundOptions.push_back(SoundOption(soundFile, listNumber));
            config.repeatDelaySeconds = repeatDelay;

            logger::info("Loaded sound mapping: {} -> {} [delay: {}s, list: {}]", 
                        animationName, soundFile, repeatDelay, listNumber);

            if (arrayEnd == std::string::npos) break;

            currentPos = arrayEnd + 1;

            size_t nextComma = content.find(',', currentPos);
            size_t nextBrace = content.find('}', currentPos);

            if (nextBrace != std::string::npos && (nextComma == std::string::npos || nextBrace < nextComma)) {
                break;
            }

            currentPos = (nextComma != std::string::npos) ? nextComma + 1 : nextBrace;
        }

        size_t positionKeyPos = content.find("\"SoundPositionKey\"");
        if (positionKeyPos != std::string::npos) {
            size_t positionBracePos = content.find('{', positionKeyPos);
            if (positionBracePos != std::string::npos) {
                size_t currentPosPosition = positionBracePos + 1;
                
                while (currentPosPosition < content.length()) {
                    size_t quoteStart = content.find('"', currentPosPosition);
                    if (quoteStart == std::string::npos) break;
                    
                    size_t quoteEnd = content.find('"', quoteStart + 1);
                    if (quoteEnd == std::string::npos) break;
                    
                    std::string fragmentName = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                    
                    size_t arrayStart = content.find('[', quoteEnd);
                    if (arrayStart == std::string::npos) break;
                    
                    size_t soundQuoteStart = content.find('"', arrayStart);
                    if (soundQuoteStart == std::string::npos) break;
                    
                    size_t soundQuoteEnd = content.find('"', soundQuoteStart + 1);
                    if (soundQuoteEnd == std::string::npos) break;
                    
                    std::string soundFile = content.substr(soundQuoteStart + 1, soundQuoteEnd - soundQuoteStart - 1);
                    
                    auto& config = g_positionSoundMap[fragmentName];
                    config.soundOptions.push_back(SoundOption(soundFile, 1));
                    config.repeatDelaySeconds = 0;
                    
                    logger::info("Loaded position mapping: {} -> {}", fragmentName, soundFile);
                    
                    size_t arrayEnd = content.find(']', soundQuoteEnd);
                    if (arrayEnd == std::string::npos) break;
                    
                    currentPosPosition = arrayEnd + 1;
                    
                    size_t nextComma = content.find(',', currentPosPosition);
                    size_t nextBrace = content.find('}', currentPosPosition);
                    
                    if (nextBrace != std::string::npos && (nextComma == std::string::npos || nextBrace < nextComma)) {
                        break;
                    }
                    
                    currentPosPosition = (nextComma != std::string::npos) ? nextComma + 1 : nextBrace;
                }
            }
        }
        
        WriteToSoundPlayerLog("Loaded " + std::to_string(g_animationSoundMap.size()) + " sound mappings from JSON",
                              __LINE__);

        for (const auto& [anim, config] : g_animationSoundMap) {
            std::string delayInfo = (config.repeatDelaySeconds == 0) ? "loop" : std::to_string(config.repeatDelaySeconds) + "s delay";
            std::string soundList;
            for (size_t i = 0; i < config.soundOptions.size(); ++i) {
                soundList += config.soundOptions[i].soundFile;
                if (i < config.soundOptions.size() - 1) soundList += " | ";
            }
            WriteToSoundPlayerLog("  " + anim + " -> " + soundList + " [" + delayInfo + "]", __LINE__);
        }
        
        if (!g_positionSoundMap.empty()) {
            WriteToSoundPlayerLog("Loaded " + std::to_string(g_positionSoundMap.size()) + " position fragment mappings", __LINE__);
            for (const auto& [fragment, config] : g_positionSoundMap) {
                std::string soundList;
                for (size_t i = 0; i < config.soundOptions.size(); ++i) {
                    soundList += config.soundOptions[i].soundFile;
                    if (i < config.soundOptions.size() - 1) soundList += " | ";
                }
                WriteToSoundPlayerLog("  Fragment '" + fragment + "' -> " + soundList, __LINE__);
            }
        }

        size_t soundMenuKeyPos = content.find("\"SoundMenuKey\"");
        if (soundMenuKeyPos != std::string::npos) {
            size_t menuBracePos = content.find('{', soundMenuKeyPos);
            if (menuBracePos != std::string::npos) {
                size_t currentPosMenu = menuBracePos + 1;
                int braceDepth = 1;
                
                while (currentPosMenu < content.length() && braceDepth > 0) {
                    size_t authorQuoteStart = content.find('"', currentPosMenu);
                    if (authorQuoteStart == std::string::npos) break;
                    
                    size_t authorQuoteEnd = content.find('"', authorQuoteStart + 1);
                    if (authorQuoteEnd == std::string::npos) break;
                    
                    std::string authorName = content.substr(authorQuoteStart + 1, authorQuoteEnd - authorQuoteStart - 1);
                    
                    if (authorName == "SoundMenuKey" || authorName.empty()) {
                        currentPosMenu = authorQuoteEnd + 1;
                        continue;
                    }
                    
                    size_t authorArrayStart = content.find('[', authorQuoteEnd);
                    if (authorArrayStart == std::string::npos) break;
                    
                    size_t authorArrayEnd = authorArrayStart + 1;
                    int bracketDepth = 1;
                    while (authorArrayEnd < content.length() && bracketDepth > 0) {
                        if (content[authorArrayEnd] == '[') bracketDepth++;
                        else if (content[authorArrayEnd] == ']') bracketDepth--;
                        authorArrayEnd++;
                    }
                    
                    std::string authorArrayContent = content.substr(authorArrayStart, authorArrayEnd - authorArrayStart);
                    
                    std::vector<SoundOption> authorSongs;
                    size_t songArrayPos = 0;
                    while ((songArrayPos = authorArrayContent.find('[', songArrayPos)) != std::string::npos) {
                        size_t songArrayEnd = authorArrayContent.find(']', songArrayPos);
                        if (songArrayEnd == std::string::npos) break;
                        
                        std::string songEntry = authorArrayContent.substr(songArrayPos + 1, songArrayEnd - songArrayPos - 1);
                        
                        size_t firstQuote = songEntry.find('"');
                        if (firstQuote != std::string::npos) {
                            size_t secondQuote = songEntry.find('"', firstQuote + 1);
                            if (secondQuote != std::string::npos) {
                                std::string soundFile = songEntry.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                                if (!soundFile.empty() && soundFile.find("list-") == std::string::npos) {
                                    authorSongs.push_back(SoundOption(soundFile, 1));
                                }
                            }
                        }
                        
                        songArrayPos = songArrayEnd + 1;
                    }
                    
                    if (!authorSongs.empty()) {
                        g_soundMenuKeyMap[authorName] = authorSongs;
                        logger::info("Loaded SoundMenuKey author: {} with {} songs", authorName, authorSongs.size());
                    }
                    
                    currentPosMenu = authorArrayEnd;
                    
                    size_t nextComma = content.find(',', currentPosMenu);
                    size_t nextBrace = content.find('}', currentPosMenu);
                    
                    if (nextBrace != std::string::npos && (nextComma == std::string::npos || nextBrace < nextComma)) {
                        break;
                    }
                    
                    currentPosMenu = (nextComma != std::string::npos) ? nextComma + 1 : content.length();
                }
            }
            
            WriteToSoundPlayerLog("Loaded " + std::to_string(g_soundMenuKeyMap.size()) + " SoundMenuKey authors", __LINE__);
            for (const auto& [author, songs] : g_soundMenuKeyMap) {
                std::string songList;
                for (size_t i = 0; i < songs.size(); ++i) {
                    songList += songs[i].soundFile;
                    if (i < songs.size() - 1) songList += " | ";
                }
                WriteToSoundPlayerLog("  Author '" + author + "' -> " + songList, __LINE__);
            }
        }

        if (!g_animationSoundMap.empty()) {
            WriteToSoundPlayerLog("Sound mappings loaded successfully for BASS Audio", __LINE__);
        }

        return !g_animationSoundMap.empty();

    } catch (const std::exception& e) {
        logger::error("Error loading sound mappings: {}", e.what());
        return false;
    }
}

void PlayStartupSound() {
    if (!g_startupSoundEnabled.load()) {
        logger::info("Startup sound is disabled in INI");
        WriteToSoundPlayerLog("Startup sound is disabled in INI", __LINE__);
        return;
    }

    auto startIt = g_animationSoundMap.find("Start");
    if (startIt != g_animationSoundMap.end() && !startIt->second.soundOptions.empty()) {
        std::string soundFile = startIt->second.soundOptions[0].soundFile;
        WriteToSoundPlayerLog("PLAYING STARTUP SOUND (BASS): " + soundFile, __LINE__);
        
        if (!g_bassInitialized) {
            if (!InitializeBASSLibrary()) {
                WriteToSoundPlayerLog("BASS: Failed to initialize for startup sound", __LINE__);
                return;
            }
        }
        
        fs::path soundPath = FindSoundFile(soundFile);
        if (soundPath.empty()) {
            WriteToSoundPlayerLog("Startup sound file not found: " + soundFile, __LINE__);
            return;
        }
        
        HSTREAM tempStream = pBASS_StreamCreateFile(FALSE, soundPath.wstring().c_str(), 0, 0, BASS_UNICODE);
        if (tempStream) {
            float volume = g_volumeControlEnabled.load() ? g_menuVolume.load() : 1.0f;
            if (pBASS_ChannelSetAttribute) {
                pBASS_ChannelSetAttribute(tempStream, BASS_ATTRIB_VOL, volume);
            }
            pBASS_ChannelPlay(tempStream, FALSE);
            
            std::thread([tempStream]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                while (pBASS_ChannelIsActive && pBASS_ChannelIsActive(tempStream) == BASS_ACTIVE_PLAYING) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (pBASS_StreamFree) {
                    pBASS_StreamFree(tempStream);
                }
                WriteToSoundPlayerLog("BASS: Startup sound stream freed", __LINE__);
            }).detach();
            
            std::string displayName = soundFile;
            if (displayName.size() >= 4 && displayName.substr(displayName.size() - 4) == ".wav") {
                displayName = displayName.substr(0, displayName.size() - 4);
            }
            ShowGameNotification("OSoundtracks - \"" + displayName + "\" is played");
        }
    } else {
        logger::info("No startup sound found in JSON mappings for 'Start'");
        WriteToSoundPlayerLog("No startup sound found in JSON mappings for 'Start'", __LINE__);
    }
}

void PlaySound(const std::string& soundFileName, bool waitForCompletion) {
    try {
        fs::path soundPath = g_soundsDirectory / soundFileName;
        WriteToSoundPlayerLog("Playing sound: " + soundFileName, __LINE__);

        if (!fs::exists(soundPath)) {
            logger::error("Sound file not found: {}", soundPath.string());
            WriteToSoundPlayerLog("ERROR: Sound file not found: " + soundPath.string(), __LINE__);
            return;
        }

        std::string command = "powershell.exe -WindowStyle Hidden -Command \"(New-Object Media.SoundPlayer '" +
                              soundPath.string() + "').PlaySync()\"";

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        ZeroMemory(&pi, sizeof(pi));

        if (CreateProcessA(NULL, (LPSTR)command.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            if (waitForCompletion) {
                WaitForSingleObject(pi.hProcess, INFINITE);
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            logger::error("Error executing sound command. Code: {}", GetLastError());
        }

    } catch (const std::exception& e) {
        logger::error("Error in PlaySound: {}", e.what());
    }
}

void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) {
        SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
        return;
    }
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::info);
}

void InitializePlugin() {
    try {
        g_documentsPath = GetDocumentsPath();
        g_gamePath = GetGamePath();

        if (g_gamePath.empty()) {
            logger::error("Could not determine game path! Plugin may not work correctly.");
            g_gamePath = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition";
        }

        g_iniPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "OSoundtracks-SA-Expansion-Sounds-NG.ini";

        auto paths = GetAllSKSELogsPaths();
        if (!paths.primary.empty()) {
            std::vector<fs::path> logFolders = { paths.primary, paths.secondary };
            
            for (const auto& folder : logFolders) {
                try {
                    auto soundPlayerLogPath = folder / "OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.log";
                    std::ofstream clearLog(soundPlayerLogPath, std::ios::trunc);
                    clearLog.close();

                    auto heartbeatLogPath = folder / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log";
                    std::ofstream clearHeartbeat(heartbeatLogPath, std::ios::trunc);
                    clearHeartbeat.close();

                    auto actionsLogPath = folder / "OSoundtracks-SA-Expansion-Sounds-NG-Actions.log";
                    std::ofstream clearActions(actionsLogPath, std::ios::trunc);
                    clearActions.close();
                } catch (...) {
                }
            }

            fs::path ostimLogPath = paths.primary / "OStim.log";
            bool foundInPrimary = fs::exists(ostimLogPath);
            
            if (foundInPrimary) {
                logger::info("OStim.log found in PRIMARY path: {}", ostimLogPath.string());
                WriteToSoundPlayerLog("OStim.log found in PRIMARY SKSE logs folder", __LINE__);
                auto fileSize = fs::file_size(ostimLogPath);
                logger::info("OStim.log size: {} bytes", fileSize);
            } else {
                ostimLogPath = paths.secondary / "OStim.log";
                if (fs::exists(ostimLogPath)) {
                    logger::info("OStim.log found in SECONDARY path: {}", ostimLogPath.string());
                    WriteToSoundPlayerLog("OStim.log found in SECONDARY SKSE logs folder", __LINE__);
                    auto fileSize = fs::file_size(ostimLogPath);
                    logger::info("OStim.log size: {} bytes", fileSize);
                } else {
                    logger::warn("OStim.log not found in either PRIMARY or SECONDARY locations");
                    WriteToSoundPlayerLog("OStim.log not found - waiting for OStim to create it", __LINE__);
                }
            }
        }

        WriteToSoundPlayerLog("OSoundtracks Plugin with BASS Audio Library, DUAL-PATH and Wabbajack/MO2 Support - Starting...", __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("OSoundtracks Plugin - v16.3.0 BASS", __LINE__);
        WriteToSoundPlayerLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("Documents: " + g_documentsPath, __LINE__);
        WriteToSoundPlayerLog("Game Path (initial): " + g_gamePath, __LINE__);
        WriteToSoundPlayerLog("FEATURES: BASS Audio Library for native sound playback", __LINE__);
        WriteToSoundPlayerLog("DUAL-PATH SYSTEM: PRIMARY + SECONDARY log locations", __LINE__);
        WriteToSoundPlayerLog("WABBAJACK/MO2 SUPPORT: Enhanced path detection with DLL validation", __LINE__);
        WriteToSoundPlayerLog("FORMATS: WAV, MP3, OGG supported via BASS", __LINE__);
        WriteToSoundPlayerLog("CHANNELS: 6 simultaneous streams (Base, Menu, Specific, Effect, Position, Tag)", __LINE__);
        WriteToSoundPlayerLog("NEW: Random sound selection with list-1, list-2, etc.", __LINE__);
        WriteToSoundPlayerLog("NEW: Position-based sounds (fragment matching)", __LINE__);
        WriteToSoundPlayerLog("NEW: Individual volume control for all 6 channels", __LINE__);
        WriteToSoundPlayerLog("NEW: Author preview system (3s sample on INI change)", __LINE__);
        WriteToSoundPlayerLog("NEW: Automatic INI backup to OSoundtracks_MCM_Backup", __LINE__);

        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("OSoundtracks Actions Monitor - v16.3.0 BASS", __LINE__);
        WriteToActionsLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("Monitoring game events: Menu.", __LINE__);
        WriteToActionsLog("", __LINE__);

        if (LoadSoundMappings()) {
            if (g_usingDllPath) {
                g_iniPath = g_dllDirectory / "OSoundtracks-SA-Expansion-Sounds-NG.ini";
                logger::info("Updated INI path to DLL-relative: {}", g_iniPath.string());
                WriteToSoundPlayerLog("INI Path (Wabbajack mode): " + g_iniPath.string(), __LINE__);
            } else {
                fs::path iniPathFound;
                fs::path iniBasePath = BuildPathCaseInsensitive(fs::path(g_gamePath), {"Data", "SKSE", "Plugins"});
                if (FindFileWithFallback(iniBasePath, "OSoundtracks-SA-Expansion-Sounds-NG.ini", iniPathFound)) {
                    g_iniPath = iniPathFound;
                    logger::info("Found INI with case-insensitive search: {}", g_iniPath.string());
                }
                WriteToSoundPlayerLog("INI Path (Standard mode): " + g_iniPath.string(), __LINE__);
            }
            
            LoadIniSettings();
            ProcessBackupUpdate();
            StartIniMonitoring();
            
            g_isInitialized = true;
            WriteToSoundPlayerLog("PLUGIN INITIALIZED WITH BASS AUDIO SYSTEM", __LINE__);
            WriteToSoundPlayerLog("Note: BASS streams will be created when animations are detected", __LINE__);
        } else {
            logger::error("Failed to load sound mappings from JSON");
            WriteToSoundPlayerLog("ERROR: Failed to load sound mappings from JSON", __LINE__);
            g_isInitialized = true;
        }

        WriteToSoundPlayerLog("PLUGIN FULLY ACTIVE WITH BASS AUDIO", __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("Starting OStim animation monitoring...", __LINE__);
        WriteToSoundPlayerLog("BASS streams: Base, Menu, Specific (volume-controlled)", __LINE__);
        if (g_usingDllPath) {
            WriteToSoundPlayerLog("WABBAJACK/MO2 MODE CONFIRMED - All paths are DLL-relative", __LINE__);
        }

        StartHeartbeatThread();
        StartMonitoringThread();

    } catch (const std::exception& e) {
        logger::error("CRITICAL ERROR in Initialize: {}", e.what());
    }
}

void TestPlaySound() {
    logger::info("MANUAL SOUND TEST WITH BASS AUDIO SYSTEM");
    PlayStartupSound();
}

void ShutdownPlugin() {
    logger::info("PLUGIN SHUTTING DOWN");
    WriteToSoundPlayerLog("PLUGIN SHUTTING DOWN", __LINE__);
    WriteToActionsLog("PLUGIN SHUTTING DOWN", __LINE__);

    g_isShuttingDown = true;

    if (g_previewPlaying.load()) {
        StopAuthorPreview();
    }
    
    if (g_previewTimerActive.load()) {
        g_previewTimerActive = false;
    }

    StopAllSounds();
    StopMonitoringThread();
    StopIniMonitoring();
    StopHeartbeatThread();

    if (g_soundPlayerLog.is_open()) {
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("Plugin shutdown complete at: " + GetCurrentTimeString(), __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        g_soundPlayerLog.close();
    }

    logger::info("Plugin shutdown complete");
}

void MessageListener(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kNewGame:
            logger::info("kNewGame: New game started - resetting system");
            StopMonitoringThread();
            StopHeartbeatThread();
            g_lastOStimLogPosition = 0;
            g_lastFileSize = 0;
            g_processedLines.clear();
            g_lastAnimation = "";
            g_currentBaseAnimation = "";
            g_currentSpecificAnimation = "";
            g_firstAnimationDetected = false;
            g_initialDelayComplete = false;
            g_scriptsInitialized = false;

            g_activationMessageShown = false;
            g_pauseMonitoring = false;

            WriteToSoundPlayerLog("NEW GAME: All flags reset, ready for fresh initialization", __LINE__);
            InitializePlugin();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            logger::info("kPostLoadGame: Game loaded - checking monitoring");
            if (!g_monitoringActive) {
                StartMonitoringThread();
            }
            if (!g_heartbeatActive.load()) {
                StartHeartbeatThread();
            }
            if (!g_monitoringIni.load()) {
                StartIniMonitoring();
            }
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("kDataLoaded: Game fully loaded");

            {
                auto& eventProcessor = GameEventProcessor::GetSingleton();

                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESActivateEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESCombatEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESContainerChangedEvent>(
                    &eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESEquipEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESFurnitureEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESHitEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESQuestStageEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSleepStartEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSleepStopEvent>(&eventProcessor);
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESWaitStopEvent>(&eventProcessor);

                RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&eventProcessor);

                logger::info("Game event processor registered for all events");
                WriteToSoundPlayerLog("Game event processor registered", __LINE__);
                WriteToActionsLog("Event monitoring system active", __LINE__);
            }

            logger::info("Running initial sound test...");
            TestPlaySound();
            break;

        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SetupLog();

    logger::info("OSoundtracks Plugin v16.3.0 BASS - Starting...");

    InitializePlugin();

    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

    logger::info("Plugin loaded successfully with BASS Audio Library");

    return true;
}

constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({16, 3, 0});
    v.PluginName("OSoundtracks OStim Monitor");
    v.AuthorName("John95AC");
    v.UsesAddressLibrary();
    v.UsesSigScanning();
    v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST, SKSE::RUNTIME_LATEST_VR});

    return v;
}();