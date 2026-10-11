/**
 * Azyre Client — Interactive Script
 * Navigation, module filtering, live HUD preview, docs scroll-spy, commit feed.
 */

const GITHUB_OWNER = 'AnarchDevelopment';
const GITHUB_REPO  = 'AzyreDll';

// Local fallback commits (used when the GitHub API is rate-limited).
const LOCAL_COMMITS = [
  {
    sha: 'de24a64e057259c666a7dc705d9b8541b8036d75',
    commit: { message: 'ci: green build across MSVC, Clang-CL and MinGW', author: { name: 'iVyz3r', date: '2026-10-11T03:55:00-04:00' } },
    html_url: `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/commit/de24a64e057259c666a7dc705d9b8541b8036d75`
  },
  {
    sha: '0244b7615384c76424b823f4d31cc0a1aa16dc5c',
    commit: { message: 'ci: cross-toolchain compatibility (SEH / WinRT / CRT)', author: { name: 'iVyz3r', date: '2026-10-11T03:18:00-04:00' } },
    html_url: `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/commit/0244b7615384c76424b823f4d31cc0a1aa16dc5c`
  },
  {
    sha: '7cae045c4bd5763d4fc02d2ca3bf656fbcbf0583',
    commit: { message: 'feat: add networking system to Spotify module and IRC implementation', author: { name: 'iVyz3r', date: '2026-10-11T02:59:00-04:00' } },
    html_url: `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/commit/7cae045c4bd5763d4fc02d2ca3bf656fbcbf0583`
  }
];

document.addEventListener('DOMContentLoaded', () => {
  initNavbar();
  initModulesGrid();
  initHUDInteractiveSim();
  initCopyButtons();
  initGitHubCommits();
  initDocsScrollSpy();
});

/* ─── Navbar ─────────────────────────────────────────────────────────────── */
function initNavbar() {
  const navbar       = document.querySelector('.navbar');
  const mobileToggle = document.querySelector('.mobile-toggle');
  const navLinks     = document.querySelector('.nav-links');

  if (navbar) {
    window.addEventListener('scroll', () => {
      navbar.classList.toggle('scrolled', window.scrollY > 30);
    });
  }

  if (mobileToggle && navLinks) {
    mobileToggle.addEventListener('click', () => navLinks.classList.toggle('open'));
    navLinks.querySelectorAll('a').forEach(link => {
      link.addEventListener('click', () => navLinks.classList.remove('open'));
    });
  }
}

/* ─── Module Grid ────────────────────────────────────────────────────────── */
function initModulesGrid() {
  const gridContainer = document.getElementById('modules-grid');
  const searchInput   = document.getElementById('module-search');
  const tabButtons    = document.querySelectorAll('.tab-btn');

  if (!gridContainer || typeof azyreModules === 'undefined') return;

  let currentCategory = 'ALL';
  let searchQuery     = '';

  function renderModules() {
    gridContainer.innerHTML = '';

    const filtered = azyreModules.filter(mod => {
      const matchesCat    = (currentCategory === 'ALL') || (mod.category.toUpperCase() === currentCategory);
      const matchesSearch = mod.name.toLowerCase().includes(searchQuery) ||
                            mod.description.toLowerCase().includes(searchQuery) ||
                            mod.tags.some(t => t.toLowerCase().includes(searchQuery));
      return matchesCat && matchesSearch;
    });

    if (filtered.length === 0) {
      gridContainer.innerHTML = `
        <div style="grid-column: 1 / -1; text-align: center; padding: 3rem 1rem; color: var(--text-muted);">
          <p style="font-size: 1.1rem; margin-bottom: 0.5rem;">No modules found for this query.</p>
          <span style="font-size: 0.85rem;">Try another keyword or select the "All" tab.</span>
        </div>`;
      return;
    }

    filtered.forEach(mod => {
      const card = document.createElement('div');
      card.className = 'module-card';
      card.innerHTML = `
        <div class="card-header">
          <div class="title-group">
            <div class="module-category">${mod.category}</div>
            <h3 class="module-name">${mod.name}</h3>
          </div>
          <span class="badge ${mod.status === 'Core' ? 'badge--purple' : ''}">${mod.status}</span>
        </div>
        <p class="module-desc">${mod.description}</p>
        <div class="card-footer">
          <div style="display:flex;gap:.4rem;flex-wrap:wrap;">
            ${mod.tags.map(t => `<span class="tag">#${t}</span>`).join('')}
          </div>
          <span class="keybind-badge">${mod.keybind}</span>
        </div>`;
      gridContainer.appendChild(card);
    });
  }

  renderModules();

  tabButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      tabButtons.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      currentCategory = btn.getAttribute('data-category');
      renderModules();
    });
  });

  if (searchInput) {
    searchInput.addEventListener('input', e => {
      searchQuery = e.target.value.toLowerCase().trim();
      renderModules();
    });
  }
}

/* ─── HUD Simulator ──────────────────────────────────────────────────────── */
function initHUDInteractiveSim() {
  const keyW       = document.querySelector('.key-w');
  const keyA       = document.querySelector('.key-a');
  const keyS       = document.querySelector('.key-s');
  const keyD       = document.querySelector('.key-d');
  const cpsDisplay = document.getElementById('sim-cps');
  const fpsDisplay = document.getElementById('sim-fps');

  if (!keyW) return;

  let clickCount = 0;

  window.addEventListener('keydown', e => {
    const k = e.key.toLowerCase();
    if (k === 'w') keyW?.classList.add('active');
    if (k === 'a') keyA?.classList.add('active');
    if (k === 's') keyS?.classList.add('active');
    if (k === 'd') keyD?.classList.add('active');
  });

  window.addEventListener('keyup', e => {
    const k = e.key.toLowerCase();
    if (k === 'w') keyW?.classList.remove('active');
    if (k === 'a') keyA?.classList.remove('active');
    if (k === 's') keyS?.classList.remove('active');
    if (k === 'd') keyD?.classList.remove('active');
  });

  window.addEventListener('mousedown', () => {
    clickCount++;
    if (cpsDisplay) cpsDisplay.textContent = Math.min(18, clickCount + 6);
  });

  setInterval(() => {
    if (clickCount > 0) clickCount--;
    if (cpsDisplay) cpsDisplay.textContent = clickCount > 0 ? clickCount + 4 : 12;
    if (fpsDisplay) fpsDisplay.textContent = 240 + Math.floor(Math.random() * 9) - 4;
  }, 1000);
}

/* ─── Copy Buttons ───────────────────────────────────────────────────────── */
function initCopyButtons() {
  document.querySelectorAll('.copy-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const el = document.getElementById(btn.getAttribute('data-target'));
      if (!el) return;
      navigator.clipboard.writeText(el.textContent.trim()).then(() => {
        const orig = btn.innerHTML;
        btn.innerHTML = '<span>✓ Copied!</span>';
        setTimeout(() => { btn.innerHTML = orig; }, 2000);
      });
    });
  });
}

/* ─── Docs scroll spy ────────────────────────────────────────────────────── */
function initDocsScrollSpy() {
  const links = Array.from(document.querySelectorAll('.docs-toc a'));
  if (links.length === 0) return;

  const targets = links
    .map(a => document.querySelector(a.getAttribute('href')))
    .filter(Boolean);
  if (targets.length === 0) return;

  const setActive = (id) => {
    links.forEach(a => a.classList.toggle('active', a.getAttribute('href') === '#' + id));
  };

  const observer = new IntersectionObserver(entries => {
    entries.forEach(entry => {
      if (entry.isIntersecting) setActive(entry.target.id);
    });
  }, { rootMargin: '-90px 0px -70% 0px', threshold: 0 });

  targets.forEach(t => observer.observe(t));
}

/* ─── GitHub Real Commits ─────────────────────────────────────────────────── */
function initGitHubCommits() {
  const commitsList    = document.getElementById('real-commits-list');
  const latestMsgEl    = document.getElementById('latest-commit-msg');
  const latestShaEl    = document.getElementById('latest-commit-sha');

  if (!commitsList) return;

  const apiUrl = `https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/commits?per_page=5`;

  commitsList.innerHTML = `
    <div class="file-row" style="justify-content:center;color:var(--text-muted);">
      <span>⟳ Fetching latest commits from GitHub API…</span>
    </div>`;

  fetch(apiUrl, { headers: { 'Accept': 'application/vnd.github.v3+json' } })
    .then(res => {
      if (!res.ok) throw new Error(`GitHub API ${res.status}`);
      return res.json();
    })
    .then(commits => renderCommits(commits))
    .catch(() => renderCommits(LOCAL_COMMITS, true));

  function renderCommits(commits, isFallback = false) {
    if (!commits || commits.length === 0) {
      renderCommits(LOCAL_COMMITS, true);
      return;
    }

    const latest = commits[0];
    const latestMsg = latest.commit.message.split('\n')[0];
    const latestSha = latest.sha.substring(0, 7);

    if (latestMsgEl) latestMsgEl.textContent = latestMsg;
    if (latestShaEl) latestShaEl.textContent = latestSha;

    commitsList.innerHTML = '';

    if (isFallback) {
      const notice = document.createElement('div');
      notice.style.cssText = 'font-size:.75rem;color:var(--text-muted);margin-bottom:.75rem;padding:.4rem .85rem;';
      notice.textContent = '⚠ GitHub API rate limit — showing cached local commits';
      commitsList.appendChild(notice);
    }

    commits.forEach(c => {
      const sha     = c.sha.substring(0, 7);
      const message = c.commit.message.split('\n')[0];
      const author  = c.commit.author.name;
      const date    = new Date(c.commit.author.date);
      const dateStr = date.toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
      const url     = c.html_url || `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/commit/${c.sha}`;

      const row = document.createElement('a');
      row.href   = url;
      row.target = '_blank';
      row.rel    = 'noopener noreferrer';
      row.className = 'file-row';
      row.style.textDecoration = 'none';
      row.innerHTML = `
        <span class="file-name">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <circle cx="12" cy="12" r="4"/>
            <line x1="1.05" y1="12" x2="7" y2="12"/>
            <line x1="17.01" y1="12" x2="22.96" y2="12"/>
          </svg>
          <span style="max-width:340px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;" title="${message}">${message}</span>
        </span>
        <span class="file-desc" style="display:flex;align-items:center;gap:.5rem;flex-shrink:0;">
          <span class="keybind-badge">${sha}</span>
          <strong>${author}</strong>
          <span>${dateStr}</span>
        </span>`;
      commitsList.appendChild(row);
    });
  }
}
