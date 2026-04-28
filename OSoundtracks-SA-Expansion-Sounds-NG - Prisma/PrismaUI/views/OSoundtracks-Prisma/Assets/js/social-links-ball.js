(function() {
  const IMAGE_BASE = 'Assets/Data/images/';

  const defaults = [
    {
      name: 'Web Doc John95AC',
      image: IMAGE_BASE + 'web.png',
      url: 'https://john95ac.github.io/website-documents-John95AC/index.html',
      alt: 'Visit Web Doc John95AC'
    },
    {
      name: 'News',
      image: IMAGE_BASE + 'news.png',
      url: 'https://john95ac.github.io/website-documents-John95AC/NEWS_MCM/index.html',
      alt: 'News, acknowledgments and updates'
    },
    {
      name: 'YouTube',
      image: IMAGE_BASE + 'support_me_on_Youtube_badge.png',
      url: 'https://www.youtube.com/@John1995ac',
      alt: 'Subscribe on YouTube'
    },
    {
      name: 'Discord',
      image: IMAGE_BASE + 'support_me_on_Discord_badge_beige.png',
      url: '#',
      alt: 'Discord server coming soon'
    },
    {
      name: 'Nexus Mods',
      image: IMAGE_BASE + 'support_me_on_Nexus_badge.png',
      url: 'https://next.nexusmods.com/profile/John1995ac',
      alt: 'Follow on Nexus Mods'
    },
    {
      name: 'GitHub',
      image: IMAGE_BASE + 'support_me_on_GITHUB_badge_blue.png',
      url: 'https://github.com/John95ac',
      alt: 'Follow on GitHub'
    }
  ];

  let socialLinks = defaults.map(x => ({ ...x }));

  async function overrideFromIni() {
    try {
      const res = await fetch('Data/Links.ini', { cache: 'no-store' });
      if (!res.ok) return;
      const txt = await res.text();
      const lines = txt.split(/\r?\n/).map(l => l.trim()).filter(Boolean);

      const mapByName = (name) => socialLinks.find(l => l.name === name);

      for (const line of lines) {
        if (line.includes('nexusmods.com')) {
          const n = mapByName('Nexus Mods'); if (n) n.url = line; else socialLinks.push({ name:'Nexus Mods', image: IMAGE_BASE + 'support_me_on_Nexus_badge.png', url: line, alt:'Follow on Nexus Mods' });
        }
        if (line.includes('github.com')) {
          const g = mapByName('GitHub'); if (g) g.url = line; else socialLinks.push({ name:'GitHub', image: IMAGE_BASE + 'support_me_on_GITHUB_badge_blue.png', url: line, alt:'Follow on GitHub' });
        }
        if (line.includes('youtube.com') || line.includes('youtu.be')) {
          const y = mapByName('YouTube'); if (y) y.url = line;
        }
        // Removed LoversLab link as requested
      }
    } catch {}
  }

  function createSocialLinksStyles() {
    const style = document.createElement('style');
    style.textContent = `
      #social-links-ball {
        position: fixed; bottom: 70px; right: 20px; width: 40px; height: 40px;
        background: linear-gradient(135deg, #6B7280, #4B5563);
        border-radius: 50%; cursor: pointer; z-index: 9998;
        display: none; align-items: center; justify-content: center;
        color: white; font-size: 16px;
        transition: all 0.6s cubic-bezier(0.25,0.46,0.45,0.94);
        box-shadow: 0 4px 20px rgba(107,114,128,0.4);
        border: 2px solid rgba(255,255,255,0.3);
      }
      #social-links-ball:hover {
        background: linear-gradient(135deg, #4B5563, #374151);
        transform: scale(1.15);
        box-shadow: 0 8px 35px rgba(107,114,128,0.7);
      }
      #social-links-panel {
        position: fixed; bottom: 120px; right: 20px;
        background: linear-gradient(135deg, rgba(0,0,0,0.95), rgba(30,30,30,0.95));
        border-radius: 15px; z-index: 9999; display: none;
        min-width: 280px; max-width: 350px;
        box-shadow: 0 8px 32px rgba(0,0,0,0.6);
        border: 1px solid rgba(255,255,255,0.1); backdrop-filter: blur(10px);
        overflow: hidden; transition: all 0.3s ease;
      }
      #social-links-panel.show { display: block; }
      .social-links-header {
        background: linear-gradient(135deg, #7289da, #5b6eae); color: white; padding: 20px; text-align: center;
        position: relative; overflow: hidden;
      }
      .social-links-header::before {
        content:''; position:absolute; top:-50%; left:-50%; width:200%; height:200%;
        background: radial-gradient(circle, rgba(255,255,255,0.1) 0%, transparent 70%);
        animation: socialLinksHeaderShine 3s infinite;
      }
      @keyframes socialLinksHeaderShine { 0%{transform:rotate(0)} 100%{transform:rotate(360deg)} }
      .social-links-header h3 { margin:0 0 8px 0; font-size:18px; font-weight:700; position:relative; z-index:1; }
      .social-links-header p { margin:0; font-size:14px; opacity:.9; position:relative; z-index:1; }
      .social-links { padding: 25px 20px; display:flex; flex-direction:column; gap:15px; }
      .social-link {
        display:flex; align-items:center; padding:15px; background:rgba(255,255,255,0.05);
        border-radius:12px; border:1px solid rgba(255,255,255,0.1); cursor:pointer;
        transition:all .3s cubic-bezier(.4,0,.2,1); text-decoration:none; color:inherit;
        position:relative; overflow:hidden;
      }
      .social-link::before {
        content:''; position:absolute; top:0; left:-100%; width:100%; height:100%;
        background: linear-gradient(90deg, transparent, rgba(255,255,255,0.1), transparent);
        transition: left .5s;
      }
      .social-link:hover::before { left:100%; }
      .social-link:hover { background:rgba(255,255,255,0.1); border-color:rgba(255,255,255,0.3); transform:translateY(-2px); box-shadow:0 4px 15px rgba(0,0,0,0.2); }
      .social-link img { width:60px; height:30px; object-fit:contain; border-radius:6px; margin-right:15px; transition: transform .3s ease; }
      .social-link:hover img { transform: scale(1.05); }
      .social-link-info { flex:1; }
      .social-link-title { font-size:16px; font-weight:600; margin:0 0 4px 0; color:#fff; }
      .social-link-desc { font-size:12px; color:rgba(255,255,255,0.7); margin:0; }
      .social-link-icon { font-size:20px; color:rgba(255,255,255,0.5); transition: transform .3s ease; }
      .social-link:hover .social-link-icon { transform: translateX(3px); color:#7289da; }
      .social-links-footer { background: rgba(0,0,0,0.2); padding: 15px 20px; text-align:center; border-top:1px solid rgba(255,255,255,0.1); }
      .social-links-footer small { color: rgba(255,255,255,0.6); font-style: italic; }
      @media (max-width: 768px) {
        #social-links-panel { right: 10px; left: 10px; min-width:auto; max-width:none; }
        #social-links-ball { width: 45px; height: 45px; font-size: 20px; }
      }
    `;
    document.head.appendChild(style);
  }

  function createSocialLinks() {
    const linksContainer = document.getElementById('social-links');
    linksContainer.innerHTML = '';
    socialLinks.forEach((link, index) => {
      const a = document.createElement('a');
      a.href = link.url; a.target = '_blank'; a.rel = 'noopener noreferrer';
      a.className = 'social-link'; a.style.animationDelay = `${index * 0.1}s`;
      a.innerHTML = `
        <img src="${link.image}" alt="${link.alt}" loading="lazy">
        <div class="social-link-info">
          <h4 class="social-link-title">${link.name}</h4>
          <p class="social-link-desc">${link.name === 'Discord' ? 'Discord server coming soon' : link.name === 'Web Doc John95AC' ? 'Web - Work in progress' : link.name === 'News' ? 'News, acknowledgments and updates' : `Click to follow on ${link.name}`}</p>
        </div>
        <div class="social-link-icon">→</div>
      `;
      // Intercept click - show confirmation dialog first
      a.addEventListener('click', function(e) {
        e.preventDefault();
        if (window.showConfirmDialog) {
          window.showConfirmDialog(
            'Open this link in your browser?',
            function() {
              // User accepted - open URL
              if (window.onOpenURL) {
                window.onOpenURL(link.url);
              } else {
                window.open(link.url, '_blank');
              }
            },
            null // Cancel does nothing
          );
        } else {
          // Fallback if dialog not available
          if (window.onOpenURL) {
            window.onOpenURL(link.url);
          } else {
            window.open(link.url, '_blank');
          }
        }
      });
      linksContainer.appendChild(a);
    });
  }

  function initializeSocialLinksEvents() {
    const $ball = document.getElementById('social-links-ball');
    const $panel = document.getElementById('social-links-panel');

    function animateLinks() {
      const links = $panel.querySelectorAll('.social-link');
      links.forEach((link, index) => {
        link.style.opacity = '0'; link.style.transform = 'translateY(20px)';
        setTimeout(() => {
          link.style.transition = 'all 0.6s cubic-bezier(0.25,0.46,0.45,0.94)';
          link.style.opacity = '1'; link.style.transform = 'translateY(0)';
        }, index * 150 + 300);
      });
    }

    $ball.onmouseenter = () => { $panel.classList.add('show'); animateLinks(); };
    $ball.onmouseleave = () => { setTimeout(() => { if (!$panel.matches(':hover')) $panel.classList.remove('show'); }, 200); };
    $panel.onmouseenter = () => {};
    $panel.onmouseleave = () => { $panel.classList.remove('show'); };
    document.addEventListener('click', (e) => { if (!e.target.closest('#social-links-ball, #social-links-panel')) $panel.classList.remove('show'); });
  }

  function createSocialLinksElements() {
    if (document.getElementById('social-links-ball')) return;

    const ball = document.createElement('div');
    ball.id = 'social-links-ball';
    ball.innerHTML = '📢';
    ball.title = 'Follow & Contact';
    document.body.appendChild(ball);

    const panel = document.createElement('div');
    panel.id = 'social-links-panel';
    panel.innerHTML = `
      <div class="social-links-header">
        <h3>📢 Follow and Contact</h3>
        <p>Connect to review more projects!</p>
      </div>
      <div class="social-links" id="social-links"></div>
      <div class="social-links-footer">
        <small>🌟 Discord will be available soon, I'm just improving security</small>
      </div>
    `;
    document.body.appendChild(panel);

    createSocialLinksStyles();
    createSocialLinks(); initializeSocialLinksEvents();
  }

  // ============================================
  // GLOBAL FUNCTIONS FOR VISIBILITY SYNC
  // ============================================
  
  window.showSocialLinksBall = function() {
    const ball = document.getElementById('social-links-ball');
    const panel = document.getElementById('social-links-panel');
    if (ball) ball.style.display = 'flex';
    // Panel remains hidden until hover
  };
  
  window.hideSocialLinksBall = function() {
    const ball = document.getElementById('social-links-ball');
    const panel = document.getElementById('social-links-panel');
    if (ball) ball.style.display = 'none';
    if (panel) panel.classList.remove('show');
  };
  
  // State getter for ImpactEngine
  window.isSocialLinksBallVisible = function() {
    const ball = document.getElementById('social-links-ball');
    return ball && ball.style.display !== 'none';
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', createSocialLinksElements);
  } else {
    createSocialLinksElements();
  }
})();