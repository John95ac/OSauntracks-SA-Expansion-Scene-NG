(function() {
  const BASE_URL = `http://${window.location.hostname}:${window.location.port}`;

  function createSandboxStyles() {
    const style = document.createElement('style');
    style.textContent = `
      #sandbox-ball {
        position: fixed; top: 65px; right: 25px; width: 40px; height: 40px;
        background: linear-gradient(135deg, #0a4a3a, #063d2f);
        border-radius: 50%; cursor: pointer; z-index: 9999;
        display: flex; align-items: center; justify-content: center;
        color: white; font-size: 16px;
        transition: all 0.6s cubic-bezier(0.25,0.46,0.45,0.94);
        box-shadow: 0 4px 20px rgba(107,155,122,0.1);
        border: 2px solid rgba(107,155,122,0.1);
      }
      #sandbox-ball:hover {
        background: linear-gradient(135deg, #0a4a3a, #063d2f);
        transform: scale(1.15);
        box-shadow: 0 8px 35px rgba(13,127,92,0.2);
      }
      #sandbox-panel {
        position: fixed; top: 115px; right: 20px;
        background: rgba(0,0,0,0.2);
        border-radius: 20px; z-index: 10000; display: none;
        min-width: 600px; max-width: 750px; width: min(80vw, 750px);
        box-shadow: 0 8px 32px rgba(40,40,40,0.4);
        border: 1px solid rgba(50,50,50,0.2); backdrop-filter: blur(10px);
        overflow: hidden; transition: all 0.3s ease;
      }
      #sandbox-panel.show { display: block; }
      .sandbox-header {
        background: linear-gradient(135deg, #0a4a3a, #063d2f); color: white;
        padding: 15px 20px; display: flex; justify-content: space-between; align-items: center; position: relative; overflow: hidden;
      }
      .sandbox-header h3 { margin: 0; font-size: 14px; font-weight: 700; position: relative; z-index: 1; }
      .header-buttons { display: flex; gap: 10px; }
      .sandbox-header button {
        background: none;
        color: white;
        font-size: 10px;
        cursor: pointer;
        padding: 3px 8px;
        border-radius: 6px;
        transition: all 0.3s ease;
        border: 1px solid rgba(255,255,255,0.3);
      }
      .sandbox-header button:hover {
        background-color: rgba(255, 255, 255, 0.2);
        transform: scale(1.05);
        box-shadow: 0 2px 4px rgba(0,0,0,0.2);
      }
      #sandbox-open-btn, #sandbox-save-btn {
        background: none;
        border: 1px solid rgba(255,255,255,0.3);
        font-size: 10px;
      }
      .sandbox-content {
        padding: 15px; background: rgba(0,0,0,0.1);
      }
      .sandbox-textarea {
        width: 100%; height: 300px; resize: vertical;
        background: rgba(0,0,0,0.5); color: #fff;
        border: 1px solid rgba(255,255,255,0.2); border-radius: 8px;
        padding: 10px; font-family: inherit; font-size: 14px;
        outline: none; transition: border-color 0.3s ease;
      }
      .sandbox-textarea:focus {
        border-color: rgba(13,127,92,0.3);
        box-shadow: 0 0 0 3px rgba(13,127,92,0.05);
      }
      .sandbox-footer {
        padding: 5px 15px; background: rgba(0,0,0,0.1); border-top: 1px solid rgba(50,50,50,0.2);
        display: flex; justify-content: space-between; align-items: center;
      }
      .sandbox-footer small { color: rgba(255,255,255,0.6); font-size: 12px; }
      .sync-indicator { color: rgba(255,255,255,0.7); font-size: 12px; padding: 2px 6px; border-radius: 4px; transition: background 0.3s ease; float: right; }
      @media (max-width: 768px) {
        #sandbox-panel { right: 10px; left: 10px; min-width: auto; max-width: none; }
        #sandbox-ball { width: 45px; height: 45px; font-size: 20px; }
      }
    `;
    document.head.appendChild(style);
  }

  function createSandboxElements() {
    if (document.getElementById('sandbox-ball')) return;

    const ball = document.createElement('div');
    ball.id = 'sandbox-ball';
    ball.innerHTML = '📋';
    ball.title = 'Generated Rules Sandbox';
    document.body.appendChild(ball);

    const panel = document.createElement('div');
    panel.id = 'sandbox-panel';
    panel.innerHTML = `
      <div class="sandbox-header">
        <h3>📋 Generated Rules Sandbox</h3>
        <div class="header-buttons">
          <button id="sandbox-delete-btn">🗑️ Delete</button>
          <button id="sandbox-open-btn">📥 Open</button>
          <button id="sandbox-save-btn">💾 Save</button>
          <button id="sandbox-mod-pack-btn">📦 Mod Pack</button>
        </div>
      </div>
      <div class="sandbox-content">
        <textarea id="sandbox-textarea" class="sandbox-textarea" placeholder="Write your data or rules here..."></textarea>
      </div>
      <div class="sandbox-footer">
        <div class="sync-indicator" id="sync-indicator">Auto-sync</div>
      </div>
    `;
    document.body.appendChild(panel);

    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.id = 'sandbox-file-input';
    fileInput.accept = '.ini';
    fileInput.style.display = 'none';
    document.body.appendChild(fileInput);

    createSandboxStyles();
    initializeSandboxEvents();
    initializeSandboxSync();
  }

  function initializeSandboxEvents() {
    const $ball = document.getElementById('sandbox-ball');
    const $panel = document.getElementById('sandbox-panel');
    const $deleteBtn = document.getElementById('sandbox-delete-btn');
    const $openBtn = document.getElementById('sandbox-open-btn');
    const $fileInput = document.getElementById('sandbox-file-input');
    const $textarea = document.getElementById('sandbox-textarea');
    let fadeTimeout;

    $ball.onmouseenter = () => { $panel.classList.add('show'); };
    $panel.onmouseenter = () => { clearTimeout(fadeTimeout); };
    $panel.onmouseleave = () => { fadeTimeout = setTimeout(() => { $panel.style.opacity = '0'; setTimeout(() => { $panel.classList.remove('show'); $panel.style.opacity = '1'; }, 300); }, 2000); };
    document.addEventListener('click', (e) => { if (!e.target.closest('#sandbox-ball, #sandbox-panel')) $panel.classList.remove('show'); });

    $deleteBtn.addEventListener('click', async () => {
      const audio = new Audio('../Sound/ding-small-bell-sfx.wav');
      audio.play();
      $textarea.value = '';
      try {
        await fetch(`${BASE_URL}/save-sandbox-ini`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ content: '' })
        });
      } catch (error) {
        console.error('Error deleting sandbox ini:', error);
      }
    });

    $openBtn.addEventListener('click', () => {
      const audio = new Audio('../Sound/ding-small-bell-sfx.wav');
      audio.play();
      fetch(`${BASE_URL}/run-sandbox-open-script`, { method: 'POST' })
        .then((res) => res.json())
        .then((data) => {
          if (data && data.status === 'success') {
            $textarea.value = data.content || '';
            $textarea.dispatchEvent(new Event('input', { bubbles: true }));
          }
        })
        .catch((error) => {
          console.error('Error opening sandbox ini:', error);
        });
    });

    const $saveBtn = document.getElementById('sandbox-save-btn');
    $saveBtn.addEventListener('click', async () => {
      const audio = new Audio('../Sound/ding-small-bell-sfx.wav');
      audio.play();
      try {
        if ($textarea && typeof $textarea.__forceSaveSandboxNow === 'function') {
          await $textarea.__forceSaveSandboxNow();
        } else {
          await fetch(`${BASE_URL}/save-sandbox-ini`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ content: $textarea ? $textarea.value : '' })
          });
        }
        await fetch(`${BASE_URL}/run-sandbox-export-script`, { method: 'POST' });
      } catch (error) {
        console.error('Error saving:', error);
      }
    });

    const $modPackBtn = document.getElementById('sandbox-mod-pack-btn');
    $modPackBtn.addEventListener('click', async () => {
      const audio = new Audio('../Sound/ding-small-bell-sfx.wav');
      audio.play();
      try {
        await fetch(`${BASE_URL}/run-mod-pack-script`, { method: 'POST' });
      } catch (error) {
        console.error('Error running mod pack:', error);
      }
    });

    $fileInput.addEventListener('change', (e) => {
      const file = e.target.files[0];
      if (file) {
        const reader = new FileReader();
        reader.onload = (e) => {
          $textarea.value = e.target.result;
        };
        reader.onerror = () => {
          console.error('Error reading file');
        };
        reader.readAsText(file);
      }
    });
  }

  function initializeSandboxSync() {
    const $textarea = document.getElementById('sandbox-textarea');
    const $indicator = document.getElementById('sync-indicator');
    let lastHash = '';
    let pollingTimer = null;
    let saveTimer = null;
    let lastInputTime = 0;
    let localChangesPending = false;
    let operationQueue = [];
    let isProcessingQueue = false;

    function addToQueue(operation) {
      operationQueue.push(operation);
      processQueue();
    }

    async function processQueue() {
      if (isProcessingQueue || operationQueue.length === 0) return;
      isProcessingQueue = true;

      while (operationQueue.length > 0) {
        const operation = operationQueue.shift();
        try {
          await operation();
        } catch (error) {
          console.error('Error processing operation:', error);
        }
      }

      isProcessingQueue = false;
    }

    async function testConnection() {
      try {
        const res = await fetch(`${BASE_URL}/test-connection`, { cache: 'no-store' });
        const data = await res.json();
        if (data.status === 'ok') {
          updateIndicator('Connected');
          setTimeout(() => updateIndicator('Auto-sync'), 1000);
          return true;
        } else {
          updateIndicator('Server error');
          return false;
        }
      } catch (error) {
         updateIndicator('Sync: Connection failed', 'rgba(239, 68, 68, 0.2)');
         return false;
       }
    }

    async function loadSandboxContent() {
      try {
        const res = await fetch(`${BASE_URL}/load-sandbox-ini`, { cache: 'no-store' });
        if (!res.ok) throw new Error('Failed to load sandbox content');
        const data = await res.json();
        if (data.status === 'success') {
          $textarea.value = data.content || '';
          lastHash = calculateHash($textarea.value);
          updateIndicator('Auto-sync');
         }
       } catch (error) {
         updateIndicator('Load failed', 'rgba(239, 68, 68, 0.2)');
       }
    }

    function calculateHash(str) {
      let hash = 0;
      for (let i = 0; i < str.length; i++) {
        const char = str.charCodeAt(i);
        hash = ((hash << 5) - hash) + char;
        hash = hash & hash;
      }
      return hash.toString();
    }

    async function saveSandboxContent() {
      try {
        const content = $textarea.value;
        const currentHash = calculateHash(content);

        if (currentHash === lastHash) {
          updateIndicator('Auto-sync');
          return;
        }

        const res = await fetch(`${BASE_URL}/save-sandbox-ini`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ content })
        });

        if (!res.ok) throw new Error('Failed to save sandbox content');
        const result = await res.json();
        if (result.status === 'success') {
            lastHash = currentHash;
            localChangesPending = false; // Clear pending flag after successful save
            updateIndicator('Saved to ini/OBodyNG_PDA_Sandbox_temp.ini', 'rgba(16, 185, 129, 0.2)');
            setTimeout(() => updateIndicator('Auto-sync'), 2000);
        } else {
            updateIndicator('Save failed', 'rgba(239, 68, 68, 0.2)');
        }
      } catch (error) {
       updateIndicator('Save failed - ' + error.message, 'rgba(239, 68, 68, 0.2)');
       }
    }

    $textarea.__forceSaveSandboxNow = async () => {
      try {
        localChangesPending = true;
        clearTimeout(saveTimer);
        await saveSandboxContent();
      } catch (error) {
        console.error('Error forcing sandbox save:', error);
      }
    };

    async function checkExternalChanges() {
      if (Date.now() - lastInputTime < 1000) return;
      if (localChangesPending) return; // Don't update from external if there are local changes pending
      try {
        const res = await fetch(`${BASE_URL}/load-sandbox-ini`, { cache: 'no-store' });
        if (!res.ok) throw new Error('Failed to check external changes');
        const data = await res.json();
        if (data.status === 'success') {
          const currentHash = calculateHash($textarea.value);
          const serverHash = calculateHash(data.content || '');

          if (serverHash !== currentHash) {
            $textarea.value = data.content || '';
            lastHash = serverHash;
            updateIndicator('Updated from external');
            setTimeout(() => updateIndicator('Auto-sync'), 1500);
          }
        }
      } catch (error) {
        console.error('Error checking external changes:', error);
      }
    }

    function updateIndicator(message, background = '') {
       if (message === 'Editing...') {
         $indicator.innerHTML = '<img src="Data/013.gif" width="16" height="16" style="vertical-align: middle;"> Editing...';
       } else {
         $indicator.textContent = message;
       }
       $indicator.style.background = background;
     }

    $textarea.addEventListener('input', () => {
       updateIndicator('Editing...', 'rgba(251, 191, 36, 0.2)');
       lastInputTime = Date.now();
       localChangesPending = true; // Mark that there are local changes pending
       clearTimeout(saveTimer);
       saveTimer = setTimeout(() => addToQueue(saveSandboxContent), 2000);
     });

    addToQueue(testConnection);
    addToQueue(loadSandboxContent);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', createSandboxElements);
  } else {
    createSandboxElements();
  }
})();
