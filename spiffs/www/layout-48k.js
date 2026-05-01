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

  const ROW_1 = [
    { main: "1", code: "1", keywordAbove: "EDIT",       stripeColor: "blue",    stripeLabel: "BLUE",    symbolRed: "!", commandBelow: "DEF FN", symbolChar: "!" },
    { main: "2", code: "2", keywordAbove: "CAPS LOCK",  stripeColor: "red",     stripeLabel: "RED",     symbolRed: "@", commandBelow: "FN", symbolChar: "@" },
    { main: "3", code: "3", keywordAbove: "TRUE VIDEO", stripeColor: "magenta", stripeLabel: "MAGENTA", symbolRed: "#", commandBelow: "LINE", symbolChar: "#" },
    { main: "4", code: "4", keywordAbove: "INV. VIDEO", stripeColor: "green",   stripeLabel: "GREEN",   symbolRed: "$", commandBelow: "OPEN #", symbolChar: "$" },
    { main: "5", code: "5", keywordAbove: "◄",          stripeColor: "cyan",    stripeLabel: "CYAN",    symbolRed: "%", commandBelow: "CLOSE #", symbolChar: "%" },
    { main: "6", code: "6", keywordAbove: "▼",          stripeColor: "yellow",  stripeLabel: "YELLOW",  symbolRed: "&", commandBelow: "MOVE", symbolChar: "&" },
    { main: "7", code: "7", keywordAbove: "▲",          stripeColor: "white",   stripeLabel: "WHITE",   symbolRed: "'", commandBelow: "ERASE", symbolChar: "'" },
    { main: "8", code: "8", keywordAbove: "►",          stripeColor: null,      stripeLabel: "",        symbolRed: "(", commandBelow: "POINT", symbolChar: "(" },
    { main: "9", code: "9", keywordAbove: "GRAPHICS",   stripeColor: null,      stripeLabel: "",        symbolRed: ")", commandBelow: "CAT", symbolChar: ")" },
    { main: "0", code: "0", keywordAbove: "DELETE",     stripeColor: "black",   stripeLabel: "BLACK",   symbolRed: "_", commandBelow: "FORMAT", symbolChar: "_" },
  ];

  const ROW_2 = [
    { main: "Q", code: "Q", keywordAbove: "SIN",   keyword: "PLOT",   eModeRed: "<=",  keywordBelow: "ASN", symbolChar: "<=" },
    { main: "W", code: "W", keywordAbove: "COS",   keyword: "DRAW",   eModeRed: "<>",  keywordBelow: "ACS", symbolChar: "<>" },
    { main: "E", code: "E", keywordAbove: "TAN",   keyword: "REM",    eModeRed: ">=",  keywordBelow: "ATN", symbolChar: ">=" },
    { main: "R", code: "R", keywordAbove: "INT",   keyword: "RUN",    eModeRed: "<",   keywordBelow: "VERIFY", symbolChar: "<" },
    { main: "T", code: "T", keywordAbove: "RND",   keyword: "RAND",   eModeRed: ">",   keywordBelow: "MERGE", symbolChar: ">" },
    { main: "Y", code: "Y", keywordAbove: "STR $", keyword: "RETURN", eModeRed: "AND", keywordBelow: "[", symbolChar: "[" },
    { main: "U", code: "U", keywordAbove: "CHR $", keyword: "IF",     eModeRed: "OR",  keywordBelow: "]", symbolChar: "]" },
    { main: "I", code: "I", keywordAbove: "CODE",  keyword: "INPUT",  eModeRed: "AT",  keywordBelow: "IN", symbolChar: "IN" },
    { main: "O", code: "O", keywordAbove: "PEEK",  keyword: "POKE",   eModeRed: ";",   keywordBelow: "OUT", symbolChar: ";" },
    { main: "P", code: "P", keywordAbove: "TAB",   keyword: "PRINT",  eModeRed: '"',   keywordBelow: "©", symbolChar: '"' },
  ];

  const ROW_3 = [
    { main: "A", code: "A", keywordAbove: "READ",    keyword: "NEW",   eModeRed: "STOP", keywordBelow: "~", symbolChar: "~" },
    { main: "S", code: "S", keywordAbove: "RESTORE", keyword: "SAVE",  eModeRed: "NOT",  keywordBelow: "|", symbolChar: "|" },
    { main: "D", code: "D", keywordAbove: "DATA",    keyword: "DIM",   eModeRed: "STEP", keywordBelow: "\\", symbolChar: "\\" },
    { main: "F", code: "F", keywordAbove: "SGN",     keyword: "FOR",   eModeRed: "TO",   keywordBelow: "{", symbolChar: "{" },
    { main: "G", code: "G", keywordAbove: "ABS",     keyword: "GOTO",  eModeRed: "THEN", keywordBelow: "}", symbolChar: "}" },
    { main: "H", code: "H", keywordAbove: "SQR",     keyword: "GOSUB", eModeRed: "↑",    keywordBelow: "CIRCLE", symbolChar: "↑" },
    { main: "J", code: "J", keywordAbove: "VAL",     keyword: "LOAD",  eModeRed: "-",    keywordBelow: "VAL $", symbolChar: "-" },
    { main: "K", code: "K", keywordAbove: "LEN",     keyword: "LIST",  eModeRed: "+",    keywordBelow: "SCREEN $", symbolChar: "+" },
    { main: "L", code: "L", keywordAbove: "USR",     keyword: "LET",   eModeRed: "=",    keywordBelow: "ATTR", symbolChar: "=" },
    { main: "ENTER", code: "ENTER", wide: 1.35, special: "enter" },
  ];

  const ROW_4 = [
    { main: "CAPS SHIFT",  code: "CAPS", wide: 1.5, special: "caps" },
    { main: "Z", code: "Z", keywordAbove: "LN",       keyword: "COPY",   eModeRed: ":", keywordBelow: "BEEP", symbolChar: ":" },
    { main: "X", code: "X", keywordAbove: "EXP",      keyword: "CLEAR",  eModeRed: "£", keywordBelow: "INK", symbolChar: "£" },
    { main: "C", code: "C", keywordAbove: "LPRINT",  keyword: "CONT",   eModeRed: "?", keywordBelow: "PAPER", symbolChar: "?" },
    { main: "V", code: "V", keywordAbove: "LLIST",   keyword: "CLS",    eModeRed: "/", keywordBelow: "FLASH", symbolChar: "/" },
    { main: "B", code: "B", keywordAbove: "BIN",      keyword: "BORDER", eModeRed: "*", keywordBelow: "BRIGHT", symbolChar: "*" },
    { main: "N", code: "N", keywordAbove: "INKEY$", keyword: "NEXT",   eModeRed: ",", keywordBelow: "OVER", symbolChar: "," },
    { main: "M", code: "M", keywordAbove: "PI",       keyword: "PAUSE",  eModeRed: ".", keywordBelow: "INVERSE", symbolChar: "." },
    { main: "SYMBOL SHIFT", code: "SYM", wide: 1.25, special: "symbol" },
    { main: "BREAK SPACE",  code: "SPACE", wide: 1.50, special: "space" },
  ];

  if (!window.ZX_LAYOUT_VARIANTS) window.ZX_LAYOUT_VARIANTS = {};
  window.ZX_LAYOUT_VARIANTS["48k"] = {
    ROW_1, ROW_2, ROW_3, ROW_4, STRIPE
  };
})(window);
