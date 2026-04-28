(function(window) {
  'use strict';

  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, c => (
    { '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;' }[c]));

  const API = {
    LOAD:      '/api/load',
    RESET:     '/api/reset',
    SNAPSHOTS: '/api/snapshots',
    TAPES:     '/api/tapes',
    TAPE_CMD:  '/api/tape'
  };

  window.ZX_UTILS = { esc, API };
})(window);
