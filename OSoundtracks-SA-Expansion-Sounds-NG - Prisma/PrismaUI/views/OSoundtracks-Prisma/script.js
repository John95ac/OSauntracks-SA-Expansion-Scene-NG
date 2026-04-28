// STATE MANAGERS
let stats = { hp: 100, mp: 100, st: 100 };

let playerState = {
    isPlaying: false,
    isPaused: false,
    currentTrack: '',
    currentSeconds: 0,
    totalSeconds: 0,
    progressInterval: null,
    imageNotFoundLogged: {}
};

let nowPlayingState = {
    liveNowPlayingMode: 'timed',
    liveNowPlayingSeconds: 10,
    liveNowPlayingGif: 'viaductk-music-7683.gif'
};
let liveVisibilityTimer = null;
let lastNowPlayingTrack = '';
// UI COMPONENTS
let lastLoadedAuthor = null;
let isMusicPaused       = false;
let gifIsPaused         = false;
const ALBUM_IMAGE_EXTS  = ['png', 'jpg', 'jpeg', 'webp', 'gif'];
let cfg = {
    firstKey: 0x2A,
    secondKey: 0x1E,
    singleMode: false,
    volume: 70,
    muteHubSound: false,
    hubSound: "miau-PDA.wav",
    posX: 50,
    posY: 50,
    hubSize: 100,
    menusSubmenuVisible: false,
    gifVisible: false,
    gifPosX: 60,
    gifPosY: 50,
    gifZoom: 100,
    gifWidth: 180,
    gifHeight: 180,
    videoVisible: false,
    videoPosX: 70,
    videoPosY: 50,
    videoWidth: 320,
    videoHeight: 240,
    logsVisible: false,
    logsPosX: 50,
    logsPosY: 50,
    logsWidth: 400,
    logsHeight: 300,
    logsFontSize: 10,
    logsAutoRefresh: true,
    liveVisible: false,
    liveAutoStart: false,
    livePosX: 80,
    livePosY: 50,
    liveSize: 100,
    bootPosX: 10,
    bootPosY: 10,

    globalAlpha: 97,
    globalColor: 'default',
    liveBackground: 'default',
    menusSize: 100,
    itemsSize: 100,
    toastSize: 14
};
let dragging = false;
let logsAutoRefreshInterval = null;

let jsInspectorLog = [];
const INSPECTOR_LOG_MAX_LINES = 200;

function addToInspectorLog(level, message) {
    const timestamp = new Date().toLocaleTimeString();
    const entry = '[' + timestamp + '] [' + level + '] ' + message;
    jsInspectorLog.push(entry);
    if (jsInspectorLog.length > INSPECTOR_LOG_MAX_LINES) {
        jsInspectorLog.shift();
    }
}

function copyLogsToClipboard() {
    const content = document.getElementById('logsContent');
    if (content) {
        const text = content.innerText || content.textContent;
        navigator.clipboard.writeText(text).then(function() {
            const btn = document.getElementById('logsCopy');
            if (btn) {
                const original = btn.textContent;
                btn.textContent = '✓ Copied!';
                setTimeout(function() { btn.textContent = original; }, 1000);
            }
        }).catch(function(err) {
            console.error('[Inspector] Failed to copy:', err);
            addToInspectorLog('ERROR', 'Failed to copy to clipboard');
        });
    }
}

function getInspectorLogContent() {
    if (jsInspectorLog.length === 0) {
        return '<span class="log-info">[Inspector] Ready - waiting for events...</span>';
    }
    return jsInspectorLog.map(function(entry) {
        if (entry.includes('[ERROR]')) {
            return '<span class="log-error">' + entry + '</span>';
        } else if (entry.includes('[WARN]')) {
            return '<span class="log-warning">' + entry + '</span>';
        } else {
            return '<span class="log-info">' + entry + '</span>';
        }
    }).join('\n');
}

function resolveAlbumImage(authorName, callback) {
    console.log('[BLOQUE 2] resolveAlbumImage: authorName=' + authorName);
    
    if (!authorName) {
        console.log('[BLOQUE 2] resolveAlbumImage: authorName is null');
        callback(null);
        return;
    }
    
    tryImageExtension(authorName, 0, callback);
}

function tryImageExtension(authorName, extIndex, callback) {
    if (extIndex >= ALBUM_IMAGE_EXTS.length) {
        console.log('[BLOQUE 2] tryImageExtension: All extensions failed for ' + authorName);
        callback(null);
        return;
    }
    
    const ext = ALBUM_IMAGE_EXTS[extIndex];
    const path = 'Assets/Images/' + authorName + '.' + ext;
    console.log('[BLOQUE 2] tryImageExtension: Attempting ' + path);
    
    const coverImg = document.getElementById('playerCoverImg');
    
    if (!coverImg) {
        console.log('[BLOQUE 2] tryImageExtension: ERROR - #playerCoverImg not found in DOM!');
        callback(null);
        return;
    }
    
    coverImg.onerror = function() {
        console.log('[BLOQUE 2] tryImageExtension: FAILED ' + path);
        coverImg.onerror = null;
        tryImageExtension(authorName, extIndex + 1, callback);
    };
    
    coverImg.onload = function() {
        console.log('[BLOQUE 2] tryImageExtension: SUCCESS ' + path);
        coverImg.onerror = null;
        coverImg.style.display = 'block';
        
        const placeholder = document.querySelector('.player-cover-placeholder');
        if (placeholder) placeholder.style.display = 'none';
        
        callback(path);
    };
    
    coverImg.src = path;
}

function parseDuration(durationStr) {
    if (!durationStr || typeof durationStr !== 'string') return 0;
    const parts = durationStr.split(':');
    if (parts.length !== 2) return 0;
    const mins = parseInt(parts[0], 10) || 0;
    const secs = parseInt(parts[1], 10) || 0;
    return mins * 60 + secs;
}

function formatTime(seconds) {
    if (typeof seconds !== 'number' || seconds < 0) return '00:00';
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return mins.toString().padStart(2, '0') + ':' + secs.toString().padStart(2, '0');
}

let trackInfoCache = {};
let iniLoaded = false;

function findTrackInfoInIni(trackName, system) {
    const cacheKey = system + '_' + trackName;
    if (trackInfoCache[cacheKey]) {
        return trackInfoCache[cacheKey];
    }
    
    return null;
}

async function loadTrackListIni() {
    try {
        const response = await fetch('Assets/ini/OSoundtracks-SA-Prisma-List.ini');
        if (!response.ok) {
            console.warn('[BLOQUE 2] Could not load INI file');
            return false;
        }
        const text = await response.text();
        parseIniText(text);
        iniLoaded = true;
        console.log('[BLOQUE 2] INI loaded successfully');
        return true;
    } catch (e) {
        console.warn('[BLOQUE 2] Error loading INI:', e);
        return false;
    }
}

function parseIniText(iniText) {
    if (!iniText) return;
    
    const lines = iniText.split('\n');
    let currentSection = '';
    
    for (let line of lines) {
        line = line.trim();
        
        if (!line || line.startsWith(';') || line.startsWith('#')) continue;
        
        if (line.startsWith('[') && line.endsWith(']')) {
            currentSection = line.slice(1, -1);
            continue;
        }
        
        const eqIndex = line.indexOf('=');
        if (eqIndex === -1) continue;
        
        const trackName = line.substring(0, eqIndex).trim();
        const valuePart = line.substring(eqIndex + 1).trim();
        const parts = valuePart.split('|').map(p => p.trim());
        
        if (parts.length >= 4) {
            const info = {
                duration: parts[0],
                format: parts[1],
                imageFile: parts[2],
                album: parts[3]
            };
            
            const cacheKey = currentSection + '_' + trackName;
            trackInfoCache[cacheKey] = info;
        }
    }
}

function setPlayerCover(imageUrl) {
    const coverImg = document.getElementById('playerCoverImg');
    const placeholder = document.querySelector('.player-cover-placeholder');
    
    if (imageUrl && coverImg) {
        coverImg.src = imageUrl;
        coverImg.style.display = 'block';
        if (placeholder) placeholder.style.display = 'none';
    } else if (coverImg) {
        coverImg.style.display = 'none';
        if (placeholder) placeholder.style.display = 'flex';
    }
}

function startProgressSimulation(trackName, duration) {
    if (playerState.progressInterval) {
        clearInterval(playerState.progressInterval);
    }
    
    playerState.currentTrack = trackName;
    playerState.totalSeconds = parseDuration(duration);
    playerState.currentSeconds = 0;
    playerState.isPlaying = true;
    playerState.isPaused = false;
    
    if (!playerState.totalSeconds || playerState.totalSeconds <= 0) {
        console.warn('[BLOQUE 2] Invalid duration, cannot start progress simulation');
        playerState.isPlaying = false;
        return;
    }
    
    const totalTimeEl = document.getElementById('playerTotalTime');
    const currentTimeEl = document.getElementById('playerCurrentTime');
    const progressFill = document.getElementById('playerProgressFill');
    
    if (totalTimeEl) totalTimeEl.textContent = duration;
    if (currentTimeEl) currentTimeEl.textContent = '00:00';
    if (progressFill) progressFill.style.width = '0%';
    
    playerState.progressInterval = setInterval(function() {
        if (playerState.isPaused) return;
        
        playerState.currentSeconds++;
        
        const currentTime = formatTime(playerState.currentSeconds);
        const progress = (playerState.currentSeconds / playerState.totalSeconds) * 100;
        
        if (currentTimeEl) currentTimeEl.textContent = currentTime;
        if (progressFill) progressFill.style.width = Math.min(progress, 100) + '%';
        
        if (playerState.currentSeconds >= playerState.totalSeconds) {
            clearInterval(playerState.progressInterval);
            playerState.progressInterval = null;
        }
    }, 1000);
}

function pauseProgressSimulation() {
    playerState.isPaused = true;
}

function resumeProgressSimulation() {
    playerState.isPaused = false;
}

function stopProgressSimulation() {
    if (playerState.progressInterval) {
        clearInterval(playerState.progressInterval);
        playerState.progressInterval = null;
    }
    
    playerState.isPlaying = false;
    playerState.isPaused = false;
    playerState.currentTrack = '';
    playerState.currentSeconds = 0;
    playerState.totalSeconds = 0;
    
    const currentTimeEl = document.getElementById('playerCurrentTime');
    const totalTimeEl = document.getElementById('playerTotalTime');
    const progressFill = document.getElementById('playerProgressFill');
    
    if (currentTimeEl) currentTimeEl.textContent = '00:00';
    if (totalTimeEl) totalTimeEl.textContent = '00:00';
    if (progressFill) progressFill.style.width = '0%';
}

function findAndSetAlbumImage(trackName, authorName, system) {
    resolveAlbumImage(authorName, function(imagePath) {
        if (imagePath) {
            setPlayerCover(imagePath);
            return;
        }
        
        const cacheKey = system + '_' + trackName;
        const trackInfo = trackInfoCache[cacheKey];
        
        if (trackInfo && trackInfo.imageFile && trackInfo.imageFile !== 'SPECIAL_NO_IMAGE') {
            resolveAlbumImage(trackInfo.imageFile, function(imagePath) {
                if (imagePath) {
                    setPlayerCover(imagePath);
                    return;
                }
                tryImageByTrackName(trackName);
            });
        } else {
            tryImageByTrackName(trackName);
        }
    });
}

function tryImageByTrackName(trackName, extIndex) {
    if (extIndex === undefined) extIndex = 0;
    
    if (extIndex >= ALBUM_IMAGE_EXTS.length) {
        console.log('[BLOQUE 2] tryImageByTrackName: All extensions failed for track ' + trackName);
        setPlayerCover(null);
        
        if (!playerState.imageNotFoundLogged[trackName]) {
            console.log('[BLOQUE 2] Image not found for track:', trackName);
            playerState.imageNotFoundLogged[trackName] = true;
        }
        return;
    }
    
    const ext = ALBUM_IMAGE_EXTS[extIndex];
    const path = 'Assets/Images/' + trackName + '.' + ext;
    console.log('[BLOQUE 2] tryImageByTrackName: Attempting ' + path);
    
    const coverImg = document.getElementById('playerCoverImg');
    
    if (!coverImg) {
        console.log('[BLOQUE 2] tryImageByTrackName: ERROR - #playerCoverImg not found in DOM!');
        return;
    }
    
    coverImg.onerror = function() {
        console.log('[BLOQUE 2] tryImageByTrackName: FAILED ' + path);
        coverImg.onerror = null;
        tryImageByTrackName(trackName, extIndex + 1);
    };
    
    coverImg.onload = function() {
        console.log('[BLOQUE 2] tryImageByTrackName: SUCCESS ' + path);
        coverImg.onerror = null;
        coverImg.style.display = 'block';
        
        const placeholder = document.querySelector('.player-cover-placeholder');
        if (placeholder) placeholder.style.display = 'none';
    };
    
    coverImg.src = path;
}

function handleMarqueeText(element, text) {
    if (!element) return;
    
    element.classList.remove('marquee');
    element.textContent = text;
    
    const isOverflowing = element.scrollWidth > element.clientWidth;
    
    if (isOverflowing) {
        element.classList.add('marquee');
    }
}

function triggerLiveVisibility(trackName) {
    const mode = nowPlayingState.liveNowPlayingMode || 'disabled';
    if (mode === 'disabled') return;
    const isNewTrack = (trackName !== lastNowPlayingTrack);
    lastNowPlayingTrack = trackName;
    if (mode === 'always') {
        if (liveVisibilityTimer) { clearTimeout(liveVisibilityTimer); liveVisibilityTimer = null; }
        forcePanelLiveVisible(true);
        return;
    }
    if (mode === 'timed') {
        if (!isNewTrack) return;
        if (liveVisibilityTimer) clearTimeout(liveVisibilityTimer);
        forcePanelLiveVisible(true);
        const ms = (nowPlayingState.liveNowPlayingSeconds || 10) * 1000;
        liveVisibilityTimer = setTimeout(function() {
            forcePanelLiveVisible(false);
            liveVisibilityTimer = null;
        }, ms);
    }
}

function onMusicStopped() {
    lastNowPlayingTrack = '';
    if (liveVisibilityTimer) { clearTimeout(liveVisibilityTimer); liveVisibilityTimer = null; }
    forcePanelLiveVisible(false);
}

function forcePanelLiveVisible(show) {
    if (!isPersistentMode) return;
    
    var panel = document.getElementById('panelLive');
    if (!panel) return;
    
    if (show) {
        panel.style.visibility = 'visible';
    } else if (window._lastShouldHide) {
        panel.style.visibility = 'hidden';
    }
}

window.updateNowPlaying = function(data) {
    var d = (typeof data === 'string') ? JSON.parse(data) : data;
    var playerTitle = document.getElementById('playerSongTitle');
    var playerProgressFill = document.getElementById('playerProgressFill');
    var isPlaying = (d.status === 'playing' || d.status === 'paused' || d.status > 0);
    
    if (isPlaying && d.track && d.track !== '') {
        if (playerTitle) {
            handleMarqueeText(playerTitle, d.track);
        }
        
        if (d.status === 'paused') {
            freezeGif();
            pauseProgressSimulation();
        } else if (d.status === 'playing') {
            unfreezeGif();
            if (playerState.isPlaying && playerState.currentTrack === d.track) {
                resumeProgressSimulation();
            } else {
                const cacheKey = d.system + '_' + d.track;
                const trackInfo = trackInfoCache[cacheKey];
                
                if (trackInfo && trackInfo.duration) {
                    startProgressSimulation(d.track, trackInfo.duration);
                }
            }
        }
        
        if (d.author && d.system && d.author !== lastLoadedAuthor) {
            findAndSetAlbumImage(d.track, d.author, d.system);
            lastLoadedAuthor = d.author;
        }
        
        if (!playerState.isPlaying) {
            if (playerProgressFill) playerProgressFill.style.width = Math.round((d.progress || 0) * 100) + '%';
        }
        triggerLiveVisibility(d.track);
    } else {
        stopProgressSimulation();
        setPlayerCover(null);
        onMusicStopped();
    }
};

var gifBtn = null;
var gifFreeze = null;

function freezeGif() {
    if (!gifBtn) return;
    if (gifBtn.src.endsWith('.png')) return;
    var gifSrc = gifBtn.src;
    var pngSrc = gifSrc.replace('.gif', '.png');
    gifBtn.src = pngSrc;
    var playIcon = document.getElementById('gifPlayIcon');
    if (playIcon) playIcon.textContent = '▶';
}

function unfreezeGif() {
    if (!gifBtn) return;
    if (gifBtn.src.endsWith('.gif')) return;
    var pngSrc = gifBtn.src;
    var gifSrc = pngSrc.replace('.png', '.gif');
    gifBtn.src = gifSrc;
    var playIcon = document.getElementById('gifPlayIcon');
    if (playIcon) playIcon.textContent = '⏸';
}

// ENGINE INIT
document.addEventListener('DOMContentLoaded', function() {

    loadTrackListIni();

    gifBtn = document.getElementById('playerDiscGif');
    var gifOverlay = document.getElementById('gifOverlay');
    var gifPlayIcon = document.getElementById('gifPlayIcon');
    
    if (gifPlayIcon) gifPlayIcon.textContent = '▶';
    
    if (gifOverlay) {
        gifOverlay.addEventListener('click', function(e) {
            e.preventDefault();
            e.stopPropagation();
            if (gifIsPaused) {
                unfreezeGif();
                gifIsPaused = false;
                if (isMusicPaused && window.onResumeMusic) {
                    window.onResumeMusic('');
                    isMusicPaused = false;
                }
            } else {
                freezeGif();
                gifIsPaused = true;
                if (!isMusicPaused && window.onPauseMusic) {
                    window.onPauseMusic('');
                    isMusicPaused = true;
                }
            }
        });
    }

    document.querySelectorAll('.gif-option').forEach(function(opt) {
        opt.addEventListener('click', function() {
            var gifName = this.dataset.gif;
            document.querySelectorAll('.gif-option').forEach(function(o) {
                o.classList.remove('active');
            });
            this.classList.add('active');
            var disc = document.getElementById('playerDiscGif');
            if (disc) disc.src = 'Assets/Images/' + gifName;
            nowPlayingState.liveNowPlayingGif = gifName;
            saveNowPlayingConfig();
        });
    });

    document.querySelectorAll('input[name="nowPlayingMode"]').forEach(function(r) {
        r.addEventListener('change', function() {
            nowPlayingState.liveNowPlayingMode = this.value;
            var row = document.getElementById('timedSecondsRow');
            if (row) row.style.display = (this.value === 'timed') ? 'flex' : 'none';
            saveNowPlayingConfig();
        });
    });

    var secInput = document.getElementById('nowPlayingSeconds');
    if (secInput) {
        secInput.addEventListener('change', function() {
            nowPlayingState.liveNowPlayingSeconds =
                Math.max(3, Math.min(120, parseInt(this.value) || 10));
            saveNowPlayingConfig();
        });
    }

});

function saveNowPlayingConfig() {
    if (window.onSaveNowPlayingConfig) {
        window.onSaveNowPlayingConfig(JSON.stringify(nowPlayingState));
    }
}

function startPanelDrag(e, draggingKey, posXKey, posYKey, resizingKey) {
    if (resizingKey && window[resizingKey]) return;
    window[draggingKey] = true;
    const centerX = window.innerWidth * (cfg[posXKey] / 100);
    const centerY = window.innerHeight * (cfg[posYKey] / 100);
    window[draggingKey + 'OffsetX'] = e.clientX - centerX;
    window[draggingKey + 'OffsetY'] = e.clientY - centerY;
    e.preventDefault();
    e.stopPropagation();
}

function applyPanelDrag(e, panelId, draggingKey, posXKey, posYKey) {
    if (!window[draggingKey]) return;
    const panel = document.getElementById(panelId);
    if (!panel) return;

    const rect = panel.getBoundingClientRect();
    const panelWidth = rect.width;
    const panelHeight = rect.height;

    const newCenterX = e.clientX - window[draggingKey + 'OffsetX'];
    const newCenterY = e.clientY - window[draggingKey + 'OffsetY'];

    let posX = (newCenterX / window.innerWidth) * 100;
    let posY = (newCenterY / window.innerHeight) * 100;

    const halfWidthPct = (panelWidth / window.innerWidth) * 50;
    const halfHeightPct = (panelHeight / window.innerHeight) * 50;

    const minY = halfHeightPct;
    const maxY = 100 - halfHeightPct;
    const minX = halfWidthPct;
    const maxX = 100 - halfWidthPct;

    cfg[posXKey] = Math.max(minX, Math.min(maxX, posX));
    cfg[posYKey] = Math.max(minY, Math.min(maxY, posY));

    panel.style.left = cfg[posXKey] + '%';
    panel.style.top = cfg[posYKey] + '%';
}

function setVol(v) {
    cfg.volume = parseInt(v);
    document.getElementById('vol-txt').textContent = v;
    saveAllConfig();
}

function changeSize(delta) {
    cfg.hubSize = Math.max(50, Math.min(200, cfg.hubSize + delta));
    document.getElementById('size-txt').textContent = cfg.hubSize + '%';
    applySize();
    saveAllConfig();
    show('Size: ' + cfg.hubSize + '%');
}

function applySize() {
    const hub = document.getElementById('hub');
    const scale = cfg.hubSize / 100;
    hub.style.left = cfg.posX + '%';
    hub.style.top = cfg.posY + '%';
    hub.style.transform = 'translate(-50%, -50%) scale(' + scale + ')';
}

function applyPos() {
    const hub = document.getElementById('hub');
    hub.style.left = cfg.posX + '%';
    hub.style.top = cfg.posY + '%';
    hub.style.transform = 'translate(-50%, -50%) scale(' + (cfg.hubSize / 100) + ')';
}

function changePanelSize(panel, delta) {
    if (panel === 'gif') {
        cfg.gifZoom = Math.max(50, Math.min(200, cfg.gifZoom + delta));
        document.getElementById('gifSizeTxt').textContent = cfg.gifZoom + '%';
        applyGifZoom();
    } else if (panel === 'video') {
        const currentScale = cfg.videoWidth / 320;
        const newScale = Math.max(50, Math.min(200, (currentScale * 100) + delta));
        cfg.videoWidth = Math.round(320 * newScale / 100);
        cfg.videoHeight = Math.round(240 * newScale / 100);
        document.getElementById('videoSizeTxt').textContent = Math.round(newScale) + '%';
        applyVideoSize();
    } else if (panel === 'logs') {
        const currentScale = cfg.logsWidth / 400;
        const newScale = Math.max(50, Math.min(200, (currentScale * 100) + delta));
        cfg.logsWidth = Math.round(400 * newScale / 100);
        cfg.logsHeight = Math.round(300 * newScale / 100);
        document.getElementById('logsSizeTxt').textContent = Math.round(newScale) + '%';
        const panelEl = document.getElementById('panelLogs');
        if (panelEl) {
            panelEl.style.width = cfg.logsWidth + 'px';
            panelEl.style.height = cfg.logsHeight + 'px';
        }
    } else if (panel === 'live') {
        cfg.liveSize = Math.max(50, Math.min(200, cfg.liveSize + delta));
        document.getElementById('liveSizeTxt').textContent = cfg.liveSize + '%';
        const panelEl = document.getElementById('panelLive');
        if (panelEl) {
            panelEl.style.transform = 'translate(-50%, -50%) scale(' + (cfg.liveSize / 100) + ')';
        }
    }
    saveAllConfig();
}

function setGlobalOpacity(value) {
    cfg.globalAlpha = parseInt(value);
    document.getElementById('opacityTxt').textContent = value + '%';
    const opacity = value / 100;
    document.documentElement.style.setProperty('--global-opacity', opacity);
    saveAllConfig();
}

function setColorTheme(theme) {
    cfg.globalColor = theme;
    document.body.className = document.body.className.replace(/theme-\w+/g, '');
    if (theme !== 'default') {
        document.body.classList.add('theme-' + theme);
    }
    document.querySelectorAll('.color-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.theme === theme);
    });
    saveAllConfig();
}

function setLiveBackground(bg) {
    cfg.liveBackground = bg;
    const panel = document.getElementById('panelLive');
    
    if (panel) {
        panel.classList.remove('default-bg', 'custom-bg', 'glass-bg', 'frameless-mode');
        
        if (bg === 'default') {
            panel.classList.add('default-bg');
            panel.style.background = '';
            panel.style.backgroundImage = '';
        } else if (bg === 'frame-v1') {
            panel.classList.add('custom-bg');
            panel.classList.add('frameless-mode');
            panel.style.backgroundImage = "url('Assets/Images/frame-v1.png')";
            panel.style.backgroundSize = "contain";
            panel.style.backgroundPosition = "center";
            panel.style.backgroundRepeat = "no-repeat";
        } else if (bg === 'glass-bg') {
            panel.classList.add('glass-bg');
            panel.style.background = '';
            panel.style.backgroundImage = '';
        }
    }
    
    document.querySelectorAll('.bg-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.bg === bg);
    });
    
    saveAllConfig();
}

function saveAllConfig() {
    if (window.onSaveConfig) {
        window.onSaveConfig(JSON.stringify({
            firstKey: cfg.firstKey,
            secondKey: cfg.secondKey,
            singleKeyMode: cfg.singleMode,
            volume: cfg.volume,
            muteHubSound: cfg.muteHubSound,
            hubSound: cfg.hubSound,
            posX: cfg.posX,
            posY: cfg.posY,
            hubSize: cfg.hubSize,
            gifVisible: cfg.gifVisible,
            gifPosX: cfg.gifPosX,
            gifPosY: cfg.gifPosY,
            gifZoom: cfg.gifZoom,
            gifWidth: cfg.gifWidth,
            gifHeight: cfg.gifHeight,
            videoVisible: cfg.videoVisible,
            videoPosX: cfg.videoPosX,
            videoPosY: cfg.videoPosY,
            videoWidth: cfg.videoWidth,
            videoHeight: cfg.videoHeight,
            logsVisible: cfg.logsVisible,
            logsPosX: cfg.logsPosX,
            logsPosY: cfg.logsPosY,
            logsWidth: cfg.logsWidth,
            logsHeight: cfg.logsHeight,
            logsFontSize: cfg.logsFontSize,
            logsAutoRefresh: cfg.logsAutoRefresh,
            liveVisible: cfg.liveVisible,
            liveAutoStart: cfg.liveAutoStart,
            livePosX: cfg.livePosX,
            livePosY: cfg.livePosY,
            liveSize: cfg.liveSize,

            bootPosX: cfg.bootPosX,
            bootPosY: cfg.bootPosY,

            menusSubmenuVisible: cfg.menusSubmenuVisible,
            globalAlpha: cfg.globalAlpha,
            globalColor: cfg.globalColor,
            liveBackground: cfg.liveBackground,
            menusSize: cfg.menusSize,
            itemsSize: cfg.itemsSize,
            toastSize: cfg.toastSize,

            hubVisibleSettings: (window.getHubToggleState ? window.getHubToggleState().hubVisible : true),
            proximitySettings: (window.getHubToggleState ? window.getHubToggleState().ballsProximityMode : false),

            liveSize: cfg.liveSize
        }));
    }
}
window.saveAllConfig = saveAllConfig;

function autoSaveKeyConfig() {
    cfg.firstKey = parseInt(document.getElementById('firstKey').value, 16);
    cfg.secondKey = parseInt(document.getElementById('secondKey').value, 16);
    cfg.singleMode = document.getElementById('singleMode').checked;
    cfg.muteHubSound = document.getElementById('muteHubSound').checked;
    cfg.hubSound = document.getElementById('hubSound').value;
    saveAllConfig();
    show('Config saved');
}

function saveKeys() {
    autoSaveKeyConfig();
    show('Keys saved!');
}

function healAll() {
    console.log('healAll called');
    if (window.onHealAll) {
        console.log('calling window.onHealAll');
        window.onHealAll('all');
    }
    show('Healing all...');
}

function healHealth() {
    console.log('healHealth called');
    if (window.onHealHealth) {
        console.log('calling window.onHealHealth');
        window.onHealHealth('hp');
    }
    show('Healing health...');
}

function healMagicka() {
    console.log('healMagicka called');
    if (window.onHealMagicka) {
        console.log('calling window.onHealMagicka');
        window.onHealMagicka('mp');
    }
    show('Healing magicka...');
}

function healStamina() {
    console.log('healStamina called');
    if (window.onHealStamina) {
        console.log('calling window.onHealStamina');
        window.onHealStamina('st');
    }
    show('Healing stamina...');
}

function restorePercent(p) {
    console.log('restorePercent called: ' + p);
    if (window.onRestorePercent) {
        window.onRestorePercent(p.toString());
    }
    show('+' + p + '%');
}

function closeMenu() {
    console.log('closeMenu called');
    if (window.onCloseMenu) {
        window.onCloseMenu('close');
    }
}

function updateUI() {
    const bar = (id, val) => { const el = document.getElementById(id); if (el) el.style.width = val + '%'; };
    const txt = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };
    bar('hp-bar', stats.hp);
    bar('mp-bar', stats.mp);
    bar('st-bar', stats.st);
    txt('hp-txt', Math.round(stats.hp));
    txt('mp-txt', Math.round(stats.mp));
    txt('st-txt', Math.round(stats.st));
}

function show(msg) {
    const el = document.getElementById('status');
    if (el) el.textContent = msg;
}

let notifyCallCount = 0;
let configSavedCount = 0;

function notify(msg) {
    notifyCallCount++;
    
    const n = document.createElement('div');
    n.className = 'notification';
    n.textContent = msg;
    
    if (notifyCallCount === 1) {
        n.style.opacity = '0';
        n.style.pointerEvents = 'none';
    }
    
    document.body.appendChild(n);
    setTimeout(() => n.remove(), 2000);
}

function toggleGifPanel() {
    cfg.gifVisible = !cfg.gifVisible;
    updateGifPanel();
    saveAllConfig();
}

function updateGifPanel() {
    const panel = document.getElementById('panelGif');
    const btn = document.getElementById('btnToggleGif');
    
    if (cfg.gifVisible) {
        panel.classList.add('visible');
        btn.classList.add('active');
        panel.style.left = cfg.gifPosX + '%';
        panel.style.top = cfg.gifPosY + '%';
        panel.style.transform = 'translate(-50%, -50%)';
        applyGifSize();
        applyGifZoom();
    } else {
        panel.classList.remove('visible');
        btn.classList.remove('active');
    }
}

function applyGifSize() {
    const panel = document.getElementById('panelGif');
    if (panel) {
        panel.style.width = cfg.gifWidth + 'px';
        panel.style.minHeight = cfg.gifHeight + 'px';
        panel.style.height = 'auto';
    }
}

function applyGifZoom() {
    const img = document.getElementById('gifImage');
    const scale = cfg.gifZoom / 100;
    img.style.transform = 'scale(' + scale + ')';
    document.getElementById('gifZoomTxt').textContent = cfg.gifZoom + '%';
}

function changeGifZoom(delta) {
    cfg.gifZoom = Math.max(50, Math.min(200, cfg.gifZoom + delta));
    applyGifZoom();
    saveAllConfig();
}

window.gifDragging = false;
window.gifResizing = false;

function initGifDrag() {
    const head = document.getElementById('dragGif');
    head.addEventListener('mousedown', function(e) {
        startPanelDrag(e, 'gifDragging', 'gifPosX', 'gifPosY', 'gifResizing');
    });
}

function initGifResize() {
    const handle = document.getElementById('resizeGif');
    
    handle.addEventListener('mousedown', function(e) {
        window.gifResizing = true;
        window.gifStartX = e.clientX;
        window.gifStartY = e.clientY;
        window.gifStartW = cfg.gifWidth;
        window.gifStartH = cfg.gifHeight;
        e.preventDefault();
        e.stopPropagation();
    });
}

function toggleVideoPanel() {
    cfg.videoVisible = !cfg.videoVisible;
    updateVideoPanel();
    saveAllConfig();
}

function updateVideoPanel() {
    const panel = document.getElementById('panelVideo');
    const btn = document.getElementById('btnToggleVideo');
    
    if (cfg.videoVisible) {
        panel.classList.add('visible');
        btn.classList.add('active');
        panel.style.left = cfg.videoPosX + '%';
        panel.style.top = cfg.videoPosY + '%';
        panel.style.transform = 'translate(-50%, -50%)';
        applyVideoSize();
        updateVideoSizeText();
        
        if (!videoLoaded) {
            setTimeout(() => {
                initVideo();
            }, 100);
        } else {
            setTimeout(() => {
                drawVideoFrame(videoCurrentFrame);
            }, 50);
        }
    } else {
        panel.classList.remove('visible');
        btn.classList.remove('active');
        pauseVideo();
    }
}

function applyVideoSize() {
    const container = document.querySelector('.video-container');
    if (container) {
        if (videoConfig) {
            const aspectRatio = videoConfig.frameWidth / videoConfig.frameHeight;
            cfg.videoHeight = Math.round(cfg.videoWidth / aspectRatio);
        }
        
        container.style.width = cfg.videoWidth + 'px';
        container.style.height = cfg.videoHeight + 'px';
        setTimeout(() => resizeVideo(), 10);
    }
}

function updateVideoSizeText() {
    const pct = Math.round((cfg.videoWidth / 320) * 100);
    const txtHub = document.getElementById('videoSizeTxt');
    if (txtHub) txtHub.textContent = pct + '%';
}

function initVideoDrag() {
    const head = document.getElementById('dragVideo');
    head.addEventListener('mousedown', function(e) {
        startPanelDrag(e, 'videoDragging', 'videoPosX', 'videoPosY', 'videoResizing');
    });
}

function initVideoResize() {
    const handle = document.getElementById('resizeVideo');
    
    handle.addEventListener('mousedown', function(e) {
        window.videoResizing = true;
        window.videoStartX = e.clientX;
        window.videoStartY = e.clientY;
        window.videoStartW = cfg.videoWidth;
        window.videoStartH = cfg.videoHeight;
        e.preventDefault();
        e.stopPropagation();
    });
}

document.addEventListener('mousemove', function(e) {
    applyPanelDrag(e, 'panelGif', 'gifDragging', 'gifPosX', 'gifPosY');
    applyPanelDrag(e, 'panelVideo', 'videoDragging', 'videoPosX', 'videoPosY');
    applyPanelDrag(e, 'panelLogs', 'logsDragging', 'logsPosX', 'logsPosY');
    applyPanelDrag(e, 'panelLive', 'liveDragging', 'livePosX', 'livePosY');
    
    
    if (window.gifResizing) {
        const dx = e.clientX - window.gifStartX;
        const dy = e.clientY - window.gifStartY;
        cfg.gifWidth = Math.max(120, window.gifStartW + dx);
        cfg.gifHeight = Math.max(120, window.gifStartH + dy);
        applyGifSize();
    }
    if (window.videoResizing) {
        const dx = e.clientX - window.videoStartX;
        const dy = e.clientY - window.videoStartY;
        cfg.videoWidth = Math.max(200, window.videoStartW + dx);
        cfg.videoHeight = Math.max(150, window.videoStartH + dy);
        applyVideoSize();
        updateVideoSizeText();
    }
    if (window.logsResizing) {
        const dx = e.clientX - window.logsStartX;
        const dy = e.clientY - window.logsStartY;
        cfg.logsWidth = Math.max(300, window.logsStartW + dx);
        cfg.logsHeight = Math.max(200, window.logsStartH + dy);
        const logsPanel = document.getElementById('panelLogs');
        if (logsPanel) {
            logsPanel.style.width = cfg.logsWidth + 'px';
            logsPanel.style.height = cfg.logsHeight + 'px';
        }
    }
    if (window.liveResizing) {
        const dx = e.clientX - window.liveStartX;
        const dy = e.clientY - window.liveStartY;
        const livePanel = document.getElementById('panelLive');
        if (livePanel) {
            const newW = Math.max(150, window.liveStartW + dx);
            livePanel.style.width = newW + 'px';
        }
    }

});

document.addEventListener('mouseup', function() {
    if (window.gifDragging) {
        window.gifDragging = false;
        saveAllConfig();
    }
    if (window.gifResizing) {
        window.gifResizing = false;
        saveAllConfig();
    }
    if (window.videoDragging) {
        window.videoDragging = false;
        saveAllConfig();
    }
    if (window.videoResizing) {
        window.videoResizing = false;
        saveAllConfig();
    }
    if (window.logsDragging) {
        window.logsDragging = false;
        saveAllConfig();
    }
    if (window.logsResizing) {
        window.logsResizing = false;
        saveAllConfig();
    }
    if (window.liveDragging) {
        window.liveDragging = false;
        saveAllConfig();
    }
    if (window.liveResizing) {
        window.liveResizing = false;
        saveAllConfig();
    }

});

function toggleLogsPanel() {
    cfg.logsVisible = !cfg.logsVisible;
    updateLogsPanel();
    saveAllConfig();
}

function updateLogsPanel() {
    const panel = document.getElementById('panelLogs');
    const btn = document.getElementById('btnToggleLogs');
    
    if (cfg.logsVisible) {
        panel.classList.add('visible');
        btn.classList.add('active');
        panel.style.left = cfg.logsPosX + '%';
        panel.style.top = cfg.logsPosY + '%';
        panel.style.transform = 'translate(-50%, -50%)';
        applyLogsSize();
        refreshLogs();
        startLogsAutoRefresh();
    } else {
        panel.classList.remove('visible');
        btn.classList.remove('active');
        stopLogsAutoRefresh();
    }
}

function applyLogsSize() {
    const panel = document.getElementById('panelLogs');
    if (panel) {
        panel.style.width = cfg.logsWidth + 'px';
        panel.style.height = cfg.logsHeight + 'px';
    }
}

let currentLogFile = 'main';

function refreshLogs() {
    if (currentLogFile === 'inspector') {
        const content = document.getElementById('logsContent');
        if (content) content.innerHTML = getInspectorLogContent();
        return;
    }
    
    if (window.onGetLogs) {
        window.onGetLogs(currentLogFile);
    } else {
        let logPath = 'Assets/Logs/OSoundtracks-Prisma.log';
        if (currentLogFile === 'menus') {
            logPath = 'Assets/Logs/OSoundtracks-Prisma-Menus.log';
        }
        fetch(logPath)
            .then(response => response.text())
            .then(text => window.updateLogsContent(text))
            .catch(err => {
                const content = document.getElementById('logsContent');
                if (content) content.innerHTML = '<span class="log-warning">Unable to load logs. C++ integration required.</span>';
            });
    }
}

function clearLogsDisplay() {
    if (currentLogFile === 'inspector') {
        jsInspectorLog = [];
        addToInspectorLog('INFO', 'Log cleared');
        const content = document.getElementById('logsContent');
        if (content) content.innerHTML = getInspectorLogContent();
        return;
    }
    
    const content = document.getElementById('logsContent');
    if (content) content.innerHTML = '<span class="log-info">Logs cleared...</span>';
}

function startLogsAutoRefresh() {
    if (logsAutoRefreshInterval) clearInterval(logsAutoRefreshInterval);
    if (cfg.logsAutoRefresh) {
        logsAutoRefreshInterval = setInterval(refreshLogs, 2000);
    }
}

function stopLogsAutoRefresh() {
    if (logsAutoRefreshInterval) {
        clearInterval(logsAutoRefreshInterval);
        logsAutoRefreshInterval = null;
    }
}

function changeLogsFontSize(delta) {
    cfg.logsFontSize = Math.max(8, Math.min(18, cfg.logsFontSize + delta));
    applyLogsFontSize();
    saveAllConfig();
}

function applyLogsFontSize() {
    const content = document.getElementById('logsContent');
    if (content) {
        content.style.fontSize = cfg.logsFontSize + 'px';
    }
    const txt = document.getElementById('logsFontSizeTxt');
    if (txt) {
        txt.textContent = cfg.logsFontSize + 'px';
    }
}

window.updateLogsContent = function(logText) {
    const content = document.getElementById('logsContent');
    if (!content || !logText) return;
    
    const lines = logText.split('\n');
    let html = '';
    
    for (let line of lines) {
        if (!line.trim()) continue;
        
        let lineClass = 'log-debug';
        let coloredLine = escapeHtml(line);
        
        if (line.includes('[critical]') || line.includes('[CRITICAL]') || line.includes('CRITICAL')) {
            lineClass = 'log-critical';
        } else if (line.includes('[error]') || line.includes('[ERROR]') || line.includes('ERROR') || line.includes('error:')) {
            lineClass = 'log-error';
        } else if (line.includes('[warning]') || line.includes('[WARN]') || line.includes('WARNING') || line.includes('warn:')) {
            lineClass = 'log-warning';
        } else if (line.includes('[info]') || line.includes('[INFO]') || line.includes('INFO')) {
            lineClass = 'log-info';
        } else if (line.includes('[trace]') || line.includes('[TRACE]')) {
            lineClass = 'log-trace';
        }
        
        coloredLine = coloredLine.replace(/(\d{4}-\d{2}-\d{2}|\d{2}:\d{2}:\d{2})/g, '<span class="log-timestamp">$1</span>');
        
        coloredLine = coloredLine.replace(/\[([^\]]+)\]/, '<span class="log-source">[$1]</span>');
        
        html += '<div class="log-line ' + lineClass + '">' + coloredLine + '</div>';
    }
    
    content.innerHTML = html;
    
    const container = document.querySelector('.logs-container');
    if (container) container.scrollTop = container.scrollHeight;
};

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

window.logsDragging = false;
window.logsResizing = false;

function initLogsDrag() {
    const head = document.getElementById('dragLogs');
    head.addEventListener('mousedown', function(e) {
        startPanelDrag(e, 'logsDragging', 'logsPosX', 'logsPosY', 'logsResizing');
    });
}

function initLogsResize() {
    const handle = document.getElementById('resizeLogs');
    
    handle.addEventListener('mousedown', function(e) {
        window.logsResizing = true;
        window.logsStartX = e.clientX;
        window.logsStartY = e.clientY;
        window.logsStartW = cfg.logsWidth;
        window.logsStartH = cfg.logsHeight;
        e.preventDefault();
        e.stopPropagation();
    });
}

function toggleLivePanel() {
    cfg.liveVisible = !cfg.liveVisible;
    updateLivePanel();
    saveAllConfig();
}

function updateLivePanel() {
    const panel = document.getElementById('panelLive');
    const btn = document.getElementById('btnToggleLive');
    
    if (cfg.liveVisible) {
        panel.classList.add('visible');
        btn.classList.add('active');
        panel.style.left = cfg.livePosX + '%';
        panel.style.top = cfg.livePosY + '%';
        panel.style.transform = 'translate(-50%, -50%) scale(' + (cfg.liveSize / 100) + ')';
        updateLiveBars();
    } else {
        panel.classList.remove('visible');
        btn.classList.remove('active');
    }
}

function updateLiveBars() {
    const hpBar = document.getElementById('live-hp-bar');
    const mpBar = document.getElementById('live-mp-bar');
    const stBar = document.getElementById('live-st-bar');
    const hpTxt = document.getElementById('live-hp-txt');
    const mpTxt = document.getElementById('live-mp-txt');
    const stTxt = document.getElementById('live-st-txt');
    
    if (hpBar) hpBar.style.width = stats.hp + '%';
    if (mpBar) mpBar.style.width = stats.mp + '%';
    if (stBar) stBar.style.width = stats.st + '%';
    if (hpTxt) hpTxt.textContent = Math.round(stats.hp);
    if (mpTxt) mpTxt.textContent = Math.round(stats.mp);
    if (stTxt) stTxt.textContent = Math.round(stats.st);
}

window.liveDragging = false;
window.liveResizing = false;

function initLiveDrag() {
    const panel = document.getElementById('panelLive');
    if (!panel) return;
    
    panel.addEventListener('mousedown', function(e) {
        if (e.target.classList.contains('resize-handle')) return;
        startPanelDrag(e, 'liveDragging', 'livePosX', 'livePosY', 'liveResizing');
    });
}

function initLiveResize() {
    const handle = document.getElementById('resizeLive');
    
    handle.addEventListener('mousedown', function(e) {
        window.liveResizing = true;
        window.liveStartX = e.clientX;
        window.liveStartY = e.clientY;
        const panel = document.getElementById('panelLive');
        window.liveStartW = panel.offsetWidth;
        window.liveStartH = panel.offsetHeight;
        e.preventDefault();
        e.stopPropagation();
    });
}

function applyMenusSize() {
    const submenu = document.getElementById('menusSubmenu');
    if (submenu) {
        const scale = cfg.menusSize / 100;
        submenu.style.transform = `translate(-50%, -50%) scale(${scale})`;
    }
}

function changeMenusSize(delta) {
    let newSize = cfg.menusSize + delta;
    if (newSize < 50) newSize = 50;
    if (newSize > 200) newSize = 200;
    cfg.menusSize = newSize;
    const txt = document.getElementById('menusSizeTxt');
    if (txt) txt.textContent = newSize + '%';
    applyMenusSize();
    saveItemsConfig();
}

function applyItemsSize() {
    const submenu = document.getElementById('itemsSubmenu');
    if (submenu) {
        const scale = cfg.itemsSize / 100;
        submenu.style.transform = `translate(-50%, -50%) scale(${scale})`;
    }
}

function changeItemsSize(delta) {
    let newSize = cfg.itemsSize + delta;
    if (newSize < 50) newSize = 50;
    if (newSize > 200) newSize = 200;
    cfg.itemsSize = newSize;
    const txt = document.getElementById('itemsSizeTxt');
    if (txt) txt.textContent = newSize + '%';
    applyItemsSize();
    saveItemsConfig();
}

function applyToastSize() {
}

function changeToastSize(delta) {
    let newSize = cfg.toastSize + delta;
    if (newSize < 10) newSize = 10;
    if (newSize > 24) newSize = 24;
    cfg.toastSize = newSize;
    const txt = document.getElementById('toastSizeTxt');
    if (txt) txt.textContent = newSize + 'px';
    applyToastSize();
    saveItemsConfig();
}

window.updatePlayerStats = function(j) {
    const s = typeof j === 'string' ? JSON.parse(j) : j;
    if (s.health) stats.hp = s.health.current;
    if (s.magicka) stats.mp = s.magicka.current;
    if (s.stamina) stats.st = s.stamina.current;
    updateUI();
    updateLiveBars();
};

window.showMessage = function(m) {
    if (m === 'Configuration saved!') {
        configSavedCount++;
        if (configSavedCount <= 25) {
            return;
        }
    }
    show(m);
    notify(m);
};

window.showConfirmDialog = function(message, onAccept, onCancel) {
    const existing = document.getElementById('confirm-dialog-overlay');
    if (existing) existing.remove();
    
    const overlay = document.createElement('div');
    overlay.id = 'confirm-dialog-overlay';
    overlay.style.cssText = `
        position: fixed; top: 0; left: 0; width: 100%; height: 100%;
        background: rgba(0, 0, 0, 0.8); display: flex;
        align-items: center; justify-content: center; z-index: 99999;
    `;
    
    const dialog = document.createElement('div');
    dialog.style.cssText = `
        background: linear-gradient(135deg, rgba(30, 30, 30, 0.98), rgba(20, 20, 20, 0.98));
        border: 2px solid rgba(255, 255, 255, 0.2); border-radius: 10px;
        padding: 30px; max-width: 400px; text-align: center;
        box-shadow: 0 10px 40px rgba(0, 0, 0, 0.5);
    `;
    
    const msgEl = document.createElement('p');
    msgEl.textContent = message;
    msgEl.style.cssText = `
        color: #fff; font-size: 16px; margin: 0 0 25px 0;
        font-family: 'Segoe UI', sans-serif;
    `;
    
    const btnContainer = document.createElement('div');
    btnContainer.style.cssText = `display: flex; gap: 15px; justify-content: center;`;
    
    const acceptBtn = document.createElement('button');
    acceptBtn.textContent = 'ACCEPT';
    acceptBtn.style.cssText = `
        background: linear-gradient(135deg, #4a9eff, #2d7dd2);
        border: none; border-radius: 5px; padding: 12px 30px;
        color: white; font-size: 14px; font-weight: bold; cursor: pointer;
        transition: all 0.2s ease;
    `;
    acceptBtn.onmouseenter = () => acceptBtn.style.transform = 'scale(1.05)';
    acceptBtn.onmouseleave = () => acceptBtn.style.transform = 'scale(1)';
    acceptBtn.onclick = () => {
        overlay.remove();
        if (onAccept) onAccept();
    };
    
    const cancelBtn = document.createElement('button');
    cancelBtn.textContent = 'CANCEL';
    cancelBtn.style.cssText = `
        background: linear-gradient(135deg, #666, #444);
        border: none; border-radius: 5px; padding: 12px 30px;
        color: white; font-size: 14px; font-weight: bold; cursor: pointer;
        transition: all 0.2s ease;
    `;
    cancelBtn.onmouseenter = () => cancelBtn.style.transform = 'scale(1.05)';
    cancelBtn.onmouseleave = () => cancelBtn.style.transform = 'scale(1)';
    cancelBtn.onclick = () => {
        overlay.remove();
        if (onCancel) onCancel();
    };
    
    btnContainer.appendChild(acceptBtn);
    btnContainer.appendChild(cancelBtn);
    dialog.appendChild(msgEl);
    dialog.appendChild(btnContainer);
    overlay.appendChild(dialog);
    document.body.appendChild(overlay);
    
    overlay.onclick = (e) => {
        if (e.target === overlay) {
            overlay.remove();
            if (onCancel) onCancel();
        }
    };
    
    acceptBtn.focus();
};

let savedPanelStates = {
    gifVisible: false,
    videoVisible: false,
    logsVisible: false
};

let livePanelStatsInterval = null;

let isPersistentMode = false;

let isStartupComplete = false;

let pendingPanelLiveVisibility = null;

window.setPanelLiveVisibility = function(shouldHide) {
    window._lastShouldHide = shouldHide;
    pendingPanelLiveVisibility = shouldHide;
    if (!isPersistentMode) {
        console.log('setPanelLiveVisibility: Not in persistent mode, storing for later');
        return;
    }
    
    const panelLive = document.getElementById('panelLive');
    if (!panelLive) {
        console.log('setPanelLiveVisibility: Panel Live not found');
        return;
    }
    
    if (shouldHide) {
        panelLive.style.visibility = 'hidden';
        console.log('Panel Live HIDDEN (tracked menu open)');
    } else {
        panelLive.style.visibility = 'visible';
        console.log('Panel Live VISIBLE (all tracked menus closed)');
    }
};

window.hideHubKeepLive = function() {
    const hub = document.getElementById('hub');
    if (hub) hub.classList.add('hidden');
    
    savedPanelStates.gifVisible = cfg.gifVisible;
    savedPanelStates.videoVisible = cfg.videoVisible;
    savedPanelStates.logsVisible = cfg.logsVisible;
    
    const panelGif = document.getElementById('panelGif');
    const panelVideo = document.getElementById('panelVideo');
    const panelLogs = document.getElementById('panelLogs');
    const panelLive = document.getElementById('panelLive');
    
    if (panelGif) panelGif.classList.remove('visible');
    if (panelVideo) panelGif.classList.remove('visible');
    if (panelLogs) panelLogs.classList.remove('visible');
    
    if (panelLive && cfg.liveVisible) {
        panelLive.classList.add('persistent-mode');
        panelLive.style.visibility = 'visible';
        if (window.onRequestPlayerStats) {
            livePanelStatsInterval = setInterval(() => {
                window.onRequestPlayerStats('');
            }, 1000);
        }
    }
    
    isPersistentMode = true;
    
    const menusSubmenu = document.getElementById('menusSubmenu');
    if (menusSubmenu && cfg.menusSubmenuVisible) {
        menusSubmenu.classList.add('persistent-mode');
    }
    
    if (window.hideSupportBall) window.hideSupportBall();
    if (window.hideSocialLinksBall) window.hideSocialLinksBall();
    if (window.hideDailyUpdatesBall) window.hideDailyUpdatesBall();
    if (window.hideKeyTrackerBall) window.hideKeyTrackerBall();
    if (window.hideHubToggle) window.hideHubToggle();
    
    console.log('Hub hidden, Panel Live and Menu Tracking remain visible (persistent mode)');
};

window.showHub = function() {
    const hubToggleState = window.getHubToggleState ? window.getHubToggleState() : null;
    const hubVisibleToggle = hubToggleState ? hubToggleState.hubVisible : true;
    
    if (hubVisibleToggle) {
        const hub = document.getElementById('hub');
        if (hub) hub.classList.remove('hidden');
    }
    
    const panelGif = document.getElementById('panelGif');
    const panelVideo = document.getElementById('panelVideo');
    const panelLogs = document.getElementById('panelLogs');
    const panelLive = document.getElementById('panelLive');
    
    if (panelLive) {
        panelLive.classList.remove('persistent-mode');
        panelLive.style.visibility = 'visible';
    }
    if (livePanelStatsInterval) {
        clearInterval(livePanelStatsInterval);
        livePanelStatsInterval = null;
    }
    
    isPersistentMode = false;
    
    const menusSubmenu = document.getElementById('menusSubmenu');
    if (menusSubmenu) {
        menusSubmenu.classList.remove('persistent-mode');
    }
    
    if (savedPanelStates.gifVisible)   { cfg.gifVisible   = true; updateGifPanel(); }
    if (savedPanelStates.videoVisible) { cfg.videoVisible = true; updateVideoPanel(); }
    if (savedPanelStates.logsVisible)  { cfg.logsVisible  = true; updateLogsPanel(); }

    if (window.showSupportBall) window.showSupportBall();
    if (window.showSocialLinksBall) window.showSocialLinksBall();
    if (window.showDailyUpdatesBall) window.showDailyUpdatesBall();
    if (window.showKeyTrackerBall) window.showKeyTrackerBall();
    if (window.syncBallsProximityStyles) window.syncBallsProximityStyles();
    if (window.showHubToggle) window.showHubToggle();
    
    console.log('Hub shown, panels restored');
};

window.showPanelLive = function() {
    cfg.liveVisible = true;
    updateLivePanel();
    console.log('Panel Live shown at startup (persistent mode)');
};

// CONFIGURATION SYNC
window.updateConfigUI = function(j) {
    console.log('updateConfigUI called with:', j);
    const c = typeof j === 'string' ? JSON.parse(j) : j;
    
    if (c.firstKey !== undefined) {
        cfg.firstKey = c.firstKey;
        document.getElementById('firstKey').value = '0x' + c.firstKey.toString(16).toUpperCase();
    }
    if (c.secondKey !== undefined) {
        cfg.secondKey = c.secondKey;
        document.getElementById('secondKey').value = '0x' + c.secondKey.toString(16).toUpperCase();
    }
    if (c.singleKeyMode !== undefined) {
        cfg.singleMode = c.singleKeyMode;
        document.getElementById('singleMode').checked = c.singleKeyMode;
    }
    if (c.volume !== undefined) {
        cfg.volume = c.volume;
        document.getElementById('vol').value = c.volume;
        document.getElementById('vol-txt').textContent = c.volume;
    }
    if (c.posX !== undefined) cfg.posX = c.posX;
    if (c.posY !== undefined) cfg.posY = c.posY;
    if (c.hubSize !== undefined) {
        cfg.hubSize = c.hubSize;
        document.getElementById('size-txt').textContent = c.hubSize + '%';
    }
    if (c.muteHubSound !== undefined) {
        cfg.muteHubSound = c.muteHubSound;
        document.getElementById('muteHubSound').checked = c.muteHubSound;
    }
    if (c.hubSound !== undefined) {
        cfg.hubSound = c.hubSound;
        document.getElementById('hubSound').value = c.hubSound;
    }
    
    if (c.gifVisible !== undefined) cfg.gifVisible = c.gifVisible;
    if (c.gifPosX !== undefined) cfg.gifPosX = c.gifPosX;
    if (c.gifPosY !== undefined) cfg.gifPosY = c.gifPosY;
    if (c.gifZoom !== undefined) cfg.gifZoom = c.gifZoom;
    
    if (c.videoVisible !== undefined) cfg.videoVisible = c.videoVisible;
    if (c.videoPosX !== undefined) cfg.videoPosX = c.videoPosX;
    if (c.videoPosY !== undefined) cfg.videoPosY = c.videoPosY;
    if (c.videoWidth !== undefined) cfg.videoWidth = c.videoWidth;
    if (c.videoHeight !== undefined) cfg.videoHeight = c.videoHeight;
    
    if (c.logsVisible !== undefined) cfg.logsVisible = c.logsVisible;
    if (c.logsPosX !== undefined) cfg.logsPosX = c.logsPosX;
    if (c.logsPosY !== undefined) cfg.logsPosY = c.logsPosY;
    if (c.logsWidth !== undefined) cfg.logsWidth = c.logsWidth;
    if (c.logsHeight !== undefined) cfg.logsHeight = c.logsHeight;
    if (c.logsFontSize !== undefined) cfg.logsFontSize = c.logsFontSize;
    if (c.logsAutoRefresh !== undefined) {
        cfg.logsAutoRefresh = c.logsAutoRefresh;
        const cb = document.getElementById('logsAutoRefresh');
        if (cb) cb.checked = c.logsAutoRefresh;
    }
    
    if (c.liveVisible !== undefined) cfg.liveVisible = c.liveVisible;
    if (c.livePosX !== undefined) cfg.livePosX = c.livePosX;
    if (c.livePosY !== undefined) cfg.livePosY = c.livePosY;
    

    if (c.liveSize !== undefined) {
        cfg.liveSize = c.liveSize;
        const liveSizeTxt = document.getElementById('liveSizeTxt');
        if (liveSizeTxt) liveSizeTxt.textContent = c.liveSize + '%';
        const panelLive = document.getElementById('panelLive');
        if (panelLive) panelLive.style.transform = 'translate(-50%, -50%) scale(' + (c.liveSize / 100) + ')';
    }
    
    if (c.liveNowPlayingMode !== undefined) {
        nowPlayingState.liveNowPlayingMode = c.liveNowPlayingMode;
        document.querySelectorAll('input[name="nowPlayingMode"]').forEach(function(r) {
            r.checked = (r.value === c.liveNowPlayingMode);
        });
        var row = document.getElementById('timedSecondsRow');
        if (row) row.style.display = (c.liveNowPlayingMode === 'timed') ? 'flex' : 'none';
    }
    if (c.liveNowPlayingSeconds !== undefined) {
        nowPlayingState.liveNowPlayingSeconds = c.liveNowPlayingSeconds;
        var el = document.getElementById('nowPlayingSeconds');
        if (el) el.value = c.liveNowPlayingSeconds;
    }
    if (c.liveNowPlayingGif !== undefined) {
        nowPlayingState.liveNowPlayingGif = c.liveNowPlayingGif;
        var disc = document.getElementById('playerDiscGif');
        if (disc) disc.src = 'Assets/Images/' + c.liveNowPlayingGif;
        document.querySelectorAll('.gif-option').forEach(function(o) {
            o.classList.toggle('active', o.dataset.gif === c.liveNowPlayingGif);
        });
    }
    
    if (c.globalAlpha !== undefined) {
        cfg.globalAlpha = c.globalAlpha;
        document.getElementById('globalOpacity').value = c.globalAlpha;
        document.getElementById('opacityTxt').textContent = c.globalAlpha + '%';
        setGlobalOpacity(c.globalAlpha);
    }
    if (c.globalColor !== undefined) {
        cfg.globalColor = c.globalColor;
        setColorTheme(c.globalColor);
    }
    if (c.liveBackground !== undefined) {
        cfg.liveBackground = c.liveBackground;
        setLiveBackground(c.liveBackground);
    }
    if (c.menusSize !== undefined) {
        cfg.menusSize = c.menusSize;
        applyMenusSize();
    }
    if (c.itemsSize !== undefined) {
        cfg.itemsSize = c.itemsSize;
        applyItemsSize();
    }
    if (c.toastSize !== undefined) {
        cfg.toastSize = c.toastSize;
        applyToastSize();
    }
    
    applyPos();
    if (isStartupComplete) {
        updateGifPanel();
        updateVideoPanel();
        updateLogsPanel();
        updateLivePanel();

    }
    applyLogsFontSize();

    if (window.applySettingsJsConfig) {
        window.applySettingsJsConfig(c);
    }
};

window.updateConfig = window.updateConfigUI;

document.querySelectorAll('.hub-tab').forEach(tab => {
    tab.addEventListener('click', function() {
        document.querySelectorAll('.hub-tab').forEach(t => t.classList.remove('active'));
        
        document.querySelectorAll('.hub-tab-content').forEach(c => c.classList.remove('active'));
        
        this.classList.add('active');
        
        const targetId = this.getAttribute('data-target');
        const targetContent = document.getElementById(targetId);
        if (targetContent) {
            targetContent.classList.add('active');
        }
    });
});

document.addEventListener('DOMContentLoaded', function() {
    console.log('OSoundtracks Prisma UI loaded v6.1.0');
    
    updateUI();
    applyPos();
    
    const hub = document.getElementById('hub');
    const dragHead = document.getElementById('dragHead');
    let startX, startY, startLeft, startTop;
    
    dragHead.addEventListener('mousedown', function(e) {
        dragging = true;
        startX = e.clientX;
        startY = e.clientY;
        const rect = hub.getBoundingClientRect();
        startLeft = rect.left + rect.width / 2;
        startTop = rect.top + rect.height / 2;
        e.preventDefault();
        e.stopPropagation();
    });
    
    document.addEventListener('mousemove', function(e) {
        if (!dragging) return;
        const dx = e.clientX - startX;
        const dy = e.clientY - startY;
        const newX = (startLeft + dx) / window.innerWidth * 100;
        const newY = (startTop + dy) / window.innerHeight * 100;
        
        const hubRect = hub.getBoundingClientRect();
        const halfWidthPct = (hubRect.width / window.innerWidth) * 50;
        const halfHeightPct = (hubRect.height / window.innerHeight) * 50;
        
        const minX = halfWidthPct;
        const maxX = 100 - halfWidthPct;
        const minY = halfHeightPct;
        const maxY = 100 - halfHeightPct;
        
        cfg.posX = Math.max(minX, Math.min(maxX, newX));
        cfg.posY = Math.max(minY, Math.min(maxY, newY));
        hub.style.left = cfg.posX + '%';
        hub.style.top = cfg.posY + '%';
        hub.style.transform = 'translate(-50%, -50%) scale(' + (cfg.hubSize / 100) + ')';
    });
    
    document.addEventListener('mouseup', function() {
        if (dragging) {
            dragging = false;
            saveAllConfig();
            show('Position: ' + Math.round(cfg.posX) + '%, ' + Math.round(cfg.posY) + '%');
        }
    });
    
    document.getElementById('btnClose')?.addEventListener('click', closeMenu);
    document.getElementById('btnHealAll')?.addEventListener('click', healAll);
    document.getElementById('btnHp')?.addEventListener('click', healHealth);
    document.getElementById('btnMp')?.addEventListener('click', healMagicka);
    document.getElementById('btnSt')?.addEventListener('click', healStamina);
    document.getElementById('btn25')?.addEventListener('click', function() { restorePercent(25); });
    document.getElementById('btn50')?.addEventListener('click', function() { restorePercent(50); });
    document.getElementById('btn100')?.addEventListener('click', function() { restorePercent(100); });

    document.getElementById('btn9Gold')?.addEventListener('click', function() {
        if (window.onGiveGold) {
            window.onGiveGold('9');
        }
    });

    document.getElementById('btnSave')?.addEventListener('click', saveKeys);
    
    document.getElementById('vol')?.addEventListener('input', function() {
        setVol(this.value);
    });
    
    document.getElementById('btnSizeDown')?.addEventListener('click', function() { changeSize(-10); });
    document.getElementById('btnSizeUp')?.addEventListener('click', function() { changeSize(10); });
    
    document.getElementById('firstKey')?.addEventListener('change', autoSaveKeyConfig);
    document.getElementById('secondKey')?.addEventListener('change', autoSaveKeyConfig);
    document.getElementById('singleMode')?.addEventListener('change', autoSaveKeyConfig);
    document.getElementById('muteHubSound')?.addEventListener('change', autoSaveKeyConfig);
    document.getElementById('hubSound')?.addEventListener('change', autoSaveKeyConfig);
    
    document.getElementById('btnToggleGif')?.addEventListener('click', toggleGifPanel);
    document.getElementById('btnToggleVideo')?.addEventListener('click', toggleVideoPanel);
    document.getElementById('btnToggleLogs')?.addEventListener('click', toggleLogsPanel);
    document.getElementById('btnToggleLive')?.addEventListener('click', toggleLivePanel);

    document.getElementById('btnCloseGif')?.addEventListener('click', function() {
        cfg.gifVisible = false;
        updateGifPanel();
        saveAllConfig();
    });
    document.getElementById('gifZoomOut')?.addEventListener('click', function() { changeGifZoom(-10); });
    document.getElementById('gifZoomIn')?.addEventListener('click', function() { changeGifZoom(10); });
    
    document.getElementById('btnCloseVideo')?.addEventListener('click', function() {
        cfg.videoVisible = false;
        updateVideoPanel();
        saveAllConfig();
    });
    
    document.getElementById('btnCloseLogs')?.addEventListener('click', function() {
        cfg.logsVisible = false;
        updateLogsPanel();
        saveAllConfig();
    });
    document.getElementById('logsRefresh')?.addEventListener('click', refreshLogs);
    document.getElementById('logsClear')?.addEventListener('click', clearLogsDisplay);
    document.getElementById('logsCopy')?.addEventListener('click', copyLogsToClipboard);
    document.getElementById('logsAutoRefresh')?.addEventListener('change', function() {
        cfg.logsAutoRefresh = this.checked;
        if (cfg.logsAutoRefresh && cfg.logsVisible) {
            startLogsAutoRefresh();
        } else {
            stopLogsAutoRefresh();
        }
        saveAllConfig();
    });
    document.getElementById('logsFontDown')?.addEventListener('click', function() { changeLogsFontSize(-1); });
    document.getElementById('logsFontUp')?.addEventListener('click', function() { changeLogsFontSize(1); });

    document.querySelectorAll('.log-tab').forEach(tab => {
        tab.addEventListener('click', function() {
            document.querySelectorAll('.log-tab').forEach(t => t.classList.remove('active'));
            this.classList.add('active');
            currentLogFile = this.dataset.log;
            const content = document.getElementById('logsContent');
            if (content) content.innerHTML = '<span class="log-info">Loading ' + currentLogFile + ' log...</span>';
            refreshLogs();
        });
    });
    

    
    document.getElementById('btnGifSizeDown')?.addEventListener('click', function() { changePanelSize('gif', -10); });
    document.getElementById('btnGifSizeUp')?.addEventListener('click', function() { changePanelSize('gif', 10); });
    document.getElementById('btnVideoSizeDown')?.addEventListener('click', function() { changePanelSize('video', -10); });
    document.getElementById('btnVideoSizeUp')?.addEventListener('click', function() { changePanelSize('video', 10); });
    document.getElementById('btnLogsSizeDown')?.addEventListener('click', function() { changePanelSize('logs', -10); });
    document.getElementById('btnLogsSizeUp')?.addEventListener('click', function() { changePanelSize('logs', 10); });
    document.getElementById('btnLiveSizeDown')?.addEventListener('click', function() { changePanelSize('live', -10); });
    document.getElementById('btnLiveSizeUp')?.addEventListener('click', function() { changePanelSize('live', 10); });

    
    document.getElementById('globalOpacity')?.addEventListener('input', function(e) {
        setGlobalOpacity(e.target.value);
    });
    
    document.querySelectorAll('.color-btn').forEach(btn => {
        btn.addEventListener('click', function() { setColorTheme(this.dataset.theme); });
    });
    
    document.querySelectorAll('.bg-btn').forEach(btn => {
        btn.addEventListener('click', function() { setLiveBackground(this.dataset.bg); });
    });
    
    initGifDrag();
    initGifResize();
    initVideoDrag();
    initVideoResize();
    initLogsDrag();
    initLogsResize();
    initLiveDrag();
    initLiveResize();

    applyLogsFontSize();
    
    if (window.onRequestStats) window.onRequestStats('get');
    if (window.onGetConfig) window.onGetConfig('get');
    
    
    const initialTitleEl = document.getElementById('playerSongTitle');
    if (initialTitleEl) {
        applyTitleWithTruncation(null, initialTitleEl);
    }
    
    show('Ready - Hold Shift + Press A');
});

document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') closeMenu();
});

let videoCanvas = null;
let videoCtx = null;
let videoConfig = null;
let videoSheets = new Map();
let videoCurrentFrame = 0;
let videoIsPlaying = false;
let videoInterval = null;
let videoLoaded = false;

function getSheetForFrame(frameNum) {
    if (!videoConfig) return null;
    for (let sheet of videoConfig.spritesheets) {
        if (frameNum >= sheet.startFrame && frameNum <= sheet.endFrame) {
            return sheet;
        }
    }
    return null;
}

function loadSheetImage(sheetInfo) {
    return new Promise((resolve, reject) => {
        if (videoSheets.has(sheetInfo.file)) {
            resolve(videoSheets.get(sheetInfo.file));
            return;
        }
        
        const img = new Image();
        img.onload = () => {
            videoSheets.set(sheetInfo.file, img);
            console.log('[Video] Sheet cargado:', sheetInfo.file);
            resolve(img);
        };
        img.onerror = () => {
            console.error('[Video] Error cargando sheet:', sheetInfo.file);
            reject(null);
        };
        img.src = 'Assets/Video/corto-v1/' + sheetInfo.file;
    });
}

async function drawVideoFrame(frameNum) {
    if (!videoCtx || !videoConfig) {
        console.error('[Video] Not initialized');
        return;
    }
    
    const sheet = getSheetForFrame(frameNum);
    if (!sheet) {
        console.error('[Video] No hay sheet para frame', frameNum);
        return;
    }
    
    try {
        const img = await loadSheetImage(sheet);
        if (!img) return;
        
        const localFrame = frameNum - sheet.startFrame;
        const col = localFrame % videoConfig.columns;
        const row = Math.floor(localFrame / videoConfig.columns);
        
        const srcX = col * videoConfig.frameWidth;
        const srcY = row * videoConfig.frameHeight;
        
        videoCtx.drawImage(
            img,
            srcX, srcY, videoConfig.frameWidth, videoConfig.frameHeight,
            0, 0, videoCanvas.width, videoCanvas.height
        );
        
        updateVideoTimeDisplay();
        
    } catch (e) {
        console.error('[Video] Error drawing frame:', e);
    }
}

function updateVideoTimeDisplay() {
    const timeLabel = document.getElementById('videoTime');
    const seekSlider = document.getElementById('videoSeek');
    
    if (!videoConfig) return;
    
    if (seekSlider) {
        seekSlider.value = videoCurrentFrame;
    }
    
    if (timeLabel) {
        const currentSec = videoCurrentFrame / videoConfig.fps;
        const totalSec = videoConfig.totalFrames / videoConfig.fps;
        const currentMin = Math.floor(currentSec / 60);
        const currentSecRem = Math.floor(currentSec % 60);
        const totalMin = Math.floor(totalSec / 60);
        const totalSecRem = Math.floor(totalSec % 60);
        timeLabel.textContent = `${currentMin}:${currentSecRem.toString().padStart(2, '0')} / ${totalMin}:${totalSecRem.toString().padStart(2, '0')}`;
    }
}

async function initVideo() {
    console.log('[Video] Inicializando...');
    
    videoCanvas = document.getElementById('videoCanvas');
    if (!videoCanvas) {
        console.error('[Video] Canvas no encontrado');
        return false;
    }
    
    videoCtx = videoCanvas.getContext('2d');
    if (!videoCtx) {
        console.error('[Video] No se pudo obtener contexto 2D');
        return false;
    }
    
    if (typeof CORTO_V1_CONFIG === 'undefined') {
        console.error('[Video] No se encontró CORTO_V1_CONFIG');
        return false;
    }
    videoConfig = CORTO_V1_CONFIG;
    
    const seekSlider = document.getElementById('videoSeek');
    if (seekSlider) {
        seekSlider.max = videoConfig.totalFrames - 1;
        seekSlider.value = 0;
    }
    
    videoLoaded = true;
    videoCurrentFrame = 0;
    
    resizeVideo();
    
    console.log('[Video] Listo:', videoConfig.totalFrames, 'frames,', videoConfig.spritesheets.length, 'sheets');
    
    await drawVideoFrame(0);
    
    return true;
}

function resizeVideo() {
    if (!videoCanvas || !videoConfig) return;
    
    videoCanvas.width = videoConfig.frameWidth;
    videoCanvas.height = videoConfig.frameHeight;
    
    if (videoLoaded) {
        drawVideoFrame(videoCurrentFrame);
    }
}

function playVideo() {
    if (videoIsPlaying || !videoLoaded) return;
    
    videoIsPlaying = true;
    console.log('[Video] Play');
    
    const currentSeconds = videoCurrentFrame / videoConfig.fps;
    if (window.onPlayVideoAudio) window.onPlayVideoAudio(currentSeconds.toString());
    
    videoInterval = setInterval(async () => {
        videoCurrentFrame++;
        if (videoCurrentFrame >= videoConfig.totalFrames) {
            videoCurrentFrame = 0;
            if (window.onSeekVideoAudio) window.onSeekVideoAudio("0");
        }
        await drawVideoFrame(videoCurrentFrame);
    }, 1000 / videoConfig.fps);
}

function pauseVideo() {
    videoIsPlaying = false;
    if (videoInterval) {
        clearInterval(videoInterval);
        videoInterval = null;
    }
    if (window.onPauseVideoAudio) window.onPauseVideoAudio();
    console.log('[Video] Pause');
}

function stopVideo() {
    pauseVideo();
    videoCurrentFrame = 0;
    drawVideoFrame(0);
    if (window.onStopVideoAudio) window.onStopVideoAudio();
    console.log('[Video] Stop');
}

function seekVideo(frameNum) {
    videoCurrentFrame = Math.max(0, Math.min(frameNum, videoConfig.totalFrames - 1));
    drawVideoFrame(videoCurrentFrame);
    
    const segundos = videoCurrentFrame / videoConfig.fps;
    if (window.onSeekVideoAudio) window.onSeekVideoAudio(segundos.toString());
}

document.getElementById('videoPlay')?.addEventListener('click', playVideo);
document.getElementById('videoPause')?.addEventListener('click', pauseVideo);
document.getElementById('videoStop')?.addEventListener('click', stopVideo);

const videoSeekEl = document.getElementById('videoSeek');
if (videoSeekEl) {
    videoSeekEl.addEventListener('input', function() {
        pauseVideo();
        seekVideo(parseInt(this.value));
    });
}

document.getElementById('videoFullscreen')?.addEventListener('click', function() {
    const panel = document.getElementById('panelVideo');
    if (panel) {
        panel.classList.toggle('fullscreen');
        if (panel.classList.contains('fullscreen')) {
            this.textContent = '⛶';
        } else {
            this.textContent = '⛶';
        }
        setTimeout(() => resizeVideo(), 100);
    }
});

const videoOpacityEl = document.getElementById('videoOpacity');
if (videoOpacityEl) {
    videoOpacityEl.addEventListener('input', function() {
        const opacity = parseInt(this.value) / 100;
        const panel = document.getElementById('panelVideo');
        if (panel) {
            panel.style.opacity = opacity;
        }
        const txt = document.getElementById('videoOpacityTxt');
        if (txt) txt.textContent = this.value + '%';
    });
}

const videoVolumeEl = document.getElementById('videoVolume');
if (videoVolumeEl) {
    videoVolumeEl.addEventListener('input', function() {
        const volume = parseInt(this.value);
        if (window.onSetVideoVolume) window.onSetVideoVolume(volume.toString());
        const txt = document.getElementById('videoVolumeTxt');
        if (txt) txt.textContent = volume + '%';
    });
}

// VISUAL IMPACTS
const JackpotEngine = {
    isActive: false,
    overlay: null,
    cancelButton: null,
    letterboxTop: null,
    letterboxBottom: null,
    messageBox: null,
    fadeInterval: null,
    countdownInterval: null,
    autoStopTimeout: null,
    fireworksTimeout: null,
    jumpersTimeout: null,
    letterboxRetractTimeout: null,
    messageTimeout: null,
    overlayTransitionInterval: null,
    letterboxRetracted: false,
    messageShown: false,
    messageHidden: false,
    overlayPhase: 'green',
    duration: 33000,
    maxSeconds: 251,
    currentSeconds: 251,
    currentFractions: 99,
    startTime: 0,
    jackpotOutSoundPlayed: false,
    gifs: [],
    jumpers: [],
    countdownElement: null,
    countdownFractions: null,
    countdownContainer: null,
    
    outIsActive: false,
    outCountdownInterval: null,
    outDuration: 5,
    outSeconds: 5,
    outFractions: 99,
    outStartTime: 0,
    outSoundPlayed: false,
    outEnlarged: false,
    outImpactShown: false,
    outOverlay: null,
    outCountdownContainer: null,
    outCountdownElement: null,
    outCountdownFractions: null,
    outLetterboxTop: null,
    outLetterboxBottom: null,
    outLetterboxShown: false,
    
    savedStates: {
        hubVisible: true,
        panels: {},
        supportBallVisible: false,
        socialLinksBallVisible: false
    },
    
    init() {
        this.createOverlay();
        this.createLetterbox();
        this.createMessageBox();
        this.createCancelButton();
        this.createGifs();
        this.createJumpers();
        this.createCountdown();
    },
    
    createOverlay() {
        this.overlay = document.createElement('div');
        this.overlay.id = 'jackpotOverlay';
        this.overlay.style.cssText = `
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(144, 238, 144, 0.1);
            z-index: 9997;
            display: none;
            opacity: 0;
            transition: opacity 3s ease;
        `;
        document.body.appendChild(this.overlay);
    },
    
    createLetterbox() {
        this.letterboxTop = document.createElement('div');
        this.letterboxTop.id = 'letterboxTop';
        this.letterboxTop.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            height: 0px;
            background: #000000;
            z-index: 9998;
            transition: height 1.5s ease;
        `;
        document.body.appendChild(this.letterboxTop);
        
        this.letterboxBottom = document.createElement('div');
        this.letterboxBottom.id = 'letterboxBottom';
        this.letterboxBottom.style.cssText = `
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            height: 0px;
            background: #000000;
            z-index: 9998;
            transition: height 1.5s ease;
        `;
        document.body.appendChild(this.letterboxBottom);
    },
    
    createMessageBox() {
        this.messageBox = document.createElement('div');
        this.messageBox.id = 'jackpotMessage';
        this.messageBox.innerHTML = `
            <div style="margin-bottom: 8px;">This is just a visual effect test for the Jackpot Buff. This test will only take 33 seconds.</div>
            <div style="font-size: 11px; opacity: 0.85;">Notice: In the Let's Go Gambling project, you will have the option to skip this cinematic or remove elements you don't like. Each time you get a jackpot, you can choose to receive the full buff or just the items.</div>
        `;
        this.messageBox.style.cssText = `
            position: fixed;
            bottom: 100px;
            left: 50%;
            transform: translateX(-50%);
            background: rgba(30, 30, 35, 0.95);
            border: 1px solid rgba(212, 175, 55, 0.4);
            border-radius: 8px;
            padding: 15px 25px;
            max-width: 500px;
            text-align: center;
            color: #d4af37;
            font-family: 'Arial', sans-serif;
            font-size: 13px;
            line-height: 1.5;
            z-index: 10003;
            display: none;
            opacity: 0;
            transition: opacity 2s ease;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.5);
        `;
        document.body.appendChild(this.messageBox);
    },
    
    createCancelButton() {
        this.cancelButton = document.createElement('button');
        this.cancelButton.id = 'btnCancelPreview';
        this.cancelButton.textContent = '❌ CANCEL PREVIEW';
        this.cancelButton.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            z-index: 10001;
            padding: 12px 20px;
            background: linear-gradient(180deg, rgba(212, 175, 55, 0.4) 0%, rgba(180, 140, 40, 0.6) 100%);
            border: 1px solid rgba(212, 175, 55, 0.6);
            color: #3d2e0a;
            font-size: 12px;
            font-weight: bold;
            cursor: pointer;
            border-radius: 4px;
            display: none;
            opacity: 0;
            transition: opacity 3s ease;
        `;
        this.cancelButton.addEventListener('click', () => this.cancel());
        document.body.appendChild(this.cancelButton);
    },
    
    createGifs() {
        const fireworkPath = 'Assets/Images/placidplace-fire-works.gif';
        
        const corners = [
            { id: 'fw-bottom-left', src: fireworkPath, bottom: '-80px', left: '-80px', transform: 'none' },
            { id: 'fw-bottom-right', src: fireworkPath, bottom: '-80px', right: '-80px', transform: 'scaleX(-1)' },
            { id: 'fw-top-right', src: fireworkPath, top: '-80px', right: '-80px', transform: 'scaleY(-1)' },
            { id: 'fw-top-left', src: fireworkPath, top: '-80px', left: '-80px', transform: 'scale(-1, -1)' }
        ];
        
        corners.forEach(cfg => {
            const img = document.createElement('img');
            img.id = cfg.id;
            img.src = cfg.src;
            img.style.cssText = `
                position: fixed;
                ${cfg.top ? 'top: ' + cfg.top + ';' : ''}
                ${cfg.bottom ? 'bottom: ' + cfg.bottom + ';' : ''}
                ${cfg.left ? 'left: ' + cfg.left + ';' : ''}
                ${cfg.right ? 'right: ' + cfg.right + ';' : ''}
                width: 320px;
                height: 320px;
                z-index: 9999;
                display: none;
                opacity: 0;
                transition: opacity 1s ease;
                transform: ${cfg.transform};
                pointer-events: none;
            `;
            document.body.appendChild(img);
            this.gifs.push(img);
        });
    },
    
    createJumpers() {
        const jumpers = [
            { id: 'jumper-paarthurnax', file: 'Paarthurnax.png', startX: '15%', rotation: -10, flip: true },
            { id: 'jumper-mijol', file: 'mijol.png', startX: '35%', rotation: 12, flip: true },
            { id: 'jumper-aelia', file: 'Aela.png', startX: '55%', rotation: -15, flip: false },
            { id: 'jumper-serana', file: 'serana.png', startX: '75%', rotation: 8, flip: false }
        ];
        
        jumpers.forEach(cfg => {
            const img = document.createElement('img');
            img.id = cfg.id;
            const flipTransform = cfg.flip ? 'scaleX(-1) ' : '';
            img.style.cssText = `
                position: fixed;
                bottom: -500px;
                left: ${cfg.startX};
                width: 200px;
                height: auto;
                z-index: 10000;
                display: none;
                opacity: 0;
                pointer-events: none;
                transform: ${flipTransform}rotate(${cfg.rotation}deg);
            `;
            document.body.appendChild(img);
            this.jumpers.push({ element: img, rotation: cfg.rotation, flip: cfg.flip, file: cfg.file });
        });
    },
    
    createCountdown() {
        const countdownContainer = document.createElement('div');
        countdownContainer.id = 'jackpotCountdownContainer';
        countdownContainer.style.cssText = `
            position: fixed;
            top: 55px;
            left: 50%;
            transform: translateX(-50%);
            background: rgba(60, 60, 65, 0.95);
            border: 2px solid rgba(80, 80, 85, 0.8);
            border-radius: 6px;
            padding: 8px 14px;
            z-index: 10002;
            display: none;
            opacity: 0;
            transition: opacity 1s ease;
            pointer-events: none;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
        `;
        
        const mainTime = document.createElement('span');
        mainTime.id = 'countdownMain';
        mainTime.style.cssText = `
            font-family: 'Arial', sans-serif;
            font-size: 26px;
            font-weight: bold;
            color: #d4af37;
            text-shadow: 0 0 6px rgba(212, 175, 55, 0.5);
            letter-spacing: 1px;
        `;
        mainTime.textContent = '4:11';
        
        const fractions = document.createElement('span');
        fractions.id = 'countdownFractions';
        fractions.style.cssText = `
            font-family: 'Arial', sans-serif;
            font-size: 17px;
            font-weight: bold;
            color: #d4af37;
            text-shadow: 0 0 4px rgba(212, 175, 55, 0.5);
            letter-spacing: 0px;
            margin-left: 1px;
            vertical-align: baseline;
        `;
        fractions.textContent = ':47';
        
        this.countdownElement = mainTime;
        this.countdownFractions = fractions;
        
        countdownContainer.appendChild(mainTime);
        countdownContainer.appendChild(fractions);
        this.countdownContainer = countdownContainer;
        document.body.appendChild(countdownContainer);
    },
    
    startCountdown() {
        this.currentSeconds = this.maxSeconds;
        this.currentFractions = 99;
        this.fireworksShown = false;
        this.jumpersShown = false;
        this.letterboxRetracted = false;
        this.messageShown = false;
        this.messageHidden = false;
        this.overlayPhase = 'green';
        this.jackpotOutSoundPlayed = false;
        this.startTime = Date.now();
        this.updateCountdownDisplay();
        
        this.countdownInterval = setInterval(() => {
            const elapsedMs = Date.now() - this.startTime;
            const elapsed = elapsedMs / 1000;
            
            const remainingMs = (this.maxSeconds * 1000) - elapsedMs;
            this.currentSeconds = Math.max(0, Math.floor(remainingMs / 1000));
            this.currentFractions = Math.max(0, Math.floor((remainingMs % 1000) / 10));
            this.updateCountdownDisplay();
            
            if (elapsed >= 3 && !this.messageShown) {
                this.messageShown = true;
                this.showMessageBox();
            }
            
            if (elapsed >= 15 && !this.messageHidden) {
                this.messageHidden = true;
                this.hideMessageBox();
            }
            
            if (elapsed >= 10 && elapsed < 20 && this.overlayPhase === 'green') {
                this.overlayPhase = 'transition';
                this.transitionOverlayToGold();
            }
            
            if (elapsed >= 28 && !this.fireworksShown) {
                this.fireworksShown = true;
                this.showGifs();
                this.showCornersOnly();
            }
            
            if (elapsed >= 30 && !this.jumpersShown) {
                this.jumpersShown = true;
                this.showJumpers();
            }
            
            if (elapsed >= 31 && !this.letterboxRetracted) {
                this.letterboxRetracted = true;
                this.hideLetterbox();
            }
            
            if (this.currentSeconds <= 0 && this.currentFractions <= 0) {
                this.stopCountdown();
            }
        }, 10);
    },
    
    stopCountdown() {
        if (this.countdownInterval) {
            clearInterval(this.countdownInterval);
            this.countdownInterval = null;
        }
    },
    
    updateCountdownDisplay() {
        const minutes = Math.floor(this.currentSeconds / 60);
        const seconds = this.currentSeconds % 60;
        const mainDisplay = `${minutes}:${seconds.toString().padStart(2, '0')}`;
        const fractionsDisplay = `:${this.currentFractions.toString().padStart(2, '0')}`;
        this.countdownElement.textContent = mainDisplay;
        this.countdownFractions.textContent = fractionsDisplay;
    },
    
    showJumpers() {
        this.jumpers.forEach((jumper, index) => {
            const el = jumper.element;
            const baseRotation = jumper.rotation;
            const flip = jumper.flip;
            const flipTransform = flip ? 'scaleX(-1) ' : '';
            const startBottom = -500;
            
            if (jumper.file && jackpotImageUrls[jumper.file]) {
                el.src = jackpotImageUrls[jumper.file];
            }
            
            el.style.display = 'block';
            el.style.opacity = '1';
            
            const startX = parseFloat(el.style.left);
            const duration = 2000;
            const startTime = Date.now();
            const delay = index * 150;
            
            setTimeout(() => {
                const animate = () => {
                    const elapsed = Date.now() - startTime - delay;
                    if (elapsed < 0) {
                        requestAnimationFrame(animate);
                        return;
                    }
                    
                    const progress = elapsed / duration;
                    
                    if (progress < 1) {
                        const peakHeight = window.innerHeight * 0.5;
                        const parabola = -4 * progress * (progress - 1);
                        
                        const bottom = startBottom + (peakHeight + Math.abs(startBottom)) * parabola;
                        
                        const rotationVariation = Math.sin(progress * Math.PI * 2) * 15;
                        const currentRotation = baseRotation + rotationVariation;
                        
                        el.style.bottom = `${bottom}px`;
                        el.style.transform = `${flipTransform}rotate(${currentRotation}deg)`;
                        
                        requestAnimationFrame(animate);
                    } else {
                        el.style.opacity = '0';
                        setTimeout(() => {
                            el.style.display = 'none';
                            el.style.bottom = `${startBottom}px`;
                            el.style.transform = `${flipTransform}rotate(${baseRotation}deg)`;
                        }, 500);
                    }
                };
                
                animate();
            }, delay);
        });
    },
    
    showGifs() {
        this.gifs.forEach(gif => {
            gif.style.display = 'block';
            setTimeout(() => {
                gif.style.opacity = '1';
            }, 10);
        });
    },
    
    hideGifs() {
        this.gifs.forEach(gif => {
            gif.style.opacity = '0';
            setTimeout(() => {
                gif.style.display = 'none';
            }, 3000);
        });
    },
    
    showCountdown() {
        if (this.countdownContainer) {
            this.countdownContainer.style.display = 'block';
            setTimeout(() => {
                this.countdownContainer.style.opacity = '1';
            }, 10);
        }
    },
    
    hideCountdown() {
        if (this.countdownContainer) {
            this.countdownContainer.style.opacity = '0';
            setTimeout(() => {
                this.countdownContainer.style.display = 'none';
            }, 2000);
        }
    },
    
    hideAllPanels() {
        const hub = document.getElementById('hub');
        const panelIds = ['panelGif', 'panelVideo', 'panelLogs', 'panelLive'];
        
        this.savedStates.hubVisible = hub && hub.style.display !== 'none';
        
        panelIds.forEach(id => {
            const panel = document.getElementById(id);
            if (panel) {
                this.savedStates.panels[id] = panel.classList.contains('visible');
            }
        });
        
        this.savedStates.supportBallVisible = window.isSupportBallVisible ? window.isSupportBallVisible() : false;
        this.savedStates.socialLinksBallVisible = window.isSocialLinksBallVisible ? window.isSocialLinksBallVisible() : false;
        
        let opacity = 1;
        const fadeStep = () => {
            opacity -= 0.02;
            if (opacity <= 0) {
                opacity = 0;
                if (hub) hub.style.display = 'none';
                panelIds.forEach(id => {
                    const panel = document.getElementById(id);
                    if (panel) panel.classList.remove('visible');
                });
                if (window.hideSupportBall) window.hideSupportBall();
                if (window.hideSocialLinksBall) window.hideSocialLinksBall();
                if (window.hideDailyUpdatesBall) window.hideDailyUpdatesBall();
    if (window.hideKeyTrackerBall) window.hideKeyTrackerBall();
    if (window.hideHubToggle) window.hideHubToggle();
                clearInterval(this.fadeInterval);
            } else {
                if (hub) hub.style.opacity = opacity;
                panelIds.forEach(id => {
                    const panel = document.getElementById(id);
                    if (panel) panel.style.opacity = opacity;
                });
                const supportBall = document.getElementById('support-ball');
                const socialBall = document.getElementById('social-links-ball');
                const dailyBall = document.getElementById('daily-updates-ball');
                if (supportBall) supportBall.style.opacity = opacity;
                if (socialBall) socialBall.style.opacity = opacity;
                if (dailyBall) dailyBall.style.opacity = opacity;
            }
        };
        
        this.fadeInterval = setInterval(fadeStep, 100);
    },
    
    restorePanels() {
        const hub = document.getElementById('hub');
        const panelIds = ['panelGif', 'panelVideo', 'panelLogs', 'panelLive'];
        
        if (hub) {
            hub.style.display = 'block';
            hub.style.opacity = '1';
        }
        
        panelIds.forEach(id => {
            const panel = document.getElementById(id);
            if (panel) {
                panel.style.opacity = '1';
                if (this.savedStates.panels[id]) {
                    panel.classList.add('visible');
                }
            }
        });
        
        if (this.savedStates.supportBallVisible && window.showSupportBall) {
            window.showSupportBall();
        }
        if (this.savedStates.socialLinksBallVisible && window.showSocialLinksBall) {
            window.showSocialLinksBall();
        }
        if (this.savedStates.dailyUpdatesBallVisible && window.showDailyUpdatesBall) {
            window.showDailyUpdatesBall();
        }
        if (this.savedStates.keyTrackerBallVisible && window.showKeyTrackerBall) {
            window.showKeyTrackerBall();
        }
        
        const supportBall = document.getElementById('support-ball');
        const socialBall = document.getElementById('social-links-ball');
        const dailyBall = document.getElementById('daily-updates-ball');
        const keyTrackerBall = document.getElementById('key-tracker-ball');
        if (supportBall) supportBall.style.opacity = '1';
        if (socialBall) socialBall.style.opacity = '1';
        if (dailyBall) dailyBall.style.opacity = '1';
        if (keyTrackerBall) keyTrackerBall.style.opacity = '1';
    },
    
    showOverlay() {
        if (this.overlay) {
            this.overlay.style.display = 'block';
            setTimeout(() => {
                this.overlay.style.opacity = '1';
            }, 10);
        }
    },
    
    hideOverlay() {
        if (this.overlay) {
            this.overlay.style.opacity = '0';
            setTimeout(() => {
                this.overlay.style.display = 'none';
            }, 500);
        }
    },
    
    showCancelButton() {
        if (this.cancelButton) {
            this.cancelButton.style.display = 'block';
            setTimeout(() => {
                this.cancelButton.style.opacity = '1';
            }, 10);
        }
    },
    
    hideCancelButton() {
        if (this.cancelButton) {
            this.cancelButton.style.opacity = '0';
            setTimeout(() => {
                this.cancelButton.style.display = 'none';
            }, 500);
        }
    },
    
    showLetterbox() {
        if (this.letterboxTop) {
            this.letterboxTop.style.height = '80px';
        }
        if (this.letterboxBottom) {
            this.letterboxBottom.style.height = '80px';
        }
    },
    
    hideLetterbox() {
        if (this.letterboxTop) {
            this.letterboxTop.style.height = '0px';
        }
        if (this.letterboxBottom) {
            this.letterboxBottom.style.height = '0px';
        }
    },
    
    showMessageBox() {
        if (this.messageBox) {
            this.messageBox.style.display = 'block';
            setTimeout(() => {
                this.messageBox.style.opacity = '1';
            }, 50);
        }
    },
    
    hideMessageBox() {
        if (this.messageBox) {
            this.messageBox.style.opacity = '0';
            setTimeout(() => {
                this.messageBox.style.display = 'none';
            }, 500);
        }
    },
    
    transitionOverlayToGold() {
        let progress = 0;
        const duration = 10000;
        const startTime = Date.now();
        
        this.overlayTransitionInterval = setInterval(() => {
            const elapsed = Date.now() - startTime;
            progress = Math.min(elapsed / duration, 1);
            
            const r = Math.round(144 + (212 - 144) * progress);
            const g = Math.round(238 + (175 - 238) * progress);
            const b = Math.round(144 + (55 - 144) * progress);
            const alpha = 0.05 + (0.02 * progress);
            
            if (this.overlay) {
                this.overlay.style.background = `rgba(${r}, ${g}, ${b}, ${alpha})`;
            }
            
            if (progress >= 1) {
                clearInterval(this.overlayTransitionInterval);
                this.overlayTransitionInterval = null;
            }
        }, 50);
    },
    
    showCornersOnly() {
        if (this.overlayTransitionInterval) {
            clearInterval(this.overlayTransitionInterval);
            this.overlayTransitionInterval = null;
        }
        
        if (this.overlay) {
            const size = Math.max(window.innerWidth, window.innerHeight) * 0.8;
            this.overlay.style.transition = 'background 3s ease';
            this.overlay.style.background = `radial-gradient(circle at center, transparent ${size}px, rgba(212, 175, 55, 0.04) ${size + 100}px)`;
        }
    },
    
    start() {
        if (this.isActive) return;
        
        this.isActive = true;
        this.messageShown = false;
        this.messageHidden = false;
        this.overlayPhase = 'green';
        this.fireworksShown = false;
        this.jumpersShown = false;
        this.letterboxRetracted = false;
        
        if (this.overlay) {
            this.overlay.style.background = 'rgba(144, 238, 144, 0.1)';
        }
        
        const langSelect = document.getElementById('jackpotLang');
        const lang = langSelect ? langSelect.value : 'en';
        
        if (window.onPlayJackpotSound) {
            window.onPlayJackpotSound('start_' + lang);
        }
        
        this.hideAllPanels();
        this.showOverlay();
        this.showLetterbox();
        this.showCountdown();
        this.showCancelButton();
        
        this.startCountdown();
        
        this.autoStopTimeout = setTimeout(() => {
            this.cancel();
        }, this.duration);
    },
    
    startOut() {
        if (this.outIsActive) return;
        
        this.outIsActive = true;
        this.outSeconds = 5;
        this.outFractions = 99;
        this.outEnlarged = false;
        this.outImpactShown = false;
        this.outLetterboxShown = false;
        this.outSoundPlayed = false;
        this.outStartTime = Date.now();
        
        this.hideAllPanels();
        this.createOutLetterbox();
        this.createOutCountdown();
        this.showOutCountdown();
        this.startOutCountdown();
    },
    
    createOutCountdown() {
        if (this.outCountdownContainer) {
            this.outCountdownContainer.remove();
        }
        
        this.outCountdownContainer = document.createElement('div');
        this.outCountdownContainer.id = 'jackpot-out-countdown-container';
        this.outCountdownContainer.style.cssText = `
            position: fixed;
            top: 55px;
            left: 50%;
            transform: translateX(-50%);
            background: rgba(60, 60, 65, 0.95);
            padding: 12px 20px;
            border-radius: 6px;
            z-index: 10002;
            display: flex;
            align-items: center;
            font-family: 'Arial', sans-serif;
        `;
        
        this.outCountdownElement = document.createElement('span');
        this.outCountdownElement.id = 'jackpot-out-countdown';
        this.outCountdownElement.style.cssText = `
            font-size: 26px;
            color: #d4af37;
            font-weight: bold;
        `;
        this.outCountdownElement.textContent = '0:05';
        
        this.outCountdownFractions = document.createElement('span');
        this.outCountdownFractions.id = 'jackpot-out-fractions';
        this.outCountdownFractions.style.cssText = `
            font-size: 17px;
            color: #d4af37;
            font-weight: bold;
            margin-left: 2px;
        `;
        this.outCountdownFractions.textContent = ':99';
        
        this.outCountdownContainer.appendChild(this.outCountdownElement);
        this.outCountdownContainer.appendChild(this.outCountdownFractions);
        document.body.appendChild(this.outCountdownContainer);
    },
    
    createOutLetterbox() {
        if (this.outLetterboxTop) {
            this.outLetterboxTop.remove();
        }
        if (this.outLetterboxBottom) {
            this.outLetterboxBottom.remove();
        }
        
        this.outLetterboxTop = document.createElement('div');
        this.outLetterboxTop.id = 'jackpot-out-letterbox-top';
        this.outLetterboxTop.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 0px;
            background: rgba(0, 0, 0, 0.95);
            z-index: 9998;
            transition: height 2s ease-out;
            pointer-events: none;
        `;
        document.body.appendChild(this.outLetterboxTop);
        
        this.outLetterboxBottom = document.createElement('div');
        this.outLetterboxBottom.id = 'jackpot-out-letterbox-bottom';
        this.outLetterboxBottom.style.cssText = `
            position: fixed;
            bottom: 0;
            left: 0;
            width: 100%;
            height: 0px;
            background: rgba(0, 0, 0, 0.95);
            z-index: 9998;
            transition: height 2s ease-out;
            pointer-events: none;
        `;
        document.body.appendChild(this.outLetterboxBottom);
    },
    
    showOutCountdown() {
        if (this.outCountdownContainer) {
            this.outCountdownContainer.style.display = 'flex';
        }
    },
    
    hideOutCountdown() {
        if (this.outCountdownContainer) {
            this.outCountdownContainer.style.display = 'none';
        }
    },
    
    startOutCountdown() {
        requestAnimationFrame(() => {
            if (this.outLetterboxTop) {
                this.outLetterboxTop.style.height = '80px';
            }
            if (this.outLetterboxBottom) {
                this.outLetterboxBottom.style.height = '80px';
            }
        });
        
        this.outCountdownInterval = setInterval(() => {
            const elapsedMs = Date.now() - this.outStartTime;
            const elapsed = elapsedMs / 1000;
            
            const remainingMs = (this.outDuration * 1000) - elapsedMs;
            this.outSeconds = Math.max(0, Math.floor(remainingMs / 1000));
            this.outFractions = Math.max(0, Math.floor((remainingMs % 1000) / 10));
            
            if (this.outSeconds <= 2 && !this.outEnlarged) {
                this.outEnlarged = true;
                this.enlargeOutCountdown();
            }
            
            if (remainingMs <= 2200 && !this.outSoundPlayed) {
                this.outSoundPlayed = true;
                if (window.onPlayJackpotOutSound) window.onPlayJackpotOutSound();
            }
            
            if (this.outSeconds <= 0 && this.outFractions <= 0) {
                clearInterval(this.outCountdownInterval);
                this.outCountdownInterval = null;
                this.showOutImpact();
                return;
            }
            
            this.updateOutCountdownDisplay();
        }, 10);
    },
    
    updateOutCountdownDisplay() {
        if (this.outCountdownElement && this.outCountdownFractions) {
            this.outCountdownElement.textContent = `0:0${this.outSeconds}`;
            this.outCountdownFractions.textContent = `:${this.outFractions.toString().padStart(2, '0')}`;
        }
    },
    
    enlargeOutCountdown() {
        if (this.outCountdownContainer) {
            this.outCountdownContainer.style.cssText = `
                position: fixed;
                top: 50%;
                left: 50%;
                transform: translate(-50%, -50%);
                background: transparent;
                z-index: 10002;
                display: flex;
                align-items: center;
                font-family: 'Arial', sans-serif;
            `;
        }
        
        if (this.outCountdownElement) {
            this.outCountdownElement.style.fontSize = '120px';
        }
        
        if (this.outCountdownFractions) {
            this.outCountdownFractions.style.fontSize = '75px';
        }
    },
    
    showOutImpact() {
        if (this.outImpactShown) return;
        this.outImpactShown = true;
        
        if (this.outCountdownContainer) {
            this.outCountdownContainer.style.transition = 'opacity 2.5s ease-out';
        }
        
        if (this.outCountdownElement) {
            this.outCountdownElement.style.color = '#000000';
        }
        if (this.outCountdownFractions) {
            this.outCountdownFractions.style.color = '#000000';
        }
        if (this.outCountdownElement) {
            this.outCountdownElement.textContent = '0:00';
        }
        if (this.outCountdownFractions) {
            this.outCountdownFractions.textContent = ':00';
        }
        
        this.outOverlay = document.createElement('div');
        this.outOverlay.id = 'jackpot-out-overlay';
        this.outOverlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(180, 20, 20, 0.92);
            z-index: 9997;
            pointer-events: none;
            transition: opacity 2.5s ease-out;
        `;
        document.body.appendChild(this.outOverlay);
        
        if (this.outLetterboxTop) {
            this.outLetterboxTop.style.transition = 'height 2.5s ease-out';
        }
        if (this.outLetterboxBottom) {
            this.outLetterboxBottom.style.transition = 'height 2.5s ease-out';
        }
        
        setTimeout(() => {
            if (this.outOverlay) {
                this.outOverlay.style.opacity = '0';
            }
            if (this.outCountdownContainer) {
                this.outCountdownContainer.style.opacity = '0';
            }
            if (this.outLetterboxTop) {
                this.outLetterboxTop.style.height = '0px';
            }
            if (this.outLetterboxBottom) {
                this.outLetterboxBottom.style.height = '0px';
            }
        }, 500);
        
        setTimeout(() => {
            this.endOut();
        }, 3000);
    },
    
    endOut() {
        if (this.outCountdownInterval) {
            clearInterval(this.outCountdownInterval);
            this.outCountdownInterval = null;
        }
        
        if (this.outOverlay) {
            this.outOverlay.remove();
            this.outOverlay = null;
        }
        
        if (this.outCountdownContainer) {
            this.outCountdownContainer.remove();
            this.outCountdownContainer = null;
            this.outCountdownElement = null;
            this.outCountdownFractions = null;
        }
        
        if (this.outLetterboxTop) {
            this.outLetterboxTop.remove();
            this.outLetterboxTop = null;
        }
        
        if (this.outLetterboxBottom) {
            this.outLetterboxBottom.remove();
            this.outLetterboxBottom = null;
        }
        
        this.restorePanels();
        
        this.outIsActive = false;
    },
    
    cancel() {
        if (!this.isActive) return;
        
        if (this.fadeInterval) {
            clearInterval(this.fadeInterval);
            this.fadeInterval = null;
        }
        
        if (this.autoStopTimeout) {
            clearTimeout(this.autoStopTimeout);
            this.autoStopTimeout = null;
        }
        
        if (this.letterboxRetractTimeout) {
            clearTimeout(this.letterboxRetractTimeout);
            this.letterboxRetractTimeout = null;
        }
        
        if (this.messageTimeout) {
            clearTimeout(this.messageTimeout);
            this.messageTimeout = null;
        }
        
        if (this.overlayTransitionInterval) {
            clearInterval(this.overlayTransitionInterval);
            this.overlayTransitionInterval = null;
        }
        
        this.stopCountdown();
        
        if (window.onStopJackpotSound) {
            window.onStopJackpotSound('');
        }
        
        this.hideOverlay();
        this.hideLetterbox();
        this.hideMessageBox();
        this.hideGifs();
        this.hideCountdown();
        this.hideCancelButton();
        this.restorePanels();
        
        this.isActive = false;
    }
};

document.addEventListener('DOMContentLoaded', () => {
    JackpotEngine.init();
});

document.getElementById('btnJackpotStart')?.addEventListener('click', () => JackpotEngine.start());

document.getElementById('btnJackpotEnd')?.addEventListener('click', () => JackpotEngine.startOut());

const ImpactEngine = {
    isActive: false,
    currentType: null,
    overlay: null,
    frame: null,
    dissolve: null,
    
    savedStates: {
        hubVisible: true,
        panels: {}
    },
    
    framePaths: {
        boom: 'Assets/Video/impact_frames/Boom_v1/',
        blood: 'Assets/Video/impact_frames/Cut_blood/',
        parry: 'Assets/Video/impact_frames/Cut_Parry/'
    },
    
    dissolveDuration: {
        boom: 2000,
        blood: 2000,
        parry: 1000
    },
    
    init() {
        this.overlay = document.getElementById('impactOverlay');
        this.frame = document.getElementById('impactFrame');
        this.dissolve = document.getElementById('impactDissolve');
    },
    
    hideAllPanels() {
        const hub = document.getElementById('hub');
        const panelIds = ['panelGif', 'panelVideo', 'panelLogs', 'panelLive'];
        
        this.savedStates.hubVisible = hub && hub.style.display !== 'none';
        
        panelIds.forEach(id => {
            const panel = document.getElementById(id);
            if (panel) {
                this.savedStates.panels[id] = panel.classList.contains('visible');
                panel.classList.remove('visible');
            }
        });
        
        if (hub) hub.style.display = 'none';
        
        this.savedStates.supportBallVisible = window.isSupportBallVisible ? window.isSupportBallVisible() : false;
        this.savedStates.socialLinksBallVisible = window.isSocialLinksBallVisible ? window.isSocialLinksBallVisible() : false;
        this.savedStates.dailyUpdatesBallVisible = window.isDailyUpdatesBallVisible ? window.isDailyUpdatesBallVisible() : false;
        this.savedStates.keyTrackerBallVisible = window.isKeyTrackerBallVisible ? window.isKeyTrackerBallVisible() : false;
        if (window.hideSupportBall) window.hideSupportBall();
        if (window.hideSocialLinksBall) window.hideSocialLinksBall();
        if (window.hideDailyUpdatesBall) window.hideDailyUpdatesBall();
    if (window.hideKeyTrackerBall) window.hideKeyTrackerBall();
    if (window.hideHubToggle) window.hideHubToggle();
    },
    
    restorePanels() {
        const hub = document.getElementById('hub');
        const panelIds = ['panelGif', 'panelVideo', 'panelLogs', 'panelLive'];
        
        if (hub && this.savedStates.hubVisible) {
            hub.style.display = 'block';
        }
        
        panelIds.forEach(id => {
            const panel = document.getElementById(id);
            if (panel && this.savedStates.panels[id]) {
                panel.classList.add('visible');
            }
        });
        
        if (this.savedStates.supportBallVisible && window.showSupportBall) {
            window.showSupportBall();
        }
        if (this.savedStates.socialLinksBallVisible && window.showSocialLinksBall) {
            window.showSocialLinksBall();
        }
        if (this.savedStates.dailyUpdatesBallVisible && window.showDailyUpdatesBall) {
            window.showDailyUpdatesBall();
        }
        if (this.savedStates.keyTrackerBallVisible && window.showKeyTrackerBall) {
            window.showKeyTrackerBall();
        }
    },
    
    playFrames(type) {
        const frameEl = this.frame;
        const overlay = this.overlay;

        const frameBackgrounds = ['#000000', '#FFFFFF', '#000000'];

        frameEl.style.display = 'block';

        const cachedFrames = impactFrameCache[type];
        const frameCount = cachedFrames ? cachedFrames.length : 3;
        let frameIndex = 0;

        const showNextFrame = () => {
            if (frameIndex < frameCount) {
                overlay.style.backgroundColor = frameBackgrounds[frameIndex];
                void overlay.offsetHeight;
                if (cachedFrames && cachedFrames[frameIndex]) {
                    frameEl.src = cachedFrames[frameIndex].url;
                }
                frameIndex++;
                setTimeout(showNextFrame, 50);
            } else {
                frameEl.style.display = 'none';
                overlay.style.backgroundColor = 'transparent';
                this.startDissolve(type);
            }
        };

        showNextFrame();
    },
    
    startDissolve(type) {
        const dissolveEl = this.dissolve;
        const duration = this.dissolveDuration[type];
        
        dissolveEl.className = 'impact-dissolve';
        if (type === 'blood') {
            dissolveEl.classList.add('dissolve-blood');
        } else {
            dissolveEl.classList.add(type === 'parry' ? 'dissolve-white-fast' : 'dissolve-white');
        }
        
        setTimeout(() => {
            this.endImpact();
        }, duration);
    },
    
    endImpact() {
        if (window.onStopImpactSound) window.onStopImpactSound();
        
        if (this.overlay) this.overlay.classList.remove('active');
        
        if (this.dissolve) this.dissolve.className = 'impact-dissolve';
        
        this.restorePanels();
        
        this.isActive = false;
        this.currentType = null;
    },
    
    execute(type) {
        if (this.isActive) return;
        
        this.isActive = true;
        this.currentType = type;
        
        this.hideAllPanels();
        
        if (this.overlay) this.overlay.classList.add('active');
        
        if (window.onPlayImpactSound) window.onPlayImpactSound(type);
        
        this.playFrames(type);
    }
};

document.addEventListener('DOMContentLoaded', () => {
    ImpactEngine.init();
});

document.getElementById('btnImpactBoom')?.addEventListener('click', () => ImpactEngine.execute('boom'));
document.getElementById('btnImpactBlood')?.addEventListener('click', () => ImpactEngine.execute('blood'));
document.getElementById('btnImpactParry')?.addEventListener('click', () => ImpactEngine.execute('parry'));

document.getElementById('btnOpenYouTube')?.addEventListener('click', function() {
    if (window.showConfirmDialog) {
        window.showConfirmDialog(
            'Open this link in your browser?',
            function() {
                if (window.onOpenURL) {
                    window.onOpenURL('https://www.youtube.com/watch?v=fd0Ff15Nk-E');
                } else {
                    window.open('https://www.youtube.com/watch?v=fd0Ff15Nk-E', '_blank');
                }
            },
            null
        );
    } else {
        if (window.onOpenURL) {
            window.onOpenURL('https://www.youtube.com/watch?v=fd0Ff15Nk-E');
        } else {
            window.open('https://www.youtube.com/watch?v=fd0Ff15Nk-E', '_blank');
        }
    }
});

document.getElementById('btnOpenGenerator')?.addEventListener('click', function() {
    if (window.showConfirmDialog) {
        window.showConfirmDialog(
            'Open this link in your browser?',
            function() {
                if (window.onOpenURL) {
                    window.onOpenURL('https://john95ac.github.io/website-documents-John95AC/OSoundtracks_Expansion_Sounds_SA/index.html');
                } else {
                    window.open('https://john95ac.github.io/website-documents-John95AC/OSoundtracks_Expansion_Sounds_SA/index.html', '_blank');
                }
            },
            null
        );
    } else {
        if (window.onOpenURL) {
            window.onOpenURL('https://john95ac.github.io/website-documents-John95AC/OSoundtracks_Expansion_Sounds_SA/index.html');
        } else {
            window.open('https://john95ac.github.io/website-documents-John95AC/OSoundtracks_Expansion_Sounds_SA/index.html', '_blank');
        }
    }
});

// MENU DETECTIONS
let detectedMenusList = [];
let menusSubmenuDragging = false;
let menusSubmenuOffsetX = 0;
let menusSubmenuOffsetY = 0;

document.addEventListener('DOMContentLoaded', function() {
    const submenu = document.getElementById('menusSubmenu');
    const dragHead = document.getElementById('dragMenusSubmenu');
    
    document.getElementById('btnMenuTracking')?.addEventListener('click', function() {
        if (submenu) {
            submenu.classList.add('visible');
            submenu.style.left = '50%';
            submenu.style.top = '50%';
            submenu.style.transform = 'translate(-50%, -50%) scale(' + (cfg.menusSize / 100) + ')';
            if (window.onGetMenusConfig) {
                window.onGetMenusConfig('');
            }
        }
    });

    document.getElementById('btnCloseMenusSubmenu')?.addEventListener('click', function() {
        if (submenu) {
            submenu.classList.remove('visible');
        }
    });
    
    if (dragHead) {
        dragHead.addEventListener('mousedown', function(e) {
            if (e.target.closest('.btn-x')) return;
            menusSubmenuDragging = true;
            const rect = submenu.getBoundingClientRect();
            menusSubmenuOffsetX = e.clientX - rect.left;
            menusSubmenuOffsetY = e.clientY - rect.top;
            submenu.style.left = rect.left + 'px';
            submenu.style.top = rect.top + 'px';
            const scale = cfg.menusSize / 100;
            submenu.style.transform = `scale(${scale})`;
            submenu.style.transformOrigin = 'top left';
            e.preventDefault();
        });
    }
});

document.addEventListener('mousemove', function(e) {
    if (!menusSubmenuDragging) return;
    const submenu = document.getElementById('menusSubmenu');
    if (!submenu) return;
    
    let newX = e.clientX - menusSubmenuOffsetX;
    let newY = e.clientY - menusSubmenuOffsetY;
    
    newX = Math.max(0, Math.min(window.innerWidth - submenu.offsetWidth, newX));
    newY = Math.max(0, Math.min(window.innerHeight - submenu.offsetHeight, newY));
    
    submenu.style.left = newX + 'px';
    submenu.style.top = newY + 'px';
});

document.addEventListener('mouseup', function() {
    if (menusSubmenuDragging) {
        menusSubmenuDragging = false;
        const submenu = document.getElementById('menusSubmenu');
        if (submenu) {
            const scale = cfg.menusSize / 100;
            submenu.style.transform = `scale(${scale})`;
            submenu.style.transformOrigin = 'top left';
        }
    }
    
    if (itemsSubmenuDragging) {
        itemsSubmenuDragging = false;
        const itemsSubmenu = document.getElementById('itemsSubmenu');
        if (itemsSubmenu) {
            const scale = cfg.itemsSize / 100;
            itemsSubmenu.style.transform = `scale(${scale})`;
            itemsSubmenu.style.transformOrigin = 'top left';
        }
    }
});

let itemsSubmenuDragging = false;
let itemsSubmenuOffsetX = 0;
let itemsSubmenuOffsetY = 0;

const notificationTypes = ['game', 'hub', 'false'];
const notificationLabels = {
    'game': 'Game',
    'hub': 'Hub',
    'false': 'Off'
};

let goldEnabled = true;

document.addEventListener('DOMContentLoaded', function() {
    const itemsSubmenu = document.getElementById('itemsSubmenu');
    const dragHead = document.getElementById('dragItemsSubmenu');
    
    document.getElementById('btnItemsConfig')?.addEventListener('click', function() {
        if (itemsSubmenu) {
            itemsSubmenu.classList.add('visible');
            itemsSubmenu.style.left = '50%';
            itemsSubmenu.style.top = '50%';
            itemsSubmenu.style.transform = `translate(-50%, -50%) scale(${cfg.itemsSize / 100})`;
            loadItemsConfig();
        }
    });
    
    document.getElementById('btnCloseItemsSubmenu')?.addEventListener('click', function() {
        if (itemsSubmenu) {
            itemsSubmenu.classList.remove('visible');
        }
    });
    
    document.getElementById('goldAmountInput')?.addEventListener('input', function() {
        saveItemsConfig();
    });
    
    document.getElementById('btnNotificationType')?.addEventListener('click', function() {
        const btn = this;
        const currentType = btn.getAttribute('data-current');
        const currentIndex = notificationTypes.indexOf(currentType);
        const nextIndex = (currentIndex + 1) % notificationTypes.length;
        const nextType = notificationTypes[nextIndex];
        
        btn.setAttribute('data-current', nextType);
        btn.textContent = notificationLabels[nextType];
        saveItemsConfig();
    });
    
    document.getElementById('btnGoldEnabled')?.addEventListener('click', function() {
        goldEnabled = !goldEnabled;
        this.textContent = goldEnabled ? 'Enabled' : 'Disabled';
        this.classList.toggle('disabled', !goldEnabled);
        saveItemsConfig();
    });
    
    document.getElementById('btnTestNotification')?.addEventListener('click', function() {
        showHubNotification('Received 9 Gold', 'gold');
    });
    
    document.getElementById('btnMenusSizeDown')?.addEventListener('click', function() {
        changeMenusSize(-10);
    });
    document.getElementById('btnMenusSizeUp')?.addEventListener('click', function() {
        changeMenusSize(10);
    });
    document.getElementById('btnItemsSizeDown')?.addEventListener('click', function() {
        changeItemsSize(-10);
    });
    document.getElementById('btnItemsSizeUp')?.addEventListener('click', function() {
        changeItemsSize(10);
    });
    
    document.getElementById('toastSizeDown')?.addEventListener('click', function() {
        changeToastSize(-1);
    });
    document.getElementById('toastSizeUp')?.addEventListener('click', function() {
        changeToastSize(1);
    });
    
    if (dragHead) {
        dragHead.addEventListener('mousedown', function(e) {
            if (e.target.closest('.btn-x')) return;
            itemsSubmenuDragging = true;
            const rect = itemsSubmenu.getBoundingClientRect();
            itemsSubmenuOffsetX = e.clientX - rect.left;
            itemsSubmenuOffsetY = e.clientY - rect.top;
            itemsSubmenu.style.left = rect.left + 'px';
            itemsSubmenu.style.top = rect.top + 'px';
            const scale = cfg.itemsSize / 100;
            itemsSubmenu.style.transform = `scale(${scale})`;
            itemsSubmenu.style.transformOrigin = 'top left';
            e.preventDefault();
        });
    }
});

document.addEventListener('mousemove', function(e) {
    if (!itemsSubmenuDragging) return;
    const itemsSubmenu = document.getElementById('itemsSubmenu');
    if (!itemsSubmenu) return;
    
    let newX = e.clientX - itemsSubmenuOffsetX;
    let newY = e.clientY - itemsSubmenuOffsetY;
    
    newX = Math.max(0, Math.min(window.innerWidth - itemsSubmenu.offsetWidth, newX));
    newY = Math.max(0, Math.min(window.innerHeight - itemsSubmenu.offsetHeight, newY));
    
    itemsSubmenu.style.left = newX + 'px';
    itemsSubmenu.style.top = newY + 'px';
});

function showHubNotification(message, colorClass) {
    const toast = document.getElementById('notificationToast');
    const messageEl = document.getElementById('notificationToastMessage');
    
    if (!toast || !messageEl) return;
    
    messageEl.style.fontSize = cfg.toastSize + 'px';
    
    toast.classList.remove('color-gold', 'color-blue', 'color-green', 'color-red', 'color-purple');
    
    if (colorClass) {
        toast.classList.add('color-' + colorClass);
    }
    
    messageEl.textContent = message;
    toast.classList.remove('hidden');
    
    setTimeout(function() {
        toast.style.animation = 'toastSlideOut 0.3s ease-out forwards';
        setTimeout(function() {
            toast.classList.add('hidden');
            toast.style.animation = '';
        }, 300);
    }, 3000);
}

window.updateMenusList = function(jsonStr) {
    try {
        const menus = JSON.parse(jsonStr);
        detectedMenusList = menus;
        
        const container = document.getElementById('menusList');
        if (!container) return;
        
        container.innerHTML = '';
        
        menus.forEach(menu => {
            const item = document.createElement('div');
            item.className = 'menu-item';
            item.setAttribute('data-menu-name', menu.name);
            
            const leftSide = document.createElement('div');
            leftSide.className = 'menu-item-left';
            
            const checkbox = document.createElement('input');
            checkbox.type = 'checkbox';
            checkbox.id = 'menu-' + menu.name.replace(/\s+/g, '-');
            checkbox.checked = menu.enabled === 1;
            checkbox.addEventListener('click', function() {
                if (window.onToggleMenu) {
                    window.onToggleMenu(menu.name);
                }
                show('Saved');
                notify('Saved');
            });
            
            const label = document.createElement('label');
            label.textContent = menu.name;
            label.setAttribute('for', checkbox.id);
            
            leftSide.appendChild(checkbox);
            leftSide.appendChild(label);
            
            const statusBadge = document.createElement('span');
            statusBadge.className = 'menu-status status-closed';
            statusBadge.id = 'status-' + menu.name.replace(/\s+/g, '-');
            statusBadge.textContent = 'CLOSED';
            
            item.appendChild(leftSide);
            item.appendChild(statusBadge);
            container.appendChild(item);
        });
        
        if (menus.length > 10) {
            container.classList.add('scroll-limited');
        } else {
            container.classList.remove('scroll-limited');
        }
        
        console.log('Menus list updated: ' + menus.length + ' menus');
    } catch (e) {
        console.error('Error parsing menus JSON:', e);
    }
};

window.updateItemsConfigUI = function(jsonStr) {
    try {
        const config = JSON.parse(jsonStr);
        
        const notifBtn = document.getElementById('btnNotificationType');
        if (notifBtn && config.showNotification) {
            notifBtn.setAttribute('data-current', config.showNotification);
            const labels = { 'game': 'Game', 'hub': 'Hub', 'false': 'Off' };
            notifBtn.textContent = labels[config.showNotification] || 'Game';
        }
        
        const goldEnabledBtn = document.getElementById('btnGoldEnabled');
        if (goldEnabledBtn && config.goldEnabled !== undefined) {
            goldEnabled = config.goldEnabled;
            goldEnabledBtn.textContent = goldEnabled ? 'Enabled' : 'Disabled';
            goldEnabledBtn.classList.toggle('disabled', !goldEnabled);
        }
        
        const goldAmountInput = document.getElementById('goldAmountInput');
        if (goldAmountInput && config.goldAmount !== undefined) {
            goldAmountInput.value = config.goldAmount;
        }
        
        console.log('Items config UI updated from C++');
    } catch (e) {
        console.error('Error parsing items config JSON:', e);
    }
};

function loadItemsConfig() {
    if (window.onGetItemsConfig) {
        window.onGetItemsConfig('');
    }
}

function saveItemsConfig() {
    const notifType = document.getElementById('btnNotificationType')?.getAttribute('data-current') || 'game';
    const goldEnabledVal = goldEnabled;
    const goldAmountVal = parseInt(document.getElementById('goldAmountInput')?.value) || 9;
    
    const configData = {
        showNotification: notifType,
        goldEnabled: goldEnabledVal,
        goldAmount: goldAmountVal,
        menusSize: cfg.menusSize,
        itemsSize: cfg.itemsSize,
        toastSize: cfg.toastSize
    };
    
    if (window.onSaveItemsConfig) {
        window.onSaveItemsConfig(JSON.stringify(configData));
        console.log('Items config saved:', configData);
    }
}

// STARTUP ROUTINE
const BOOT_DELAY_MS = 5000;
const BOOT_DURATION_MS = 2000;

let bootAnimationState = {
    isRunning: false,
    waitTimeout: null,
    progressInterval: null
};

window.startBootAnimation = function() {
    if (bootAnimationState.isRunning) return;

    bootAnimationState.isRunning = true;

    const bootEl = document.getElementById('bootAnimation');

    bootAnimationState.waitTimeout = setTimeout(() => {
        if (!bootAnimationState.isRunning) return;

        if (cfg.liveAutoStart && bootEl) {
            bootEl.style.left = cfg.bootPosX + '%';
            bootEl.style.top = cfg.bootPosY + '%';
            bootEl.classList.remove('hidden');

            const progressBar = document.getElementById('bootProgressBar');
            if (progressBar) {
                let progress = 0;
                const steps = 100;
                const intervalTime = BOOT_DURATION_MS / steps;

                bootAnimationState.progressInterval = setInterval(() => {
                    progress++;
                    progressBar.style.width = progress + '%';

                    if (progress >= 100) {
                        clearInterval(bootAnimationState.progressInterval);
                        setTimeout(() => finishBootAnimation(), 500);
                    }
                }, intervalTime);
            }
        } else {
            setTimeout(() => finishBootAnimation(), BOOT_DURATION_MS);
        }
    }, BOOT_DELAY_MS);
};

function finishBootAnimation() {
    const bootEl = document.getElementById('bootAnimation');
    if (bootEl) {
        bootEl.classList.add('hidden');
    }

    bootAnimationState.isRunning = false;

    isStartupComplete = true;

    savedPanelStates.gifVisible = cfg.gifVisible;
    savedPanelStates.videoVisible = cfg.videoVisible;
    savedPanelStates.logsVisible = cfg.logsVisible;

    cfg.gifVisible = false;
    cfg.videoVisible = false;
    cfg.logsVisible = false;

    if (cfg.liveVisible) {
        updateLivePanel();

        const panelLive = document.getElementById('panelLive');
        if (panelLive) {
            panelLive.classList.add('persistent-mode');
            panelLive.style.visibility = 'visible';
        }

        if (window.onRequestPlayerStats) {
            livePanelStatsInterval = setInterval(() => {
                window.onRequestPlayerStats('');
            }, 1000);
            window.onRequestPlayerStats('');
        }
    }

    isPersistentMode = true;

    if (pendingPanelLiveVisibility !== null && panelLive) {
        panelLive.style.visibility = pendingPanelLiveVisibility ? 'hidden' : 'visible';
        console.log('Boot: applied pending menu tracking state, hidden=' + pendingPanelLiveVisibility);
    }

    console.log('Boot sequence complete, system active');
}

function isAnyTrackedMenuOpen() {
    return false;
}

window.cancelBootAnimation = function() {
    if (!bootAnimationState.isRunning) return;

    clearTimeout(bootAnimationState.waitTimeout);
    clearInterval(bootAnimationState.progressInterval);
    bootAnimationState.isRunning = false;

    const bootEl = document.getElementById('bootAnimation');
    if (bootEl) {
        bootEl.classList.add('hidden');
    }
};

let bootPreviewState = {
    isActive: false,
    isDragging: false,
    startX: 0,
    startY: 0,
    startLeft: 0,
    startTop: 0,
    intervalId: null,
    moveHandler: null,
    upHandler: null
};

window.previewBootAnimation = function() {
    const bootEl = document.getElementById('bootAnimation');
    const progressBar = document.getElementById('bootProgressBar');
    const previewBtn = document.getElementById('btnBootPreview');

    if (!bootEl || !progressBar) return;

    if (bootPreviewState.isActive) {
        bootEl.classList.add('hidden');
        clearInterval(bootPreviewState.intervalId);
        bootPreviewState.isActive = false;
        if (previewBtn) previewBtn.classList.remove('active');
        
        if (bootPreviewState.moveHandler) {
            document.removeEventListener('mousemove', bootPreviewState.moveHandler);
        }
        if (bootPreviewState.upHandler) {
            document.removeEventListener('mouseup', bootPreviewState.upHandler);
        }
        return;
    }

    cancelBootAnimation();

    let leftPx, topPx;
    if (cfg.bootPosX === 10 && cfg.bootPosY === 10) {
        leftPx = 20;
        topPx = 20;
    } else {
        leftPx = (cfg.bootPosX / 100) * window.innerWidth;
        topPx = (cfg.bootPosY / 100) * window.innerHeight;
    }

    bootEl.style.left = leftPx + 'px';
    bootEl.style.top = topPx + 'px';
    bootEl.style.transform = 'none';
    bootEl.classList.remove('hidden');

    bootPreviewState.isActive = true;
    if (previewBtn) previewBtn.classList.add('active');

    let progress = 0;
    bootPreviewState.intervalId = setInterval(() => {
        progress += 1;
        progressBar.style.width = (progress % 100) + '%';
    }, 100);

    bootPreviewState.moveHandler = (e) => {
        if (!bootPreviewState.isDragging) return;

        const dx = e.clientX - bootPreviewState.startX;
        const dy = e.clientY - bootPreviewState.startY;

        const padding = 10;
        let newLeft = bootPreviewState.startLeft + dx;
        let newTop = bootPreviewState.startTop + dy;

        newLeft = Math.max(padding, Math.min(window.innerWidth - bootEl.offsetWidth - padding, newLeft));
        newTop = Math.max(padding, Math.min(window.innerHeight - bootEl.offsetHeight - padding, newTop));

        bootEl.style.left = newLeft + 'px';
        bootEl.style.top = newTop + 'px';
    };

    bootPreviewState.upHandler = () => {
        if (!bootPreviewState.isDragging) return;
        bootPreviewState.isDragging = false;
        bootEl.style.cursor = 'grab';

        cfg.bootPosX = (bootEl.offsetLeft / window.innerWidth) * 100;
        cfg.bootPosY = (bootEl.offsetTop / window.innerHeight) * 100;

        saveAllConfig();
        show('Boot position saved');
    };

    document.addEventListener('mousemove', bootPreviewState.moveHandler);
    document.addEventListener('mouseup', bootPreviewState.upHandler);
};

document.addEventListener('DOMContentLoaded', function() {
    const bootEl = document.getElementById('bootAnimation');
    if (!bootEl) return;

    bootEl.addEventListener('mousedown', (e) => {
        if (!bootPreviewState.isActive) return;
        
        e.preventDefault();
        bootPreviewState.isDragging = true;
        bootPreviewState.startX = e.clientX;
        bootPreviewState.startY = e.clientY;
        bootPreviewState.startLeft = bootEl.offsetLeft;
        bootPreviewState.startTop = bootEl.offsetTop;
        bootEl.style.cursor = 'grabbing';
    });
});

window.updateMenuStatus = function(menuName, isOpen) {
    const statusId = 'status-' + menuName.replace(/\s+/g, '-');
    const statusBadge = document.getElementById(statusId);
    
    if (!statusBadge) return;
    
    if (isOpen) {
        statusBadge.className = 'menu-status status-open';
        statusBadge.textContent = 'OPEN';
    } else {
        statusBadge.className = 'menu-status status-closed';
        statusBadge.textContent = 'CLOSED';
    }
};

(function() {
    let devShiftHeld = false;
    let devPlusHeld = false;
    let devModeActive = false;
    
    function showDevModeMessage() {
        let msgEl = document.getElementById('devModeMessage');
        if (!msgEl) {
            msgEl = document.createElement('div');
            msgEl.id = 'devModeMessage';
            msgEl.style.cssText = 'position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);background:rgba(0,0,0,0.9);color:#d4af37;font-size:48px;font-weight:bold;padding:30px 60px;border:3px solid #d4af37;border-radius:10px;z-index:99999;font-family:Arial,sans-serif;text-align:center;';
            document.body.appendChild(msgEl);
        }
        msgEl.textContent = 'DEVELOPER MODE';
        msgEl.style.display = 'block';
        
        setTimeout(function() {
            msgEl.style.display = 'none';
        }, 2000);
    }
    
    document.addEventListener('keydown', function(e) {
        if (e.key === 'Shift') {
            devShiftHeld = true;
        }
        
        if ((e.key === '+' || e.key === '=' || e.keyCode === 187 || e.keyCode === 107) && devShiftHeld) {
            devPlusHeld = true;
        }
        
        if ((e.key === '*' || e.keyCode === 56 || e.keyCode === 106) && devShiftHeld && devPlusHeld) {
            const hub = document.getElementById('hub');
            if (hub) {
                showDevModeMessage();
                const toggleState = window.getHubToggleState ? window.getHubToggleState() : null;
                const hubVisibleToggle = toggleState ? toggleState.hubVisible : true;
                
                if (!devModeActive) {
                    if (hubVisibleToggle) {
                        hub.classList.remove('hidden');
                    }
                    if (window.showSupportBall) window.showSupportBall();
                    if (window.showSocialLinksBall) window.showSocialLinksBall();
                    if (window.showDailyUpdatesBall) window.showDailyUpdatesBall();
                    if (window.showKeyTrackerBall) window.showKeyTrackerBall();
                    if (window.showHubToggle) window.showHubToggle();
                    devModeActive = true;
                } else {
                    hub.classList.add('hidden');
                    if (window.hideSupportBall) window.hideSupportBall();
                    if (window.hideSocialLinksBall) window.hideSocialLinksBall();
                    if (window.hideDailyUpdatesBall) window.hideDailyUpdatesBall();
                    if (window.hideKeyTrackerBall) window.hideKeyTrackerBall();
                    if (window.hideHubToggle) window.hideHubToggle();
                    devModeActive = false;
                }
            }
            e.preventDefault();
        }
    });
    
    document.addEventListener('keyup', function(e) {
        if (e.key === 'Shift') {
            devShiftHeld = false;
            devPlusHeld = false;
        }
        if (e.key === '+' || e.key === '=' || e.keyCode === 187 || e.keyCode === 107) {
            devPlusHeld = false;
        }
    });
})();

function applyTitleWithTruncation(title, titleEl) {
    const MAX_CHARS = 20;
    const defaultText = 'No track playing right now - select music to start';
    
    if (!titleEl) return;
    
    const displayTitle = title || defaultText;
    const needsMarquee = displayTitle.length > MAX_CHARS;
    
    let innerSpan = titleEl.querySelector('.title-text');
    if (!innerSpan) {
        innerSpan = document.createElement('span');
        innerSpan.className = 'title-text';
        titleEl.textContent = '';
        titleEl.appendChild(innerSpan);
    }
    
    innerSpan.textContent = displayTitle;
    
    if (needsMarquee) {
        titleEl.classList.add('marquee');
    } else {
        titleEl.classList.remove('marquee');
    }
}

// MUSIC STATUS
window.updatePlayerDisplay = function(songTitle, currentTime, totalTime, progress) {
    const titleEl = document.getElementById('playerSongTitle');
    const currentEl = document.getElementById('playerCurrentTime');
    const totalEl = document.getElementById('playerTotalTime');
    const progressEl = document.getElementById('playerProgressFill');
    
    applyTitleWithTruncation(songTitle, titleEl);
    
    if (currentEl) currentEl.textContent = currentTime || '00:00';
    if (totalEl) totalEl.textContent = totalTime || '00:00';
    if (progressEl) {
        const p = Math.max(0, Math.min(100, progress || 0));
        progressEl.style.width = p + '%';
    }
};

window.setPlayerCover = function(imageUrl) {
    const coverEl = document.getElementById('playerCoverImg');
    if (coverEl) {
        if (imageUrl) {
            coverEl.src = imageUrl;
            coverEl.style.display = 'block';
        } else {
            coverEl.style.display = 'none';
        }
    }
};

window.setPlayerDiscGif = function(gifUrl) {
    const discEl = document.getElementById('playerDiscGif');
    if (discEl) {
        if (gifUrl) {
            discEl.src = gifUrl;
            discEl.style.display = 'block';
        } else {
            discEl.src = 'Assets/Images/viaductk-music-7683.gif';
            discEl.style.display = 'block';
        }
    }
};
var discGifImg = null;
var discGifCanvas = null;
var discGifPaused = false;

function freezeDiscGif() {
    if (!discGifImg || !discGifCanvas) return;
    var ctx = discGifCanvas.getContext('2d');
    discGifCanvas.width = discGifImg.offsetWidth;
    discGifCanvas.height = discGifImg.offsetHeight;
    ctx.drawImage(discGifImg, 0, 0, discGifCanvas.width, discGifCanvas.height);
    discGifImg.style.display = 'none';
    discGifCanvas.style.display = 'block';
    discGifPaused = true;
}

function unfreezeDiscGif() {
    if (!discGifImg || !discGifCanvas) return;
    discGifImg.style.display = 'block';
    discGifCanvas.style.display = 'none';
    discGifPaused = false;
}

document.addEventListener('DOMContentLoaded', function() {
    discGifImg = document.getElementById('playerDiscGif');
    discGifCanvas = document.getElementById('playerDiscGifFreeze');
    
    if (discGifImg) {
        discGifImg.addEventListener('click', function(e) {
            e.preventDefault();
            e.stopPropagation();
            if (discGifPaused) {
                if (window.onResumeMusic) {
                    window.onResumeMusic('');
                    unfreezeDiscGif();
                }
            } else {
                if (window.onPauseMusic) {
                    window.onPauseMusic('');
                    freezeDiscGif();
                }
            }
        });
    }
    
    if (discGifCanvas) {
        discGifCanvas.addEventListener('click', function(e) {
            e.preventDefault();
            e.stopPropagation();
            if (window.onResumeMusic) {
                window.onResumeMusic('');
                unfreezeDiscGif();
            }
        });
    }
});

document.querySelectorAll('.config-submenu-btn').forEach(btn => {
    btn.addEventListener('click', function() {
        document.querySelectorAll('.config-submenu-btn').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        document.querySelectorAll('.config-content').forEach(c => c.classList.add('hidden'));
        
        const target = this.getAttribute('data-config');
        const targetElement = document.getElementById('config' + target.charAt(0).toUpperCase() + target.slice(1));
        if (targetElement) {
            targetElement.classList.remove('hidden');
        }
    });
});

function updateSliderColor(slider) {
    const value = parseInt(slider.value);
    
    slider.classList.remove('color-0', 'color-50', 'color-100', 'color-150', 'color-200');
    const valueTxt = document.getElementById(slider.id + 'Txt');
    if (valueTxt) {
        valueTxt.classList.remove('color-0', 'color-50', 'color-100', 'color-150', 'color-200');
    }
    
    if (value <= 50) {
        slider.classList.add('color-0');
        if (valueTxt) valueTxt.classList.add('color-0');
    } else if (value <= 100) {
        slider.classList.add('color-50');
        if (valueTxt) valueTxt.classList.add('color-50');
    } else if (value <= 150) {
        slider.classList.add('color-100');
        if (valueTxt) valueTxt.classList.add('color-100');
    } else {
        slider.classList.add('color-200');
        if (valueTxt) valueTxt.classList.add('color-200');
    }
}

document.querySelectorAll('.config-slider').forEach(slider => {
    const valueTxt = document.getElementById(slider.id + 'Txt');
    if (valueTxt) {
        valueTxt.textContent = slider.value + '%';
    }
    
    updateSliderColor(slider);
    
    slider.addEventListener('input', function() {
        const valueTxt = document.getElementById(this.id + 'Txt');
        if (valueTxt) {
            valueTxt.textContent = this.value + '%';
        }
        updateSliderColor(this);
        
        const iniKey = this.getAttribute('data-ini');
        const iniValue = (this.value / 100).toFixed(1);
        if (window.onUpdateIniValue) {
            window.onUpdateIniValue(JSON.stringify({key: iniKey, value: iniValue}));
        }
    });
});

document.querySelectorAll('.config-toggle').forEach(btn => {
    btn.addEventListener('click', function() {
        this.classList.toggle('active');
        const isActive = this.classList.contains('active');
        
        if (isActive) {
            this.textContent = this.id.includes('Notification') ? 'Visible' : 'Enabled';
        } else {
            this.textContent = this.id.includes('Notification') ? 'Hidden' : 'Disabled';
        }
        
        const iniKey = this.getAttribute('data-ini');
        const iniValue = isActive ? 'true' : 'false';
        if (window.onUpdateIniValue) {
            window.onUpdateIniValue(JSON.stringify({key: iniKey, value: iniValue}));
        }
    });
});

const toastCheckbox = document.getElementById('cfgToastEnabled');
if (toastCheckbox) {
    toastCheckbox.addEventListener('change', function() {
        configToastEnabled = this.checked;
        
        const iniKey = this.getAttribute('data-ini');
        const iniValue = this.checked ? 'true' : 'false';
        if (window.onUpdateIniValue) {
            window.onUpdateIniValue(JSON.stringify({key: iniKey, value: iniValue}));
        }
    });
}

document.querySelectorAll('.config-select').forEach(select => {
    select.addEventListener('change', function() {
        const iniKey = this.getAttribute('data-ini');
        const iniValue = this.value;
        if (window.onUpdateIniValue) {
            window.onUpdateIniValue(JSON.stringify({key: iniKey, value: iniValue}));
        }
    });
});

window.loadOsoundtracksConfig = function(configData) {
    
    for (const [key, value] of Object.entries(configData)) {
        if (key === 'Toast') {
            configToastEnabled = value === 'true';
            const toastCheckbox = document.getElementById('cfgToastEnabled');
            if (toastCheckbox) {
                toastCheckbox.checked = configToastEnabled;
            }
            continue;
        }
        
        const element = document.querySelector(`[data-ini="${key}"]`);
        if (!element) continue;
        
        if (element.type === 'range') {
            element.value = Math.round(parseFloat(value) * 100);
            const valueTxt = document.getElementById(element.id + 'Txt');
            if (valueTxt) {
                valueTxt.textContent = element.value + '%';
            }
            updateSliderColor(element);
        } else if (element.classList.contains('config-toggle')) {
            const isActive = value === 'true';
            element.classList.toggle('active', isActive);
            if (isActive) {
                element.textContent = element.id.includes('Notification') ? 'Visible' : 'Enabled';
            } else {
                element.textContent = element.id.includes('Notification') ? 'Hidden' : 'Disabled';
            }
        } else if (element.tagName === 'SELECT') {
            element.value = value;
        }
    }
};

window.updateAuthorsList = function(jsonStr) {
    try {
        const authors = JSON.parse(jsonStr);
        
        const authorSelect = document.getElementById('cfgAuthor');
        if (!authorSelect) {
            console.warn('cfgAuthor dropdown not found');
            return;
        }
        
        const savedAuthor = authorSelect.value;
        
        authorSelect.innerHTML = '';
        
        authors.forEach(author => {
            const option = document.createElement('option');
            option.value = author;
            option.textContent = author;
            authorSelect.appendChild(option);
        });
        
        if (savedAuthor && authors.includes(savedAuthor)) {
            authorSelect.value = savedAuthor;
        }
        
        console.log('Authors list updated: ' + authors.length + ' authors');
    } catch (e) {
        console.error('Error parsing authors JSON:', e);
    }
};

window.loadOsoundtracksAuthors = function(authorsList) {
    if (authorsList && authorsList.length > 0) {
        const jsonStr = JSON.stringify(authorsList);
        window.updateAuthorsList(jsonStr);
    }
};

window.onGetAuthorsConfig = function(data) {
};

const configTooltips = {
    'BaseVolume': {
        title: 'Base Volume',
        message: 'Controls the volume for base animations (families without numbers). 0% = mute, 100% = normal volume, above 100% = amplification. Warning: High amplification may affect hearing.'
    },
    'MenuVolume': {
        title: 'Menu Volume',
        message: 'Controls the volume for menu sounds (Start, OStimAlignMenu). 0% = mute, 100% = normal volume, above 100% = amplification.'
    },
    'SpecificVolume': {
        title: 'Specific Volume',
        message: 'Controls the volume for specific animations (with numbers like Animation-1, Animation-2). 0% = mute, 100% = normal volume, above 100% = amplification.'
    },
    'EffectVolume': {
        title: 'Effect Volume',
        message: 'Controls the volume for sound effects (reserved for future features). WIP. 0% = mute, 100% = normal volume, above 100% = amplification.'
    },
    'PositionVolume': {
        title: 'Position Volume',
        message: 'Controls the volume for position-based sounds (fragment matching like hug, kiss, etc.). 0% = mute, 100% = normal volume, above 100% = amplification.'
    },
    'TAGVolume': {
        title: 'TAG Volume',
        message: 'Controls the volume for tag-based sounds (reserved for future features). WIP. 0% = mute, 100% = normal volume, above 100% = amplification.'
    },
    'SoundMenuKeyVolume': {
        title: 'SoundMenu Volume',
        message: 'Controls the volume for background music during OStim. 0% = mute, 100% = normal volume, above 100% = amplification.'
    },
    'MasterVolumeEnabled': {
        title: 'Master Volume',
        message: 'Master volume control switch. When enabled, all volume controls above are active. When disabled, system default volume (100%) is used for all channels.'
    },
    'Visible': {
        title: 'Top Notifications',
        message: 'Show or hide in-game notifications in the top-left corner. Displays messages like "OSoundtracks - SongName is played" when a song starts.'
    },
    'Startup': {
        title: 'Startup Sound',
        message: 'Activates or deactivates the startup sound when the plugin loads. The cat sound you hear suddenly at game start.'
    },
    'MuteGameMusicDuringOStim': {
        title: 'Skyrim Audio',
        message: 'Background music mute mode for Skyrim. false = disable completely. MUSCombatBoss = Option A (works in most versions). MUSSpecialDeath = Option B (low notes, subtle transition).'
    },
    'SoundMenuKey': {
        title: 'SoundMenu Mode',
        message: 'Playback mode for background music during OStim animations. false = disabled. All_Order = all songs sequential. All_Random = all songs random. Author_Order = selected author sequential. Author_Random = selected author random.'
    },
    'Author': {
        title: 'Author Selection',
        message: 'Select the author for Author_Order or Author_Random modes. Choose from the list of available authors.'
    },
    'NowPlayingMode': {
        title: 'Now Playing Mode',
        message: 'Controls when the Now Playing panel appears. Disabled = never shows automatically. Timed = shows for X seconds when track changes. Always = visible while music is playing.'
    },
    'PlayerGifButton': {
        title: 'Player GIF Button',
        message: 'Select the animated GIF displayed in the Now Playing panel. Cat = playful cat animation. Audio = audio visualizer style. Heart = heart pulse animation. Vinyl = vinyl record spinning.'
    }
};

let configToastTimeout = null;
let configToastEnabled = true;

function showConfigToast(controlId) {
    if (!configToastEnabled) return;
    
    const toast = document.getElementById('configToast');
    const title = document.getElementById('configToastTitle');
    const message = document.getElementById('configToastMessage');
    
    const tooltip = configTooltips[controlId];
    if (!tooltip) return;
    
    title.textContent = tooltip.title;
    message.textContent = tooltip.message;
    
    if (configToastTimeout) {
        clearTimeout(configToastTimeout);
        configToastTimeout = null;
    }
    
    toast.classList.remove('hidden');
    setTimeout(() => {
        toast.classList.add('visible');
    }, 10);
    
    configToastTimeout = setTimeout(() => {
        hideConfigToast();
    }, 5000);
}

function hideConfigToast() {
    const toast = document.getElementById('configToast');
    toast.classList.remove('visible');
    setTimeout(() => {
        toast.classList.add('hidden');
    }, 300);
}

document.querySelectorAll('.config-slider').forEach(slider => {
    const iniKey = slider.getAttribute('data-ini');
    
    slider.addEventListener('mouseenter', function() {
        showConfigToast(iniKey);
    });
    
    slider.addEventListener('focus', function() {
        showConfigToast(iniKey);
    });
});

document.querySelectorAll('.config-toggle').forEach(toggle => {
    const iniKey = toggle.getAttribute('data-ini');
    
    toggle.addEventListener('mouseenter', function() {
        showConfigToast(iniKey);
    });
    
    toggle.addEventListener('focus', function() {
        showConfigToast(iniKey);
    });
});

document.querySelectorAll('.config-select').forEach(select => {
    const iniKey = select.getAttribute('data-ini');
    
    select.addEventListener('mouseenter', function() {
        showConfigToast(iniKey);
    });
    
    select.addEventListener('focus', function() {
        showConfigToast(iniKey);
    });
});

document.querySelectorAll('.visibility-option').forEach(option => {
    option.addEventListener('mouseenter', function() {
        showConfigToast('NowPlayingMode');
    });
    
    option.addEventListener('focus', function() {
        showConfigToast('NowPlayingMode');
    }, true);
});

document.querySelectorAll('.gif-option').forEach(option => {
    option.addEventListener('mouseenter', function() {
        showConfigToast('PlayerGifButton');
    });
    
    option.addEventListener('focus', function() {
        showConfigToast('PlayerGifButton');
    }, true);
});

let catClickCount = 0;
let catClickTimeout = null;

function showSecretToast(message) {
    const toast = document.getElementById('secret-toast');
    const messageEl = document.getElementById('secret-toast-message');
    
    if (!toast || !messageEl) return;
    
    messageEl.textContent = message;
    toast.classList.add('visible');
    
    setTimeout(() => {
        toast.classList.remove('visible');
    }, 2000);
}

let jackpotImagesLoaded = false;
let jackpotImageCache = {};
let jackpotImageUrls = {};

function preloadJackpotImages() {
    if (jackpotImagesLoaded) return;

    try {
        const b64 = window.ADVANCE_JACKPOT_BIN_B64;
        const names = window.ADVANCE_JACKPOT_NAMES;

        if (!b64 || !names) {
            console.error('Jackpot assets not loaded');
            return;
        }

        const binaryString = atob(b64);
        const bytes = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
            bytes[i] = binaryString.charCodeAt(i);
        }
        const buffer = bytes.buffer;

        const view = new DataView(buffer);
        const count = view.getUint32(0, true);

        for (let i = 0; i < count; i++) {
            const imgOffset = view.getUint32(4 + i * 8, true);
            const imgSize = view.getUint32(4 + i * 8 + 4, true);

            const slice = bytes.slice(imgOffset, imgOffset + imgSize);
            const blob = new Blob([slice], { type: 'image/png' });
            const url = URL.createObjectURL(blob);

            const img = new Image();
            img.src = url;

            const name = names[i] || ('image_' + i);
            jackpotImageCache[name] = img;
            jackpotImageUrls[name] = url;
        }

        jackpotImagesLoaded = true;
    } catch (error) {
        console.error('Error loading jackpot images:', error);
    }
}

let impactFramesLoaded = false;
let impactFrameCache = {};

function decodeBinFromB64(b64) {
    const binaryString = atob(b64);
    const bytes = new Uint8Array(binaryString.length);
    for (let i = 0; i < binaryString.length; i++) {
        bytes[i] = binaryString.charCodeAt(i);
    }
    return bytes;
}

function extractFramesFromBin(bytes, names) {
    const buffer = bytes.buffer;
    const view = new DataView(buffer);
    const count = view.getUint32(0, true);
    const frames = [];

    for (let i = 0; i < count; i++) {
        const imgOffset = view.getUint32(4 + i * 8, true);
        const imgSize = view.getUint32(4 + i * 8 + 4, true);

        const slice = bytes.slice(imgOffset, imgOffset + imgSize);
        const blob = new Blob([slice], { type: 'image/webp' });
        const url = URL.createObjectURL(blob);
        const name = names[i] || ('frame_' + i);
        frames.push({ name, url });
    }

    return frames;
}

function preloadImpactFrames() {
    if (impactFramesLoaded) return;

    try {
        const types = [
            { key: 'boom', b64Var: 'BOOM_V1_BIN_B64', namesVar: 'BOOM_V1_NAMES' },
            { key: 'blood', b64Var: 'CUT_BLOOD_BIN_B64', namesVar: 'CUT_BLOOD_NAMES' },
            { key: 'parry', b64Var: 'CUT_PARRY_BIN_B64', namesVar: 'CUT_PARRY_NAMES' }
        ];

        types.forEach(t => {
            const b64 = window[t.b64Var];
            const names = window[t.namesVar];

            if (!b64 || !names) {
                console.error('Impact frames not loaded for', t.key);
                return;
            }

            const bytes = decodeBinFromB64(b64);
            const frames = extractFramesFromBin(bytes, names);

            impactFrameCache[t.key] = frames;
        });

        impactFramesLoaded = true;
    } catch (error) {
        console.error('Error loading impact frames:', error);
    }
}

let secretCatBarInterval = null;

function triggerPreviewCat() {
    const bootEl = document.getElementById('bootAnimation');
    const progressBar = document.getElementById('bootProgressBar');

    if (!bootEl || !progressBar) {
        preloadJackpotImages();
        return;
    }

    cancelBootAnimation();

    let leftPx, topPx;
    if (cfg.bootPosX === 10 && cfg.bootPosY === 10) {
        leftPx = 20;
        topPx = 20;
    } else {
        leftPx = (cfg.bootPosX / 100) * window.innerWidth;
        topPx = (cfg.bootPosY / 100) * window.innerHeight;
    }

    bootEl.style.left = leftPx + 'px';
    bootEl.style.top = topPx + 'px';
    bootEl.style.transform = 'none';
    bootEl.classList.remove('hidden');

    let progress = 0;
    const steps = 100;
    const intervalTime = 2000 / steps;

    secretCatBarInterval = setInterval(() => {
        progress++;
        progressBar.style.width = progress + '%';

        if (progress >= 100) {
            clearInterval(secretCatBarInterval);
            secretCatBarInterval = null;
            setTimeout(() => {
                bootEl.classList.add('hidden');
                progressBar.style.width = '0%';
            }, 500);
        }
    }, intervalTime);

    preloadJackpotImages();
}

function activateImpactTab() {
    const impactTab = document.getElementById('tab-impact');
    const impactNavItem = document.querySelector('[data-target="tab-impact"]');
    
    if (impactNavItem) {
        impactNavItem.style.display = '';
    }
    
    document.querySelectorAll('.hub-tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.hub-tab-content').forEach(c => c.classList.remove('active'));
    
    if (impactNavItem) {
        impactNavItem.classList.add('active');
    }
    
    if (impactTab) {
        impactTab.classList.add('active');
    }
    
    triggerPreviewCat();
    preloadImpactFrames();
    if (window.onPlayRobotTalkSound) window.onPlayRobotTalkSound();
}

document.addEventListener('DOMContentLoaded', function() {
    const impactNavItem = document.querySelector('[data-target="tab-impact"]');
    
    if (impactNavItem) {
        impactNavItem.style.display = 'none';
    }
});

document.getElementById('btnSecretCat')?.addEventListener('click', function() {
    catClickCount++;
    
    clearTimeout(catClickTimeout);
    catClickTimeout = setTimeout(() => {
        catClickCount = 0;
    }, 2000);
    
    if (catClickCount === 1) {
        showSecretToast('meow');
        if (window.onPlayMeowSound) window.onPlayMeowSound();
    }
    
    if (catClickCount === 3) {
        showSecretToast('2 more for frame impacts');
    }
    
    if (catClickCount >= 5) {
        catClickCount = 0;
        activateImpactTab();
        showSecretToast('Secret mode activated');
    }
});

const DwemerDoomEngine = {
    isActive: false,
    savedStates: {},
    overlay: null,
    iframe: null,
    globalKeyHandler: null,

    init() {
        this.overlay = document.getElementById('dwemerDoomOverlay');
        this.iframe = document.getElementById('dwemerDoomFrame');
    },

    open() {
        if (this.isActive) return;
        this.isActive = true;

        this.savedStates.hubVisible = document.getElementById('hub')?.style.display !== 'none';

        document.getElementById('hub').style.display = 'none';

        this.iframe.src = 'Assets/Media/nexus-render-x7.html';
        this.overlay.classList.add('active');

        window.onPlayDoomMusic?.();

        setTimeout(() => { this.iframe.focus(); }, 100);

        this.globalKeyHandler = (e) => {
            if (e.key === 'Escape') {
                this.close();
                e.preventDefault();
            }
        };
        document.addEventListener('keydown', this.globalKeyHandler, true);
    },

    close() {
        if (!this.isActive) return;
        this.isActive = false;

        window.onStopDoomMusic?.();

        document.removeEventListener('keydown', this.globalKeyHandler, true);
        this.iframe.src = '';
        this.overlay.classList.remove('active');

        if (this.savedStates.hubVisible) {
            document.getElementById('hub').style.display = 'block';
        }
    }
};

document.getElementById('btnDwemerDoom')?.addEventListener('click', function() {
    if (window.showConfirmDialog) {
        window.showConfirmDialog(
            'Try Project Doom Alpha?',
            function() {
                DwemerDoomEngine.open();
            },
            null
        );
    } else {
        DwemerDoomEngine.open();
    }
});
document.getElementById('btnDoomExit')?.addEventListener('click', () => DwemerDoomEngine.close());
DwemerDoomEngine.init();

function applyDefaultConfig() {
    cfg.firstKey = 0x2A;
    cfg.secondKey = 0x11;
    cfg.singleMode = false;
    cfg.volume = 70;
    cfg.muteHubSound = false;
    cfg.hubSound = 'miau-PDA.wav';
    cfg.gifVisible = false;
    cfg.videoVisible = false;
    cfg.logsVisible = false;
    cfg.logsAutoRefresh = true;
    cfg.liveVisible = true;
    cfg.liveAutoStart = false;
    cfg.globalAlpha = 95;
    cfg.globalColor = 'default';
    cfg.liveBackground = 'glass-bg';
    cfg.hubVisibleSettings = true;

    nowPlayingState.liveNowPlayingMode = 'timed';
    nowPlayingState.liveNowPlayingSeconds = 20;
    nowPlayingState.liveNowPlayingGif = 'agp_studios-audio-22816.gif';

    document.getElementById('firstKey').value = '0x2A';
    document.getElementById('secondKey').value = '0x11';
    document.getElementById('singleMode').checked = false;
    document.getElementById('muteHubSound').checked = false;
    document.getElementById('hubSound').value = 'miau-PDA.wav';
    document.getElementById('vol').value = 70;
    document.getElementById('vol-txt').textContent = '70';
    document.getElementById('logsAutoRefresh').checked = true;

    setColorTheme('default');
    setGlobalOpacity(95);
    setLiveBackground('glass-bg');

    document.querySelectorAll('input[name="nowPlayingMode"]').forEach(function(r) {
        r.checked = (r.value === 'timed');
    });
    var npsEl = document.getElementById('nowPlayingSeconds');
    if (npsEl) { npsEl.value = 20; }

    var timedRow = document.getElementById('timedSecondsRow');
    if (timedRow) { timedRow.style.display = 'flex'; }

    var discEl = document.getElementById('nowPlayingDisc');
    if (discEl) { discEl.src = 'Assets/Images/agp_studios-audio-22816.gif'; }

    document.querySelectorAll('.gif-option').forEach(function(o) {
        o.classList.toggle('active', o.dataset.gif === 'agp_studios-audio-22816.gif');
    });

    var volSliders = {
        cfgSoundMenuKeyVolume: 10,
        cfgMenuVolume: 80,
        cfgBaseVolume: 130,
        cfgSpecificVolume: 120,
        cfgEffectVolume: 40,
        cfgPositionVolume: 130,
        cfgTAGVolume: 120
    };
    Object.keys(volSliders).forEach(function(id) {
        var s = document.getElementById(id);
        if (s) {
            s.value = volSliders[id];
            var vt = document.getElementById(id + 'Txt');
            if (vt) { vt.textContent = volSliders[id] + '%'; }
            updateSliderColor(s);
            s.dispatchEvent(new Event('input', { bubbles: true }));
        }
    });

    var masterVolBtn = document.getElementById('cfgMasterVolumeEnabled');
    if (masterVolBtn && !masterVolBtn.classList.contains('active')) { masterVolBtn.click(); }

    var topNotifBtn = document.getElementById('cfgTopNotifications');
    if (topNotifBtn) {
        topNotifBtn.textContent = 'Hidden';
        if (topNotifBtn.classList.contains('active')) { topNotifBtn.classList.remove('active'); }
    }

    var startupBtn = document.getElementById('cfgStartupSound');
    if (startupBtn && !startupBtn.classList.contains('active')) { startupBtn.click(); }

    var skyrimAudioSelect = document.getElementById('cfgSkyrimAudio');
    if (skyrimAudioSelect) {
        skyrimAudioSelect.value = 'MUSSpecialDeath';
        skyrimAudioSelect.dispatchEvent(new Event('change', { bubbles: true }));
    }

    cfg.gifVisible = false;
    updateGifPanel();
    cfg.videoVisible = false;
    updateVideoPanel();
    cfg.logsVisible = false;
    updateLogsPanel();
    cfg.liveVisible = true;
    updateLivePanel();

    saveAllConfig();
    if (window.onUpdateIniValue) {
        window.onUpdateIniValue(JSON.stringify({key: 'Toast', value: 'false'}));
    }

    show('Default config applied (John settings)');
}

configTooltips.DefaultConfig = {
    title: 'Default Config',
    message: 'Applies John preset configuration:\n\n' +
        'Activation: L.Shift + W, Single Key = off\n' +
        'Volume: 70%, Mute Hub = off\n' +
        'Panels: GIF/Video/Logs = off, Live = ON\n' +
        'Live Now Playing: Timed (20s), Audio GIF\n' +
        'Live Background: Glass\n' +
        'Theme: Default, Opacity: 95%\n' +
        'Volumes: SoundMenu=10%, Menu=80%, Base=130%,\n' +
        'Specific=120%, Effect=40%, Position=130%, TAG=120%\n' +
        'Skyrim Audio: Option B (MUSSpecialDeath)\n' +
        'Toast Notifications: off'
};

var btnDefaultConfigEl = document.getElementById('btnDefaultConfig');
if (btnDefaultConfigEl) {
    btnDefaultConfigEl.addEventListener('mouseenter', function() {
        showConfigToast('DefaultConfig');
    });
    btnDefaultConfigEl.addEventListener('click', applyDefaultConfig);
}
