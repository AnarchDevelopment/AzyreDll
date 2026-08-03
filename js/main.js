/**
 * Aegleseeker Client - Interactive Script
 * Handles navigation, module filters, live HUD interactive preview & code copy
 */

const GITHUB_OWNER = 'AnarchDevelopment';
const GITHUB_REPO  = 'aegledll';

// Local fallback commits from git log (in case GitHub API is rate-limited)
const LOCAL_COMMITS = [
  {
    sha: '4e37ef29d9d07be9ba7e382c01ad7e1c44ee22f2',
    commit: {
      message: 'Solution modified to Multi Thread',
      author: { name: 'iVyz3r', date: '2026-07-20T16:17:46-04:00' }
    },
    html_url: `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/commit/4e37ef29d9d07be9ba7e382c01ad7e1c44ee22f2`
  },
  {
    sha: 'a90242969689c91b02b8df096010ac47ff026455',
    commit: {
      message: 'Update license information in README.md',
      author: { name: 'NqtVyzer', date: '2026-07-20T16:05:10-04:00' }
    },
    html_url: `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/commit/a90242969689c91b02b8df096010ac47ff026455`
  },
  {
    sha: 'dff222cc75f2b9f5b3b28dabf687abc6170705cb',
    commit: {
      message: 'Add an4rch Development Public Source License v1.1',
      author: { name: 'NqtVyzer', date: '2026-07-20T16:04:07-04:00' }
    },
    html_url: `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/commit/dff222cc75f2b9f5b3b28dabf687abc6170705cb`
  }
];

document.addEventListener('DOMContentLoaded', () => {
  initNavbar();
  initModulesGrid();
  initHUDInteractiveSim();
  initCopyButtons();
  initGitHubCommits();
});

/* ─── Navbar ─────────────────────────────────────────────────────────────── */
function initNavbar() {
  const navbar       = document.querySelector('.navbar');
  const mobileToggle = document.querySelector('.mobile-toggle');
  const navLinks     = document.querySelector('.nav-links');

  window.addEventListener('scroll', () => {
    navbar.classList.toggle('scrolled', window.scrollY > 30);
  });

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

  if (!gridContainer || typeof aegleModules === 'undefined') return;

  let currentCategory = 'ALL';
  let searchQuery     = '';

  function renderModules() {
    gridContainer.innerHTML = '';

    const filtered = aegleModules.filter(mod => {
      const matchesCat    = (currentCategory === 'ALL') || (mod.category.toUpperCase() === currentCategory);
      const matchesSearch = mod.name.toLowerCase().includes(searchQuery) ||
                            mod.description.toLowerCase().includes(searchQuery) ||
                            mod.tags.some(t => t.toLowerCase().includes(searchQuery));
      return matchesCat && matchesSearch;
    });

    if (filtered.length === 0) {
      gridContainer.innerHTML = `
        <div style="grid-column: 1 / -1; text-align: center; padding: 3rem 1rem; color: var(--text-muted);">
          <p style="font-size: 1.1rem; margin-bottom: 0.5rem;">No modules found matching your current search query.</p>
          <span style="font-size: 0.85rem;">Try searching another keyword or select the "All" tab.</span>
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
            ${mod.tags.map(t => `<span style="font-size:.72rem;color:var(--text-muted);background:rgba(255,255,255,.03);padding:.15rem .45rem;border-radius:4px;">#${t}</span>`).join('')}
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

/* ─── GitHub Real Commits ─────────────────────────────────────────────────── */
function initGitHubCommits() {
  const commitsList    = document.getElementById('real-commits-list');
  const latestMsgEl    = document.getElementById('latest-commit-msg');
  const latestShaEl    = document.getElementById('latest-commit-sha');

  if (!commitsList) return;

  const apiUrl = `https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/commits?per_page=5`;

  // Show loading state
  commitsList.innerHTML = `
    <div class="file-row" style="justify-content:center;color:var(--text-muted);">
      <span>⟳ Fetching latest commits from GitHub API…</span>
    </div>`;

  fetch(apiUrl, {
    headers: { 'Accept': 'application/vnd.github.v3+json' }
  })
    .then(res => {
      if (!res.ok) throw new Error(`GitHub API ${res.status}`);
      return res.json();
    })
    .then(commits => renderCommits(commits))
    .catch(() => {
      // Fallback: use local commits recorded from git log
      renderCommits(LOCAL_COMMITS, true);
    });

  function renderCommits(commits, isFallback = false) {
    if (!commits || commits.length === 0) {
      renderCommits(LOCAL_COMMITS, true);
      return;
    }

    // Update latest commit header
    const latest = commits[0];
    const latestMsg = latest.commit.message.split('\n')[0]; // first line only
    const latestSha = latest.sha.substring(0, 7);

    if (latestMsgEl) latestMsgEl.textContent = latestMsg;
    if (latestShaEl) latestShaEl.textContent = latestSha;

    // Build commit rows
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
