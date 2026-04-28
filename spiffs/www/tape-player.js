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

  // Find WebSocket or fallback to HTTP
  let ws = window.ws;
  if (!ws || ws.readyState !== 1) {
    // Try to find a global ws or create a new one
    try {
      ws = new WebSocket('ws://' + location.host + '/ws');
    } catch (e) { ws = null; }
  }

  function sendTapeCmd(cmd) {
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify({ cmd: 'tape_' + cmd }));
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
})();
