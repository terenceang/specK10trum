(function(window) {
  'use strict';

  const esc = window.ZX_UTILS.esc;

  let currentTapeMode = (function() { try { return localStorage.getItem('zx_tape_mode') || 'normal'; } catch (_) { return 'normal'; } })();
  let lastTape = (function() { try { return localStorage.getItem('zx_last_tape') || null; } catch (_) { return null; } })();

  function stopTape() {
    if (window.ZX_WS) window.ZX_WS.send(JSON.stringify({ cmd: 'tape_stop' }));
  }

  function init() {
    const autoPlayToggle = document.getElementById('zx-auto-play-toggle');
    const btnLoadTape = document.getElementById('zx-btn-load-tape');
    const btnInstaload = document.getElementById('zx-btn-instaload');
    const playerRefresh = document.getElementById('zx-player-refresh');
    const btnPlayerClose = document.getElementById('zx-player-close');
    const tapePlayer = document.getElementById('zx-player');

    if (autoPlayToggle) {
      autoPlayToggle.checked = (currentTapeMode === 'normal');
      autoPlayToggle.addEventListener('change', (e) => {
        currentTapeMode = e.target.checked ? 'normal' : 'player';
        try { localStorage.setItem('zx_tape_mode', currentTapeMode); } catch(_) {}
        if (window.ZX_WS) window.ZX_WS.send(JSON.stringify({ cmd: 'tape_mode_' + currentTapeMode }));
        updateUI();
      });
    }

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
        if (window.ZX_WS) window.ZX_WS.send(JSON.stringify({ cmd: 'tape_instaload' }));
        document.getElementById('zx-player-label').textContent = 'INSTALOAD COMPLETE';
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
        } else if (cmd === 'tape_rewind') {
          leftReel.classList.add('spin-fast');
        } else if (cmd === 'tape_ffwd') {
          rightReel.classList.add('spin-fast');
        } else if (cmd === 'tape_eject') {
          document.getElementById('zx-player-label').textContent = 'NO TAPE LOADED';
        }
      });
    }

    updateUI();
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
        window.ZX_WS.send(JSON.stringify({ cmd: 'tape_mode_' + currentTapeMode }));
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
    if (cassetteDeck) {
      cassetteDeck.style.display = (currentTapeMode === 'normal') ? 'none' : 'block';
    }
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
          const res = await fetch(`${window.ZX_UTILS.API.LOAD}?file=${encodeURIComponent(file)}`);
          if (res.ok && window.ZX_WS) window.ZX_WS.send(JSON.stringify({ cmd: 'tape_play' }));
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
