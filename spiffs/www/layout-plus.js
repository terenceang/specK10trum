(function(window) {
  'use strict';

  const STRIPE = {
    blue:    "#2f5dff",
    red:     "#ff3b30",
    magenta: "#d939a8",
    green:   "#35c759",
    cyan:    "#2ec4d4",
    yellow:  "#ffcc1e",
    white:   "#e8e6df",
    black:   "#0b0b0c",
  };

  const ROWS = [
    [
      { main: "EDIT", combo: ["CAPS", "1"], special: "plus" },
      { main: "1", code: "1" }, { main: "2", code: "2" }, { main: "3", code: "3" }, { main: "4", code: "4" }, { main: "5", code: "5" },
      { main: "6", code: "6" }, { main: "7", code: "7" }, { main: "8", code: "8" }, { main: "9", code: "9" }, { main: "0", code: "0" },
      { main: "DELETE", combo: ["CAPS", "0"], special: "plus" },
    ],
    [
      { main: "CAPS LOCK", combo: ["CAPS", "2"], special: "plus" },
      { main: "Q", code: "Q" }, { main: "W", code: "W" }, { main: "E", code: "E" }, { main: "R", code: "R" }, { main: "T", code: "T" },
      { main: "Y", code: "Y" }, { main: "U", code: "U" }, { main: "I", code: "I" }, { main: "O", code: "O" }, { main: "P", code: "P" },
      { main: "GRAPHICS", combo: ["CAPS", "9"], special: "plus" },
    ],
    [
      { main: "TRUE VIDEO", combo: ["CAPS", "3"], special: "plus" },
      { main: "A", code: "A" }, { main: "S", code: "S" }, { main: "D", code: "D" }, { main: "F", code: "F" }, { main: "G", code: "G" },
      { main: "H", code: "H" }, { main: "J", code: "J" }, { main: "K", code: "K" }, { main: "L", code: "L" },
      { main: "EXTEND", combo: ["CAPS", "SYM"], special: "plus" },
      { main: "ENTER", code: "ENTER", wide: 1.35, special: "enter" },
    ],
    [
      { main: "INV VIDEO", combo: ["CAPS", "4"], special: "plus" },
      { main: "CAPS SHIFT", code: "CAPS", wide: 1.55, special: "caps" },
      { main: "Z", code: "Z" }, { main: "X", code: "X" }, { main: "C", code: "C" }, { main: "V", code: "V" },
      { main: "B", code: "B" }, { main: "N", code: "N" }, { main: "M", code: "M" },
      { main: "SYMBOL SHIFT", code: "SYM", wide: 1.55, special: "symbol" },
      { main: "BREAK", combo: ["CAPS", "SPACE"], special: "plus" },
    ],
    [
      { main: "SPACE", code: "SPACE", wide: 4.0, special: "space" },
      { main: "◄", combo: ["CAPS", "5"], special: "plus" },
      { main: "▼", combo: ["CAPS", "6"], special: "plus" },
      { main: "▲", combo: ["CAPS", "7"], special: "plus" },
      { main: "►", combo: ["CAPS", "8"], special: "plus" },
    ]
  ];

  if (!window.ZX_LAYOUT_VARIANTS) window.ZX_LAYOUT_VARIANTS = {};
  window.ZX_LAYOUT_VARIANTS["plus"] = {
    ROWS,
    OFFSETS: [0, 0.1, 0.2, 0, 0.5],
    STRIPE,
    HIDE_LEDS: true,
    HIDE_STRIPE: false
  };
})(window);
