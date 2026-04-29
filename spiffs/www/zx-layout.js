/* global window */
// Keyboard Layout Manager
// Loads variants from individual files and sets the default.

(function(window) {
  'use strict';

  // The variants are populated by layout-48k.js, layout-plus.js, etc.
  if (!window.ZX_LAYOUT_VARIANTS) window.ZX_LAYOUT_VARIANTS = {};

  // Set default layout if not already set (e.g., by menu.js from localStorage)
  if (!window.ZX_LAYOUT && window.ZX_LAYOUT_VARIANTS["48k"]) {
    window.ZX_LAYOUT = window.ZX_LAYOUT_VARIANTS["48k"];
  }
})(window);
