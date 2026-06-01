/* console.js — builds the control surface and talks to the Python js_api.
 * Exposes ui* helpers on window for bridge.js (Python→JS) to call.
 */
(function () {
  const MOODS = window.MOODS, GROUPS = window.GROUPS, CUE = window.CUE_STATES;
  const $ = (id) => document.getElementById(id);

  function api() { return (window.pywebview && window.pywebview.api) || null; }

  // ── build the 20-state grid ────────────────────────────────────────────────
  const wrap = $('controls');
  GROUPS.forEach((g) => {
    const sec = document.createElement('div'); sec.className = 'group';
    const h = document.createElement('div'); h.className = 'gh'; h.textContent = g.h; sec.appendChild(h);
    const grid = document.createElement('div'); grid.className = 'grid';
    g.keys.forEach((key) => {
      const m = MOODS[key];
      const b = document.createElement('button');
      b.className = 'chip'; b.dataset.key = key;
      b.innerHTML = `<span class="nm"><span class="swatch" style="background:${m.color}"></span>${m.label}</span><span class="id">${key}</span>`;
      b.onclick = () => pushState(key);
      grid.appendChild(b);
    });
    sec.appendChild(grid); wrap.appendChild(sec);
  });

  // ── cue-tone test row ──────────────────────────────────────────────────────
  const cuerow = $('cuerow');
  CUE.forEach((key) => {
    const m = MOODS[key];
    const b = document.createElement('button');
    b.className = 'cue';
    b.innerHTML = `<span class="swatch" style="background:${m.color}"></span>${m.label}`;
    b.onclick = () => pushState(key);
    cuerow.appendChild(b);
  });

  // ── top tabs (表情 / 余量) ──────────────────────────────────────────────────
  $('tabs').querySelectorAll('button').forEach((btn) => {
    btn.onclick = () => {
      const tab = btn.dataset.tab;
      $('tabs').querySelectorAll('button').forEach((b) => b.classList.toggle('on', b === btn));
      $('page-face').style.display = tab === 'face' ? '' : 'none';
      $('page-usage').style.display = tab === 'usage' ? '' : 'none';
    };
  });

  // ── mode toggle ────────────────────────────────────────────────────────────
  $('seg').querySelectorAll('button').forEach((btn) => {
    btn.onclick = () => {
      const mode = btn.dataset.mode;
      setModeUI(mode);
      const a = api(); if (a) a.set_mode(mode);
    };
  });

  function pushState(key) {
    const a = api(); if (a) a.set_state(key);
    // optimistic: reflect immediately even before Python echoes back
    window.uiSetState(key, 'manual');
  }

  // ── ui helpers (called by bridge.js / hydrate) ─────────────────────────────
  window.uiSetConnection = function (connected) {
    const pill = $('pill');
    pill.classList.toggle('on', !!connected);
    $('pillText').textContent = connected ? 'connected' : 'offline';
  };

  window.uiSetState = function (key, source) {
    if (!MOODS[key]) return;
    window.setMood(key);
    const m = MOODS[key];
    $('npLabel').textContent = m.label;
    $('npId').textContent = key;
    const src = $('npSrc');
    if (source && source !== 'init') { src.style.display = ''; src.textContent = source; }
    document.querySelectorAll('.chip').forEach((b) => b.classList.toggle('on', b.dataset.key === key));
  };

  window.uiSetMode = function (mode) {
    $('seg').querySelectorAll('button').forEach((b) => b.classList.toggle('on', b.dataset.mode === mode));
  };

  function setModeUI(mode) { window.uiSetMode(mode); }

  window.uiAppendLog = function (line) {
    const log = $('log');
    const d = document.createElement('div'); d.textContent = line;
    log.appendChild(d);
    while (log.childNodes.length > 80) log.removeChild(log.firstChild);
    log.scrollTop = log.scrollHeight;
  };

  // ── usage page ─────────────────────────────────────────────────────────────
  function fmtReset(mins) {
    if (mins == null || mins < 0) return '—';
    if (mins < 60) return `${mins}m`;
    if (mins < 1440) return `${Math.floor(mins / 60)}h${mins % 60}m`;
    const d = Math.floor(mins / 1440), h = Math.floor((mins % 1440) / 60);
    return `${d}d${h}h`;
  }
  function pad2(n) { return n < 10 ? '0' + n : '' + n; }
  function barClass(pct) { return pct < 60 ? 'lo' : (pct < 85 ? 'mid' : 'hi'); }

  function setMetric(pctEl, fillEl, resetEl, pct, resetMins) {
    pct = Math.max(0, Math.min(100, pct | 0));
    pctEl.textContent = `${pct}%`;
    fillEl.style.width = `${pct}%`;
    fillEl.className = 'fill ' + barClass(pct);
    resetEl.textContent = `重置于 ${fmtReset(resetMins)}`;
  }

  window.uiSetUsage = function (u) {
    const card = $('usageCard'), empty = $('usageEmpty');
    if (!u || u.ok === false) {
      card.style.display = 'none';
      empty.style.display = '';
      return;
    }
    empty.style.display = 'none';
    card.style.display = '';
    setMetric($('uSessionPct'), $('uSessionFill'), $('uSessionReset'), u.s, u.sr);
    setMetric($('uWeeklyPct'), $('uWeeklyFill'), $('uWeeklyReset'), u.w, u.wr);
    $('uStatus').textContent = '状态 · ' + (u.st || 'unknown');
    const now = new Date();
    $('uUpdated').textContent = `更新于 ${pad2(now.getHours())}:${pad2(now.getMinutes())}`;
  };

  $('uRefresh').onclick = () => { const a = api(); if (a) a.refresh_usage(); };

  // ── hydrate once pywebview API is ready ────────────────────────────────────
  function hydrate() {
    const a = api(); if (!a) return;
    Promise.resolve(a.get_status()).then((st) => {
      if (!st) return;
      window.uiSetConnection(st.connected);
      window.uiSetMode(st.mode);
      window.uiSetState(st.state, 'init');
    }).catch(() => {});
    Promise.resolve(a.get_usage()).then((u) => window.uiSetUsage(u)).catch(() => {});
  }
  window.addEventListener('pywebviewready', hydrate);
  // Fallback in case the event already fired before this listener attached.
  setTimeout(hydrate, 600);
})();
