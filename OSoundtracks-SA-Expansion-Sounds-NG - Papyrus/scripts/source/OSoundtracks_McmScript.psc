scriptName OSoundtracks_McmScript extends SKI_ConfigBase

Int sliderBaseVolume
Int sliderMenuVolume
Int sliderSpecificVolume
Int sliderEffectVolume
Int sliderPositionVolume
Int sliderTAGVolume
Int sliderSoundMenuKeyVolume
Int toggleMasterVolume
Int toggleStartup
Int toggleVisible
Int toggleBackup
Int toggleMuteGameMusic
Int toggleMuteLegacy
Int toggleMuteOptionA
Int toggleMuteOptionB
Int menuSoundMenuKeyMode
Int menuAuthor
Int buttonStandaloneMode
Int buttonNexus
Int buttonPatreon
Int buttonKoFi
Int buttonWebAdvances
Int buttonOstrDocuments
Int buttonJaxonzMCM
Int buttonMenuMaid
Int buttonConsoleUtilSSE
Int useImage

Float currentBaseVolume
Float currentMenuVolume
Float currentSpecificVolume
Float currentEffectVolume
Float currentPositionVolume
Float currentTAGVolume
Float currentSoundMenuKeyVolume
Bool currentMasterVolumeEnabled
Bool currentStartup
Bool currentVisible
Bool currentBackup
Bool currentMuteGameMusicEnabled
String currentMuteGameMusicMethod
String currentSoundMenuKeyMode
String currentAuthor
String[] soundMenuKeyModes
String[] authorList

Event OnConfigInit()
    soundMenuKeyModes = new String[5]
    soundMenuKeyModes[0] = "false"
    soundMenuKeyModes[1] = "All_Order"
    soundMenuKeyModes[2] = "All_Random"
    soundMenuKeyModes[3] = "Author_Order"
    soundMenuKeyModes[4] = "Author_Random"
EndEvent

Event OnConfigOpen()
    Pages = new String[4]
    Pages[0] = "Settings Volume"
    Pages[1] = "Settings Menu background"
    Pages[2] = "Advanced MCM - WIP"
    Pages[3] = "About OSoundtracks SA"
    
    String authorListRaw = OSoundtracks_NativeScript.GetAuthorList()
    if authorListRaw == ""
        authorList = new String[1]
        authorList[0] = "[No Authors Found]"
    else
        authorList = StringUtil.Split(authorListRaw, "|")
    endIf
EndEvent

Event OnPageReset(String page)
    if page == ""
        DisplaySplashScreen()
    else
        UnloadCustomContent()
        if page == "Settings Volume"
            currentBaseVolume = OSoundtracks_NativeScript.GetBaseVolume()
            currentMenuVolume = OSoundtracks_NativeScript.GetMenuVolume()
        currentSpecificVolume = OSoundtracks_NativeScript.GetSpecificVolume()
        currentEffectVolume = OSoundtracks_NativeScript.GetEffectVolume()
        currentPositionVolume = OSoundtracks_NativeScript.GetPositionVolume()
        currentTAGVolume = OSoundtracks_NativeScript.GetTAGVolume()
        currentSoundMenuKeyVolume = OSoundtracks_NativeScript.GetSoundMenuKeyVolume()
        currentMasterVolumeEnabled = OSoundtracks_NativeScript.GetMasterVolumeEnabled()
        currentStartup = OSoundtracks_NativeScript.GetStartup()
        currentVisible = OSoundtracks_NativeScript.GetVisible()
        currentBackup = OSoundtracks_NativeScript.GetBackup()

        SetCursorFillMode(TOP_TO_BOTTOM)

        SetCursorPosition(0)
        AddHeaderOption("Volume Settings SoundKeys")
        AddTextOption("Range: 0-200 (100 = normal, 100+ = amplification)", "", OPTION_FLAG_DISABLED)
        sliderBaseVolume = AddSliderOption("Base Volume (scene music)", currentBaseVolume * 100.0, "{0}%")
        sliderMenuVolume = AddSliderOption("Menu Aliasing Volume (L)", currentMenuVolume * 100.0, "{0}%")
        sliderSpecificVolume = AddSliderOption("Specific Volume (animation music)", currentSpecificVolume * 100.0, "{0}%")
        sliderEffectVolume = AddSliderOption("Effect Volume - (Magical, terrain...) WIP", currentEffectVolume * 100.0, "{0}%")
        AddEmptyOption()
        toggleMasterVolume = AddToggleOption("Master Volume Enabled", currentMasterVolumeEnabled)

        SetCursorPosition(1)
        AddHeaderOption("Additional Volumes (new keys)")
        AddTextOption("Range: 0-200 (100 = normal, 100+ = amplification)", "", OPTION_FLAG_DISABLED)
        sliderPositionVolume = AddSliderOption("Position Volume (Breathing, physical sounds)", currentPositionVolume * 100.0, "{0}%")
        sliderTAGVolume = AddSliderOption("TAG Volume - (In the future) WIP", currentTAGVolume * 100.0, "{0}%")
        sliderSoundMenuKeyVolume = AddSliderOption("SoundMenuKey Volume (background music)", currentSoundMenuKeyVolume * 100.0, "{0}%")
        AddEmptyOption()
        AddHeaderOption("Feature Toggles")
        AddTextOption("Startup: meow sound at game load", "", OPTION_FLAG_DISABLED)
        toggleStartup = AddToggleOption("Startup Sound", currentStartup)
        AddTextOption("Notifications: song name top-left", "", OPTION_FLAG_DISABLED)
        toggleVisible = AddToggleOption("Top Notifications", currentVisible)
        AddTextOption("Backup: keep JSON Sure", "", OPTION_FLAG_DISABLED)
        toggleBackup = AddToggleOption("JSON Backup", currentBackup)

    elseIf page == "Settings Menu background"
        String iniValue = OSoundtracks_NativeScript.GetMuteGameMusicValue()
        
        if iniValue == "false"
            currentMuteGameMusicEnabled = false
            currentMuteGameMusicMethod = "MUSCombatBoss"
        else
            currentMuteGameMusicEnabled = true
            currentMuteGameMusicMethod = iniValue
        endIf
        
        currentSoundMenuKeyMode = OSoundtracks_NativeScript.GetSoundMenuKeyMode()
        currentAuthor = OSoundtracks_NativeScript.GetAuthor()

        SetCursorFillMode(TOP_TO_BOTTOM)
        
        SetCursorPosition(0)
        AddHeaderOption("Audio Settings")
        AddTextOption("Mute Skyrim music during OStim scenes", "", OPTION_FLAG_DISABLED)
        toggleMuteGameMusic = AddToggleOption("Enable Mute Game Music System", currentMuteGameMusicEnabled)
        
        Int methodFlags = OPTION_FLAG_NONE
        if !currentMuteGameMusicEnabled
            methodFlags = OPTION_FLAG_DISABLED
        endIf
        
        toggleMuteLegacy = AddToggleOption("  Legacy (Functional old)", currentMuteGameMusicMethod == "0010486c", methodFlags)
        toggleMuteOptionA = AddToggleOption("  Option A (Recommended)", currentMuteGameMusicMethod == "MUSCombatBoss", methodFlags)
        toggleMuteOptionB = AddToggleOption("  Option B (Experimental)", currentMuteGameMusicMethod == "MUSSpecialDeath", methodFlags)
        AddEmptyOption()
        AddHeaderOption("SoundMenuKey Mode")
        AddTextOption("Background music playback mode", "", OPTION_FLAG_DISABLED)
        Int currentModeIndex = GetSoundMenuKeyModeIndex(currentSoundMenuKeyMode)
        menuSoundMenuKeyMode = AddMenuOption("Playback Mode", soundMenuKeyModes[currentModeIndex])
        
        SetCursorPosition(1)
        AddHeaderOption("Author Selection")
        AddTextOption("Select background music by Author list", "", OPTION_FLAG_DISABLED)
        Int currentAuthorIndex = GetAuthorIndex(currentAuthor)
        menuAuthor = AddMenuOption("Selected Author (includes 7s sample)", authorList[currentAuthorIndex])
        AddEmptyOption()
        AddHeaderOption("Mode Descriptions")
        AddTextOption("false: Disable background music", "", OPTION_FLAG_DISABLED)
        AddTextOption("All_Order: All songs sequential", "", OPTION_FLAG_DISABLED)
        AddTextOption("All_Random: All songs random", "", OPTION_FLAG_DISABLED)
        AddTextOption("Author_Order: Author songs sequential", "", OPTION_FLAG_DISABLED)
        AddTextOption("Author_Random: Author songs random", "", OPTION_FLAG_DISABLED)

    elseIf page == "Advanced MCM - WIP"
        SetCursorFillMode(TOP_TO_BOTTOM)
        
        SetCursorPosition(0)
        AddHeaderOption("Standalone Mode")
        AddEmptyOption()
        buttonStandaloneMode = AddTextOption("Activate Standalone Mode - WIP", "")
        AddEmptyOption()
        AddTextOption("Launch external configuration tool", "", OPTION_FLAG_DISABLED)
        
        SetCursorPosition(1)
        AddHeaderOption("Launch Information")
        AddTextOption("Advanced mode", "", OPTION_FLAG_DISABLED)
        AddTextOption("Hub created to configure rules", "", OPTION_FLAG_DISABLED)
        AddTextOption("Created from Python", "", OPTION_FLAG_DISABLED)

    elseIf page == "About OSoundtracks SA"
        SetCursorFillMode(TOP_TO_BOTTOM)
        SetCursorPosition(0)
        AddHeaderOption("OSoundtracks SA MCM")
        AddTextOption("Control your soundtrack settings.", "", OPTION_FLAG_DISABLED)
        AddTextOption("Version 17.3.0", "", OPTION_FLAG_DISABLED)
        AddTextOption("I love playing Skyrim, and as a mod creator", "", OPTION_FLAG_DISABLED)
        AddTextOption("I don't touch MCMs much. This is due to the problems", "", OPTION_FLAG_DISABLED)
        AddTextOption("of updating in advanced playthroughs.", "", OPTION_FLAG_DISABLED)
        AddTextOption("But I will focus on making each update", "", OPTION_FLAG_DISABLED)
        AddTextOption("compatible with everything as always.", "", OPTION_FLAG_DISABLED)
        AddTextOption("It takes me time to configure everything in detail,", "", OPTION_FLAG_DISABLED)
        AddTextOption("Papyrus things.", "", OPTION_FLAG_DISABLED)
        AddTextOption("Thank you all very much for your patience", "", OPTION_FLAG_DISABLED)
        AddTextOption("Created by John95AC", "", OPTION_FLAG_DISABLED)

        SetCursorPosition(1)
        AddHeaderOption("Direct links")
        buttonNexus = AddTextOption("John95ac Nexus Mods", "Click to Open")
        buttonPatreon = AddTextOption("Patreon", "Click to Open")
        buttonKoFi = AddTextOption("Ko-Fi", "Click to Open")
        buttonWebAdvances = AddTextOption("John95ac Web Advances", "Click to Open")
        buttonOstrDocuments = AddTextOption("OSTR (O-Sound-tracks-Rules) documents - WIP", "Click")
        AddEmptyOption()
        AddHeaderOption("Recommended MCM links")
        buttonJaxonzMCM = AddTextOption("Jaxonz MCM Kicker SE", "Click to Open")
        buttonMenuMaid = AddTextOption("Menu Maid 2 - MCM manager", "Click to Open")
        buttonConsoleUtilSSE = AddTextOption("ConsoleUtilSSE", "Click to Open")
    endIf
    endIf
EndEvent

Int Function GetSoundMenuKeyModeIndex(String mode)
    Int i = 0
    While i < soundMenuKeyModes.Length
        if soundMenuKeyModes[i] == mode
            return i
        endIf
        i += 1
    EndWhile
    return 3
EndFunction

Int Function GetAuthorIndex(String author)
    Int i = 0
    While i < authorList.Length
        if authorList[i] == author
            return i
        endIf
        i += 1
    EndWhile
    return 0
EndFunction

Event OnOptionSliderOpen(Int option)
    if option == sliderBaseVolume
        SetSliderDialogStartValue(currentBaseVolume * 100.0)
        SetSliderDialogDefaultValue(90.0)
        SetSliderDialogRange(0.0, 200.0)
        SetSliderDialogInterval(1.0)
    elseIf option == sliderMenuVolume
        SetSliderDialogStartValue(currentMenuVolume * 100.0)
        SetSliderDialogDefaultValue(60.0)
        SetSliderDialogRange(0.0, 200.0)
        SetSliderDialogInterval(1.0)
    elseIf option == sliderSpecificVolume
        SetSliderDialogStartValue(currentSpecificVolume * 100.0)
        SetSliderDialogDefaultValue(70.0)
        SetSliderDialogRange(0.0, 200.0)
        SetSliderDialogInterval(1.0)
    elseIf option == sliderEffectVolume
        SetSliderDialogStartValue(currentEffectVolume * 100.0)
        SetSliderDialogDefaultValue(120.0)
        SetSliderDialogRange(0.0, 200.0)
        SetSliderDialogInterval(1.0)
    elseIf option == sliderPositionVolume
        SetSliderDialogStartValue(currentPositionVolume * 100.0)
        SetSliderDialogDefaultValue(120.0)
        SetSliderDialogRange(0.0, 200.0)
        SetSliderDialogInterval(1.0)
    elseIf option == sliderTAGVolume
        SetSliderDialogStartValue(currentTAGVolume * 100.0)
        SetSliderDialogDefaultValue(120.0)
        SetSliderDialogRange(0.0, 200.0)
        SetSliderDialogInterval(1.0)
    elseIf option == sliderSoundMenuKeyVolume
        SetSliderDialogStartValue(currentSoundMenuKeyVolume * 100.0)
        SetSliderDialogDefaultValue(20.0)
        SetSliderDialogRange(0.0, 200.0)
        SetSliderDialogInterval(1.0)
    endIf
EndEvent

Event OnOptionSliderAccept(Int option, Float value)
    Float iniValue = value / 100.0
    if option == sliderBaseVolume
        currentBaseVolume = iniValue
        OSoundtracks_NativeScript.SetBaseVolume(iniValue)
        SetSliderOptionValue(option, value, "{0}%")
    elseIf option == sliderMenuVolume
        currentMenuVolume = iniValue
        OSoundtracks_NativeScript.SetMenuVolume(iniValue)
        SetSliderOptionValue(option, value, "{0}%")
    elseIf option == sliderSpecificVolume
        currentSpecificVolume = iniValue
        OSoundtracks_NativeScript.SetSpecificVolume(iniValue)
        SetSliderOptionValue(option, value, "{0}%")
    elseIf option == sliderEffectVolume
        currentEffectVolume = iniValue
        OSoundtracks_NativeScript.SetEffectVolume(iniValue)
        SetSliderOptionValue(option, value, "{0}%")
    elseIf option == sliderPositionVolume
        currentPositionVolume = iniValue
        OSoundtracks_NativeScript.SetPositionVolume(iniValue)
        SetSliderOptionValue(option, value, "{0}%")
    elseIf option == sliderTAGVolume
        currentTAGVolume = iniValue
        OSoundtracks_NativeScript.SetTAGVolume(iniValue)
        SetSliderOptionValue(option, value, "{0}%")
    elseIf option == sliderSoundMenuKeyVolume
        currentSoundMenuKeyVolume = iniValue
        OSoundtracks_NativeScript.SetSoundMenuKeyVolume(iniValue)
        SetSliderOptionValue(option, value, "{0}%")
    endIf
EndEvent

Event OnOptionMenuOpen(Int option)
    if option == menuSoundMenuKeyMode
        SetMenuDialogStartIndex(GetSoundMenuKeyModeIndex(currentSoundMenuKeyMode))
        SetMenuDialogDefaultIndex(3)
        SetMenuDialogOptions(soundMenuKeyModes)
    elseIf option == menuAuthor
        SetMenuDialogStartIndex(GetAuthorIndex(currentAuthor))
        SetMenuDialogDefaultIndex(0)
        SetMenuDialogOptions(authorList)
    endIf
EndEvent

Event OnOptionMenuAccept(Int option, Int index)
    if option == menuSoundMenuKeyMode
        currentSoundMenuKeyMode = soundMenuKeyModes[index]
        OSoundtracks_NativeScript.SetSoundMenuKeyMode(currentSoundMenuKeyMode)
        SetMenuOptionValue(option, soundMenuKeyModes[index])
    elseIf option == menuAuthor
        if authorList[index] != "[No Authors Found]"
            currentAuthor = authorList[index]
            OSoundtracks_NativeScript.SetAuthor(currentAuthor)
            SetMenuOptionValue(option, authorList[index])
        endIf
    endIf
EndEvent

Event OnOptionSelect(Int option)
    if option == toggleMasterVolume
        currentMasterVolumeEnabled = !currentMasterVolumeEnabled
        OSoundtracks_NativeScript.SetMasterVolumeEnabled(currentMasterVolumeEnabled)
        SetToggleOptionValue(option, currentMasterVolumeEnabled)
    elseIf option == toggleStartup
        currentStartup = !currentStartup
        OSoundtracks_NativeScript.SetStartup(currentStartup)
        SetToggleOptionValue(option, currentStartup)
    elseIf option == toggleVisible
        currentVisible = !currentVisible
        OSoundtracks_NativeScript.SetVisible(currentVisible)
        SetToggleOptionValue(option, currentVisible)
    elseIf option == toggleBackup
        currentBackup = !currentBackup
        OSoundtracks_NativeScript.SetBackup(currentBackup)
        SetToggleOptionValue(option, currentBackup)
    elseIf option == toggleMuteGameMusic
        currentMuteGameMusicEnabled = !currentMuteGameMusicEnabled
        
        if currentMuteGameMusicEnabled
            OSoundtracks_NativeScript.SetMuteGameMusicValue(currentMuteGameMusicMethod)
        else
            OSoundtracks_NativeScript.SetMuteGameMusicValue("false")
        endIf
        
        SetToggleOptionValue(option, currentMuteGameMusicEnabled)
        
        if currentMuteGameMusicEnabled
            SetOptionFlags(toggleMuteLegacy, OPTION_FLAG_NONE)
            SetOptionFlags(toggleMuteOptionA, OPTION_FLAG_NONE)
            SetOptionFlags(toggleMuteOptionB, OPTION_FLAG_NONE)
        else
            SetOptionFlags(toggleMuteLegacy, OPTION_FLAG_DISABLED)
            SetOptionFlags(toggleMuteOptionA, OPTION_FLAG_DISABLED)
            SetOptionFlags(toggleMuteOptionB, OPTION_FLAG_DISABLED)
        endIf
    elseIf option == toggleMuteLegacy
        currentMuteGameMusicMethod = "0010486c"
        OSoundtracks_NativeScript.SetMuteGameMusicValue("0010486c")
        SetToggleOptionValue(toggleMuteLegacy, true)
        SetToggleOptionValue(toggleMuteOptionA, false)
        SetToggleOptionValue(toggleMuteOptionB, false)
    elseIf option == toggleMuteOptionA
        currentMuteGameMusicMethod = "MUSCombatBoss"
        OSoundtracks_NativeScript.SetMuteGameMusicValue("MUSCombatBoss")
        SetToggleOptionValue(toggleMuteLegacy, false)
        SetToggleOptionValue(toggleMuteOptionA, true)
        SetToggleOptionValue(toggleMuteOptionB, false)
    elseIf option == toggleMuteOptionB
        currentMuteGameMusicMethod = "MUSSpecialDeath"
        OSoundtracks_NativeScript.SetMuteGameMusicValue("MUSSpecialDeath")
        SetToggleOptionValue(toggleMuteLegacy, false)
        SetToggleOptionValue(toggleMuteOptionA, false)
        SetToggleOptionValue(toggleMuteOptionB, true)
    elseIf option == buttonStandaloneMode
        Bool result = ShowMessage("Not yet implemented - coming soon - WIP", true)
        if result
            OSoundtracks_NativeScript.ActivateStandaloneMode()
        endIf
    elseIf option == buttonNexus
        OSoundtracks_NativeScript.OpenURL("https://www.nexusmods.com/profile/John1995ac")
    elseIf option == buttonPatreon
        OSoundtracks_NativeScript.OpenURL("https://www.patreon.com/c/John95ac")
    elseIf option == buttonKoFi
        OSoundtracks_NativeScript.OpenURL("https://ko-fi.com/john95ac")
    elseIf option == buttonWebAdvances
        OSoundtracks_NativeScript.OpenURL("https://john95ac.github.io/website-documents-John95AC/NEWS_MCM/index.html")
    elseIf option == buttonOstrDocuments
        Bool result = ShowMessage("OSTR (O-Sound-tracks-Rules) documents are currently under construction. Check back later for updates!", true)
    elseIf option == buttonJaxonzMCM
        OSoundtracks_NativeScript.OpenURL("https://www.nexusmods.com/skyrimspecialedition/mods/36801")
    elseIf option == buttonMenuMaid
        OSoundtracks_NativeScript.OpenURL("https://www.nexusmods.com/skyrimspecialedition/mods/67556")
    elseIf option == buttonConsoleUtilSSE
        OSoundtracks_NativeScript.OpenURL("https://www.nexusmods.com/skyrimspecialedition/mods/24858")
    endIf
EndEvent

Event OnOptionHighlight(Int option)
    if option == sliderBaseVolume
        SetInfoText("Control of scene music volume. This is the same as specific volume, it controls the volume of music in scenes like dances and more. It can be amplified to double.")
    elseIf option == sliderMenuVolume
        SetInfoText("Control of the alignment tab volume. Pressing L opens this alignment table. I added music to it, in the future improving the tracks in this configuration.")
    elseIf option == sliderSpecificVolume
        SetInfoText("Controls the volume of some sounds or background music. It's something from the old version, but it's similar to the base volume.")
    elseIf option == sliderEffectVolume
        SetInfoText("Volume for magical, terrain, and environmental effect sounds. Work in progress, may not be fully functional yet.")
    elseIf option == sliderPositionVolume
        SetInfoText("Volume for positional sounds like breathing and physical effects.")
    elseIf option == sliderTAGVolume
        SetInfoText("Volume for TAG Ostim animations sounds. Work in progress, may not be fully functional yet.")
    elseIf option == sliderSoundMenuKeyVolume
        SetInfoText("Volume for background music played during OStim scenes using SoundMenuKey.")
    elseIf option == toggleMasterVolume
        SetInfoText("Enable or disable the master volume control for all sounds.")
    elseIf option == toggleStartup
        SetInfoText("Play a startup sound (meow) when the game loads. Yes, from here you can deactivate it.")
    elseIf option == toggleVisible
        SetInfoText("Show song name notifications in the top-left corner of the screen.")
    elseIf option == toggleBackup
        SetInfoText("Keep a backup copy of the JSON configuration file. Preferably keep this active.")
    elseIf option == toggleMuteGameMusic
        SetInfoText("Mute Skyrim's built-in music during OStim scenes to avoid audio conflicts. This was very complicated to integrate because there is no documentation to mute the game's settings in any of its versions without using Papyrus scripts, so I tried multiple methods to affect a game music mute in a clear and lag-free way.")
    elseIf option == toggleMuteLegacy
        SetInfoText("Legacy mute method using code 0010486c, which I discovered is one of the few that, via commands - addmusic 0010486c and - removemusic 0010486c. These commands are applied in seconds when starting a scene and when exiting a scene; this makes the game switch music tracks, but I immediately apply a mute to all tracks, and after applying removemusic, I clean the remaining music cache smoothly without third-party intervention. It's the first method I tried on my PC and it works, with the drawback of hearing a bit of other music, about 75ms of another music track. I consider it an old version and don't use it in my playthrough.")
    elseIf option == toggleMuteOptionA
        SetInfoText("Much appreciated the legacy, but I use the command MUSCombatBoss which in my playthrough results in having no sound track: - addmusic MUSCombatBoss and - removemusic MUSCombatBoss. I perform the same sequencing and apply cache cleaning. This took me a lot of research time, I documented everything on GITHUB. It's the method I currently use, at least it works for me without filtered sounds.")
    elseIf option == toggleMuteOptionB
        SetInfoText("MUSSpecialDeath - Method similar to the previous one, but uses another uncommon audio track, and it does filter strange sounds, but it's functional; I don't recommend it.")
    elseIf option == menuSoundMenuKeyMode
        SetInfoText("Select the playback method for background music. You can mangle/turn off the background music. You can choose all the music from all the lists in order or random, as you can also choose your favorite list creator and listen to their music in order or random.")
    elseIf option == menuAuthor
        SetInfoText("Selection of the list creator you prefer. Once you choose one, you'll listen to 7 seconds of one of their songs to see if you like it or not. Over time I'll create more lists, but it's very simple, it's the same way I create SPID but for music.")
    elseIf option == buttonStandaloneMode
        SetInfoText("Launch the external Python configuration tool It's a kind of colorful Hub with advanced configuration to easily create your personalized music lists. Configure everything from a single site interactively. I already use it in closed betas, but it's not yet available to the public. Work in progress.")
    elseIf option == buttonNexus
        SetInfoText("Open John95ac Nexus Mods page so you can see his mods in your web browser. I have many mods there and more all over, there are music packs and effects managed by the sound mod. Once pressed, it takes a few seconds to open.")
    elseIf option == buttonPatreon
        SetInfoText("With this button, you can go to my Patreon where I publish mod advancements, beta links via Drive, host meetups, tutorials. Many thanks to everyone who sponsors me; each day I'm closer to finishing the first prototype of a robot connected to Skyrim, it is one of my goals. I hope to have the first recorded progress in video by mid-2026. Many thanks to everyone.")
    elseIf option == buttonKoFi
        SetInfoText("My Ko-Fi, many thanks to everyone who supports me for my mods, translations, and animations. Many thanks whenever you donate; I drink coffee, it helps a lot to continue with the mods and overcome the technical challenges I face. Many thanks to everyone.")
    elseIf option == buttonWebAdvances
        SetInfoText("It's my website where I publish my progress, lists of my mods on different platforms, also post acknowledgments, my beta testers, personal thoughts, publish what I will do in a month or a week so they know what I'm working on, photos, tips, a bit of everything. It's still a WIP but I'm gradually building it and improving it.")
    elseIf option == buttonOstrDocuments
        SetInfoText("OSTR (O-Sound-tracks-Rules) documentation will be the same as the one I created for OBody PDA, but for Ostim music because the .ini rules can be generated with a simple generator. It will take me a few weeks since it's January and I'm on vacation and I have to go out with my family from time to time, but I'll be ready soon.")
    elseIf option == buttonJaxonzMCM
        SetInfoText("Jaxonz MCM Kicker SE: this mod is great for updating the MCM mod list when you install a mod that has an MCM but it doesn't appear in the list. It could be due to script lag since the MCM system is tied to Skyrim's engine, or from exceeding the MCM limit of more than 120. This mod helps in the first case; it makes the MCM list update in each playthrough, forcing stuck MCMs to appear. It does what it can but no miracles. Remember to keep your game with optimized Papyrus scripts; many MCM issues are due to poorly optimized mods or the game itself.")
    elseIf option == buttonMenuMaid
        SetInfoText("Menu Maid 2 - MCM manager, my favorite. This mod allows surpassing the theoretical limit of 120 MCM to a number I haven't reached yet, but I have many mods and easily exceed 200 MCM. With this mod I can configure and customize the MCM, also in the rare case that you uninstall a mod with MCM and it causes a CTD when entering the playthrough without that mod. My advice is to deactivate the Menu Maid mod, enter the playthrough, save, then exit, activate Menu Maid again, and re-enter, and done, the problem is solved.")
    elseIf option == buttonConsoleUtilSSE
        SetInfoText("ConsoleUtilSSE: very good mod, helps DLLs like this one to properly interface with the console to send commands directly using the game engine. My mod only uses it for background music mute, but I will use it more, and it is compatible with all Skyrim SE versions.")
    endIf
EndEvent

Function DisplaySplashScreen()
    if useImage == 0
        LoadCustomContent("OSoundtracks/Image1.dds", 0.000000, 0.000000)
        useImage = 1
    elseIf useImage == 1
        LoadCustomContent("OSoundtracks/Image2.dds", 0.000000, 0.000000)
        useImage = 2
    else
        LoadCustomContent("OSoundtracks/Image3.dds", 0.000000, 0.000000)
        useImage = 0
    endIf
EndFunction