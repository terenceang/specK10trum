(function(window) {
  'use strict';

  const esc = window.ZX_UTILS.esc;
  const TAPE_MODES = {
    manual: { showDeck: true },
    auto: { showDeck: false },
    instant: { showDeck: false },
  };

  function normalizeTapeMode(mode) {
    if (mode === 'normal') return 'auto';
    if (mode === 'player') return 'manual';
    return mode || 'auto';
  }

  function isKnownTapeMode(mode) {
    return Object.prototype.hasOwnProperty.call(TAPE_MODES, mode);
  }

  let preferredTapeMode = (function() {
    try {
      const normalized = normalizeTapeMode(localStorage.getItem('zx_tape_mode'));
      localStorage.setItem('zx_tape_mode', normalized);
      return normalized;
    } catch (_) {
      return 'auto';
    }
  })();
  let activeTapeMode = preferredTapeMode;
  let lastTape = (function() { try { return localStorage.getItem('zx_last_tape') || null; } catch (_) { return null; } })();
  let tapeMonitorEnabled = (function() {
    try { return localStorage.getItem('zx_tape_monitor') === 'on'; }
    catch (_) { return false; }
  })();

  let btnPlay = null;
  let tapeCounter = null;
  let statusInterval = null;
  let awaitingInstaloadDiag = false;

  function setPreferredTapeMode(mode) {
    preferredTapeMode = normalizeTapeMode(mode);
    try { localStorage.setItem('zx_tape_mode', preferredTapeMode); } catch(_) {}
  }

  function setActiveTapeMode(mode) {
    const normalized = normalizeTapeMode(mode);
    if (!isKnownTapeMode(normalized)) return;
    activeTapeMode = normalized;
    updateUI();
  }

  function sendTapeMode(mode) {
    if (!window.ZX_WS) return;
    window.ZX_WS.send(JSON.stringify({ cmd: 'tape_mode_' + normalizeTapeMode(mode) }));
  }

  function sendTapeMonitorState() {
    if (!window.ZX_WS) return;
    window.ZX_WS.send(JSON.stringify({ cmd: tapeMonitorEnabled ? 'tape_monitor_on' : 'tape_monitor_off' }));
  }

  function stopTape() {
    if (window.ZX_WS) window.ZX_WS.send(JSON.stringify({ cmd: 'tape_stop' }));
  }

  function init() {
    const tapeMonitorToggle = document.getElementById('zx-tape-monitor-toggle');
    const btnLoadTape = document.getElementById('zx-btn-load-tape');
    const btnInstaload = document.getElementById('zx-btn-instaload');
    const playerRefresh = document.getElementById('zx-player-refresh');
    const btnPlayerClose = document.getElementById('zx-player-close');
    const tapePlayer = document.getElementById('zx-player');
    const modeButtons = document.querySelectorAll('[data-tape-mode]');

    modeButtons.forEach((button) => {
      button.addEventListener('click', () => {
        const mode = normalizeTapeMode(button.dataset.tapeMode);
        setPreferredTapeMode(mode);
        setActiveTapeMode(mode);
        sendTapeMode(mode);
      });
    });

    if (tapeMonitorToggle) {
      tapeMonitorToggle.checked = tapeMonitorEnabled;
      tapeMonitorToggle.addEventListener('change', (e) => {
        tapeMonitorEnabled = !!e.target.checked;
        try { localStorage.setItem('zx_tape_monitor', tapeMonitorEnabled ? 'on' : 'off'); } catch(_) {}
        sendTapeMonitorState();
      });
    }

    btnPlay = document.querySelector('[data-tape="tape_play"]');
    tapeCounter = document.getElementById('zx-player-counter');

    if (btnLoadTape) {
      btnLoadTape.addEventListener('click', () => {
        const filesList = document.getElementById('zx-player-files');
        if (filesList.style.display === 'none') {
          filesList.style.display = 'block';
          loadTapeList();
        } else {
          filesList.style.display = 'none';
        }
      });
    }

    if (btnInstaload) {
      btnInstaload.addEventListener('click', () => {
        const label = document.getElementById('zx-player-label');
        if (!window.ZX_WS) {
          if (label) label.textContent = 'WS DISCONNECTED';
          return;
        }
        awaitingInstaloadDiag = true;
        window.ZX_WS.send(JSON.stringify({ cmd: 'tape_instaload' }));
        if (label) label.textContent = 'INSTALOAD SENT';
      });
    }

    if (btnPlayerClose) {
      btnPlayerClose.addEventListener('click', () => {
        tapePlayer.classList.remove('open');
      });
    }

    if (playerRefresh) playerRefresh.addEventListener('click', () => loadTapeList());

    if (tapePlayer) {
      tapePlayer.addEventListener('click', (e) => {
        const btn = e.target.closest('.zx-player-btn');
        if (!btn || btn.id === 'zx-btn-load-tape' || btn.id === 'zx-btn-instaload') return;
        const cmd = btn.dataset.tape;
        if (window.ZX_WS) window.ZX_WS.send(JSON.stringify({ cmd }));
        
        // UI Animations
        const leftReel = document.getElementById('zx-reel-left');
        const rightReel = document.getElementById('zx-reel-right');
        leftReel.className = 'zx-player-reel';
        rightReel.className = 'zx-player-reel';
        
        if (cmd === 'tape_play') {
          leftReel.classList.add('spin');
          rightReel.classList.add('spin');
          if (btnPlay) btnPlay.classList.add('active');
        } else {
          if (btnPlay) btnPlay.classList.remove('active');
        }
        if (cmd === 'tape_rewind') {
          leftReel.classList.add('spin-fast');
        } else if (cmd === 'tape_ffwd') {
          rightReel.classList.add('spin-fast');
        } else if (cmd === 'tape_eject') {
          document.getElementById('zx-player-label').textContent = 'NO TAPE LOADED';
          updateTapeCounter(0, 0);
        }
      });
    }

    updateUI();
    startStatusPolling();
    if (lastTape) {
      const lbl = document.getElementById('zx-player-label');
      if (lbl) lbl.textContent = lastTape;
      // Load persisted tape on start, but don't auto-play
      fetch(`${window.ZX_UTILS.API.LOAD}?file=${encodeURIComponent(lastTape)}`)
        .catch(() => {});
    }

    // Handle WS open - only sync mode if page was already visible
    window.addEventListener('zx-ws-open', () => {
      if (window.ZX_WS && !document.hidden) {
        sendTapeMode(preferredTapeMode);
        sendTapeMonitorState();
      }
    });

    // Clear tape state on system reset
    document.addEventListener('click', (e) => {
      if (e.target.dataset.action === 'reset') {
        lastTape = null;
        try { localStorage.removeItem('zx_last_tape'); } catch(_) {}
        stopTape();
      }
    });
  }

  function updateUI() {
    const cassetteDeck = document.getElementById('zx-cassette-deck');
    const modeButtons = document.querySelectorAll('[data-tape-mode]');
    const modeConfig = TAPE_MODES[activeTapeMode] || TAPE_MODES.auto;

    if (cassetteDeck) {
      cassetteDeck.style.display = modeConfig.showDeck ? 'block' : 'none';
    }

    modeButtons.forEach((button) => {
      button.classList.toggle('active', normalizeTapeMode(button.dataset.tapeMode) === activeTapeMode);
    });
  }

  function setPlayButtonActive(active) {
    if (!btnPlay) return;
    btnPlay.classList.toggle('active', !!active);
  }

  function updateTapeCounter(current, total) {
    if (!tapeCounter) return;
    tapeCounter.textContent = total > 0 ? `Block ${current} / ${total}` : 'No tape blocks';
  }

  async function updateTapeStatus() {
    if (!window.ZX_UTILS || !window.ZX_UTILS.API || !window.ZX_UTILS.API.TAPE_CMD) return;
    try {
      const res = await fetch(`${window.ZX_UTILS.API.TAPE_CMD}?cmd=status`);
      if (!res.ok) return;
      const status = await res.json();
      if (status.mode && isKnownTapeMode(normalizeTapeMode(status.mode))) {
        setActiveTapeMode(status.mode);
      }
      setPlayButtonActive(status.loaded && status.playing && !status.paused);
      updateTapeCounter(status.currentBlock, status.totalBlocks);
      const label = document.getElementById('zx-player-label');
      if (awaitingInstaloadDiag && label && status.instaloadDiag) {
        label.textContent = `INSTALOAD ${status.instaloadDiag}`;
        awaitingInstaloadDiag = false;
      }
      if (label) {
        if (status.loaded && status.totalBlocks > 0 && label.textContent === 'NO TAPE LOADED') {
          label.textContent = lastTape || 'Loaded tape';
        }
        if (!status.loaded) {
          label.textContent = 'NO TAPE LOADED';
        }
      }
    } catch (e) {
      // ignore transient fetch failures
    }
  }

  function startStatusPolling() {
    if (statusInterval) return;
    updateTapeStatus();
    statusInterval = window.setInterval(updateTapeStatus, 1000);
  }

  function stopStatusPolling() {
    if (!statusInterval) return;
    clearInterval(statusInterval);
    statusInterval = null;
  }

  async function loadTapeList() {
    const container = document.getElementById('zx-player-files');
    if (!container) return;
    container.innerHTML = '<div style="padding:8px 10px;color:var(--dim);font-size:11px">Loading...</div>';
    try {
      const res = await fetch(window.ZX_UTILS.API.TAPES);
      const files = await res.json();
      renderPlayerTapeList(files);
    } catch (e) {
      container.innerHTML = '<div style="padding:8px 10px;color:var(--red);font-size:11px">Failed to load tapes</div>';
    }
  }

  function renderPlayerTapeList(files) {
    const container = document.getElementById('zx-player-files');
    if (!container) return;
    if (!files || files.length === 0) {
      container.innerHTML = '<div style="padding:8px 10px;color:var(--dim);font-size:11px">No tapes found</div>';
      return;
    }
    container.innerHTML = files.map(f => `<div class="zx-player-file" data-file="${esc(f)}">${esc(f)}</div>`).join('');
    container.querySelectorAll('.zx-player-file').forEach(el => {
      if (lastTape && el.dataset.file === lastTape) el.classList.add('selected');

      el.addEventListener('click', async () => {
        const file = el.dataset.file;
        container.querySelectorAll('.zx-player-file').forEach(i => i.classList.remove('selected'));
        el.classList.add('selected');
        onTapeLoaded(file);
        
        try {
          await fetch(`${window.ZX_UTILS.API.LOAD}?file=${encodeURIComponent(file)}`);
        } catch (e) {}
      });
    });
  }

  function onTapeLoaded(file) {
    lastTape = file;
    try { localStorage.setItem('zx_last_tape', file); } catch(_) {}
    const lbl = document.getElementById('zx-player-label');
    if (lbl) lbl.textContent = file;
  }

  window.ZX_TAPE = { init, updateUI, onTapeLoaded };
})(window);
