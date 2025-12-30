# 📜 OSoundtracks-SA-Expansion-Sounds-NG System Components

This mod manages different sounds and music for OSTIM. It's simple to use and works like SPID called OSTR (O-Sound-tracks-Rules)
The OSoundtracks-SA-Expansion-Sounds-NG project consists of **3 separate DLL components** that work together to provide a complete soundtrack management system for Skyrim Special Edition:

---

## Component 1: OSoundtracks-SA-Expansion-Sounds-NG.dll (v3.1.0)

**INI to JSON Processor** - Core data processing component

This SKSE Mod processes INI configuration files and builds the master `OSoundtracks-SA-Expansion-Sounds-NG.json` file at game startup. It translates human-readable rules into a stable JSON format with built-in backup and corruption recovery.

This is the core data processing component of the OSoundtracks system, it reads INI rules and generates the structured JSON that other components depend on.

---

## Component 2: OSoundtracks-SA-Expansion-Sounds-NG - Sound-Player.dll (v16.3.0)

**Audio Playback Engine** - BASS-based sound system

This SKSE Plugin monitors OStim.log for animation transitions and plays corresponding sounds using the BASS Library. It is a robust replacement for the previous PS1-style audio system that suffered from lag issues. The BASS implementation provides smooth, multi-channel audio playback with real-time volume control and loop management.

Features:

- Real-time OStim.log monitoring for animation transitions
- Multi-channel audio playback (base sounds + specific sounds)
- Extension-less sound file resolution (.wav/.mp3/.ogg priority)
- Loop and single-playback modes
- Volume control via INI settings
- Mute game music during OStim functionality

---

## Component 3: OSoundtracks-SA-Expansion-Sounds-NG - MCM.dll (v16.3.0)

**Mod Configuration Menu** - Independent Skyrim MCM interface

This SKSE Plugin provides an independent MCM interface for the OSoundtracks system. It improves overall project performance by keeping the menu system separate from other components. The MCM reads the generated JSON file and provides in-game configuration options.

Features:

- Independent MCM integration with Skyrim's menu system
- Reserved functionality for future advanced settings
- Direct access to OSoundtracks configuration from game menu

---

# Component 1: INI to JSON Processor - What does it do?

The OSoundtracks-SA-Expansion-Sounds-NG.dll reads INI configuration files and builds the master `OSoundtracks-SA-Expansion-Sounds-NG.json` file. It does not play sounds; it only processes configuration rules and generates a JSON structure that other OSoundtracks components use.

Upon game load, this Mod SKSE:

1. **Detects game path** (MO2, Vortex, Wabbajack, standard install)
2. **Validates JSON** - checks integrity and restores from backup if corrupted
3. **Reads backup config** - from `OSoundtracks-SA-Expansion-Sounds-NG.ini`
4. **Scans INI files** - all `OSoundtracks_*.ini` files
5. **Parses rules** - supports 5 key types: SoundKey, SoundEffectKey, SoundPositionKey, SoundTAGKey, SoundMenuKey
6. **Updates JSON** - applies rules in REPLACEMENT MODE, cleans unused entries
7. **Writes atomically** - validates indentation, verifies output, restores on failure

---

# Component 2: Audio Playback Engine - What does it do?

The OSoundtracks-SA-Expansion-Sounds-NG - Sound-Player.dll monitors OStim.log in real-time and plays sounds based on animation transitions. It uses the BASS Library for high-performance audio playback, replacing the previous lag-prone PS1-style system.

Key features:

- **OStim.log monitoring**: Detects animation transitions by parsing log file
- **BASS Library integration**: Robust audio engine with multi-channel support
- **Extension detection**: Automatically finds .wav, .mp3, or .ogg files
- **Playback modes**: Single play ("0") or loop ("loop")
- **Volume control**: Per-channel volume settings via INI
- **Game music muting**: Option to mute Skyrim music during OStim

---

# Component 3: MCM Interface - What does it do?

The OSoundtracks-SA-Expansion-Sounds-NG - MCM.dll provides an independent MCM menu that integrates with Skyrim's configuration system. This separation improves overall project performance by keeping menu operations isolated from audio processing.

Key features:

- **Independent MCM**: Uses Skyrim's native MCM framework
- **Performance optimized**: Does not interfere with audio playback
- **Future-ready**: Reserved functionality for advanced settings

---

# INI Rules Examples

The INI rules are intentionally designed to be edited by humans. The format is short, predictable, and easy to diff in modpacks. This Mod SKSE reads each valid rule, normalizes whitespace, and writes to result into master JSON using a deterministic replacement strategy.

The Mod SKSE scans for `OSoundtracks_*.ini` files in the Data folder.

```
;OSoundtracks SA Expansion Sounds NG

;Example of code designs: it's very similar to SPID but shorter and simpler.

; SoundKey = AnimationKeyName|SoundNameFile|Playback|Pista           AnimationKey: animation message you'll get it in Ostim.log when going to the animation
; SoundEffectKey = MoodName|SoundNameFile|Playback|Pista         MoodName: effect name for mood sounds (magic, impact, environmental)
; SoundPositionKey = PositionName|SoundNameFile|Playback|Pista   PositionName: position identifier (Walk, Run, Standing, Crouching, etc.)
; SoundTAGKey = TagName|SoundNameFile|Playback|Pista            TagName: tag for specific conditions (Idle, Exterior, etc.)
; SoundMenuKey = MenuName|SoundNameFile|Playback|Pista          MenuName: menu identifier (Menu, MenuJazz, MenuNord, etc.)
```
OSoundtracks_Config.ini
```
;Start Sound
SoundKey = Start|miau-03|

;OStimAlignMenu Sound
SoundKey = OStimAlignMenu|Smooth Jazz|

;SoundMenuKey Basic
SoundMenuKey = BasicMenu|ambient-piano-relaxing|

;SoundMenuKey Basic
SoundMenuKey = BasicMenu|Smooth Jazz|
```
SoundMenuKey
```
;John95acJazz
SoundMenuKey = John95acJazz|background-jazz-golden-whisper|

;John95acJazz
SoundMenuKey = John95acJazz|jazzy-lounge|

;John95acJazz
SoundMenuKey = John95acJazz|jazz-funk-groove-instrumental|

```
SoundPositionKey
```
;Sleep
SoundPositionKey = Sleep|sleeping-snoring-breathing|loop

;Hug
SoundPositionKey = Hug|snoring|

;Kiss
SoundPositionKey = Kiss|female-sigh|
```

---

## Track Indexing System (PISTA Parameter - Multiple effects at the same time ADVANCED)

Available since version 3.1.0.

The PISTA parameter enables independent track organization for sounds. Each track maintains its own list index sequence.

**List Index Format:**

- **Pista = 0** (or empty): Generates `list-1`, `list-2`, `list-3`... (default track)
- **Pista = 1**: Generates `list1-1`, `list1-2`, `list1-3`...
- **Pista = 2**: Generates `list2-1`, `list2-2`, `list2-3`...
- **Pista = N**: Generates `listN-1`, `listN-2`, `listN-3`...

**INI Format with PISTA:**

```ini
; Same AnimationKey with multiple tracks
SoundKey = Sleep|snoring.wav||0    ; Track 0, generates list-1
SoundKey = Sleep|breathing.wav|0|0    ; Track 0, generates list-2
SoundKey = Sleep|female-sigh.wav|3|1      ; Track 1, generates list1-1
SoundKey = Sleep|deep-sigh.wav|loop|1        ; Track 1, generates list1-2
```

**Counter Behavior:**

- Each track has its own independent counter (list-1, list-2, etc.)
- Track 0 counter: list-1, list-2, list-3...
- Track 1 counter: list1-1, list1-2, list1-3...
- Track N counter: listN-1, listN-2, listN-3...

This enables simultaneous playback of independent sound layers (e.g., ambient breathing + snoring) while preventing conflicts between tracks.

---

## Track System Examples

**Example 1: Sleep sounds with multiple tracks**

```ini
SoundKey = Sleep|snoring.wav||0        ; Track 0, generates list-1
SoundKey = Sleep|breathing.wav|0|0       ; Track 0, generates list-2
SoundKey = Sleep|female-sigh.wav|3|1      ; Track 1, generates list1-1
SoundKey = Sleep|deep-sigh.wav|loop|1        ; Track 1, generates list1-2
```

**JSON Output:**

```json
{
    "SoundKey": {
        "Sleep": [
            ["snoring.wav", "list-1", "0"],
            ["breathing.wav", "list-2", "0"],
            ["female-sigh.wav", "list1-1", "3"],
            ["deep-sigh.wav", "list1-2", "loop"]
        ]
    }
}
```

**Use Case:** Layering multiple independent sounds for the same animation (e.g., breathing + snoring) that play simultaneously, including their own variants.

---

# JSON Output Format

The JSON is the canonical, machine-friendly representation of your INI rules. The goal is to keep it stable in ordering and structure, because other SKSE Mods and tools will load it as a known schema.

```
{
    "SoundKey": {
        "Start": [
            ["miau-03", "list-1", "0"]
        ],
        "OStimAlignMenu": [
            ["Smooth Jazz", "list-1", "0"]
        ],
        "Gunslicer2PDanceDoggy": [
            ["clap-3-hand", "list-1", "0"]
        ],
        "ShadowmanBallroomtango": [
            ["Asi Se Baila El Tango", "list-1", "0"]
        ],
        "ShadowmanBallroomsalsa1": [
            ["BallroomSalsaMusic", "list-1", "0"]
        ],
        "ShadowmanBallroomslowfox": [
            ["Fly Me To The Moon", "list-1", "0"]
        ]
    },
    "SoundEffectKey": {},
    "SoundPositionKey": {
        "sleep": [
            ["sleeping", "list-1", "loop"],
            ["snoring", "list-2", "loop"],
            ["female-sigh-Sleep", "list-3", "loop"]
        ]
    },
    "SoundTAGKey": {},
    "SoundMenuKey": {
        "BasicMenu": [
            ["ambient-piano-relaxing", "list-1", "0"],
            ["Smooth Jazz", "list-2", "0"]
        ],
    }
}
```

Note: All 5 keys are always present in the JSON, even if empty (represented as {}).

Meaning:

```
- "SoundKey": Animation-triggered sounds
- "SoundEffectKey": Event/Effect sounds
- "SoundPositionKey": Location-based sounds
- "SoundTAGKey": Conditional/tag-based sounds
- "SoundMenuKey": Menu background music

Sound Array Elements:
  [0] = SoundNameFile from INI
  [1] = ListIndex (list-1, list-2, list1-1, etc.)
  [2] = Playback string from INI
  [3] = Track number (PISTA parameter, defaults to 0)
```

# Backup

Backup configuration is read from:

```
- Section: [Original backup]
- Key: Backup
```

Backup modes:

```
- Backup = 1
  One-time literal backup, then the Mod SKSE updates the INI to Backup = 0.

- Backup = 0
  Backup disabled.

- Backup = true
  Always-backup mode: performs a literal backup on every execution, INI is not modified.
```

Example:

```ini
[Original backup]
Backup = true
```

---

# Updates and Revisions

The OSoundtracks-SA-Expansion-Sounds-NG project consists of 3 independent DLL components, each with its own version history and changelog below.

---

## Component 1: INI to JSON Processor (OSoundtracks-SA-Expansion-Sounds-NG.dll)

**Current Version:** v3.1.0

This component is responsible for processing INI configuration files and generating the master JSON file used by other components.

**System Evolution:**
- **v1.8.7 and earlier**: PS1-dependent audio system (legacy)
- **v2.5.0 onwards**: BASS-based stable system (current)

### Version History

**Version 1.8.7:**

This Mod SKSE ships a stability-first pipeline for real mod environments:
```
- Multi-method path detection (MO2 / standard install / portable fallback)
- Mod SKSE-based validation of plugin directory
- Literal backup + restore flow when integrity checks fail
- REPLACEMENT + CLEANUP behavior to keep JSON deterministic
- Atomic writes and indentation verification/correction
```

---

**Version 2.5.0:**

This version establishes a guaranteed, predictable structure for JSON file by enforcing a specific order of keys and prioritizing certain AnimationKeys. The goal is to ensure that JSON output is always consistent and deterministic, which is critical for other tools and SKSE Mods that read this file as a known schema.

Key improvements:

- **Priority AnimationKeys**: Implemented automatic prioritization for "Start" and "OStimAlignMenu" within the SoundKey section. These two AnimationKeys always appear first, regardless of the order in which INI files are processed or defined.
- **ORDERED_JSON_KEYS Constant**: Defined a global constant that enforces the order of main JSON keys: SoundKey → SoundEffectKey → SoundPositionKey → SoundTAGKey. This ordering is applied whenever the JSON is rebuilt or written.
- **Stable Sorting Algorithm**: Implemented `sortOrderedData` method using `std::stable_sort`, which prioritizes AnimationKeys based on the `PRIORITY_ANIMATION_KEYS` list while maintaining the relative order of all other keys when they have equal priority status.
- **Empty Container Preservation**: Modified JSON generation to ensure that even keys with no data are written as `{}` rather than being omitted. This guarantees that all five keys always exist in the output file.
- **Complete JSON Reconstruction**: Updated `PreserveOriginalSections` to rebuild the entire JSON from scratch when needed, enforcing the key order at both the root level and within SoundKey's priority system.

Priority Example in SoundKey:

```json
{
    "SoundKey": {
        "Start": [...],              ← Always first (priority)
        "OStimAlignMenu": [...],    ← Always second (priority)
        "miau-03": [...],           ← Alphabetical after priorities
        "JohnAC-1": [...],
        "alegria": [...]
    },
    "SoundEffectKey": {},             ← Second main key
    "SoundPositionKey": {},          ← Third main key
    "SoundTAGKey": {}               ← Fourth main key
    "SoundMenuKey": {}              ← Menu main key
}
```

Log Output During Sorting:

```
Applying final sorting (Start and OStimAlignMenu first in SoundKey)...
  SoundKey sorted with priority keys at top
```

This deterministic structure prevents silent drift in JSON ordering over time, ensures human-readability, and provides a stable contract for downstream systems consuming this JSON.

---

**Version 2.6.0:**

This version transforms the plugin from a simple replacement system to a sophisticated accumulation system that supports multiple sounds per AnimationKey. Previously, adding a new sound to an existing AnimationKey would replace all previous sounds. Now, sounds accumulate with independent list indices, allowing for multi-sound configurations.

Core changes:

- **SoundWithPlayback Structure Enhancement**: Added `listIndex` field to track the sequential position of each sound within its AnimationKey. The structure now stores: `soundFile`, `listIndex`, and `playback`.
- **SetPresetResult Enum Update**: Added `Accumulated` state to distinguish between: (1) Creating a new AnimationKey (Added), (2) Replacing existing sounds (Replaced), (3) Adding to an existing AnimationKey (Accumulated), and (4) No changes (NoChange).
- **Accumulation Logic**: Modified `setPreset` method to check if a sound file already exists in an AnimationKey. If not found, it appends the new sound with an incremented `listIndex` (list-1, list-2, list-3, etc.) instead of clearing previous sounds.
- **Automatic Renumbering**: Updated `removePreset` method to automatically renumber the remaining sounds after deletion. If list-2 is removed, list-3 becomes list-2, maintaining continuous numbering.
- **JSON Format Update**: The JSON now stores three elements per sound instead of two: `[0]` = soundFile, `[1]` = listIndex, `[2]` = playback. This format preserves the history and order of sounds.
- **Enhanced Logging**: Added `totalRulesAccumulated` counter and detailed log messages showing the exact list index assigned to each accumulated sound.

INI Rule Example (Accumulation):

```ini
SoundKey = miau-03|0
SoundKey = miau-03|clap-3-hand
SoundKey = miau-03|cheer-1-female
```

JSON Output:

```json
{
    "SoundKey": {
        "miau-03": [
            ["0", "list-1", "0"],
            ["clap-3-hand", "list-2", "0"],
            ["cheer-1-female", "list-3", "0"]
        ]
    }
}
```

Log Output:

```
  Added: SoundKey -> AnimationKey: miau-03 -> Sound: 0 -> List: list-1 -> Playback: 0
  Accumulated: SoundKey -> AnimationKey: miau-03 -> Sound: clap-3-hand -> List: list-2 -> Playback: 0
  Accumulated: SoundKey -> AnimationKey: miau-03 -> Sound: cheer-1-female -> List: list-3 -> Playback: 0
```

This system enables complex sound combinations for single animations while maintaining traceability and independent management of each sound.

---

**Version 2.7.0:**

This version addresses critical issues with JSON Byte Order Mark (BOM) handling and introduces a robust reconstruction system for recovering from corrupted or malformed JSON files. Previous versions could incorrectly flag valid JSON files as corrupt when they contained UTF-8 BOM bytes (0xEF 0xBB 0xBF), leading to unnecessary backup restorations or processing failures.

Major improvements:

- **RemoveBOM Function**: Added a new utility function that detects and strips the UTF-8 BOM from the beginning of file content before any JSON validation or parsing occurs. This prevents false corruption detection caused by BOM bytes interfering with structural checks.
- **RebuildJsonFromScratch Function**: Implemented a complete JSON reconstruction capability that can rebuild the entire master JSON using only the INI rule data, without relying on the original JSON file content. This function guarantees the correct order of all five keys (SoundKey, SoundEffectKey, SoundPositionKey, SoundTAGKey, SoundMenuKey) and generates a clean, properly formatted JSON even when the original file is severely corrupted.
- **BOM Removal from Write**: Removed the explicit BOM writing operation from `WriteJsonAtomically`. The JSON specification recommends not using BOM, and eliminating it prevents validation conflicts where the file no longer starts with the expected '{' character.
- **Enhanced Recovery Strategy**: Modified the JSON update flow to attempt a full rebuild as the first recovery option before falling back to backup restoration. The new strategy is: (1) Try normal update → (2) If failure, try rebuild from INI data → (3) If rebuild also fails, restore from backup. This maximizes the chances of generating a valid JSON even when the original file has issues.
- **Validation Updates**: Applied `RemoveBOM` calls in `PerformSimpleJsonIntegrityCheck` and `PerformTripleValidation`, ensuring that all integrity checks operate on clean BOM-free content.

Error Recovery Flow:

```
JSON Write/Indentation Failure
    ↓
Attempt Rebuild from INI Data
    ↓ Success → Valid JSON Generated
    ↓ Failure
Restore from Backup
```

This update significantly improves the plugin's resilience to file encoding issues and corruption scenarios commonly encountered in modpack distributions (Wabbajack, NOLVUS) and international Windows installations.

---

**Version 2.8.0:**

This version introduces a new JSON section called `SoundMenuKey` designed specifically for menu music in OStim's alignment and configuration menus. This addition extends the OSoundtracks system to support background music that plays while menus are open, separate from animation-triggered sounds.

Key features:

- **New SoundMenuKey Section**: Added as the fifth and final key in the guaranteed JSON key order: SoundKey → SoundEffectKey → SoundPositionKey → SoundTAGKey → SoundMenuKey.
- **INI Rule Format**: `SoundMenuKey = Menu|SoundNameFile|Playback` where "Menu" is the identifier, SoundNameFile is the audio file in `\sound\OSoundtracks\`, and Playback is "loop", "once", or "0".
- **Full Validation Integration**: Updated `PerformSimpleJsonIntegrityCheck` and `PerformTripleValidation` to recognize SoundMenuKey as a valid key. The system now validates JSON files containing up to 5 main sections.
- **Order Preservation**: Modified the `ORDERED_JSON_KEYS` constant and all key ordering logic (in `PreserveOriginalSections` and `PreserveOriginalSectionsOLD`) to ensure that SoundMenuKey always appears last in the JSON output, maintaining a consistent structure across all generated files.
- **Change Detection**: Updated `CheckIfChangesNeeded` to include SoundMenuKey in the valid keys list, enabling proper detection of modifications to menu music rules.

INI Example:

```ini
;MenuNormal
SoundMenuKey = Menu|Smooth Jazz Punk|loop
```

JSON Output:

```json
{
    "SoundKey": {},
    "SoundEffectKey": {},
    "SoundPositionKey": {},
    "SoundTAGKey": {},
    "SoundMenuKey": {
        "Menu": [
            ["Smooth Jazz Punk", "list-1", "loop"]
        ]
    }
}
```

This feature is particularly useful for creating immersive menu experiences with different music tracks for alignment menus, settings panels, or custom UI elements.

---

**Version 2.9.0:**

This version addresses a critical bug discovered in the v2.8.0 release. While SoundMenuKey was successfully added to all global constants and validation functions, the local `validKeys` variable inside `SKSEPlugin_Load` that controls actual INI rule processing was not updated, causing all SoundMenuKey rules to be silently ignored.

Fix details:

- **Root Cause**: The `validKeys` set variable in `SKSEPlugin_Load` determines which INI keys are accepted during parsing. In v2.8.0, this local variable still contained only 4 keys ("SoundKey", "SoundEffectKey", "SoundPositionKey", "SoundTAGKey"), so when the parser encountered "SoundMenuKey = Menu|Smooth Jazz Punk|loop", the condition `validKeys.count(key)` returned 0 and the rule was skipped.
- **Correction Applied**: Updated the local `validKeys` set to include "SoundMenuKey" as the fifth key. This ensures that: (1) `OrderedPluginData` containers are created for SoundMenuKey, and (2) SoundMenuKey rules are accepted and processed during INI parsing.
- **Log Messages Updated**: Updated the log output in `RebuildJsonFromScratch` and `PreserveOriginalSections` to correctly reflect the 5-key system (SoundKey → SoundEffectKey → SoundPositionKey → SoundTAGKey → SoundMenuKey).
- **Complete Integration**: SoundMenuKey now works through the entire processing pipeline: INI parsing, JSON generation, validation, cleanup, and atomic write operations.

Expected Log Output (After Fix):

```
Processing file: OSoundtracks_(NameMod).ini
Full path: G:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data\OSoundtracks_(NameMod).ini
  Added: SoundEffectKey -> AnimationKey: alegria -> Sound: cat-purr -> List: list-1 -> Playback: loop
  Added: SoundPositionKey -> AnimationKey: kiss -> Sound: Smooth Jazz -> List: list-1 -> Playback: loop
  Added: SoundTAGKey -> AnimationKey: OAR -> Sound: Smooth Jazz -> List: list-1 -> Playback: loop
  Added: SoundMenuKey -> AnimationKey: Menu -> Sound: Smooth Jazz Punk -> List: list-1 -> Playback: loop
  Rules in file: 4 | Added: 4 | Replaced: 0 | Skipped: 0
```

This fix restores full SoundMenuKey functionality that was intended in v2.8.0 but not fully implemented.

---

**Version 3.0.0:**

This Mod SKSE ships a stability-first pipeline for real mod environments:
```
- Multi-method path detection (MO2 / standard install / portable fallback)
- Mod SKSE-based validation of plugin directory
- Literal backup + restore flow when integrity checks fail
- REPLACEMENT + CLEANUP behavior to keep JSON deterministic
- Atomic writes and indentation verification/correction
```

---

## Component 2: Audio Playback Engine (OSoundtracks-SA-Expansion-Sounds-NG-Sound-Player.dll)

**Current Version:** v16.3.0

This component monitors OStim.log for animation transitions and plays sounds using BASS Library. It is a robust replacement for the previous PS1-style audio system that suffered from lag issues. The BASS implementation provides smooth, multi-channel audio playback with real-time volume control and loop management.

### Version History

**Version 15.3.0: BASS Library Stability**

Enhanced initialization, error handling, and resource management.

**Improvements:**
- BASS file existence validation before loading
- Volume clamping to 0.0-1.0 range
- Complete stream cleanup on shutdown
- Enhanced error messages with file paths

---

**Version 15.4.0: SoundPositionKey + Multi-Format Audio**

Fragment-based sound matching with case-insensitive search and multi-format support (WAV, MP3, OGG).

**Features:**
- SoundPositionKey searches for text fragments in animation names
- Case-insensitive matching (e.g., "cowgirl" matches "Cowgirl")
- Multi-format support with priority: WAV → MP3 → OGG
- Extension-less filenames work with automatic format detection

---

**Version 15.6.0: SoundMenuKey Corrections**

Fixed pause/resume integration and added song transition monitoring.

**Corrections:**
- Added SoundMenuKey pause/resume to OStimAlignMenu
- Added SoundMenuKey to game menu pause/resume logic
- Fixed song transition issue (was only playing first song)

---

**Version 15.8.0: Skyrim Music Mute - Technical Limitation**

**NOTICE:** This feature is **NOT technically viable** with CommonLibSSE alone. Investigated methods (INISettingCollection, GameSettingCollection, BASSAudioManager, console commands) all failed. The code exists but has no effect on active audio. Requires ESP with Papyrus or invasive hooks - rejected due to external dependencies and instability risks.

**Current Status:** Marked as WIP. Users should manually lower music volume before OStim scenes.

---

**Version 16.1.0: Author Preview Optimization + Backup System**

Optimized author preview with 10-second duration, forced volume, and intelligent backup system.

**Author Preview Improvements**:
- Timer increased to 10 seconds for better song sampling
- Volume forced to 100% (ignores SoundMenuKeyVolume INI)
- `g_iniFirstLoad` flag prevents automatic preview on game startup
- Preview only plays on manual INI changes

**Backup System**:
- New `g_backupUpdateEnabled` configuration flag
- `ProcessBackupUpdate()` function restores configurations from backup
- Preserves user customizations while restoring backed-up settings
- Automatic backup creation in `OSoundtracks_MCM_Backup/` directory

Author preview behavior:
- First load: NO preview (silent author set)
- Manual changes: Plays 10-second preview at full volume

---

**Version 16.2.0: Backup Update + Author Preview Enhancements**

Backup update system, 7-second author preview, and immediate music reload.

**Author Preview**:
- Duration changed from 3 to 7 seconds
- Volume forced to 1.0 (100%) regardless of INI setting
- Does NOT play on first game startup - only on manual INI changes

**Backup Update System**:
- New `[Backup update]` section in INI
- `BackupINI = true` triggers restoration from `OSoundtracks_MCM_Backup/` folder
- Smart restoration: preserves new INI settings while restoring user configurations
- Automatically sets `BackupINI = false` after processing

New INI Settings:
```ini
[Backup update]
BackupINI = true

[Menu Sound]
SoundMenuKey = Author_Random
Author = John95ac
```

**Immediate Reload**: When SoundMenuKey settings change, playlist rebuilds, stream stops and restarts immediately.

---

**Version 16.3.0: Multi-Layer Position System + Pause Bug Fixes**

Critical pause/resume bug fixes and complete multi-layer implementation for SoundPositionKey.

**Multi-Layer Position System**: SoundPositionKey supports multiple simultaneous layers (list-, list1-, list2-, etc.) per fragment. All matching fragments (kiss, hug, sleep) play simultaneously with independent layers.

**Pause Bug Fixes**: All streams now properly pause when opening menus/console - including Effect, Tag, and multi-layer Position streams that were previously ignored.

**Multiple Fragment Detection**: Detects ALL fragments that match animation names and plays them together instead of stopping at first match.

**Structural Changes**:
- Added `layerNumber` field to SoundOption structure
- Replaced `std::vector` with `std::map<int, std::vector<SoundOption>> layers`
- Changed from single `g_positionStream` to `std::map<std::string, std::map<int, HSTREAM>>` for multi-stream support
- Completely rewrote `CheckPositionSound()` for multi-fragment detection and multi-layer playback

**Pause/Resume Improvements**:
- `PauseAllSounds()` now tracks all Position layers before pausing
- `ResumeAllSounds()` fixed incorrect condition that blocked SoundMenuKey resume
- Added SoundMenuKey DISABLED state checking before resume

**Author Preview**: Skips playback when game is paused to maintain sync.

Example: Animation "kissAllHug-sleep" triggers "kiss" layer and "sleep" layer (with its own sub-layers) simultaneously.

---

## Component 3: MCM Interface (OSoundtracks-SA-Expansion-Sounds-NG-MCM.dll)

**Current Version:** v16.3.0

This component provides an independent MCM interface for the OSoundtracks system. It improves overall project performance by keeping the menu system separate from other components. The MCM reads the generated JSON file and provides in-game configuration options.

### Version History

**Version 16.1.0: Complete System Analysis - Part 1**

This version represents a comprehensive technical analysis of the entire OSoundtracks MCM plugin system, identifying all missing functionality and planning progressive implementation strategy.

**Currently Implemented (7 Getters/Setters):**
- BaseVolume, MenuVolume, SpecificVolume, MasterVolumeEnabled, Startup, Visible, Backup
- All 7 functions work correctly via native calls

**Missing Functions Identified (8):**
- GetEffectVolume / SetEffectVolume
- GetPositionVolume / SetPositionVolume
- GetTAGVolume / SetTAGVolume
- GetSoundMenuKeyVolume / SetSoundMenuKeyVolume
- GetMuteGameMusic / SetMuteGameMusic
- GetSoundMenuKeyMode / SetSoundMenuKeyMode (String)
- GetAuthor / SetAuthor (String)

**PAPYRUS Architecture:**
- **OSoundtracks_NativeScript.psc**: Hidden bridge script, declares native functions
- **OSoundtracks_McmScript.psc**: Extends SKI_ConfigBase with 3 pages (Settings, Advanced MCM, About)
- Uses local variables synchronized with INI via native calls

**INI Sections Comparison (What MCM Controls vs What It Doesn't):**
- MCM controls: BaseVolume, MenuVolume, SpecificVolume, MasterVolumeEnabled, Startup, Visible, Backup
- MCM DOES NOT Control: EffectVolume, PositionVolume, TAGVolume, SoundMenuKeyVolume, MuteGameMusicDuringOStim, SoundMenuKey, Author

**Proposed Progressive Implementation (85% Phase 1, 60-65% Phase 2):**
1. Add 4 volume sliders + MuteGameMusic toggle
2. Implement 5-option SoundMenuKey selector
3. Implement dynamic JSON author list

---

**Version 16.2.0: Intelligent INI Verification System**

Introduced an automatic verification and update system for the Authors INI file, ensuring consistency between master JSON and Authors INI.

**New Features:**
- `GetAuthorsFromJSON()`: Extracts author list directly from SoundMenuKey section in master JSON
- `GetAuthorsFromINI()`: Reads the current author list from the Authors INI
- `CompareAuthors()`: Compares two author lists (case-sensitive alphabetically)
- `CheckAndRegenerateAuthorsINI()`: Master function that coordinates verification and regeneration

**Intelligent INI Verification:**
```
When master JSON is modified:
├─ GetAuthorsFromJSON() → reads JSON
├─ GetAuthorsFromINI() → reads INI
├─ CompareAuthors() → compares both
└─ CheckAndRegenerateAuthorsINI() → decides:
    ├─ Different → regenerate Authors INI from JSON
    └─ Same → no changes (optimization)
```

**JSON Parser Bug Fix:**
Previous version captured garbage data including:
- Filenames within quotes
- List indices like "list-1"
- Playback values like "loop"
- JSON garbage (opening brace)

**Fixed Parser:**
- Now correctly identifies valid keys (text between quotes followed by `:`)
- Filters out JSON garbage (opening brace, key name)
- Only extracts actual author names

**Optimization Benefits:**
- No unnecessary INI file writes
- Maintains data integrity
- Reduced disk I/O
- Automatic synchronization when JSON changes

**Log Messages:**
```
=======================================
Checking Authors INI consistency
No authors found in JSON / Found X authors in JSON
Authors INI not found or empty, generating...
Authors match, no changes needed / Authors mismatch detected, regenerating...
=======================================
Generating Authors INI from JSON
Found SoundMenuKey section in JSON
Found author: [AuthorName]
Total authors found: [Number]
Author list: [ListSeparatedByPipe]
=======================================
```

---

**Version 16.3.0: Critical Bug Fix - JSON Parser Key Counter**

Fixed a critical bug in the JSON parser that prevented proper author detection from SoundMenuKey.

**Critical Bug Fixed:**
- The `braceDepth` variable in `GetAuthorsFromJSON()` was not being incremented when finding the line `"SoundMenuKey": {`, causing only the first author to be detected and the loop to terminate prematurely
- **Root Cause**: The opening brace `{` was on the same line as the section identifier, but the `continue` statement skipped counting it
- **Impact**: Only the first author from JSON was extracted, others were ignored

**Solution Applied:**
- Key counting now happens BEFORE searching for quotes: increment `braceDepth` when encountering `'{'` line
- This ensures `braceDepth` correctly reflects depth when processing SoundMenuKey section
- All authors from JSON are now properly detected

**Additional Changes:**
- Updated plugin version to v16.3.0 in 4 locations
- Enhanced logging with new messages for JSON parsing operations
- Improved diagnostic capabilities for troubleshooting

This version represents a comprehensive technical analysis of the entire OSoundtracks MCM plugin system, identifying all missing functionality and planning progressive implementation strategy.

**Currently Implemented (7 Getters/Setters):**
- BaseVolume, MenuVolume, SpecificVolume, MasterVolumeEnabled, Startup, Visible, Backup
- All 7 functions work correctly via native calls

**Missing Functions Identified (8):**
- GetEffectVolume / SetEffectVolume
- GetPositionVolume / SetPositionVolume
- GetTAGVolume / SetTAGVolume
- GetSoundMenuKeyVolume / SetSoundMenuKeyVolume
- GetMuteGameMusic / SetMuteGameMusic
- GetSoundMenuKeyMode / SetSoundMenuKeyMode (String)
- GetAuthor / SetAuthor (String)

**PAPYRUS Architecture:**
- **OSoundtracks_NativeScript.psc**: Hidden bridge script, declares native functions
- **OSoundtracks_McmScript.psc**: Extends SKI_ConfigBase with 3 pages (Settings, Advanced MCM, About)
- Uses local variables synchronized with INI via native calls

**INI Sections Comparison (What MCM Controls):**
- MCM controls: BaseVolume, MenuVolume, SpecificVolume, MasterVolumeEnabled, Startup, Visible, Backup, EffectVolume, PositionVolume, TAGVolume, SoundMenuKeyVolume, MuteGameMusicDuringOStim, SoundMenuKey, Author


---
## Acknowledgements

### Beta Testers

<table>
<tr>
<td><img src="Beta Testers/Эверг.png" width="100" height="100" alt="Эверг"></td>
<td><img src="Beta Testers/nobody.png" width="100" height="100" alt="nobody"></td>
<td><img src="Beta Testers/shadowman2777.png" width="100" height="100" alt="shadowman2777"></td>
<td><img src="Beta Testers/Knuxxx.png" width="100" height="100" alt="Knuxxx"></td>
<td><img src="Beta Testers/IAleX.png" width="100" height="100" alt="IAleX"></td>
<td><img src="Beta Testers/Cryshy.png" width="100" height="100" alt="Cryshy"></td>
<td><img src="Beta Testers/Lucas.png" width="100" height="100" alt="Lucas"></td>
<td><img src="Beta Testers/djdunha.png" width="100" height="100" alt="djdunha"></td>
<td><img src="Beta Testers/Edsley.png" width="100" height="100" alt="Edsley"></td>
</tr>
</table>

I also want to extend my deepest gratitude to the beta testers who generously dedicated their valuable time to help me program, test, and refine the mods. Without their voluntary contributions and collaborative spirit, achieving a stable public version would not have been possible. I truly appreciate how they not only assisted me but also supported the broader modding community selflessly. I love you all, guys **Эверг**, **nobody**, **shadowman2777**, **Knuxxx**, **IAleX**, **Cryshy**, **Lucas**, **djdunha**, and **Edsley** - your efforts have been invaluable, and I'm incredibly thankful for your dedication.

Special thanks to **Edsley** for providing helpful instructions and support for web-related aspects during development and testing.

Special thanks to **Knuxxx** for reaching out with critical feedback about the previous PS1-dependent system. Due to his older operating system, he experienced compatibility issues that prompted a fundamental overhaul of the sound mechanics. Thanks to his observations and genuine concern for the project's development, the OSoundtracks system has evolved significantly further than it would have otherwise. His contribution was pivotal in driving the project to new heights.

Special thanks to **Cryshy** for identifying a critical issue with handling large JSON files (over 7000 lines), which led to the implementation of enhanced parsing, memory limits, and validation in Version 1.7.0. Your sharp eye for bugs made the plugin far more robust!

Also thank you to **shadowman2777** for allowing one of your music mods to be ported to OSTIM and to work thanks to this music system. Thank you very much for your hard work.

Special thanks to **Эверг** for being the only person who worked directly with me during the testing phase. Thanks to his system in another language and his constant collaboration via Discord, it was possible to detect and solve the multilingual compatibility issues, leading to the creation of the new Dual-Path system and UTF-8 BOM support.

Special thanks to the SKSE community and CommonLibSSE developers for the foundation. This plugin is based on SKSE templates and my custom parsing logic for INI and JSON. Thanks for the tools that make modding possible.

---

# CommonLibSSE NG

Because this uses [CommonLibSSE NG](https://github.com/CharmedBaryon/CommonLibSSE-NG), it supports Skyrim SE, AE, GOG, and VR.

[CommonLibSSE NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) is a fork of the popular [powerof3 fork](https://github.com/powerof3/CommonLibSSE) of the _original_ `CommonLibSSE` library created by [Ryan McKenzie](https://github.com/Ryan-rsm-McKenzie) in [2018](https://github.com/Ryan-rsm-McKenzie/CommonLibSSE/commit/224773c424bdb8e36c761810cdff0fcfefda5f4a).

---

# Requirements

- [SKSE - Skyrim Script Extender](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [OStim Standalone](https://www.nexusmods.com/skyrimspecialedition/mods/98163)
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) - Required for MCM component
- [BASS Library](https://www.un4seen.com/) - Free for non-commercial use (included in the project)

**Note:** The BASS Library (bass.dll) is required by the Sound-Player component and is included in the project. For commercial licensing information, please visit the BASS website.
