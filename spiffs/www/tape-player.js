// Minimal standalone tape player UI and logic for SpecK10trum
(function () {
  'use strict';
  // Create tape player container
  const player = document.createElement('div');
  player.className = 'zx-player open';
  player.innerHTML = `
    <div class="zx-player-deck">
      <div class="zx-player-cassette">
        <div class="zx-player-reel" id="reel-left"></div>
        <div class="zx-player-reel" id="reel-right"></div>
      </div>
    </div>
    <div class="zx-player-controls">
      <button class="zx-player-btn" data-cmd="play">Play</button>
      <button class="zx-player-btn" data-cmd="stop">Stop</button>
      <button class="zx-player-btn" data-cmd="rewind">Rew</button>
      <button class="zx-player-btn" data-cmd="ffwd">FF</button>
      <button class="zx-player-btn" data-cmd="pause">Pause</button>
      <button class="zx-player-btn" data-cmd="eject">Eject</button>
    </div>
    <div class="zx-player-label">Tape Player</div>
  `;
  // Insert into DOM
  document.body.appendChild(player);

  // Use global ws object from ws.js
  function getWs() {
    return window.ws && window.ws.readyState === 1 ? window.ws : null;
  }

  function sendTapeCmd(cmd) {
    const ws = getWs();
    if (ws) {
      try {
        ws.send(JSON.stringify({ cmd: 'tape_' + cmd }));
      } catch (e) {
        // fallback: HTTP GET
        fetch('/api/tape?cmd=' + cmd).catch(()=>{});
      }
    } else {
      // fallback: HTTP GET
      fetch('/api/tape?cmd=' + cmd).catch(()=>{});
    }
  }

  player.querySelectorAll('.zx-player-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const cmd = btn.getAttribute('data-cmd');
      sendTapeCmd(cmd);
    });
  });

  // Listen for ws reconnects and update UI if needed
  window.addEventListener('ws-reconnect', () => {
    // Optionally update UI or re-enable controls
  });
})();
