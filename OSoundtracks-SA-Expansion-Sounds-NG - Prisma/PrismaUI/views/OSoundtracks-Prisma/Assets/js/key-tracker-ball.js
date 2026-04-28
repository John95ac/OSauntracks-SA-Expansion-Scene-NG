(function() {
  let isNear = false;
  const PROXIMITY_RADIUS = 180;

  function getDistanceToBall(mx, my) {
    const ball = document.getElementById('key-tracker-ball');
    if (!ball) return 9999;
    const rect = ball.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    return Math.sqrt((mx - cx) ** 2 + (my - cy) ** 2);
  }

  function buildModListHTML() {
    if (typeof prismaKeyMemory === 'undefined' || !prismaKeyMemory || !prismaKeyMemory.mods || prismaKeyMemory.mods.length === 0) {
      return '<div class="kt-empty">No mods registered</div>';
    }
    return prismaKeyMemory.mods.map(function(mod) {
      var act = mod.activation || {};
      var firstKeyName = act.firstKeyName || act.firstKey || '?';
      var secondKeyName = act.secondKeyName || act.secondKey || '?';
      var keySequence = act.singleKeyMode ? secondKeyName + ' (Single)' : firstKeyName + ' + ' + secondKeyName;
      return '<div class="kt-mod-entry">' +
        '<div class="kt-mod-name">' + mod.name + '</div>' +
        '<div class="kt-mod-keys">' + keySequence + '</div>' +
      '</div>';
    }).join('');
  }

  function updateTrackerContent() {
    var listEl = document.getElementById('kt-mod-list');
    if (listEl) listEl.innerHTML = buildModListHTML();
  }

  function createKeyTrackerStyles() {
    var style = document.createElement('style');
    style.textContent = `
      #key-tracker-ball {
        position: fixed; bottom: 20px; left: 20px; width: 40px; height: 40px;
        background: linear-gradient(135deg, #f59e0b, #d97706);
        border-radius: 50%; cursor: pointer; z-index: 9996;
        display: none; align-items: center; justify-content: center;
        color: white; font-size: 16px;
        transition: all 0.6s cubic-bezier(0.25,0.46,0.45,0.94);
        box-shadow: 0 4px 20px rgba(245,158,11,0.4);
        border: 2px solid rgba(255,255,255,0.3);
        opacity: 0; pointer-events: auto;
      }
      #key-tracker-ball.near {
        opacity: 1;
      }
      #key-tracker-ball:hover {
        opacity: 1;
        background: linear-gradient(135deg, #d97706, #b45309);
        transform: scale(1.15);
        box-shadow: 0 8px 35px rgba(245,158,11,0.7);
      }
      #key-tracker-panel {
        position: fixed; bottom: 70px; left: 20px;
        background: linear-gradient(135deg, rgba(0,0,0,0.95), rgba(30,30,30,0.95));
        border-radius: 15px; z-index: 10000; display: none;
        min-width: 280px; max-width: 340px;
        box-shadow: 0 8px 32px rgba(0,0,0,0.6);
        border: 1px solid rgba(255,255,255,0.1); backdrop-filter: blur(10px);
        overflow: hidden; transition: all 0.3s ease;
      }
      #key-tracker-panel.show { display: block; }
      .kt-header {
        background: linear-gradient(135deg, #f59e0b, #d97706); color: white;
        padding: 15px 20px; text-align: center; position: relative; overflow: hidden;
      }
      .kt-header::before {
        content: ''; position: absolute; top: -50%; left: -50%;
        width: 200%; height: 200%;
        background: radial-gradient(circle, rgba(255,255,255,0.1) 0%, transparent 70%);
        animation: ktHeaderShine 3s infinite;
      }
      @keyframes ktHeaderShine { 0%{transform:rotate(0)} 100%{transform:rotate(360deg)} }
      .kt-header h3 {
        margin: 0; font-size: 15px; font-weight: 700;
        position: relative; z-index: 1;
        display: flex; align-items: center; justify-content: center; gap: 6px;
      }
      .kt-content {
        padding: 15px;
        max-height: calc(5 * 72px + 30px);
        overflow-y: auto;
        scrollbar-width: thin;
        scrollbar-color: rgba(245,158,11,0.4) transparent;
      }
      .kt-content::-webkit-scrollbar { width: 4px; }
      .kt-content::-webkit-scrollbar-track { background: transparent; }
      .kt-content::-webkit-scrollbar-thumb {
        background: rgba(245,158,11,0.4); border-radius: 2px;
      }
      .kt-content::-webkit-scrollbar-thumb:hover {
        background: rgba(245,158,11,0.7);
      }
      .kt-mod-entry {
        padding: 10px 12px;
        margin-bottom: 8px;
        background: rgba(255,255,255,0.05);
        border-radius: 10px;
        border: 1px solid rgba(255,255,255,0.1);
      }
      .kt-mod-entry:last-child { margin-bottom: 0; }
      .kt-mod-name {
        font-size: 12px; font-weight: 700;
        color: #f59e0b;
        margin-bottom: 4px;
        letter-spacing: 0.5px;
      }
      .kt-mod-keys {
        font-size: 13px;
        color: rgba(255,255,255,0.85);
        font-family: monospace;
        letter-spacing: 0.5px;
      }
      .kt-empty {
        text-align: center;
        color: rgba(255,255,255,0.5);
        font-size: 13px;
        padding: 20px;
      }
      .kt-footer {
        background: rgba(0,0,0,0.2);
        padding: 10px 15px;
        text-align: center;
        border-top: 1px solid rgba(255,255,255,0.1);
      }
      .kt-footer small {
        color: rgba(255,255,255,0.5);
        font-size: 10px;
        font-style: italic;
      }
      @media (max-width: 768px) {
        #key-tracker-panel { left: 10px; right: 10px; min-width: auto; max-width: none; }
        #key-tracker-ball { width: 45px; height: 45px; font-size: 20px; }
      }
    `;
    document.head.appendChild(style);
  }

  function initializeKeyTrackerEvents() {
    var $ball = document.getElementById('key-tracker-ball');
    var $panel = document.getElementById('key-tracker-panel');

    document.addEventListener('mousemove', function(e) {
      if ($ball.style.display === 'none') return;
      var dist = getDistanceToBall(e.clientX, e.clientY);
      if (dist <= PROXIMITY_RADIUS && !isNear) {
        isNear = true;
        $ball.classList.add('near');
      } else if (dist > PROXIMITY_RADIUS + 20 && isNear && !$panel.classList.contains('show')) {
        isNear = false;
        $ball.classList.remove('near');
      }
    });

    $ball.onmouseenter = function() { $panel.classList.add('show'); updateTrackerContent(); };
    $ball.onmouseleave = function() { setTimeout(function() { if (!$panel.matches(':hover')) $panel.classList.remove('show'); }, 200); };
    $panel.onmouseenter = function() {};
    $panel.onmouseleave = function() { $panel.classList.remove('show'); };
    document.addEventListener('click', function(e) { if (!e.target.closest('#key-tracker-ball, #key-tracker-panel')) $panel.classList.remove('show'); });
  }

  function createKeyTrackerElements() {
    if (document.getElementById('key-tracker-ball')) return;

    var ball = document.createElement('div');
    ball.id = 'key-tracker-ball';
    ball.innerHTML = '\u2328';
    ball.title = 'Key Tracker';
    document.body.appendChild(ball);

    var panel = document.createElement('div');
    panel.id = 'key-tracker-panel';
    panel.innerHTML = '\
      <div class="kt-header">\
        <h3>\u2328 Key Tracker</h3>\
      </div>\
      <div class="kt-content">\
        <div id="kt-mod-list" class="kt-mod-list"></div>\
      </div>\
      <div class="kt-footer">\
        <small>Activation keys for installed mods of John95AC</small>\
      </div>\
    ';
    document.body.appendChild(panel);

    createKeyTrackerStyles();
    updateTrackerContent();
    initializeKeyTrackerEvents();
    startMemoryPolling();
  }

  window.showKeyTrackerBall = function() {
    var ball = document.getElementById('key-tracker-ball');
    if (ball) ball.style.display = 'flex';
  };

  window.hideKeyTrackerBall = function() {
    var ball = document.getElementById('key-tracker-ball');
    var panel = document.getElementById('key-tracker-panel');
    if (ball) ball.style.display = 'none';
    if (panel) panel.classList.remove('show');
  };

  window.isKeyTrackerBallVisible = function() {
    var ball = document.getElementById('key-tracker-ball');
    return ball && ball.style.display !== 'none';
  };

  window.refreshKeyTracker = function() {
    updateTrackerContent();
  };

  window.updateKeyTrackerData = function(memoryJsContent) {
    try {
      var unescaped = memoryJsContent
        .replace(/\\n/g, '\n')
        .replace(/\\r/g, '\r')
        .replace(/\\'/g, "'")
        .replace(/\\\\/g, "\\");
      var startIdx = unescaped.indexOf('var prismaKeyMemory = ');
      if (startIdx === -1) {
        console.warn('Key Tracker: No prismaKeyMemory definition found');
        return;
      }
      var jsonPart = unescaped.substring(startIdx + 21).trim();
      var lastSemicolon = jsonPart.lastIndexOf(';');
      if (lastSemicolon !== -1) {
        jsonPart = jsonPart.substring(0, lastSemicolon).trim();
      }
      window.prismaKeyMemory = JSON.parse(jsonPart);
      updateTrackerContent();
      console.log('Key Tracker: Loaded', window.prismaKeyMemory.mods.length, 'mods');
    } catch (e) {
      console.error('Key Tracker: Error parsing injected data', e);
    }
  };

  window.onGetKeyTrackerConfig = function(data) {};

  window.startKeyTrackerPolling = function() { console.warn('Polling disabled - using DLL injection'); };
  window.stopKeyTrackerPolling = function() {};

  let memoryPollingInterval = null;
  const MEMORY_POLL_INTERVAL = 1000;
  const MEMORY_SCRIPT_SRC = 'Assets/ini/Prisma-John95AC-Memory.js';

  function reloadMemoryScript() {
    var oldScript = document.querySelector('script[src*="Prisma-John95AC-Memory.js"]');
    if (oldScript) oldScript.remove();
    
    var newScript = document.createElement('script');
    newScript.src = MEMORY_SCRIPT_SRC + '?t=' + Date.now();
    newScript.onload = function() { updateTrackerContent(); };
    document.head.appendChild(newScript);
  }

  function startMemoryPolling() {
    if (memoryPollingInterval) return;
    memoryPollingInterval = setInterval(reloadMemoryScript, MEMORY_POLL_INTERVAL);
  }

  function stopMemoryPolling() {
    if (memoryPollingInterval) {
      clearInterval(memoryPollingInterval);
      memoryPollingInterval = null;
    }
  }

  window.startKeyTrackerPolling = startMemoryPolling;
  window.stopKeyTrackerPolling = stopMemoryPolling;

  window.showKeyConflictToast = function(modName) {
    if (typeof showHubNotification === 'function') {
      showHubNotification('This key is already used by ' + modName, 'red');
    }
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', createKeyTrackerElements);
  } else {
    createKeyTrackerElements();
  }
})();
