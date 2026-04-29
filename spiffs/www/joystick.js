(function(window) {
  'use strict';

  // Kempston bits: 0=right, 1=left, 2=down, 3=up, 4=fire (active-HIGH)
  var JOYSTICK_ROW = 0xFF; // sentinel: not a valid keyboard row (0-7)
  var sendBuf = new Uint8Array(3);
  var active = new Map(); // pointerId -> { el, bit }

  function sendJoy(bit, pressed) {
    sendBuf[0] = JOYSTICK_ROW;
    sendBuf[1] = bit;
    sendBuf[2] = pressed ? 1 : 0;
    if (window.ZX_WS) window.ZX_WS.send(sendBuf);
  }

  function render(container) {
    container.innerHTML = [
      '<div class="zx-dpad">',
        '<div class="zx-joy-btn zx-joy-up"    data-joy-bit="3">&#9650;</div>',
        '<div class="zx-joy-btn zx-joy-left"  data-joy-bit="1">&#9664;</div>',
        '<div class="zx-joy-gap"></div>',
        '<div class="zx-joy-btn zx-joy-right" data-joy-bit="0">&#9654;</div>',
        '<div class="zx-joy-btn zx-joy-down"  data-joy-bit="2">&#9660;</div>',
      '</div>',
      '<div class="zx-joy-btn zx-joy-fire" data-joy-bit="4">FIRE</div>',
      '<div class="zx-joy-toolbar">',
        '<button class="zx-joy-back" id="zx-joy-back">&#8592; Keyboard</button>',
      '</div>'
    ].join('');
    initEvents(container);
  }

  function press(el, pointerId) {
    var bit = parseInt(el.dataset.joyBit, 10);
    if (isNaN(bit)) return;
    active.set(pointerId, { el: el, bit: bit });
    el.classList.add('pressed');
    sendJoy(bit, true);
  }

  function release(pointerId) {
    var entry = active.get(pointerId);
    if (!entry) return;
    active.delete(pointerId);
    entry.el.classList.remove('pressed');
    sendJoy(entry.bit, false);
  }

  function releaseAll() {
    active.forEach(function(entry) {
      entry.el.classList.remove('pressed');
      sendJoy(entry.bit, false);
    });
    active.clear();
  }

  function initEvents(container) {
    container.addEventListener('pointerdown', function(e) {
      var el = e.target.closest('[data-joy-bit]');
      if (!el) return;
      e.preventDefault();
      try { container.setPointerCapture(e.pointerId); } catch(_) {}
      press(el, e.pointerId);
    });
    var end = function(e) {
      release(e.pointerId);
      try { container.releasePointerCapture(e.pointerId); } catch(_) {}
    };
    container.addEventListener('pointerup', end);
    container.addEventListener('pointercancel', end);
    container.addEventListener('pointermove', function(e) {
      var entry = active.get(e.pointerId);
      if (!entry) return;
      var r = entry.el.getBoundingClientRect();
      if (e.clientX < r.left || e.clientX >= r.right ||
          e.clientY < r.top  || e.clientY >= r.bottom) {
        release(e.pointerId);
      }
    });
    container.addEventListener('contextmenu', function(e) { e.preventDefault(); });
  }

  function showGamepad() {
    releaseAll();
    var kb = document.getElementById('zx-kb');
    var gp = document.getElementById('zx-gamepad');
    if (kb) kb.style.display = 'none';
    if (gp) gp.classList.add('active');
    var backBtn = document.getElementById('zx-joy-back');
    if (backBtn) backBtn.addEventListener('click', showKeyboard);
  }

  function showKeyboard() {
    releaseAll();
    var kb = document.getElementById('zx-kb');
    var gp = document.getElementById('zx-gamepad');
    if (gp) gp.classList.remove('active');
    if (kb) kb.style.display = '';
    if (window.ZX_KB) window.ZX_KB.releaseAll();
  }

  document.addEventListener('visibilitychange', function() {
    if (document.hidden) releaseAll();
  });
  window.addEventListener('blur', releaseAll);

  window.ZX_JOY = { render: render, showGamepad: showGamepad, showKeyboard: showKeyboard, sendJoy: sendJoy };
})(window);
