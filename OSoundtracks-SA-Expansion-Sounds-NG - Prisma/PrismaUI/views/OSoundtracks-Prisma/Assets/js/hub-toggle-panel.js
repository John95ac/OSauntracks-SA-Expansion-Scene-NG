(function() {
  let hubVisible = true;
  let ballsProximityMode = false;

  function createHubToggleStyles() {
    const style = document.createElement('style');
    style.textContent = `
      #hub-toggle-trigger {
        position: fixed;
        top: 0;
        right: 0;
        width: 40px;
        height: 40px;
        background: linear-gradient(135deg, rgba(30,30,35,0.95), rgba(20,20,25,0.98));
        border-radius: 0 0 20px 20px;
        cursor: pointer;
        z-index: 9995;
        display: none;
        align-items: center;
        justify-content: center;
        color: #d4af37;
        font-size: 18px;
        transition: all 0.4s cubic-bezier(0.25,0.46,0.45,0.94);
        box-shadow: 0 2px 15px rgba(0,0,0,0.5);
        border: 1px solid rgba(100,100,100,0.3);
        border-top: none;
        border-right: none;
      }
      #hub-toggle-trigger:hover {
        width: 180px;
        background: linear-gradient(135deg, rgba(40,40,45,0.97), rgba(30,30,35,0.98));
      }
      #hub-toggle-trigger.expanded {
        width: 180px;
      }
      .hub-toggle-icon {
        position: absolute;
        left: 11px;
        transition: all 0.3s ease;
      }
      .hub-toggle-label-trigger {
        position: absolute;
        left: 35px;
        font-size: 11px;
        font-weight: bold;
        color: #ffffff;
        opacity: 0;
        transition: all 0.3s ease;
        font-family: 'Segoe UI', Arial, sans-serif;
        letter-spacing: 0.5px;
      }
      #hub-toggle-trigger:hover .hub-toggle-icon {
        left: 15px;
      }
      #hub-toggle-trigger:hover .hub-toggle-label-trigger {
        opacity: 1;
        left: 40px;
      }
      #hub-toggle-panel {
        position: fixed;
        top: 45px;
        right: 0;
        width: 180px;
        background: linear-gradient(135deg, rgba(30,30,35,0.97), rgba(20,20,25,0.98));
        border-radius: 0 0 15px 15px;
        z-index: 9994;
        display: none;
        flex-direction: column;
        box-shadow: 0 4px 20px rgba(0,0,0,0.6);
        border: 1px solid rgba(100,100,100,0.3);
        border-top: none;
        border-right: none;
        overflow: hidden;
      }
      #hub-toggle-panel.show {
        display: flex;
      }
      .hub-toggle-item {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 12px 15px;
        border-bottom: 1px solid rgba(100,100,100,0.2);
        cursor: pointer;
        transition: background 0.2s ease;
      }
      .hub-toggle-item:last-child {
        border-bottom: none;
      }
      .hub-toggle-item:hover {
        background: rgba(50,50,55,0.5);
      }
      .hub-toggle-label {
        font-size: 11px;
        color: #c6c6c6;
        font-family: Arial, sans-serif;
      }
      .hub-toggle-switch {
        width: 36px;
        height: 18px;
        background: rgba(60,60,60,0.8);
        border-radius: 9px;
        position: relative;
        transition: all 0.3s ease;
        border: 1px solid rgba(100,100,100,0.3);
      }
      .hub-toggle-switch.active {
        background: rgba(212,175,55,0.4);
        border-color: #d4af37;
      }
      .hub-toggle-switch::after {
        content: '';
        position: absolute;
        width: 14px;
        height: 14px;
        background: #888;
        border-radius: 50%;
        top: 2px;
        left: 2px;
        transition: all 0.3s ease;
      }
      .hub-toggle-switch.active::after {
        left: 20px;
        background: #d4af37;
      }
      .hub-toggle-title {
        padding: 10px 15px;
        background: rgba(0,0,0,0.3);
        font-size: 10px;
        color: #888;
        text-align: center;
        letter-spacing: 1px;
      }
    `;
    document.head.appendChild(style);
  }

  function updateHubVisibility() {
    const hub = document.getElementById('hub');
    if (hub) {
      if (hubVisible) {
        hub.classList.remove('hidden');
      } else {
        hub.classList.add('hidden');
      }
    }
    if (window.onToggleHub) {
      window.onToggleHub(hubVisible ? 'show' : 'hide');
    }
    updateSwitchUI('hub-toggle-switch-hub', hubVisible);
  }

  function updateBallsProximityMode() {
    const supportBall = document.getElementById('support-ball');
    const socialBall = document.getElementById('social-links-ball');
    
    if (ballsProximityMode) {
      if (supportBall) {
        supportBall.style.opacity = '0';
        supportBall.classList.add('proximity-mode');
      }
      if (socialBall) {
        socialBall.style.opacity = '0';
        socialBall.classList.add('proximity-mode');
      }
      startProximityDetection();
    } else {
      if (supportBall) {
        supportBall.style.opacity = '1';
        supportBall.classList.remove('proximity-mode');
      }
      if (socialBall) {
        socialBall.style.opacity = '1';
        socialBall.classList.remove('proximity-mode');
      }
      stopProximityDetection();
    }
    updateSwitchUI('hub-toggle-switch-balls', ballsProximityMode);
  }

  function updateSwitchUI(switchId, isActive) {
    const switchEl = document.getElementById(switchId);
    if (switchEl) {
      if (isActive) {
        switchEl.classList.add('active');
      } else {
        switchEl.classList.remove('active');
      }
    }
  }

  let proximityInterval = null;
  const PROXIMITY_RADIUS = 180;

  function getDistanceToElement(mx, my, el) {
    if (!el) return 9999;
    const rect = el.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    return Math.sqrt((mx - cx) ** 2 + (my - cy) ** 2);
  }

  function startProximityDetection() {
    if (proximityInterval) return;
    
    let lastNearSupport = false;
    let lastNearSocial = false;
    
    document.addEventListener('mousemove', handleProximityMove);
  }

  function handleProximityMove(e) {
    const supportBall = document.getElementById('support-ball');
    const socialBall = document.getElementById('social-links-ball');
    
    if (supportBall && supportBall.classList.contains('proximity-mode')) {
      const distSupport = getDistanceToElement(e.clientX, e.clientY, supportBall);
      if (distSupport <= PROXIMITY_RADIUS) {
        supportBall.style.opacity = '1';
      } else {
        supportBall.style.opacity = '0';
      }
    }
    
    if (socialBall && socialBall.classList.contains('proximity-mode')) {
      const distSocial = getDistanceToElement(e.clientX, e.clientY, socialBall);
      if (distSocial <= PROXIMITY_RADIUS) {
        socialBall.style.opacity = '1';
      } else {
        socialBall.style.opacity = '0';
      }
    }
  }

  function stopProximityDetection() {
    document.removeEventListener('mousemove', handleProximityMove);
  }

  function initializeHubToggleEvents() {
    const trigger = document.getElementById('hub-toggle-trigger');
    const panel = document.getElementById('hub-toggle-panel');
    const hubSwitch = document.getElementById('hub-toggle-switch-hub');
    const ballsSwitch = document.getElementById('hub-toggle-switch-balls');
    
    trigger.addEventListener('mouseenter', function() {
      trigger.classList.add('expanded');
      panel.classList.add('show');
    });
    
    trigger.addEventListener('mouseleave', function() {
      setTimeout(function() {
        if (!panel.matches(':hover') && !trigger.matches(':hover')) {
          trigger.classList.remove('expanded');
          panel.classList.remove('show');
        }
      }, 300);
    });
    
    panel.addEventListener('mouseleave', function() {
      setTimeout(function() {
        if (!trigger.matches(':hover')) {
          trigger.classList.remove('expanded');
          panel.classList.remove('show');
        }
      }, 200);
    });
    
    document.addEventListener('click', function(e) {
      if (!e.target.closest('#hub-toggle-trigger, #hub-toggle-panel')) {
        trigger.classList.remove('expanded');
        panel.classList.remove('show');
      }
    });
    
    document.getElementById('hub-toggle-item-hub').addEventListener('click', function() {
      hubVisible = !hubVisible;
      updateHubVisibility();
      if (window.saveAllConfig) {
        window.saveAllConfig();
      }
    });
    
    document.getElementById('hub-toggle-item-balls').addEventListener('click', function() {
      ballsProximityMode = !ballsProximityMode;
      updateBallsProximityMode();
      if (window.saveAllConfig) {
        window.saveAllConfig();
      }
    });
  }

  function createHubToggleElements() {
    if (document.getElementById('hub-toggle-trigger')) return;
    
    const trigger = document.createElement('div');
    trigger.id = 'hub-toggle-trigger';
    trigger.innerHTML = '<span class="hub-toggle-icon">&#9881;</span><span class="hub-toggle-label-trigger">Settings</span>';
    document.body.appendChild(trigger);
    
    const panel = document.createElement('div');
    panel.id = 'hub-toggle-panel';
    panel.innerHTML = `
      <div class="hub-toggle-item" id="hub-toggle-item-hub">
        <span class="hub-toggle-label">Hub Visible</span>
        <div class="hub-toggle-switch active" id="hub-toggle-switch-hub"></div>
      </div>
      <div class="hub-toggle-item" id="hub-toggle-item-balls">
        <span class="hub-toggle-label">Balls Proximity</span>
        <div class="hub-toggle-switch" id="hub-toggle-switch-balls"></div>
      </div>
    `;
    document.body.appendChild(panel);
    
    createHubToggleStyles();
    initializeHubToggleEvents();
    
    updateSwitchUI('hub-toggle-switch-hub', hubVisible);
    updateSwitchUI('hub-toggle-switch-balls', ballsProximityMode);
  }

  window.showHubToggle = function() {
    const trigger = document.getElementById('hub-toggle-trigger');
    if (trigger) trigger.style.display = 'flex';
  };

  window.hideHubToggle = function() {
    const trigger = document.getElementById('hub-toggle-trigger');
    const panel = document.getElementById('hub-toggle-panel');
    if (trigger) trigger.style.display = 'none';
    if (panel) panel.classList.remove('show');
  };

  window.isHubToggleVisible = function() {
    const trigger = document.getElementById('hub-toggle-trigger');
    return trigger && trigger.style.display !== 'none';
  };

  window.setHubVisible = function(visible) {
    hubVisible = visible;
    updateHubVisibility();
  };

  window.setBallsProximityMode = function(enabled) {
    ballsProximityMode = enabled;
    updateBallsProximityMode();
  };

  window.getHubToggleState = function() {
    return {
      hubVisible: hubVisible,
      ballsProximityMode: ballsProximityMode
    };
  };

  window.applySettingsJsConfig = function(config) {
    if (typeof config.hubVisibleSettings !== 'undefined') {
      hubVisible = config.hubVisibleSettings;
      updateSwitchUI('hub-toggle-switch-hub', hubVisible);
    }
    if (typeof config.proximitySettings !== 'undefined') {
      ballsProximityMode = config.proximitySettings;
      updateSwitchUI('hub-toggle-switch-balls', ballsProximityMode);
    }
  };

  window.syncBallsProximityStyles = function() {
    const supportBall = document.getElementById('support-ball');
    const socialBall = document.getElementById('social-links-ball');
    
    if (ballsProximityMode) {
      if (supportBall) {
        supportBall.style.transition = 'none';
        supportBall.style.opacity = '0';
        supportBall.classList.add('proximity-mode');
      }
      if (socialBall) {
        socialBall.style.transition = 'none';
        socialBall.style.opacity = '0';
        socialBall.classList.add('proximity-mode');
      }
      startProximityDetection();
    } else {
      if (supportBall) {
        supportBall.style.opacity = '1';
        supportBall.style.transition = 'all 0.6s cubic-bezier(0.25,0.46,0.45,0.94)';
        supportBall.classList.remove('proximity-mode');
      }
      if (socialBall) {
        socialBall.style.opacity = '1';
        socialBall.style.transition = 'all 0.6s cubic-bezier(0.25,0.46,0.45,0.94)';
        socialBall.classList.remove('proximity-mode');
      }
      stopProximityDetection();
    }
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', createHubToggleElements);
  } else {
    createHubToggleElements();
  }
})();