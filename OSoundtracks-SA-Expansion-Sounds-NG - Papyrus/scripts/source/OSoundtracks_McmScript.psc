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
Int menuSoundMenuKeyMode
Int menuAuthor
Int buttonStandaloneMode
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
Bool currentMuteGameMusic
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
    Pages[0] = "Settings"
    Pages[1] = "Settings Menu"
    Pages[2] = "Advanced MCM"
    Pages[3] = "About"
    
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
        if page == "Settings"
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
        AddHeaderOption("Volume Settings")
        AddTextOption("Range: 0-200 (100 = normal, 100+ = amplification)", "", OPTION_FLAG_DISABLED)
        sliderBaseVolume = AddSliderOption("Base Volume", currentBaseVolume * 100.0, "{0}%")
        sliderMenuVolume = AddSliderOption("Menu Volume", currentMenuVolume * 100.0, "{0}%")
        sliderSpecificVolume = AddSliderOption("Specific Volume", currentSpecificVolume * 100.0, "{0}%")
        sliderEffectVolume = AddSliderOption("Effect Volume - (Magical, terrain...) WIP", currentEffectVolume * 100.0, "{0}%")
        AddEmptyOption()
        toggleMasterVolume = AddToggleOption("Master Volume Enabled", currentMasterVolumeEnabled)

        SetCursorPosition(1)
        AddHeaderOption("Additional Volumes")
        AddTextOption("Range: 0-200 (100 = normal, 100+ = amplification)", "", OPTION_FLAG_DISABLED)
        sliderPositionVolume = AddSliderOption("Position Volume (Breathing, physical sounds)", currentPositionVolume * 100.0, "{0}%")
        sliderTAGVolume = AddSliderOption("TAG Volume - (In the future) WIP", currentTAGVolume * 100.0, "{0}%")
        sliderSoundMenuKeyVolume = AddSliderOption("SoundMenuKey Volume", currentSoundMenuKeyVolume * 100.0, "{0}%")
        AddEmptyOption()
        AddHeaderOption("Feature Toggles")
        AddTextOption("Startup: meow sound at game load", "", OPTION_FLAG_DISABLED)
        toggleStartup = AddToggleOption("Startup Sound", currentStartup)
        AddTextOption("Notifications: song name top-left", "", OPTION_FLAG_DISABLED)
        toggleVisible = AddToggleOption("Top Notifications", currentVisible)
        AddTextOption("Backup: keep JSON Sure", "", OPTION_FLAG_DISABLED)
        toggleBackup = AddToggleOption("JSON Backup", currentBackup)

    elseIf page == "Settings Menu"
        currentMuteGameMusic = OSoundtracks_NativeScript.GetMuteGameMusic()
        currentSoundMenuKeyMode = OSoundtracks_NativeScript.GetSoundMenuKeyMode()
        currentAuthor = OSoundtracks_NativeScript.GetAuthor()

        SetCursorFillMode(TOP_TO_BOTTOM)
        
        SetCursorPosition(0)
        AddHeaderOption("Audio Settings")
        AddTextOption("Mute Skyrim music during OStim scenes - WIP", "", OPTION_FLAG_DISABLED)
        toggleMuteGameMusic = AddToggleOption("Mute Game Music (Not yet)", currentMuteGameMusic)
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

    elseIf page == "Advanced MCM"
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

    elseIf page == "About"
        SetCursorFillMode(TOP_TO_BOTTOM)
        SetCursorPosition(0)
        AddHeaderOption("OSoundtracks SA MCM")
        AddTextOption("Control your soundtrack settings.", "", OPTION_FLAG_DISABLED)
        AddTextOption("Version 16.3.0", "", OPTION_FLAG_DISABLED)
        AddTextOption("Created by John95AC", "", OPTION_FLAG_DISABLED)
        AddTextOption("Thank you all very much for your patience", "", OPTION_FLAG_DISABLED)
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
        currentMuteGameMusic = !currentMuteGameMusic
        OSoundtracks_NativeScript.SetMuteGameMusic(currentMuteGameMusic)
        SetToggleOptionValue(option, currentMuteGameMusic)
    elseIf option == buttonStandaloneMode
        Bool result = ShowMessage("Not yet implemented - coming soon - WIP", true)
        if result
            OSoundtracks_NativeScript.ActivateStandaloneMode()
        endIf
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