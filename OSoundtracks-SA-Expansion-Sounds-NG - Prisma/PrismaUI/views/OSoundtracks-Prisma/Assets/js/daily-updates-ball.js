(function() {
  const DAILY_JSON_URL = 'https://john95ac.github.io/website-documents-John95AC/NEWS_MCM/Daily_Updates/daily-message.json';

  let dailyData = null;
  let imageRotationInterval = null;

  const DAILY_IMAGES = [
    'Assets/Data/001.png',
    'Assets/Data/002.png',
    'Assets/Data/003.png',
    'Assets/Data/005.png',
    'Assets/Data/006.png',
    'Assets/Data/007.png',
    'Assets/Data/009.png',
    'Assets/Data/010.png',
    'Assets/Data/011.png',
    'Assets/Data/012.gif',
    'Assets/Data/013.gif',
    'Assets/Data/015.gif'
  ];

  function getRandomImage() {
    return DAILY_IMAGES[Math.floor(Math.random() * DAILY_IMAGES.length)];
  }

  function applyImageSize(imgEl, src) {
    if (src.includes('013.gif')) {
      imgEl.style.width = '65%';
      imgEl.style.height = '65%';
      imgEl.style.objectFit = 'contain';
    } else if (src.includes('003.png')) {
      imgEl.style.width = '50%';
      imgEl.style.height = '50%';
      imgEl.style.objectFit = 'contain';
    } else {
      imgEl.style.width = '100%';
      imgEl.style.height = '100%';
      imgEl.style.objectFit = 'cover';
    }
  }

  function startImageRotation() {
    if (imageRotationInterval) clearInterval(imageRotationInterval);
    const imgEl = document.getElementById('daily-random-img');
    if (!imgEl) return;
    imageRotationInterval = setInterval(() => {
      const nextSrc = getRandomImage();
      imgEl.style.opacity = '0';
      setTimeout(() => {
        imgEl.src = nextSrc;
        applyImageSize(imgEl, nextSrc);
        imgEl.style.opacity = '1';
      }, 300);
    }, 15000);
  }

  async function loadDailyData() {
    if (typeof dailyMessageData !== 'undefined') {
      dailyData = dailyMessageData;
      updatePanelContent();
      return;
    }
    try {
      const response = await fetch(DAILY_JSON_URL, { cache: 'no-store' });
      if (!response.ok) throw new Error('Network response was not ok');
      dailyData = await response.json();
      updatePanelContent();
    } catch (err) {
      console.error('Failed to load daily updates:', err);
      dailyData = { date: new Date().toISOString().split('T')[0], message: 'No message available', tags: [], priority: 'normal' };
      updatePanelContent();
    }
  }

  function formatDate(dateStr) {
    if (!dateStr) return 'Unknown date';
    const date = new Date(dateStr);
    if (isNaN(date.getTime())) return dateStr;
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    return `${months[date.getUTCMonth()]} ${date.getUTCDate()}, ${date.getUTCFullYear()}`;
  }

  function createTagsHTML(tags) {
    if (!tags || tags.length === 0) return '';
    return tags.slice(0, 5).map(tag => `<span class="daily-tag">${tag}</span>`).join('');
  }

  function formatTime(dateStr) {
    if (!dateStr) return '';
    const date = new Date(dateStr);
    if (isNaN(date.getTime())) return '';
    const hours = date.getUTCHours();
    const minutes = date.getUTCMinutes();
    const ampm = hours >= 12 ? 'PM' : 'AM';
    const h = hours % 12 || 12;
    const m = minutes < 10 ? '0' + minutes : minutes;
    return `${h}:${m} ${ampm}`;
  }

  function updatePanelContent() {
    const messageEl = document.getElementById('daily-message-text');
    const dateEl = document.getElementById('daily-date');
    const tagsEl = document.getElementById('daily-tags');
    const detailsEl = document.getElementById('daily-details');

    if (messageEl) messageEl.textContent = dailyData.message || 'No message available';
    if (dateEl) dateEl.textContent = formatDate(dailyData.date);
    if (tagsEl) tagsEl.innerHTML = createTagsHTML(dailyData.tags);
    if (detailsEl) {
      const timeStr = formatTime(dailyData.updated_at);
      let footerText = timeStr ? `Updated at ${timeStr}` : '';
      const p = (dailyData.priority || '').toLowerCase();
      if (p === 'high') {
        footerText += ' <span class="daily-hot-badge">🔥 BIG NEWS</span>';
      } else if (p === 'normal') {
        footerText += ' <span class="daily-normal-badge">Normal Day</span>';
      }
      detailsEl.innerHTML = footerText;
    }
  }

  function createDailyUpdatesStyles() {
    const style = document.createElement('style');
    style.textContent = `
      #daily-updates-ball {
        position: fixed; bottom: 120px; right: 20px; width: 40px; height: 40px;
        background: linear-gradient(135deg, #10b981, #059669);
        border-radius: 50%; cursor: pointer; z-index: 9997;
        display: none; align-items: center; justify-content: center;
        color: white; font-size: 16px;
        transition: all 0.6s cubic-bezier(0.25,0.46,0.45,0.94);
        box-shadow: 0 4px 20px rgba(16,185,129,0.4);
        border: 2px solid rgba(255,255,255,0.3);
        opacity: 0; pointer-events: auto;
      }
      #daily-updates-ball.near {
        opacity: 1;
      }
      #daily-updates-ball:hover {
        opacity: 1;
        background: linear-gradient(135deg, #059669, #047857);
        transform: scale(1.15);
        box-shadow: 0 8px 35px rgba(16,185,129,0.7);
      }
      #daily-updates-ball.has-updates {
        animation: dailyPulse 2s infinite;
      }
      @keyframes dailyPulse {
        0%, 100% { box-shadow: 0 4px 20px rgba(16,185,129,0.4); }
        50% { box-shadow: 0 4px 30px rgba(16,185,129,0.8), 0 0 20px rgba(16,185,129,0.6); }
      }
      #daily-updates-panel {
        position: fixed; bottom: 170px; right: 20px;
        background: linear-gradient(135deg, rgba(0,0,0,0.95), rgba(30,30,30,0.95));
        border-radius: 15px; z-index: 10000; display: none;
        min-width: 380px; max-width: 440px;
        box-shadow: 0 8px 32px rgba(0,0,0,0.6);
        border: 1px solid rgba(255,255,255,0.1); backdrop-filter: blur(10px);
        overflow: hidden; transition: all 0.3s ease;
      }
      #daily-updates-panel.show { display: block; }
      .daily-updates-header {
        background: linear-gradient(135deg, #10b981, #059669); color: white;
        padding: 20px; text-align: center; position: relative; overflow: hidden;
      }
      .daily-updates-header::before {
        content: ''; position: absolute; top: -50%; left: -50%;
        width: 200%; height: 200%;
        background: radial-gradient(circle, rgba(255,255,255,0.1) 0%, transparent 70%);
        animation: dailyHeaderShine 3s infinite;
      }
      @keyframes dailyHeaderShine { 0%{transform:rotate(0)} 100%{transform:rotate(360deg)} }
      .daily-updates-header h3 { margin: 0 0 8px 0; font-size: 18px; font-weight: 700; position: relative; z-index: 1; display: flex; align-items: center; justify-content: center; gap: 8px; }
      .daily-title-icon { width: 22px; height: 22px; border-radius: 50%; object-fit: cover; }
      .daily-updates-header p { margin: 0; font-size: 14px; opacity: 0.9; position: relative; z-index: 1; }
      .daily-updates-content {
        padding: 25px 20px;
        max-height: 400px;
        overflow-y: auto;
      }
      .daily-date {
        font-size: 11px;
        color: rgba(255,255,255,0.9);
        margin-bottom: 10px;
        text-align: center;
        letter-spacing: 1.5px;
        text-transform: uppercase;
        font-weight: 300;
        display: inline-block;
        padding: 3px 12px;
        background: rgba(16,185,129,0.3);
        border: 1px solid rgba(16,185,129,0.5);
        border-radius: 20px;
      }
      .daily-content-row {
        display: flex;
        gap: 12px;
        align-items: stretch;
      }
      .daily-content-text {
        flex: 2;
        min-width: 0;
      }
      .daily-content-image {
        flex: 1;
        display: flex;
        align-items: center;
        justify-content: center;
        border-radius: 10px;
        overflow: hidden;
        background: rgba(255,255,255,0.03);
        border: 1px solid rgba(255,255,255,0.08);
      }
      .daily-content-image img {
        width: 100%;
        height: 100%;
        object-fit: cover;
        border-radius: 10px;
        transition: opacity 0.3s ease;
      }
      .daily-message {
        font-size: 14px;
        line-height: 1.6;
        color: rgba(255,255,255,0.9);
        margin-bottom: 15px;
        padding: 15px;
        background: rgba(255,255,255,0.05);
        border-radius: 10px;
        border: 1px solid rgba(255,255,255,0.1);
        max-height: 112px;
        overflow-y: auto;
        scrollbar-width: thin;
        scrollbar-color: rgba(16,185,129,0.4) transparent;
      }
      .daily-message::-webkit-scrollbar {
        width: 4px;
      }
      .daily-message::-webkit-scrollbar-track {
        background: transparent;
      }
      .daily-message::-webkit-scrollbar-thumb {
        background: rgba(16,185,129,0.4);
        border-radius: 2px;
      }
      .daily-message::-webkit-scrollbar-thumb:hover {
        background: rgba(16,185,129,0.7);
      }
      .daily-tags {
        display: flex;
        flex-wrap: wrap;
        gap: 8px;
        margin-bottom: 0;
      }
      .daily-tag {
        display: inline-block;
        padding: 2px 7px;
        background: rgba(16,185,129,0.2);
        border: 1px solid rgba(16,185,129,0.4);
        border-radius: 10px;
        font-size: 9px;
        color: #10b981;
        text-transform: uppercase;
        letter-spacing: 0.5px;
      }
      .daily-tag:hover {
        background: rgba(16,185,129,0.3);
        border-color: rgba(16,185,129,0.6);
      }
      .daily-updates-footer {
        background: rgba(0,0,0,0.2);
        padding: 15px 20px;
        text-align: center;
        border-top: 1px solid rgba(255,255,255,0.1);
      }
      .daily-updates-footer small {
        color: rgba(255,255,255,0.6);
        font-style: italic;
        font-size: 11px;
      }
      .daily-hot-badge {
        display: inline-block;
        padding: 1px 8px;
        margin-left: 6px;
        background: linear-gradient(135deg, #f59e0b, #d97706);
        border-radius: 8px;
        font-size: 9px;
        font-weight: 700;
        font-style: normal;
        color: #000;
        letter-spacing: 0.5px;
        text-transform: uppercase;
        vertical-align: middle;
      }
      .daily-normal-badge {
        display: inline-block;
        padding: 1px 8px;
        margin-left: 6px;
        background: linear-gradient(135deg, #22d3ee, #06b6d4);
        border-radius: 8px;
        font-size: 9px;
        font-weight: 700;
        font-style: normal;
        color: #000;
        letter-spacing: 0.5px;
        text-transform: uppercase;
        vertical-align: middle;
      }
      @media (max-width: 768px) {
        #daily-updates-panel { right: 10px; left: 10px; min-width: auto; max-width: none; }
        #daily-updates-ball { width: 45px; height: 45px; font-size: 20px; }
      }
    `;
    document.head.appendChild(style);
  }

  function initializeDailyUpdatesEvents() {
    const $ball = document.getElementById('daily-updates-ball');
    const $panel = document.getElementById('daily-updates-panel');

    let isNear = false;
    const PROXIMITY_RADIUS = 180;

    function getDistanceToBall(mx, my) {
      const rect = $ball.getBoundingClientRect();
      const cx = rect.left + rect.width / 2;
      const cy = rect.top + rect.height / 2;
      return Math.sqrt((mx - cx) ** 2 + (my - cy) ** 2);
    }

    document.addEventListener('mousemove', (e) => {
      if ($ball.style.display === 'none') return;
      const dist = getDistanceToBall(e.clientX, e.clientY);
      if (dist <= PROXIMITY_RADIUS && !isNear) {
        isNear = true;
        $ball.classList.add('near');
      } else if (dist > PROXIMITY_RADIUS + 20 && isNear && !$panel.classList.contains('show')) {
        isNear = false;
        $ball.classList.remove('near');
      }
    });

    function animateContent() {
      const elements = $panel.querySelectorAll('.daily-message, .daily-tag, .daily-updates-footer');
      elements.forEach((el, index) => {
        el.style.opacity = '0';
        el.style.transform = 'translateY(15px)';
        setTimeout(() => {
          el.style.transition = 'all 0.5s cubic-bezier(0.25,0.46,0.45,0.94)';
          el.style.opacity = '1';
          el.style.transform = 'translateY(0)';
        }, index * 100 + 200);
      });
    }

    $ball.onmouseenter = () => { $panel.classList.add('show'); animateContent(); };
    $ball.onmouseleave = () => { setTimeout(() => { if (!$panel.matches(':hover')) $panel.classList.remove('show'); }, 200); };
    $panel.onmouseenter = () => {};
    $panel.onmouseleave = () => { $panel.classList.remove('show'); };
    document.addEventListener('click', (e) => { if (!e.target.closest('#daily-updates-ball, #daily-updates-panel')) $panel.classList.remove('show'); });
  }

  function createDailyUpdatesElements() {
    if (document.getElementById('daily-updates-ball')) return;

    const ball = document.createElement('div');
    ball.id = 'daily-updates-ball';
    ball.innerHTML = '📰';
    ball.title = 'Daily Updates';
    document.body.appendChild(ball);

    const panel = document.createElement('div');
    panel.id = 'daily-updates-panel';
    panel.innerHTML = `
      <div class="daily-updates-header">
        <h3 id="daily-title">📰 Daily News <img src="Assets/Data/003.png" class="daily-title-icon" alt=""></h3>
        <p id="daily-date" class="daily-date">Loading...</p>
      </div>
      <div class="daily-updates-content">
        <div class="daily-content-row">
          <div class="daily-content-text">
            <div id="daily-message-text" class="daily-message">Loading daily updates...</div>
            <div id="daily-tags" class="daily-tags"></div>
          </div>
          <div class="daily-content-image">
            <img id="daily-random-img" src="Assets/Data/001.png" alt="">
          </div>
        </div>
      </div>
      <div class="daily-updates-footer">
        <small id="daily-details">🔄 Updates daily</small>
      </div>
    `;
    document.body.appendChild(panel);

    const imgEl = document.getElementById('daily-random-img');
    if (imgEl) {
      const initialSrc = getRandomImage();
      imgEl.src = initialSrc;
      applyImageSize(imgEl, initialSrc);
    }

    createDailyUpdatesStyles();
    loadDailyData();
    initializeDailyUpdatesEvents();
    startImageRotation();
  }

  window.showDailyUpdatesBall = function() {
    const ball = document.getElementById('daily-updates-ball');
    const panel = document.getElementById('daily-updates-panel');
    if (ball) ball.style.display = 'flex';
  };

  window.hideDailyUpdatesBall = function() {
    const ball = document.getElementById('daily-updates-ball');
    const panel = document.getElementById('daily-updates-panel');
    if (ball) ball.style.display = 'none';
    if (panel) panel.classList.remove('show');
  };

  window.isDailyUpdatesBallVisible = function() {
    const ball = document.getElementById('daily-updates-ball');
    return ball && ball.style.display !== 'none';
  };

  window.refreshDailyUpdates = function() {
    loadDailyData();
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', createDailyUpdatesElements);
  } else {
    createDailyUpdatesElements();
  }
})();