/* global window */
// Full authentic ZX Spectrum 40-key layout with all legends.

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

// Each key: main, keywordAbove (above key, colored), symbolRed (red symbol top-right),
// keyword (green keyword on key body), commandBelow (green below key),
// eModeRed / eModeGreen (extended-mode labels below key), stripeColor, stripeLabel, wide, special
const ROW_1 = [
  { main: "1", code: "1", keywordAbove: "EDIT",       stripeColor: "blue",    stripeLabel: "BLUE",    symbolRed: "!", commandBelow: "DEF FN" },
  { main: "2", code: "2", keywordAbove: "CAPS LOCK",  stripeColor: "red",     stripeLabel: "RED",     symbolRed: "@", commandBelow: "FN" },
  { main: "3", code: "3", keywordAbove: "TRUE VIDEO", stripeColor: "magenta", stripeLabel: "MAGENTA", symbolRed: "#", commandBelow: "LINE" },
  { main: "4", code: "4", keywordAbove: "INV. VIDEO", stripeColor: "green",   stripeLabel: "GREEN",   symbolRed: "$", commandBelow: "OPEN #" },
  { main: "5", code: "5", keywordAbove: "◄",          stripeColor: "cyan",    stripeLabel: "CYAN",    symbolRed: "%", commandBelow: "CLOSE #" },
  { main: "6", code: "6", keywordAbove: "▼",          stripeColor: "yellow",  stripeLabel: "YELLOW",  symbolRed: "&", commandBelow: "MOVE" },
  { main: "7", code: "7", keywordAbove: "▲",          stripeColor: "white",   stripeLabel: "WHITE",   symbolRed: "'", commandBelow: "ERASE" },
  { main: "8", code: "8", keywordAbove: "►",          stripeColor: null,      stripeLabel: "",        symbolRed: "(", commandBelow: "POINT" },
  { main: "9", code: "9", keywordAbove: "GRAPHICS",   stripeColor: null,      stripeLabel: "",        symbolRed: ")", commandBelow: "CAT" },
  { main: "0", code: "0", keywordAbove: "DELETE",     stripeColor: "black",   stripeLabel: "BLACK",   symbolRed: "_", commandBelow: "FORMAT" },
];

const ROW_2 = [
  { main: "Q", code: "Q", keywordAbove: "SIN",   keyword: "PLOT",   eModeRed: "<=",  eModeGreen: "ASN" },
  { main: "W", code: "W", keywordAbove: "COS",   keyword: "DRAW",   eModeRed: "<>",  eModeGreen: "ACS" },
  { main: "E", code: "E", keywordAbove: "TAN",   keyword: "REM",    eModeRed: ">=",  eModeGreen: "ATN" },
  { main: "R", code: "R", keywordAbove: "INT",   keyword: "RUN",    eModeRed: "<",   eModeGreen: "VERIFY" },
  { main: "T", code: "T", keywordAbove: "RND",   keyword: "RAND",   eModeRed: ">",   eModeGreen: "MERGE" },
  { main: "Y", code: "Y", keywordAbove: "STR $", keyword: "RETURN", eModeRed: "AND", eModeGreen: "[" },
  { main: "U", code: "U", keywordAbove: "CHR $", keyword: "IF",     eModeRed: "OR",  eModeGreen: "]" },
  { main: "I", code: "I", keywordAbove: "CODE",  keyword: "INPUT",  eModeRed: "AT",  eModeGreen: "IN" },
  { main: "O", code: "O", keywordAbove: "PEEK",  keyword: "POKE",   eModeRed: ";",   eModeGreen: "OUT" },
  { main: "P", code: "P", keywordAbove: "TAB",   keyword: "PRINT",  eModeRed: '"',   eModeGreen: "©" },
];

const ROW_3 = [
  { main: "A", code: "A", keywordAbove: "READ",    keyword: "NEW",   eModeRed: "STOP", eModeGreen: "~" },
  { main: "S", code: "S", keywordAbove: "RESTORE", keyword: "SAVE",  eModeRed: "NOT",  eModeGreen: "|" },
  { main: "D", code: "D", keywordAbove: "DATA",    keyword: "DIM",   eModeRed: "STEP", eModeGreen: "\\" },
  { main: "F", code: "F", keywordAbove: "SGN",     keyword: "FOR",   eModeRed: "TO",   eModeGreen: "{" },
  { main: "G", code: "G", keywordAbove: "ABS",     keyword: "GOTO",  eModeRed: "THEN", eModeGreen: "}" },
  { main: "H", code: "H", keywordAbove: "SQR",     keyword: "GOSUB", eModeRed: "↑",    eModeGreen: "CIRCLE" },
  { main: "J", code: "J", keywordAbove: "VAL",     keyword: "LOAD",  eModeRed: "-",    eModeGreen: "VAL $" },
  { main: "K", code: "K", keywordAbove: "LEN",     keyword: "LIST",  eModeRed: "+",    eModeGreen: "SCREEN $" },
  { main: "L", code: "L", keywordAbove: "USR",     keyword: "LET",   eModeRed: "=",    eModeGreen: "ATTR" },
  { main: "ENTER", code: "ENTER", wide: 1.35, special: "enter" },
];

const ROW_4 = [
  { main: "CAPS SHIFT",  code: "CAPS", wide: 1.55, special: "caps" },
  { main: "Z", code: "Z", keywordAbove: "LN",       keyword: "COPY",   eModeRed: ":", eModeGreen: "BEEP" },
  { main: "X", code: "X", keywordAbove: "EXP",      keyword: "CLEAR",  eModeRed: "£", eModeGreen: "INK" },
  { main: "C", code: "C", keywordAbove: "L PRINT",  keyword: "CONT",   eModeRed: "?", eModeGreen: "PAPER" },
  { main: "V", code: "V", keywordAbove: "L LIST",   keyword: "CLS",    eModeRed: "/", eModeGreen: "FLASH" },
  { main: "B", code: "B", keywordAbove: "BIN",      keyword: "BORDER", eModeRed: "*", eModeGreen: "BRIGHT" },
  { main: "N", code: "N", keywordAbove: "IN KEY $", keyword: "NEXT",   eModeRed: ",", eModeGreen: "OVER" },
  { main: "M", code: "M", keywordAbove: "PI",       keyword: "PAUSE",  eModeRed: ".", eModeGreen: "INVERSE" },
  { main: "SYMBOL SHIFT", code: "SYM", wide: 1.55, special: "symbol" },
  { main: "BREAK SPACE",  code: "SPACE", wide: 1.85, special: "space" },
];

const K48_LAYOUT = { ROW_1, ROW_2, ROW_3, ROW_4, STRIPE };

// ZX Spectrum Plus Layout (multi-row, dedicated keys)
const PLUS_ROWS = [
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

const PLUS_LAYOUT = {
  ROWS: PLUS_ROWS,
  OFFSETS: [0, 0.1, 0.2, 0, 0.5],
  STRIPE,
  HIDE_LEDS: true,
  HIDE_STRIPE: false
};

window.ZX_LAYOUT_VARIANTS = {
  "48k": K48_LAYOUT,
  "plus": PLUS_LAYOUT
};

window.ZX_LAYOUT = K48_LAYOUT;
