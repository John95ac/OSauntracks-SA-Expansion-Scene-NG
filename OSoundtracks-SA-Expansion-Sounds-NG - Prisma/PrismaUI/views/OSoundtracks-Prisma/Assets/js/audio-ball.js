(function() {
  const DEFAULT_VIDEO_IDS = ['nRe3xFeyhVY', 'wSYoT_ptT00', '-wPg1tNEWmo', '9ou1pl0XNRs', 'Jde-TFha0ko', 'vgUaZz04bkw', '8F1-1j_ZDgc'];
  let videoIds = DEFAULT_VIDEO_IDS.slice();
  let primaryId = videoIds[0];

  const storageKey = 'bgAudioStateBall';
  const volumeKey = 'bgAudioVolumeBall';
  const selectionKey = 'bgAudioVideoIdBall';
  const muteKey = 'bgAudioMuteBall';
  const TITLE_CACHE_PREFIX = 'ytTitleBall:';
  const TITLE_TTL_MS = 7 * 24 * 60 * 60 * 1000;

  let titleMemCache = {};
  let titlePromiseCache = {};
  const nowTs = () => Date.now();

  let player = null;
  let desiredState = localStorage.getItem(storageKey) || 'playing';
  let desiredMuteState = localStorage.getItem(muteKey) || 'unmuted';
  let targetVolume = parseInt(localStorage.getItem(volumeKey), 10) || 20;
  let videoId = null;

  function parseYoutubeIdsFromIni(text) {
    const ids = new Set();
    const lines = text.split(/\r?\n/);
    for (const raw of lines) {
      const line = raw.trim();
      if (!line || line.startsWith('[') || line.startsWith(';') || line.startsWith('#')) continue;
      let m = line.match(/[?&]v=([a-zA-Z0-9_-]{11})/);
      if (m) { ids.add(m[1]); continue; }
      m = line.match(/youtu\.be\/([a-zA-Z0-9_-]{11})/);
      if (m) { ids.add(m[1]); continue; }
    }
    return Array.from(ids);
  }

  async function loadVideoIdsFromIni() {
    try {
      const res = await fetch('Data/Links.ini', { cache: 'no-store' });
      if (!res.ok) return;
      const txt = await res.text();
      const found = parseYoutubeIdsFromIni(txt);
      if (found.length) {
        videoIds = found;
        primaryId = videoIds[0];
      }
    } catch (_) {}
  }

  function getCachedTitleLS(id) {
    try {
      const raw = localStorage.getItem(TITLE_CACHE_PREFIX + id);
      if (!raw) return null;
      const obj = JSON.parse(raw);
      if (nowTs() - obj.t > TITLE_TTL_MS) return null;
      return obj.title;
    } catch { return null; }
  }
  function setCachedTitleLS(id, title) {
    try { localStorage.setItem(TITLE_CACHE_PREFIX + id, JSON.stringify({ t: nowTs(), title })); } catch {}
  }
  function fetchTitleForId(id) {
    if (titleMemCache[id]) return Promise.resolve(titleMemCache[id]);
    const cached = getCachedTitleLS(id);
    if (cached) { titleMemCache[id] = cached; return Promise.resolve(cached); }
    if (titlePromiseCache[id]) return titlePromiseCache[id];
    const url = 'https://www.youtube.com/oembed?format=json&url=' + encodeURIComponent('https://www.youtube.com/watch?v=' + id);
    titlePromiseCache[id] = fetch(url).then(r => r.json()).then(data => {
      const title = data && data.title ? data.title : id;
      titleMemCache[id] = title;
      setCachedTitleLS(id, title);
      return title;
    }).catch(() => {
      titleMemCache[id] = id;
      return id;
    }).finally(() => { setTimeout(() => delete titlePromiseCache[id], 0); });
    return titlePromiseCache[id];
  }
  function updateTitle(titleEl) {
    if (!videoId) return;
    fetchTitleForId(videoId).then(t => titleEl.textContent = t);
  }

  function createElements() {
    if (document.getElementById('audio-ball')) return;

    const storedVideoId = localStorage.getItem(selectionKey) || '';
    videoId = (storedVideoId && videoIds.indexOf(storedVideoId) !== -1) ? storedVideoId : (videoIds[0] || primaryId);

    const ball = document.createElement('div');
    ball.id = 'audio-ball';
    ball.innerHTML = '🎵';
    ball.title = 'Music Selection';
    document.body.appendChild(ball);

    const controls = document.createElement('div');
    controls.id = 'audio-controls';
    controls.innerHTML = `
      <a href="#" id="abAudioToggle" class="audio-toggle" title="Toggle audio" style="margin-right: 10px; color: white;">🔊</a>
      <span id="abAudioTitle"></span>
      <br>
      <label for="abVolume" style="font-size: 12px;">Volume:</label>
      <input type="range" id="abVolume" min="0" max="100" value="${targetVolume}" style="width: 100%; margin: 5px 0;">
      <br>
      <a href="#" id="abAudioPicker" title="Choose Track" style="color: white; margin-right: 5px;">Tracklist</a>
      <div id="abAudioPickerMenu" style="display: none; position: absolute; bottom: 100%; left: 0; background: rgba(0,0,0,0.95); color: white; padding: 10px; border-radius: 5px; max-height: 200px; overflow-y: auto; min-width: 200px; z-index: 1001;">
        <ul style="list-style: none; margin: 0; padding: 0;"></ul>
      </div>
    `;
    document.body.appendChild(controls);

    const style = document.createElement('style');
    style.textContent = `
      #audio-ball {
        position: fixed; bottom: 20px; left: 20px;
        width: 40px; height: 40px;
        background: rgba(0,0,0,0.7);
        border-radius: 50%; cursor: pointer; z-index: 9999;
        display: flex; align-items: center; justify-content: center;
        color: white; font-size: 18px; transition: all 0.3s ease;
        border: 2px solid rgba(255,255,255,0.3);
      }
      #audio-ball:hover { background: rgba(0,0,0,0.9); transform: scale(1.2); }
      #audio-controls {
        position: fixed; bottom: 70px; left: 20px;
        background: rgba(0,0,0,0.95); color: white;
        padding: 15px; border-radius: 10px; z-index: 10000; display: none;
        min-width: 250px; box-shadow: 0 4px 12px rgba(0,0,0,0.5);
        font-family: Arial, sans-serif; font-size: 14px;
      }
      #audio-controls.show { display: block; }
      #abAudioTitle { display: block; margin: 5px 0; font-size: 12px; font-weight: bold; }
      #abAudioPicker { cursor: pointer; text-decoration: none; color: #00ff00; animation: pulse-green 2s infinite; text-shadow: 0 0 5px #00ff00; }
      #abAudioPicker:hover { text-decoration: underline; animation: none; text-shadow: 0 0 10px #00ff00; }
      #abAudioPickerMenu ul li a { display: block; padding: 5px; color: white; text-decoration: none; }
      #abAudioPickerMenu ul li a:hover { background: rgba(255,255,255,0.1); }
      #ytAudio { position: absolute; left: -9999px; width: 1px; height: 1px; }
      #abAudioToggle { font-size: 20px; text-decoration: none; }
      @keyframes pulse-green { 0%{opacity:1} 50%{opacity:.6} 100%{opacity:1} }
    `;
    document.head.appendChild(style);

    const $controls = document.getElementById('audio-controls');
    const $toggle = document.getElementById('abAudioToggle');
    const $title = document.getElementById('abAudioTitle');
    const $volume = document.getElementById('abVolume');
    const $picker = document.getElementById('abAudioPicker');
    const $pickerMenu = document.getElementById('abAudioPickerMenu');

    const $container = document.getElementById('ytAudio') || document.createElement('div');
    if (!$container.id) $container.id = 'ytAudio';
    $container.setAttribute('data-video-id', videoIds[0] || primaryId);
    $container.setAttribute('data-video-ids', videoIds.join(','));
    document.body.appendChild($container);

    function setIcon() {
      if (!player || typeof player.isMuted !== 'function') {
        $toggle.textContent = '🔊'; $toggle.title = 'Mute'; return;
      }
      if (player.isMuted()) { $toggle.textContent = '🔇'; $toggle.title = 'Unmute'; }
      else { $toggle.textContent = '🔊'; $toggle.title = 'Mute'; }
    }

    // Volumen: aplica en tiempo real y gestiona mute cuando llega a 0 o vuelve a >0
    $volume.value = targetVolume;
    $volume.addEventListener('input', function() {
      targetVolume = parseInt(this.value, 10) || 0;
      localStorage.setItem(volumeKey, targetVolume);
      if (player && typeof player.setVolume === 'function') {
        player.setVolume(targetVolume);
        if (targetVolume === 0 && typeof player.mute === 'function') {
          player.mute();
          desiredMuteState = 'muted';
          localStorage.setItem(muteKey, 'muted');
        } else if (targetVolume > 0 && typeof player.unMute === 'function' && desiredMuteState !== 'muted') {
          // Solo desmutear si el estado deseado no es ‘muted’
          player.unMute();
        }
      }
      setTimeout(setIcon, 200);
    });

    function buildPicker() {
      const ul = $pickerMenu.querySelector('ul');
      ul.innerHTML = '';
      videoIds.forEach(id => {
        const li = document.createElement('li');
        const a = document.createElement('a');
        a.href = '#'; a.dataset.id = id; a.textContent = id;
        fetchTitleForId(id).then(t => a.textContent = (id === videoId ? '• ' : '') + t);
        a.onclick = (e) => { e.preventDefault(); selectVideo(id); hideMenu(); };
        li.appendChild(a);
        ul.appendChild(li);
      });
    }
    function showMenu() { buildPicker(); $pickerMenu.style.display = 'block'; }
    function hideMenu() { $pickerMenu.style.display = 'none'; }
    $picker.onclick = (e) => { e.preventDefault(); ($pickerMenu.style.display === 'block') ? hideMenu() : showMenu(); };
    document.onclick = (e) => { if (!e.target.closest('#abAudioPicker, #abAudioPickerMenu')) hideMenu(); };

    function selectVideo(newId) {
      if (!newId || newId === videoId) return;
      videoId = newId;
      localStorage.setItem(selectionKey, videoId);
      if (player && player.loadVideoById) player.loadVideoById(videoId);
      updateTitle($title);
    }

    function ensureApi(cb) {
      if (window.YT && YT.Player) return cb();
      const tag = document.createElement('script');
      tag.src = 'https://www.youtube.com/iframe_api';
      const firstScript = document.getElementsByTagName('script')[0];
      firstScript.parentNode.insertBefore(tag, firstScript);
      const prev = window.onYouTubeIframeAPIReady;
      window.onYouTubeIframeAPIReady = () => { if (prev) prev(); cb(); };
    }

    function createPlayer() {
      player = new YT.Player($container, {
        height: '1', width: '1', videoId: videoId,
        playerVars: { autoplay: 1, controls: 0, loop: 1, playlist: videoIds.join(','), mute: 0, playsinline: 1, modestbranding: 1, iv_load_policy: 3, rel: 0 },
        events: {
          onReady: (ev) => {
            ev.target.setVolume(targetVolume);
            // Estado inicial de mute acorde a slider/estado guardado
            if (targetVolume === 0 || desiredMuteState === 'muted') ev.target.mute();
            else ev.target.unMute();

            if (desiredState === 'paused') ev.target.pauseVideo(); else tryPlay();
            updateTitle($title);
            setTimeout(() => updateTitle($title), 600);
            setTimeout(setIcon, 200);
          },
          onStateChange: () => { updateTitle($title); setTimeout(setIcon, 200); }
        }
      });
    }

    function tryPlay() {
      if (!player || !player.playVideo) return;
      try { player.playVideo(); setTimeout(setIcon, 200); } catch {}
    }

    function initPlayer() { ensureApi(createPlayer); }

    if (desiredState === 'playing') setTimeout(initPlayer, 500);

    const $ball = document.getElementById('audio-ball');
    $ball.onmouseenter = () => {
      $controls.classList.add('show');
      if (!player) initPlayer();
      if (desiredState === 'playing' && player) tryPlay();
    };
    $ball.onmouseleave = () => {
      setTimeout(() => { if (!$controls.matches(':hover')) $controls.classList.remove('show'); }, 200);
    };
    $controls.onmouseenter = () => {};
    $controls.onmouseleave = () => { $controls.classList.remove('show'); };

    $toggle.onclick = (e) => {
      e.preventDefault();
      if (!player || typeof player.isMuted !== 'function') { initPlayer(); return; }
      if (player.isMuted()) {
        player.unMute();
        desiredMuteState = 'unmuted';
        localStorage.setItem(muteKey, 'unmuted');
        if (targetVolume === 0) { targetVolume = 1; $volume.value = 1; player.setVolume(1); localStorage.setItem(volumeKey, 1); }
      } else {
        player.mute();
        desiredMuteState = 'muted';
        localStorage.setItem(muteKey, 'muted');
      }
      setTimeout(setIcon, 200);
    };

    const resumeInteract = () => {
      if (desiredState === 'playing' && player) tryPlay();
      window.removeEventListener('click', resumeInteract);
      window.removeEventListener('keydown', resumeInteract);
      window.removeEventListener('touchstart', resumeInteract);
    };
    window.addEventListener('click', resumeInteract, { once: true });
    window.addEventListener('keydown', resumeInteract, { once: true });
    window.addEventListener('touchstart', resumeInteract, { once: true });
  }

  async function init() {
    await loadVideoIdsFromIni();
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', createElements);
    } else {
      createElements();
    }
  }

  init();
})();