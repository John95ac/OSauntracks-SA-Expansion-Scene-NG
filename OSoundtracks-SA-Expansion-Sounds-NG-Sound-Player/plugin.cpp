#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <shlobj.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
namespace logger = SKSE::log;

// ===== SOUND CONFIGURATION STRUCTURE =====
struct SoundConfig {
    std::string soundFile;
    int repeatDelaySeconds;  // 0 = loop inmediato, >0 = segundos de espera muy util para sonidos cortos 

    SoundConfig() : soundFile(""), repeatDelaySeconds(0) {}
    SoundConfig(const std::string& file, int delay) : soundFile(file), repeatDelaySeconds(delay) {}
};

// ===== SCRIPT STATE STRUCTURE =====
enum ScriptType { SCRIPT_BASE, SCRIPT_SPECIFIC, SCRIPT_MENU, SCRIPT_CHECK };

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

// ===== GLOBAL VARIABLES =====
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

// Sound configuration map
static std::unordered_map<std::string, SoundConfig> g_animationSoundMap;

// Script states
static ScriptState g_baseScript;
static ScriptState g_menuScript;
static ScriptState g_specificScript;
static ScriptState g_checkScript;

static bool g_scriptsInitialized = false;
static bool g_firstAnimationDetected = false;
static std::chrono::steady_clock::time_point g_monitoringStartTime;
static bool g_initialDelayComplete = false;
static std::atomic<bool> g_isShuttingDown(false);

// NEW: Pause monitoring during script initialization
static std::atomic<bool> g_pauseMonitoring(false);

// NEW: Show activation message only once per game session
static bool g_activationMessageShown = false;

// Heartbeat monitoring
static std::thread g_heartbeatThread;
static std::atomic<bool> g_heartbeatActive(false);

// Current animation tracking
static std::string g_currentBaseAnimation = "";
static std::string g_currentSpecificAnimation = "";

// INI configuration variables
static std::atomic<int> g_soundVolume(50);
static std::atomic<bool> g_startupSoundEnabled(true);
static std::time_t g_lastIniCheckTime = 0;
static fs::path g_iniPath;
static std::thread g_iniMonitorThread;
static std::atomic<bool> g_monitoringIni(false);

// Pause system variables
static std::atomic<bool> g_soundsPaused(false);
static std::mutex g_pauseMutex;

// Track states before pause
static bool g_baseWasActiveBeforePause = false;
static bool g_menuWasActiveBeforePause = false;
static bool g_specificWasActiveBeforePause = false;

// Menu sound management
static std::unordered_map<std::string, std::string> g_activeMenuSounds;
static std::unordered_map<std::string, PROCESS_INFORMATION> g_menuSoundProcesses;
static std::mutex g_menuSoundMutex;

// Track classification
static std::vector<std::string> g_baseTracks;
static std::vector<std::string> g_specificTracks;
static std::vector<std::string> g_menuTracks;

// Track throttling system - prevents immediate replay
static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_lastPlayTime;
static std::mutex g_throttleMutex;

// ===== FORWARD DECLARATIONS =====
void CheckAndPlaySound(const std::string& animationName);
void PlaySound(const std::string& soundFileName, bool waitForCompletion = true);
void StartMonitoringThread();
void StopMonitoringThread();
bool LoadSoundMappings();
std::string GetAnimationBase(const std::string& animationName);
bool LoadIniSettings();
void MonitorIniFile();
void StartIniMonitoring();
void StopIniMonitoring();
void PlayStartupSound();
void StopAllSounds();
void StartHeartbeatThread();
void StopHeartbeatThread();
void WriteHeartbeat();
fs::path GetHeartbeatLogPath();
void WriteToActionsLog(const std::string& message, int lineNumber = 0);
void PauseAllSounds();
void ResumeAllSounds();
void PlayMenuSound(const std::string& menuName);
void StopMenuSound(const std::string& menuName);
void StopAllMenuSounds();
void WriteToSoundPlayerLog(const std::string& message, int lineNumber = 0, bool isAnimationEntry = false);

// NEW DECLARATIONS FOR STATIC SCRIPT SYSTEM
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

// ===== GET TRACK DIRECTORY PATH =====
std::string GetTrackDirectory() {
    fs::path trackDir = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks" / "Track";
    
    // Crear directorio si no existe
    try {
        fs::create_directories(trackDir);
    } catch (const std::exception& e) {
        logger::error("Error creating Track directory: {}", e.what());
    }
    
    // Convertir a string y normalizar barras invertidas para Windows
    std::string trackDirStr = trackDir.string();
    std::replace(trackDirStr.begin(), trackDirStr.end(), '/', '\\');
    
    return trackDirStr;
}

// ===== HELPER FUNCTION: FORCE KILL PROCESS =====
void ForceKillProcess(PROCESS_INFORMATION& pi) {
    if (pi.hProcess != 0 && pi.hProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 100);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    ZeroMemory(&pi, sizeof(pi));
}

// ===== UTILITY FUNCTIONS =====
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

// ===== TIME FUNCTIONS =====
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

// ===== HEARTBEAT LOG PATH =====
fs::path GetHeartbeatLogPath() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) return fs::path();
    return *logsFolder / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log";
}

// ===== LOG WRITERS =====
void WriteToSoundPlayerLog(const std::string& message, int lineNumber, bool isAnimationEntry) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) return;

    auto logPath = *logsFolder / "OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.log";

    if (!g_soundPlayerLog.is_open()) {
        g_soundPlayerLog.open(logPath, std::ios::app);
    }

    if (g_soundPlayerLog.is_open()) {
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

        g_soundPlayerLog << ss.str() << std::endl;
        g_soundPlayerLog.flush();
    }
}

void WriteToActionsLog(const std::string& message, int lineNumber) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) return;

    auto logPath = *logsFolder / "OSoundtracks-SA-Expansion-Sounds-NG-Actions.log";

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

    std::ofstream actionsFile(logPath, std::ios::trunc);
    if (actionsFile.is_open()) {
        for (const auto& line : g_actionLines) {
            actionsFile << line << std::endl;
        }
        actionsFile.close();
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

    auto heartbeatPath = GetHeartbeatLogPath();
    std::ofstream heartbeatFile(heartbeatPath, std::ios::trunc);

    if (heartbeatFile.is_open()) {
        for (const auto& line : g_heartbeatLines) {
            heartbeatFile << line << std::endl;
        }
        heartbeatFile.close();
    }
}

// ===== HEARTBEAT THREAD FUNCTION =====
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

// ===== SUSPEND/RESUME PROCESS FUNCTIONS =====
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

// ===== CLEAN STOP FILES =====
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

// ===== SEND DEBUG SOUND BEFORE FREEZE =====
void SendDebugSoundBeforeFreeze(ScriptType type, ScriptState& scriptState) {
    if (!scriptState.isRunning) {
        return;
    }
    
    WriteToSoundPlayerLog("Sending Debug.wav to " + scriptState.name + " before freeze (500ms active playback)", __LINE__);
    
    // Si estaba pausado/frozen, despertar temporalmente
    bool wasFrozen = scriptState.isPaused;
    if (wasFrozen) {
        ResumeProcess(scriptState.processInfo.hProcess);
        scriptState.isPaused = false;
        WriteToSoundPlayerLog("Script " + scriptState.name + " temporarily unfrozen for Debug.wav", __LINE__);
    }
    
    // Enviar comando Debug.wav
    SendTrackCommand(type, "Debug.wav");
    
    // CRUCIAL: Esperar 500ms para que Debug.wav se reproduzca activamente
    // Esto limpia completamente el buffer de audio con silencio
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Actualizar el currentTrack
    scriptState.currentTrack = "Debug.wav";
    
    WriteToSoundPlayerLog("Debug.wav 500ms playback completed for " + scriptState.name, __LINE__);
}
// ===== ANIMATION CLASSIFICATION FUNCTIONS =====
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
        if (IsMenuSound(animName)) {
            g_menuTracks.push_back(config.soundFile);
        } else if (IsSpecificSound(animName)) {
            g_specificTracks.push_back(config.soundFile);
        } else {
            g_baseTracks.push_back(config.soundFile);
        }
    }

    WriteToSoundPlayerLog("Classified animations - Base: " + std::to_string(g_baseTracks.size()) +
                              ", Specific: " + std::to_string(g_specificTracks.size()) +
                              ", Menu: " + std::to_string(g_menuTracks.size()),
                          __LINE__);
}

// ===== CLEAN OLD SCRIPTS =====
void CleanOldScripts() {
    fs::path scriptsPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks";

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

// ===== GENERATE BASE SCRIPT =====
void GenerateBaseScript() {
    fs::path scriptsPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks";
    fs::path scriptFile = scriptsPath / "OSoundtracks_Base.ps1";

    auto heartbeatPath = GetHeartbeatLogPath().string();
    std::string soundsFolder = (fs::path(g_gamePath) / "Data" / "sound" / "OSoundtracks").string();

    std::ofstream script(scriptFile);
    if (!script.is_open()) {
        logger::error("Failed to create Base script");
        return;
    }

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Base.ps1\n";
    script << "# Base animation sounds handler\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
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

    script << "# Function to play track (FIXED AUDIO BLEED)\n";
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

    script << "# Play initial track (Debug.wav by default)\n";
    script << "Play-Track -index 0\n\n";

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
    g_baseScript.stopFilePath = trackDir + "\\stop_OSoundtracks_Base.tmp";
    g_baseScript.trackFilePath = trackDir + "\\track_OSoundtracks_Base.tmp";

    WriteToSoundPlayerLog("Generated OSoundtracks_Base.ps1", __LINE__);
}

// ===== GENERATE SPECIFIC SCRIPT =====
void GenerateSpecificScript() {
    fs::path scriptsPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks";
    fs::path scriptFile = scriptsPath / "OSoundtracks_Specific.ps1";

    auto heartbeatPath = GetHeartbeatLogPath().string();
    std::string soundsFolder = (fs::path(g_gamePath) / "Data" / "sound" / "OSoundtracks").string();

    std::ofstream script(scriptFile);
    if (!script.is_open()) {
        logger::error("Failed to create Specific script");
        return;
    }

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Specific.ps1\n";
    script << "# Specific animation sounds handler\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
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

    script << "# Function to play track (FIXED AUDIO BLEED)\n";
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

    script << "# Play initial track (Debug.wav by default)\n";
    script << "Play-Track -index 0\n\n";

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
    g_specificScript.stopFilePath = trackDir + "\\stop_OSoundtracks_Specific.tmp";
    g_specificScript.trackFilePath = trackDir + "\\track_OSoundtracks_Specific.tmp";

    WriteToSoundPlayerLog("Generated OSoundtracks_Specific.ps1", __LINE__);
}
// ===== GENERATE MENU SCRIPT =====
void GenerateMenuScript() {
    fs::path scriptsPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks";
    fs::path scriptFile = scriptsPath / "OSoundtracks_Menu.ps1";

    auto heartbeatPath = GetHeartbeatLogPath().string();
    std::string soundsFolder = (fs::path(g_gamePath) / "Data" / "sound" / "OSoundtracks").string();

    std::ofstream script(scriptFile);
    if (!script.is_open()) {
        logger::error("Failed to create Menu script");
        return;
    }

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Menu.ps1\n";
    script << "# Menu sounds handler (Start + OStimAlignMenu)\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
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

    script << "# Function to play track (FIXED AUDIO BLEED)\n";
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

    script << "# Play initial track (Debug.wav by default)\n";
    script << "Play-Track -index 0\n\n";

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
    g_menuScript.stopFilePath = trackDir + "\\stop_OSoundtracks_Menu.tmp";
    g_menuScript.trackFilePath = trackDir + "\\track_OSoundtracks_Menu.tmp";

    WriteToSoundPlayerLog("Generated OSoundtracks_Menu.ps1", __LINE__);
}

// ===== GENERATE CHECK SCRIPT (WATCHDOG) =====
void GenerateCheckScript() {
    fs::path scriptsPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks";
    fs::path scriptFile = scriptsPath / "OSoundtracks_Check.ps1";

    auto heartbeatPath = GetHeartbeatLogPath().string();

    std::ofstream script(scriptFile);
    if (!script.is_open()) {
        logger::error("Failed to create Check script");
        return;
    }

    script << "# ===================================================================\n";
    script << "# OSoundtracks_Check.ps1\n";
    script << "# Watchdog to monitor heartbeat log updates\n";
    script << "# Kills sibling scripts if game crashes\n";
    script << "# Generated: " << GetCurrentTimeString() << "\n";
    script << "# ===================================================================\n\n";

    script << "$logPath = '" << heartbeatPath << "'\n";
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

    script << "WriteLog 'Watchdog started, monitoring heartbeat log'\n\n";

    script << "# Wait for log file to exist\n";
    script << "if(-not (Test-Path $logPath)) {\n";
    script << "    WriteLog 'Heartbeat log not found, waiting up to 4s...'\n";
    script << "    $waited = 0\n";
    script << "    while(-not (Test-Path $logPath) -and $waited -lt 4) {\n";
    script << "        Start-Sleep -Seconds 1\n";
    script << "        $waited++\n";
    script << "    }\n";
    script << "    if(-not (Test-Path $logPath)) {\n";
    script << "        WriteLog 'Timeout waiting for heartbeat log. Exiting...'\n";
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

    WriteToSoundPlayerLog("Generated OSoundtracks_Check.ps1 (Watchdog)", __LINE__);
}

// ===== GENERATE ALL STATIC SCRIPTS =====
void GenerateStaticScripts() {
    fs::path scriptsPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "Key_OSoundtracks";

    fs::create_directories(scriptsPath);

    CleanOldScripts();

    WriteToSoundPlayerLog("GENERATING 4 STATIC POWERSHELL SCRIPTS...", __LINE__);
    WriteToSoundPlayerLog("Scripts path: " + scriptsPath.string(), __LINE__);

    ClassifyAnimations();

    GenerateBaseScript();
    GenerateSpecificScript();
    GenerateMenuScript();
    GenerateCheckScript();

    WriteToSoundPlayerLog("GENERATED 4 STATIC SCRIPTS SUCCESSFULLY", __LINE__);
    WriteToSoundPlayerLog("Scripts: Base, Specific, Menu, Check (Watchdog)", __LINE__);
    WriteToSoundPlayerLog("Audio bleed fix applied: Debug.wav on freeze", __LINE__);
}

// ===== LAUNCH POWERSHELL SCRIPT =====
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

// ===== START ALL SCRIPTS FROZEN =====
void StartAllScriptsFrozen() {
    if (g_scriptsInitialized) {
        logger::info("Scripts already initialized, skipping");
        return;
    }

    // ========================================
    // PASO 0: PAUSAR MONITOREO DE LOGS
    // ========================================
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("PAUSING LOG MONITORING DURING SCRIPT INITIALIZATION", __LINE__);
    WriteToSoundPlayerLog("Reason: Prevent false sound triggers during warmup", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);
    
    g_pauseMonitoring = true;  // PAUSAR monitoreo de OStim.log y eventos de juego
    
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("INITIALIZING STATIC SCRIPT SYSTEM WITH DEBUG.WAV WARMUP", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    CleanStopFiles();

    // ========================================
    // PASO 1: Lanzar Check (Watchdog)
    // ========================================
    WriteToSoundPlayerLog("Step 1/5: Launching Check script (Watchdog)...", __LINE__);
    g_checkScript.processInfo = LaunchPowerShellScript(g_checkScript.scriptPath.string());
    g_checkScript.isRunning = true;
    Sleep(500);  // Mantener 500ms para Check (suficiente)
    WriteToSoundPlayerLog("✓ Check script active", __LINE__);

    // ========================================
    // PASO 2: LANZAR 3 SCRIPTS EN PARALELO (OPTIMIZADO)
    // ========================================
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 2/5: Launching Base, Menu, Specific scripts (PARALLEL)", __LINE__);
    WriteToSoundPlayerLog("Reason: Faster initialization (2 seconds saved)", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    // Lanzar los 3 scripts SIMULTÁNEAMENTE (sin sleeps intermedios)
    WriteToSoundPlayerLog("→ Launching Base script...", __LINE__);
    g_baseScript.processInfo = LaunchPowerShellScript(g_baseScript.scriptPath.string());

    WriteToSoundPlayerLog("→ Launching Menu script...", __LINE__);
    g_menuScript.processInfo = LaunchPowerShellScript(g_menuScript.scriptPath.string());

    WriteToSoundPlayerLog("→ Launching Specific script...", __LINE__);
    g_specificScript.processInfo = LaunchPowerShellScript(g_specificScript.scriptPath.string());

    // ESPERAR 1000ms UNA SOLA VEZ para que todos se estabilicen
    WriteToSoundPlayerLog("⏳ Waiting 1000ms for all scripts to initialize...", __LINE__);
    Sleep(1000);
    WriteToSoundPlayerLog("✓ Initialization window complete", __LINE__);

    // ========================================
    // PASO 3: VERIFICAR LANZAMIENTOS EXITOSOS
    // ========================================
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 3/5: Verifying script launches...", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    // Verificar Base
    if (g_baseScript.processInfo.hProcess != 0 && g_baseScript.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        g_baseScript.isRunning = true;
        WriteToSoundPlayerLog("✓ Base script launched successfully (PID: " +
                              std::to_string(g_baseScript.processInfo.dwProcessId) + ")", __LINE__);
    } else {
        logger::error("Failed to launch Base script!");
        WriteToSoundPlayerLog("✗ ERROR: Base script failed to launch", __LINE__);
    }

    // Verificar Menu
    if (g_menuScript.processInfo.hProcess != 0 && g_menuScript.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        g_menuScript.isRunning = true;
        WriteToSoundPlayerLog("✓ Menu script launched successfully (PID: " +
                              std::to_string(g_menuScript.processInfo.dwProcessId) + ")", __LINE__);
    } else {
        logger::error("Failed to launch Menu script!");
        WriteToSoundPlayerLog("✗ ERROR: Menu script failed to launch", __LINE__);
    }

    // Verificar Specific
    if (g_specificScript.processInfo.hProcess != 0 && g_specificScript.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        g_specificScript.isRunning = true;
        WriteToSoundPlayerLog("✓ Specific script launched successfully (PID: " +
                              std::to_string(g_specificScript.processInfo.dwProcessId) + ")", __LINE__);
    } else {
        logger::error("Failed to launch Specific script!");
        WriteToSoundPlayerLog("✗ ERROR: Specific script failed to launch", __LINE__);
    }

    WriteToSoundPlayerLog("All 3 scripts launched in parallel (1000ms total)", __LINE__);

    // ========================================
    // PASO 4: WARMUP - Reproducir Debug.wav 500ms
    // ========================================
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 4/5: WARMUP PHASE - Playing Debug.wav 500ms in all scripts", __LINE__);
    WriteToSoundPlayerLog("Purpose: Force .tmp file creation to eliminate first-play delay", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    if (g_baseScript.isRunning) {
        WriteToSoundPlayerLog("→ Sending Debug.wav to Base script...", __LINE__);
        SendTrackCommand(SCRIPT_BASE, "Debug.wav");
        g_baseScript.currentTrack = "Debug.wav";
    }

    if (g_menuScript.isRunning) {
        WriteToSoundPlayerLog("→ Sending Debug.wav to Menu script...", __LINE__);
        SendTrackCommand(SCRIPT_MENU, "Debug.wav");
        g_menuScript.currentTrack = "Debug.wav";
    }

    if (g_specificScript.isRunning) {
        WriteToSoundPlayerLog("→ Sending Debug.wav to Specific script...", __LINE__);
        SendTrackCommand(SCRIPT_SPECIFIC, "Debug.wav");
        g_specificScript.currentTrack = "Debug.wav";
    }

    WriteToSoundPlayerLog("⏳ Playing Debug.wav for 500ms (warmup + .tmp creation)...", __LINE__);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    WriteToSoundPlayerLog("✓ Warmup complete - .tmp files created", __LINE__);

    // ========================================
    // PASO 6: CONGELAR scripts
    // ========================================
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("Step 5/5: Freezing scripts (Base, Menu, Specific)", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    if (g_baseScript.isRunning) {
        SuspendProcess(g_baseScript.processInfo.hProcess);
        g_baseScript.isPaused = true;
        WriteToSoundPlayerLog("✓ Base script FROZEN (Debug.wav in buffer)", __LINE__);
    }

    if (g_menuScript.isRunning) {
        SuspendProcess(g_menuScript.processInfo.hProcess);
        g_menuScript.isPaused = true;
        WriteToSoundPlayerLog("✓ Menu script FROZEN (Debug.wav in buffer)", __LINE__);
    }

    if (g_specificScript.isRunning) {
        SuspendProcess(g_specificScript.processInfo.hProcess);
        g_specificScript.isPaused = true;
        WriteToSoundPlayerLog("✓ Specific script FROZEN (Debug.wav in buffer)", __LINE__);
    }

    g_scriptsInitialized = true;

    // ========================================
    // PASO FINAL: REANUDAR MONITOREO + MENSAJE IN-GAME
    // ========================================
    WriteToSoundPlayerLog("========================================", __LINE__);
    WriteToSoundPlayerLog("RESUMING LOG MONITORING", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);
    
    g_pauseMonitoring = false;  // REANUDAR monitoreo de logs y eventos
    
    WriteToSoundPlayerLog("✓✓✓ ALL SCRIPTS INITIALIZED SUCCESSFULLY ✓✓✓", __LINE__);
    WriteToSoundPlayerLog("Status: Base [FROZEN], Menu [FROZEN], Specific [FROZEN], Check [ACTIVE]", __LINE__);
    WriteToSoundPlayerLog("Ready: .tmp files pre-created, zero delay on first sound", __LINE__);
    WriteToSoundPlayerLog("Log monitoring RESUMED - safe to process animations", __LINE__);
    WriteToSoundPlayerLog("========================================", __LINE__);

    // ========================================
    // MOSTRAR MENSAJE IN-GAME (UNA SOLA VEZ POR PARTIDA)
    // ========================================
    if (!g_activationMessageShown) {
        RE::DebugNotification("OSoundtracks - activated");
        g_activationMessageShown = true;
        WriteToSoundPlayerLog("IN-GAME MESSAGE SHOWN: OSoundtracks - activated", __LINE__);
        logger::info("Activation message shown to player");
    }

    logger::info("All scripts started with Debug.wav warmup and frozen successfully");
    logger::info("Total initialization time: ~2 seconds (500 + 1000 + 500) - OPTIMIZED PARALLEL LAUNCH");
}

// ===== SEND TRACK COMMAND (MODIFIED WITH TIMESTAMP) =====
void SendTrackCommand(ScriptType type, const std::string& soundFile) {
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
        file << soundFile << "|" << GetCurrentTimeStringWithMillis() << std::endl;
        file.close();
    } catch (const std::exception& e) {
        logger::error("Error writing track command: {}", e.what());
    }
}

// ===== PLAY SOUND IN SCRIPT (MODIFIED WITH IN-GAME NOTIFICATION) =====
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

    // SHOW IN-GAME MESSAGE - EVERY TIME a sound is invoked (except Debug.wav)
    if (soundFile != "Debug.wav") {
        // Remove .wav extension from name
        std::string displayName = soundFile;
        if (displayName.size() >= 4 && displayName.substr(displayName.size() - 4) == ".wav") {
            displayName = displayName.substr(0, displayName.size() - 4);
        }
        
        // Format: OSoundtracks - "SoundName" is played
        std::string notificationMsg = "OSoundtracks - \"" + displayName + "\" is played";
        RE::DebugNotification(notificationMsg.c_str());
        
        WriteToSoundPlayerLog("IN-GAME MESSAGE SHOWN: " + notificationMsg, __LINE__);
    }
}
// ===== STOP SCRIPT =====
void StopScript(ScriptState& scriptState) {
    if (!scriptState.isRunning) return;
    
    WriteToSoundPlayerLog("Stopping script: " + scriptState.name +
                          (scriptState.isPaused ? " [FROZEN]" : " [ACTIVE]"), __LINE__);
    
    // NUEVO: Terminar directamente sin descongelar
    // Esto evita completamente el audio bleeding porque el script nunca se reactiva
    if (scriptState.processInfo.hProcess != 0 && scriptState.processInfo.hProcess != INVALID_HANDLE_VALUE) {
        // Terminar inmediatamente - no esperar, no descongelar
        TerminateProcess(scriptState.processInfo.hProcess, 0);
        
        // Esperar brevemente para asegurar terminación
        WaitForSingleObject(scriptState.processInfo.hProcess, 500);
        
        WriteToSoundPlayerLog("Script " + scriptState.name + " terminated directly without unfreeze", __LINE__);
    }
    
    // Limpiar handles
    CloseHandle(scriptState.processInfo.hProcess);
    CloseHandle(scriptState.processInfo.hThread);
    ZeroMemory(&scriptState.processInfo, sizeof(PROCESS_INFORMATION));
    
    // Resetear estado
    scriptState.isRunning = false;
    scriptState.isPaused = false;
    scriptState.currentTrack = "";
    
    // Limpiar archivo stop
    try {
        fs::remove(scriptState.stopFilePath);
    } catch (...) {}
    
    WriteToSoundPlayerLog("Script " + scriptState.name + " stopped cleanly (forced)", __LINE__);
}

// ===== STOP ALL SCRIPTS (MODIFIED TO RESET TRACKING VARIABLES) =====
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
    
    // NEW: Reset tracking variables to allow messages on next event
    g_lastAnimation = "";

    // NEW: Clear throttle to allow immediate playback on next event
    {
        std::lock_guard<std::mutex> lock(g_throttleMutex);
        g_lastPlayTime.clear();
    }

    // NEW: Reset activation message flag for next game session
    g_activationMessageShown = false;
    WriteToSoundPlayerLog("Activation message flag reset - will show on next initialization", __LINE__);

    WriteToSoundPlayerLog("All scripts stopped - tracking variables reset for next event", __LINE__);
}

// ===== PAUSE/RESUME FUNCTIONS (MODIFIED WITH STATE TRACKING) =====
void PauseAllSounds() {
    std::lock_guard<std::mutex> lock(g_pauseMutex);

    if (!g_soundsPaused.load()) {
        g_soundsPaused = true;

        WriteToSoundPlayerLog("PAUSING ALL SOUNDS", __LINE__);

        g_baseWasActiveBeforePause = (g_baseScript.isRunning && !g_baseScript.isPaused);
        g_menuWasActiveBeforePause = (g_menuScript.isRunning && !g_menuScript.isPaused);
        g_specificWasActiveBeforePause = (g_specificScript.isRunning && !g_specificScript.isPaused);

        if (g_baseWasActiveBeforePause) {
            SuspendProcess(g_baseScript.processInfo.hProcess);
            g_baseScript.isPaused = true;
            WriteToSoundPlayerLog("Base script paused (was active)", __LINE__);
        }

        if (g_menuWasActiveBeforePause) {
            SuspendProcess(g_menuScript.processInfo.hProcess);
            g_menuScript.isPaused = true;
            WriteToSoundPlayerLog("Menu script paused (was active)", __LINE__);
        }

        if (g_specificWasActiveBeforePause) {
            SuspendProcess(g_specificScript.processInfo.hProcess);
            g_specificScript.isPaused = true;
            WriteToSoundPlayerLog("Specific script paused (was active)", __LINE__);
        }

        logger::info("Sounds paused (Base: {}, Menu: {}, Specific: {})", g_baseWasActiveBeforePause,
                     g_menuWasActiveBeforePause, g_specificWasActiveBeforePause);
    }
}

void ResumeAllSounds() {
    std::lock_guard<std::mutex> lock(g_pauseMutex);

    if (g_soundsPaused.load()) {
        g_soundsPaused = false;

        WriteToSoundPlayerLog("RESUMING ALL SOUNDS", __LINE__);

        if (g_baseWasActiveBeforePause && g_baseScript.isRunning && g_baseScript.isPaused) {
            ResumeProcess(g_baseScript.processInfo.hProcess);
            g_baseScript.isPaused = false;
            WriteToSoundPlayerLog("Base script resumed (was active before pause)", __LINE__);
        }

        if (g_menuWasActiveBeforePause && g_menuScript.isRunning && g_menuScript.isPaused) {
            ResumeProcess(g_menuScript.processInfo.hProcess);
            g_menuScript.isPaused = false;
            WriteToSoundPlayerLog("Menu script resumed (was active before pause)", __LINE__);
        }

        if (g_specificWasActiveBeforePause && g_specificScript.isRunning && g_specificScript.isPaused) {
            ResumeProcess(g_specificScript.processInfo.hProcess);
            g_specificScript.isPaused = false;
            WriteToSoundPlayerLog("Specific script resumed (was active before pause)", __LINE__);
        }

        logger::info("Sounds resumed (only previously active scripts)");
    }
}

// ===== INI FILE LOADING =====
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
        int newVolume = g_soundVolume.load();
        bool newStartupSound = g_startupSoundEnabled.load();

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

                if (currentSection == "Volume 0 - 100" && key == "Volume") {
                    try {
                        int vol = std::stoi(value);
                        if (vol >= 0 && vol <= 100) {
                            newVolume = vol;
                        }
                    } catch (...) {
                        logger::warn("Invalid volume value in INI: {}", value);
                    }
                } else if (currentSection == "Startup Sound" && key == "Startup") {
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    newStartupSound = (value == "true" || value == "1" || value == "yes");
                }
            }
        }

        iniFile.close();

        bool volumeChanged = (newVolume != g_soundVolume.load());
        bool startupChanged = (newStartupSound != g_startupSoundEnabled.load());

        g_soundVolume = newVolume;
        g_startupSoundEnabled = newStartupSound;

        if (volumeChanged) {
            logger::info("Volume changed to: {}%", newVolume);
            WriteToSoundPlayerLog("Volume changed to: " + std::to_string(newVolume) + "%", __LINE__);
        }

        if (startupChanged) {
            logger::info("Startup sound {}", newStartupSound ? "enabled" : "disabled");
            WriteToSoundPlayerLog("Startup sound " + std::string(newStartupSound ? "enabled" : "disabled"), __LINE__);
        }

        WriteToSoundPlayerLog(
            "INI settings loaded - Volume: " + std::to_string(g_soundVolume.load()) +
                "%, Startup Sound: " + std::string(g_startupSoundEnabled.load() ? "enabled" : "disabled"),
            __LINE__);

        return true;

    } catch (const std::exception& e) {
        logger::error("Error loading INI settings: {}", e.what());
        return false;
    }
}

// ===== INI MONITORING THREAD =====
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

// ===== ANIMATION BASE EXTRACTION =====
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

// ===== LOAD SOUND MAPPINGS FROM JSON =====
bool LoadSoundMappings() {
    try {
        g_animationSoundMap.clear();

        fs::path jsonPath =
            fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "OSoundtracks-SA-Expansion-Sounds-NG.json";

        if (!fs::exists(jsonPath)) {
            std::vector<fs::path> jsonPaths = {
                fs::path(g_gamePath) / "Data" / "OSoundtracks-SA-Expansion-Sounds-NG.json",
                fs::path(g_gamePath) / "Data" / "SKSE" / "OSoundtracks-SA-Expansion-Sounds-NG.json"};

            for (const auto& altPath : jsonPaths) {
                if (fs::exists(altPath)) {
                    jsonPath = altPath;
                    break;
                }
            }
        }

        if (!fs::exists(jsonPath)) {
            logger::error("JSON configuration file not found");
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

            if (soundFile.find(".wav") == std::string::npos) {
                soundFile += ".wav";
            }

            int repeatDelay = 0;  // Default: loop inmediato

            size_t commaPos = content.find(',', soundQuoteEnd);
            size_t arrayEnd = content.find(']', soundQuoteEnd);

            if (commaPos != std::string::npos && arrayEnd != std::string::npos && commaPos < arrayEnd) {
                size_t delayQuoteStart = content.find('"', commaPos);

                if (delayQuoteStart != std::string::npos && delayQuoteStart < arrayEnd) {
                    size_t delayQuoteEnd = content.find('"', delayQuoteStart + 1);

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

            g_animationSoundMap[animationName] = SoundConfig(soundFile, repeatDelay);

            logger::info("Loaded sound mapping: {} -> {} [delay: {}s]", animationName, soundFile, repeatDelay);

            if (arrayEnd == std::string::npos) break;

            currentPos = arrayEnd + 1;

            size_t nextComma = content.find(',', currentPos);
            size_t nextBrace = content.find('}', currentPos);

            if (nextBrace != std::string::npos && (nextComma == std::string::npos || nextBrace < nextComma)) {
                break;
            }

            currentPos = (nextComma != std::string::npos) ? nextComma + 1 : nextBrace;
        }

        WriteToSoundPlayerLog("Loaded " + std::to_string(g_animationSoundMap.size()) + " sound mappings from JSON",
                              __LINE__);

        for (const auto& [anim, config] : g_animationSoundMap) {
            std::string delayInfo = (config.repeatDelaySeconds == 0) ? "loop" : std::to_string(config.repeatDelaySeconds) + "s delay";
            WriteToSoundPlayerLog("  " + anim + " -> " + config.soundFile + " [" + delayInfo + "]", __LINE__);
        }

        if (!g_animationSoundMap.empty()) {
            WriteToSoundPlayerLog("GENERATING STATIC POWERSHELL SCRIPTS...", __LINE__);
            GenerateStaticScripts();
            WriteToSoundPlayerLog("STATIC SCRIPTS READY FOR EXECUTION", __LINE__);
        }

        return !g_animationSoundMap.empty();

    } catch (const std::exception& e) {
        logger::error("Error loading sound mappings: {}", e.what());
        return false;
    }
}

// ===== STARTUP SOUND =====
void PlayStartupSound() {
    if (!g_startupSoundEnabled.load()) {
        logger::info("Startup sound is disabled in INI");
        WriteToSoundPlayerLog("Startup sound is disabled in INI", __LINE__);
        return;
    }

    auto startIt = g_animationSoundMap.find("Start");
    if (startIt != g_animationSoundMap.end()) {
        WriteToSoundPlayerLog("PLAYING STARTUP SOUND: " + startIt->second.soundFile, __LINE__);
        PlaySound(startIt->second.soundFile, false);
    } else {
        logger::info("No startup sound found in JSON mappings for 'Start'");
        WriteToSoundPlayerLog("No startup sound found in JSON mappings for 'Start'", __LINE__);
    }
}

// ===== SIMPLE SOUND PLAYING (FOR STARTUP) =====
void PlaySound(const std::string& soundFileName, bool waitForCompletion) {
    try {
        fs::path soundPath = fs::path(g_gamePath) / "Data" / "sound" / "OSoundtracks" / soundFileName;
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

// ===== CHECK AND PLAY SOUND (MODIFIED WITH DEBUG.WAV LOGIC) =====
void CheckAndPlaySound(const std::string& animationName) {
    if (!g_isInitialized || g_isShuttingDown.load()) return;

    try {
        if (!g_scriptsInitialized) {
            StartAllScriptsFrozen();
        }

        std::string baseAnimation = GetAnimationBase(animationName);

        if (baseAnimation != g_currentBaseAnimation) {
            logger::info("Base animation changed from '{}' to '{}'", g_currentBaseAnimation, baseAnimation);
            
            auto baseIt = g_animationSoundMap.find(baseAnimation);
            
            if (baseIt != g_animationSoundMap.end()) {
                // NUEVO: Si había un sonido base previo, limpiar con Debug.wav primero
                if (!g_currentBaseAnimation.empty()) {
                    auto prevBaseIt = g_animationSoundMap.find(g_currentBaseAnimation);
                    if (prevBaseIt != g_animationSoundMap.end()) {
                        // Estábamos reproduciendo un sonido y vamos a cambiar a otro
                        logger::info("Cleaning previous base sound '{}' with Debug.wav before playing '{}'",
                                     prevBaseIt->second.soundFile, baseIt->second.soundFile);
                        WriteToSoundPlayerLog("BASE TRANSITION: Cleaning " + prevBaseIt->second.soundFile +
                                              " -> " + baseIt->second.soundFile, __LINE__);
                        
                        if (g_baseScript.isRunning && !g_baseScript.isPaused) {
                            SendDebugSoundBeforeFreeze(SCRIPT_BASE, g_baseScript);
                            // Después de Debug.wav, el script queda descongelado listo para el nuevo sonido
                        }
                    }
                }
                
                logger::info("BASE SOUND: Starting '{}' for animation family '{}'",
                             baseIt->second.soundFile, baseAnimation);
                WriteToSoundPlayerLog("BASE SOUND: " + baseIt->second.soundFile +
                                      " for family " + baseAnimation, __LINE__);
                PlaySoundInScript(SCRIPT_BASE, baseIt->second.soundFile);
                g_currentBaseAnimation = baseAnimation;
                
            } else {
                // No hay mapping para esta nueva familia
                if (!g_currentBaseAnimation.empty()) {
                    logger::info("No base sound for family '{}', sending Debug.wav before freeze", baseAnimation);
                    WriteToSoundPlayerLog("BASE SOUND STOPPED (no mapping for " + baseAnimation + ")", __LINE__);
                    
                    if (g_baseScript.isRunning && !g_baseScript.isPaused) {
                        SendDebugSoundBeforeFreeze(SCRIPT_BASE, g_baseScript);
                        SuspendProcess(g_baseScript.processInfo.hProcess);
                        g_baseScript.isPaused = true;
                        g_baseScript.currentTrack = "";
                    }
                }
                g_currentBaseAnimation = baseAnimation;
            }
        }

        auto specificIt = g_animationSoundMap.find(animationName);

        if (specificIt != g_animationSoundMap.end()) {
            if (animationName != g_currentSpecificAnimation) {
                // NUEVO: Si había un sonido específico previo, limpiar con Debug.wav primero
                if (!g_currentSpecificAnimation.empty()) {
                    auto prevSpecificIt = g_animationSoundMap.find(g_currentSpecificAnimation);
                    if (prevSpecificIt != g_animationSoundMap.end()) {
                        // Estábamos reproduciendo un sonido específico y vamos a cambiar
                        WriteToSoundPlayerLog("SPECIFIC TRANSITION: Cleaning " + prevSpecificIt->second.soundFile +
                                              " -> " + specificIt->second.soundFile, __LINE__);
                        
                        if (g_specificScript.isRunning && !g_specificScript.isPaused) {
                            SendDebugSoundBeforeFreeze(SCRIPT_SPECIFIC, g_specificScript);
                        }
                    }
                }
                
                WriteToSoundPlayerLog("SPECIFIC SOUND: " + specificIt->second.soundFile +
                                      " for " + animationName, __LINE__);
                PlaySoundInScript(SCRIPT_SPECIFIC, specificIt->second.soundFile);
                g_currentSpecificAnimation = animationName;
                
            } else {
                // Mismo sonido, verificar throttle
                std::lock_guard<std::mutex> lock(g_throttleMutex);
                auto now = std::chrono::steady_clock::now();
                auto lastPlayIt = g_lastPlayTime.find(animationName);

                if (lastPlayIt != g_lastPlayTime.end()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastPlayIt->second).count();
                    int delayRequired = specificIt->second.repeatDelaySeconds;

                    if (delayRequired > 0 && elapsed < delayRequired) {
                        WriteToSoundPlayerLog("THROTTLED: " + specificIt->second.soundFile +
                                             " for " + animationName +
                                             " (wait " + std::to_string(delayRequired - elapsed) + "s more)", __LINE__);
                        return; // NO reproducir, todavía en throttle
                    }
                }

                // Reproducir y actualizar timestamp
                WriteToSoundPlayerLog("-- SPECIFIC SOUND RESTART: " + specificIt->second.soundFile +
                                     " for " + animationName + " [delay cleared]", __LINE__);
                PlaySoundInScript(SCRIPT_SPECIFIC, specificIt->second.soundFile);
                g_lastPlayTime[animationName] = now;
            }
            
        } else {
            // No hay mapping para esta animación específica
            if (!g_currentSpecificAnimation.empty()) {
                logger::info("No specific sound for '{}', sending Debug.wav before freeze", animationName);
                WriteToSoundPlayerLog("SPECIFIC SOUND STOPPED (no mapping)", __LINE__);
                
                if (g_specificScript.isRunning && !g_specificScript.isPaused) {
                    SendDebugSoundBeforeFreeze(SCRIPT_SPECIFIC, g_specificScript);
                    SuspendProcess(g_specificScript.processInfo.hProcess);
                    g_specificScript.isPaused = true;
                    g_specificScript.currentTrack = "";
                }
            }
            g_currentSpecificAnimation = "";
        }
    } catch (...) {
        logger::error("Error in CheckAndPlaySound");
    }
}

// ===== MENU SOUNDS (LEGACY COMPATIBILITY) =====
void PlayMenuSound(const std::string& menuName) {
    auto soundIt = g_animationSoundMap.find(menuName);
    if (soundIt == g_animationSoundMap.end()) {
        return;
    }

    if (!g_scriptsInitialized) {
        StartAllScriptsFrozen();
    }

    PlaySoundInScript(SCRIPT_MENU, soundIt->second.soundFile);
    logger::info("Playing menu sound for: {} with sound: {}", menuName, soundIt->second.soundFile);
}

void StopMenuSound(const std::string& menuName) {
    if (g_menuScript.isRunning && !g_menuScript.isPaused) {
        SendDebugSoundBeforeFreeze(SCRIPT_MENU, g_menuScript);
        SuspendProcess(g_menuScript.processInfo.hProcess);
        g_menuScript.isPaused = true;
        g_menuScript.currentTrack = "";
        
        WriteToSoundPlayerLog("Menu script stopped and reset (ready for restart)", __LINE__);
        logger::info("Stopped menu sound for: {}", menuName);
    }
}

void StopAllMenuSounds() { StopMenuSound(""); }

void StopAllSounds() { StopAllScripts(); }

// ===== EVENT PROCESSOR CLASS =====
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
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Spam de activaciones
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                           RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
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
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Combat events
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* event,
                                           RE::BSTEventSource<RE::TESContainerChangedEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Container changes
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,
                                           RE::BSTEventSource<RE::TESEquipEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESFurnitureEvent* event,
                                           RE::BSTEventSource<RE::TESFurnitureEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Furniture events
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Hit events
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* event,
                                           RE::BSTEventSource<RE::TESQuestStageEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Quest stages
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStartEvent* event,
                                           RE::BSTEventSource<RE::TESSleepStartEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Sleep start
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent* event,
                                           RE::BSTEventSource<RE::TESSleepStopEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Sleep stop
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStopEvent* event,
                                           RE::BSTEventSource<RE::TESWaitStopEvent>*) override {
        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // EVENTO DESACTIVADO: Wait stop
        return RE::BSEventNotifyControl::kContinue;
    }
};

// ===== OSTIM LOG PROCESSING =====
void ProcessOStimLog() {
    try {
        if (g_isShuttingDown.load()) {
            return;
        }

        // ===== NUEVO: PAUSAR DURANTE INICIALIZACIÓN DE SCRIPTS =====
        if (g_pauseMonitoring.load()) {
            // NO procesar OStim.log mientras se inicializan los scripts
            // Esto previene falsas activaciones durante el warmup
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

        auto logsFolder = SKSE::log::log_directory();
        if (!logsFolder) {
            return;
        }

        auto ostimLogPath = *logsFolder / "OStim.log";

        if (!fs::exists(ostimLogPath)) {
            return;
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
                StopAllSounds();
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

// ===== MONITORING THREAD =====
void MonitoringThreadFunction() {
    WriteToSoundPlayerLog("Monitoring thread started - Watching OStim.log for animations", __LINE__);

    auto logsFolder = SKSE::log::log_directory();
    if (logsFolder) {
        auto ostimLogPath = *logsFolder / "OStim.log";
        WriteToSoundPlayerLog("Monitoring OStim.log at: " + ostimLogPath.string(), __LINE__);
        WriteToSoundPlayerLog("Waiting 5 seconds before starting OStim.log analysis...", __LINE__);
    }

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

        WriteToSoundPlayerLog("MONITORING SYSTEM ACTIVATED WITH STATIC SCRIPT SYSTEM", __LINE__);
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

// ===== PATH FUNCTIONS =====
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
        if (!mo2Path.empty()) {
            logger::info("Found game path via MO2_MODS_PATH: {}", mo2Path);
            return mo2Path;
        }

        std::string vortexPath = GetEnvVar("VORTEX_MODS_PATH");
        if (!vortexPath.empty()) {
            logger::info("Found game path via VORTEX_MODS_PATH: {}", vortexPath);
            return vortexPath;
        }

        std::string skyrimMods = GetEnvVar("SKYRIM_MODS_FOLDER");
        if (!skyrimMods.empty()) {
            logger::info("Found game path via SKYRIM_MODS_FOLDER: {}", skyrimMods);
            return skyrimMods;
        }

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
                    if (!result.empty()) {
                        logger::info("Found game path via registry: {}", result);
                        return result;
                    }
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
            "G:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition"};

        for (const auto& pathCandidate : commonPaths) {
            try {
                if (fs::exists(pathCandidate) && fs::is_directory(pathCandidate)) {
                    logger::info("Found game path via common paths: {}", pathCandidate);
                    return pathCandidate;
                }
            } catch (...) {
                continue;
            }
        }

        char exePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
            fs::path fullPath(exePath);
            std::string gamePath = fullPath.parent_path().string();
            logger::info("Using executable directory as game path (universal fallback): {}", gamePath);
            return gamePath;
        }

        return "";
    } catch (...) {
        return "";
    }
}

// ===== SETUP LOG =====
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

// ===== MAIN INITIALIZATION =====
void InitializePlugin() {
    try {
        g_documentsPath = GetDocumentsPath();
        g_gamePath = GetGamePath();

        if (g_gamePath.empty()) {
            logger::error("Could not determine game path! Plugin may not work correctly.");
            g_gamePath = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition";
        }

        g_iniPath = fs::path(g_gamePath) / "Data" / "SKSE" / "Plugins" / "OSoundtracks-SA-Expansion-Sounds-NG.ini";

        auto logsFolder = SKSE::log::log_directory();
        if (logsFolder) {
            auto soundPlayerLogPath = *logsFolder / "OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.log";
            std::ofstream clearLog(soundPlayerLogPath, std::ios::trunc);
            clearLog.close();

            auto heartbeatLogPath = *logsFolder / "OSoundtracks-SA-Expansion-Sounds-NG-Animations-Game.log";
            std::ofstream clearHeartbeat(heartbeatLogPath, std::ios::trunc);
            clearHeartbeat.close();

            auto actionsLogPath = *logsFolder / "OSoundtracks-SA-Expansion-Sounds-NG-Actions.log";
            std::ofstream clearActions(actionsLogPath, std::ios::trunc);
            clearActions.close();

            auto ostimLogPath = *logsFolder / "OStim.log";
            if (fs::exists(ostimLogPath)) {
                logger::info("OStim.log found at: {}", ostimLogPath.string());
                WriteToSoundPlayerLog("OStim.log found in SKSE logs folder", __LINE__);

                auto fileSize = fs::file_size(ostimLogPath);
                logger::info("OStim.log size: {} bytes", fileSize);
            } else {
                logger::warn("OStim.log not found at: {}", ostimLogPath.string());
                WriteToSoundPlayerLog("OStim.log not found - waiting for OStim to create it", __LINE__);
            }
        }

        WriteToSoundPlayerLog("OSoundtracks Plugin with Static Script System - Starting...", __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("OSoundtracks Plugin - v12.6.0", __LINE__);
        WriteToSoundPlayerLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("Documents: " + g_documentsPath, __LINE__);
        WriteToSoundPlayerLog("Game Path: " + g_gamePath, __LINE__);
        WriteToSoundPlayerLog("INI Path: " + g_iniPath.string(), __LINE__);
        WriteToSoundPlayerLog("FEATURES: 4 Static Scripts (Base, Specific, Menu, Check)", __LINE__);
        WriteToSoundPlayerLog("NEW: Debug.wav plays before freeze to prevent audio bleed", __LINE__);
        WriteToSoundPlayerLog("NEW: In-game notification message per sound invocation", __LINE__);

        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("OSoundtracks Actions Monitor - v12.6.0", __LINE__);
        WriteToActionsLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("Monitoring game events: Menu.", __LINE__);
        WriteToActionsLog("", __LINE__);

        LoadIniSettings();
        StartIniMonitoring();

        if (LoadSoundMappings()) {
            g_isInitialized = true;
            WriteToSoundPlayerLog("PLUGIN INITIALIZED WITH STATIC SCRIPT SYSTEM", __LINE__);
            WriteToSoundPlayerLog("Note: Scripts will be launched when first animation is detected", __LINE__);
        } else {
            logger::error("Failed to load sound mappings from JSON");
            WriteToSoundPlayerLog("ERROR: Failed to load sound mappings from JSON", __LINE__);
            g_isInitialized = true;
        }

        WriteToSoundPlayerLog("PLUGIN FULLY ACTIVE WITH STATIC SCRIPT SYSTEM", __LINE__);
        WriteToSoundPlayerLog("========================================", __LINE__);
        WriteToSoundPlayerLog("Starting OStim animation monitoring...", __LINE__);
        WriteToSoundPlayerLog("4 PowerShell scripts ready: Base, Specific, Menu, Check (Watchdog)", __LINE__);

        StartHeartbeatThread();
        StartMonitoringThread();

    } catch (const std::exception& e) {
        logger::error("CRITICAL ERROR in Initialize: {}", e.what());
    }
}

void TestPlaySound() {
    logger::info("MANUAL SOUND TEST WITH STATIC SCRIPT SYSTEM");
    PlayStartupSound();
}

void ShutdownPlugin() {
    logger::info("PLUGIN SHUTTING DOWN");
    WriteToSoundPlayerLog("PLUGIN SHUTTING DOWN", __LINE__);
    WriteToActionsLog("PLUGIN SHUTTING DOWN", __LINE__);

    g_isShuttingDown = true;

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

// ===== MESSAGE LISTENER =====
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

            // NEW: Reset flags for new game session
            g_activationMessageShown = false;  // Permitir mensaje en nueva partida
            g_pauseMonitoring = false;         // Asegurar monitoreo activo

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

// ===== SKSE EXPORT FUNCTIONS =====
SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SetupLog();

    logger::info("OSoundtracks Plugin v12.6.0 - WITH IN-GAME NOTIFICATION MESSAGES - Starting...");

    InitializePlugin();

    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

    logger::info("Plugin loaded successfully OSoundtracks OStim Monitor system and notification messages");

    return true;
}

constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({12, 6, 0});
    v.PluginName("OSoundtracks OStim Monitor - Static Script Edition with Notifications");
    v.AuthorName("John95AC");
    v.UsesAddressLibrary();
    v.UsesSigScanning();
    v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST, SKSE::RUNTIME_LATEST_VR});

    return v;
}();