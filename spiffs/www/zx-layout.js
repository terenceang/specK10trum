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

const LAYOUT = { ROW_1, ROW_2, ROW_3, ROW_4, STRIPE };

window.ZX_LAYOUT = LAYOUT;