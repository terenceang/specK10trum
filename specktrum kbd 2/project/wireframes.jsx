/* global React */
const { useState, useEffect, useRef } = React;

// ============================================================================
// SHARED: sketchy style helpers
// ============================================================================

const BG = "#141416";
const INK = "#e8e6df";
const DIM = "#7a7870";
const FAINT = "#2e2c28";

// Spectrum stripe colors (the 8 BASIC colors, ordered as on number row)
const STRIPE = {
  blue:    "#1f4bd8",
  red:     "#e53935",
  magenta: "#c33b9c",
  green:   "#2fa84a",
  cyan:    "#2bb5c9",
  yellow:  "#e9c13a",
  white:   "#e8e6df",
  black:   "#111",
};

const HAND_FONT = `"Caveat", "Shadows Into Light", "Segoe Print", cursive`;
const MONO_FONT = `"JetBrains Mono", "IBM Plex Mono", ui-monospace, monospace`;

// A hand-drawn wobbly rectangle via SVG path (for that sketchy border look)
function SketchyRect({ w, h, stroke = INK, strokeWidth = 1.5, fill = "none", wobble = 1.2, dashed = false, children, style = {} }) {
  // Generate a slightly wobbly rounded rectangle path
  const r = 6;
  const jitter = (amt) => (Math.random() - 0.5) * amt;
  // use deterministic-ish wobble based on w,h so it doesn't reshuffle each render
  const seed = (w * 13 + h * 7) % 1000;
  const rand = (i) => {
    const x = Math.sin(seed + i * 12.9898) * 43758.5453;
    return (x - Math.floor(x) - 0.5) * wobble;
  };
  const path = [
    `M ${r + rand(1)} ${rand(2)}`,
    `L ${w - r + rand(3)} ${rand(4)}`,
    `Q ${w + rand(5)} ${rand(6)} ${w + rand(7)} ${r + rand(8)}`,
    `L ${w + rand(9)} ${h - r + rand(10)}`,
    `Q ${w + rand(11)} ${h + rand(12)} ${w - r + rand(13)} ${h + rand(14)}`,
    `L ${r + rand(15)} ${h + rand(16)}`,
    `Q ${rand(17)} ${h + rand(18)} ${rand(19)} ${h - r + rand(20)}`,
    `L ${rand(21)} ${r + rand(22)}`,
    `Q ${rand(23)} ${rand(24)} ${r + rand(25)} ${rand(26)}`,
    `Z`,
  ].join(" ");
  return (
    <svg width={w + 4} height={h + 4} style={{ position: "absolute", inset: -2, pointerEvents: "none", ...style }}>
      <path
        d={path}
        transform="translate(2,2)"
        fill={fill}
        stroke={stroke}
        strokeWidth={strokeWidth}
        strokeLinejoin="round"
        strokeLinecap="round"
        strokeDasharray={dashed ? "4 3" : undefined}
      />
    </svg>
  );
}

// A sketchy underline
function Squiggle({ w = 40, color = INK, style = {} }) {
  const path = `M 0 3 Q ${w * 0.2} 0 ${w * 0.4} 3 T ${w * 0.8} 3 T ${w} 3`;
  return (
    <svg width={w} height={6} style={{ display: "block", ...style }}>
      <path d={path} stroke={color} strokeWidth={1.2} fill="none" strokeLinecap="round" />
    </svg>
  );
}

// ============================================================================
// KEY DATA — the full authentic ZX Spectrum layout
// ============================================================================
//
// Each key: { main, keywordAbove, symbolRed, commandBelow, eModeRed, eModeGreen, extended, wide, stripeColor }
//
const ROW_1 = [
  { main: "1", keywordAbove: "EDIT",     stripeColor: "blue",    stripeLabel: "BLUE",    symbolRed: "!",   commandBelow: "DEF FN" },
  { main: "2", keywordAbove: "CAPS LOCK", stripeColor: "red",     stripeLabel: "RED",     symbolRed: "@",   commandBelow: "FN" },
  { main: "3", keywordAbove: "TRUE VIDEO", stripeColor: "magenta", stripeLabel: "MAGENTA", symbolRed: "#", commandBelow: "LINE" },
  { main: "4", keywordAbove: "INV. VIDEO", stripeColor: "green",   stripeLabel: "GREEN",   symbolRed: "$", commandBelow: "OPEN #" },
  { main: "5", keywordAbove: "◄",        stripeColor: "cyan",    stripeLabel: "CYAN",    symbolRed: "%",   commandBelow: "CLOSE #" },
  { main: "6", keywordAbove: "▼",        stripeColor: "yellow",  stripeLabel: "YELLOW",  symbolRed: "&",   commandBelow: "MOVE" },
  { main: "7", keywordAbove: "▲",        stripeColor: "white",   stripeLabel: "WHITE",   symbolRed: "'",   commandBelow: "ERASE" },
  { main: "8", keywordAbove: "►",        stripeColor: null,      stripeLabel: "",        symbolRed: "(",   commandBelow: "POINT" },
  { main: "9", keywordAbove: "GRAPHICS", stripeColor: null,      stripeLabel: "",        symbolRed: ")",   commandBelow: "CAT" },
  { main: "0", keywordAbove: "DELETE",   stripeColor: "black",   stripeLabel: "BLACK",   symbolRed: "_",   commandBelow: "FORMAT" },
];

const ROW_2 = [
  { main: "Q", keywordAbove: "SIN",    keyword: "PLOT",   eModeRed: "<=",  eModeGreen: "ASN" },
  { main: "W", keywordAbove: "COS",    keyword: "DRAW",   eModeRed: "<>",  eModeGreen: "ACS" },
  { main: "E", keywordAbove: "TAN",    keyword: "REM",    eModeRed: ">=",  eModeGreen: "ATN" },
  { main: "R", keywordAbove: "INT",    keyword: "RUN",    eModeRed: "<",   eModeGreen: "VERIFY" },
  { main: "T", keywordAbove: "RND",    keyword: "RAND",   eModeRed: ">",   eModeGreen: "MERGE" },
  { main: "Y", keywordAbove: "STR $",  keyword: "RETURN", eModeRed: "AND", eModeGreen: "[" },
  { main: "U", keywordAbove: "CHR $",  keyword: "IF",     eModeRed: "OR",  eModeGreen: "]" },
  { main: "I", keywordAbove: "CODE",   keyword: "INPUT",  eModeRed: "AT",  eModeGreen: "IN" },
  { main: "O", keywordAbove: "PEEK",   keyword: "POKE",   eModeRed: ";",   eModeGreen: "OUT" },
  { main: "P", keywordAbove: "TAB",    keyword: "PRINT",  eModeRed: '"',   eModeGreen: "©" },
];

const ROW_3 = [
  { main: "A", keywordAbove: "READ",    keyword: "NEW",   eModeRed: "STOP", eModeGreen: "~" },
  { main: "S", keywordAbove: "RESTORE", keyword: "SAVE",  eModeRed: "NOT",  eModeGreen: "|" },
  { main: "D", keywordAbove: "DATA",    keyword: "DIM",   eModeRed: "STEP", eModeGreen: "\\" },
  { main: "F", keywordAbove: "SGN",     keyword: "FOR",   eModeRed: "TO",   eModeGreen: "{" },
  { main: "G", keywordAbove: "ABS",     keyword: "GOTO",  eModeRed: "THEN", eModeGreen: "}" },
  { main: "H", keywordAbove: "SQR",     keyword: "GOSUB", eModeRed: "↑",    eModeGreen: "CIRCLE" },
  { main: "J", keywordAbove: "VAL",     keyword: "LOAD",  eModeRed: "-",    eModeGreen: "VAL $" },
  { main: "K", keywordAbove: "LEN",     keyword: "LIST",  eModeRed: "+",    eModeGreen: "SCREEN $" },
  { main: "L", keywordAbove: "USR",     keyword: "LET",   eModeRed: "=",    eModeGreen: "ATTR" },
  { main: "ENTER", wide: 1.3 },
];

const ROW_4 = [
  { main: "CAPS SHIFT", wide: 1.5, special: "caps" },
  { main: "Z", keywordAbove: "LN",    keyword: "COPY",   eModeRed: ":",   eModeGreen: "BEEP" },
  { main: "X", keywordAbove: "EXP",   keyword: "CLEAR",  eModeRed: "£",   eModeGreen: "INK" },
  { main: "C", keywordAbove: "L PRINT", keyword: "CONT", eModeRed: "?",   eModeGreen: "PAPER" },
  { main: "V", keywordAbove: "L LIST", keyword: "CLS",   eModeRed: "/",   eModeGreen: "FLASH" },
  { main: "B", keywordAbove: "BIN",   keyword: "BORDER", eModeRed: "*",   eModeGreen: "BRIGHT" },
  { main: "N", keywordAbove: "IN KEY $", keyword: "NEXT", eModeRed: ",",  eModeGreen: "OVER" },
  { main: "M", keywordAbove: "PI",    keyword: "PAUSE",  eModeRed: ".",   eModeGreen: "INVERSE" },
  { main: "SYMBOL SHIFT", wide: 1.5, special: "symbol" },
  { main: "BREAK SPACE", wide: 1.8, special: "space" },
];

// ============================================================================
// APPROACH 1 — FAITHFUL WIREFRAME CLONE
// Keeps original layout, but keys are thin-stroke rectangles on dark bg.
// Legends positioned exactly as on physical keyboard.
// ============================================================================

function Approach1Faithful() {
  const keyW = 56, keyH = 56, gap = 8;
  const drawKey = (k, row, i) => {
    const w = k.wide ? keyW * k.wide : keyW;
    return (
      <div key={`${row}-${i}`} style={{
        position: "relative",
        width: w, height: keyH,
        display: "flex", alignItems: "center", justifyContent: "center",
        color: INK,
      }}>
        <SketchyRect w={w} h={keyH} stroke={INK} strokeWidth={1.2} />
        {/* keyword above */}
        {k.keywordAbove && (
          <div style={{
            position: "absolute", top: -16, left: 0, right: 0,
            textAlign: "center", fontFamily: MONO_FONT, fontSize: 8,
            color: k.stripeColor ? STRIPE[k.stripeColor] : DIM,
            letterSpacing: 0.5,
          }}>{k.keywordAbove}</div>
        )}
        {/* red symbol top-right */}
        {k.symbolRed && (
          <div style={{
            position: "absolute", top: 4, right: 5,
            fontFamily: MONO_FONT, fontSize: 9, color: STRIPE.red,
          }}>{k.symbolRed}</div>
        )}
        {/* main letter */}
        <div style={{
          fontFamily: MONO_FONT, fontSize: k.main?.length > 2 ? 10 : 18,
          letterSpacing: k.main?.length > 2 ? 0.5 : 0,
          fontWeight: 600,
        }}>{k.main}</div>
        {/* keyword under letter (green tag) */}
        {k.keyword && (
          <div style={{
            position: "absolute", bottom: 4, left: 0, right: 0,
            textAlign: "center", fontFamily: MONO_FONT, fontSize: 7,
            color: STRIPE.green,
          }}>{k.keyword}</div>
        )}
        {/* command below */}
        {k.commandBelow && (
          <div style={{
            position: "absolute", bottom: -14, left: 0, right: 0,
            textAlign: "center", fontFamily: MONO_FONT, fontSize: 7,
            color: STRIPE.green,
          }}>{k.commandBelow}</div>
        )}
        {/* E-mode red */}
        {k.eModeRed && (
          <div style={{
            position: "absolute", bottom: -14, left: 3,
            fontFamily: MONO_FONT, fontSize: 7, color: STRIPE.red,
          }}>{k.eModeRed}</div>
        )}
        {k.eModeGreen && (
          <div style={{
            position: "absolute", bottom: -14, right: 3,
            fontFamily: MONO_FONT, fontSize: 7, color: STRIPE.green,
          }}>{k.eModeGreen}</div>
        )}
      </div>
    );
  };

  return (
    <div style={{ background: BG, padding: 32, width: "100%", height: "100%", color: INK, boxSizing: "border-box", position: "relative" }}>
      {/* annotations */}
      <div style={{ position: "absolute", top: 14, left: 20, fontFamily: HAND_FONT, fontSize: 20, color: INK }}>
        approach 1 — faithful clone
      </div>
      <div style={{ position: "absolute", top: 40, left: 20, fontFamily: HAND_FONT, fontSize: 14, color: DIM, maxWidth: 300 }}>
        original layout, wireframe keys, all legends preserved in their authentic positions
      </div>

      {/* status bar */}
      <div style={{ position: "absolute", top: 14, right: 20, display: "flex", gap: 16, alignItems: "center", fontFamily: MONO_FONT, fontSize: 10, color: DIM }}>
        <span>● ws://192.168.1.42:81</span>
        <span>12ms</span>
        <span>[  ]</span>
      </div>

      {/* mode strip */}
      <div style={{
        position: "absolute", top: 78, left: "50%", transform: "translateX(-50%)",
        display: "flex", gap: 4, alignItems: "center",
        padding: "6px 12px", fontFamily: MONO_FONT, fontSize: 10, color: DIM,
      }}>
        <span>MODE:</span>
        {["K", "L", "C", "E", "G"].map((m, i) => (
          <span key={m} style={{
            padding: "2px 8px",
            border: `1px solid ${i === 0 ? INK : FAINT}`,
            color: i === 0 ? INK : DIM,
            background: i === 0 ? "rgba(255,255,255,0.05)" : "transparent",
          }}>{m}</span>
        ))}
        <span style={{ marginLeft: 12, color: DIM }}>keyword entry</span>
      </div>

      {/* rainbow stripe on right */}
      <div style={{
        position: "absolute", right: 32, top: 140, bottom: 40,
        width: 8, display: "flex", flexDirection: "column",
      }}>
        {["red", "yellow", "green", "cyan"].map(c => (
          <div key={c} style={{ flex: 1, background: STRIPE[c], opacity: 0.75 }} />
        ))}
      </div>

      {/* keyboard */}
      <div style={{ marginTop: 130, display: "flex", flexDirection: "column", gap: 22, alignItems: "center" }}>
        <div style={{ display: "flex", gap }}>{ROW_1.map((k, i) => drawKey(k, 1, i))}</div>
        <div style={{ display: "flex", gap, marginTop: 6 }}>{ROW_2.map((k, i) => drawKey(k, 2, i))}</div>
        <div style={{ display: "flex", gap, marginTop: 6 }}>{ROW_3.map((k, i) => drawKey(k, 3, i))}</div>
        <div style={{ display: "flex", gap, marginTop: 6 }}>{ROW_4.map((k, i) => drawKey(k, 4, i))}</div>
      </div>

      {/* handwritten annotation */}
      <div style={{
        position: "absolute", bottom: 20, left: 40,
        fontFamily: HAND_FONT, fontSize: 16, color: STRIPE.yellow,
      }}>
        ↑ dense — authentic but busy on small screens
      </div>
    </div>
  );
}

// ============================================================================
// APPROACH 2 — TERMINAL GRID
// Strict monospace grid, all keys same size, CRT aesthetic.
// Labels stacked vertically inside each key.
// ============================================================================

function Approach2Terminal() {
  const cell = 66;
  const drawKey = (k, row, i, span = 1) => {
    const w = cell * span - 4;
    return (
      <div key={`${row}-${i}`} style={{
        position: "relative",
        width: w, height: cell - 4,
        display: "flex", flexDirection: "column", justifyContent: "center", alignItems: "center",
        color: INK, gap: 2,
      }}>
        <SketchyRect w={w} h={cell - 4} stroke={INK} strokeWidth={1} fill="rgba(255,255,255,0.02)" />
        {k.keywordAbove && (
          <div style={{
            fontFamily: MONO_FONT, fontSize: 8, letterSpacing: 0.5,
            color: k.stripeColor ? STRIPE[k.stripeColor] : DIM,
          }}>{k.keywordAbove}</div>
        )}
        <div style={{
          fontFamily: MONO_FONT, fontSize: k.main?.length > 2 ? 10 : 20,
          fontWeight: 700, color: INK,
        }}>{k.main}</div>
        {k.keyword && (
          <div style={{ fontFamily: MONO_FONT, fontSize: 8, color: STRIPE.green }}>{k.keyword}</div>
        )}
        {k.commandBelow && !k.keyword && (
          <div style={{ fontFamily: MONO_FONT, fontSize: 8, color: STRIPE.green }}>{k.commandBelow}</div>
        )}
        {(k.eModeRed || k.eModeGreen || k.symbolRed) && (
          <div style={{
            display: "flex", gap: 4,
            fontFamily: MONO_FONT, fontSize: 7,
          }}>
            {k.symbolRed && <span style={{ color: STRIPE.red }}>{k.symbolRed}</span>}
            {k.eModeRed && <span style={{ color: STRIPE.red }}>{k.eModeRed}</span>}
            {k.eModeGreen && <span style={{ color: STRIPE.green }}>{k.eModeGreen}</span>}
          </div>
        )}
      </div>
    );
  };

  return (
    <div style={{ background: "#0c0c0e", padding: 32, width: "100%", height: "100%", color: INK, boxSizing: "border-box", position: "relative" }}>
      {/* subtle CRT scanlines */}
      <div style={{
        position: "absolute", inset: 0,
        background: "repeating-linear-gradient(0deg, transparent 0, transparent 2px, rgba(255,255,255,0.015) 2px, rgba(255,255,255,0.015) 3px)",
        pointerEvents: "none",
      }} />

      <div style={{ position: "absolute", top: 14, left: 20, fontFamily: HAND_FONT, fontSize: 20 }}>
        approach 2 — terminal grid
      </div>
      <div style={{ position: "absolute", top: 40, left: 20, fontFamily: HAND_FONT, fontSize: 14, color: DIM, maxWidth: 300 }}>
        strict uniform grid, every key the same size, CRT scanlines
      </div>

      {/* status bar monospace style */}
      <div style={{
        position: "absolute", top: 14, right: 20,
        fontFamily: MONO_FONT, fontSize: 10, color: DIM,
        display: "flex", gap: 12,
      }}>
        <span style={{ color: STRIPE.green }}>[OK]</span>
        <span>192.168.1.42:81</span>
        <span>rtt=12ms</span>
        <span>tx=1.4kb</span>
        <span>[FS]</span>
      </div>

      {/* mode strip */}
      <div style={{
        position: "absolute", top: 70, left: 32, right: 32,
        display: "flex", gap: 0, fontFamily: MONO_FONT, fontSize: 10,
        border: `1px solid ${FAINT}`,
      }}>
        {[
          { m: "K", label: "keyword", on: true },
          { m: "L", label: "letter" },
          { m: "C", label: "caps" },
          { m: "E", label: "extended" },
          { m: "G", label: "graphics" },
        ].map((x, i) => (
          <div key={x.m} style={{
            flex: 1, padding: "6px 10px",
            background: x.on ? INK : "transparent",
            color: x.on ? BG : DIM,
            borderRight: i < 4 ? `1px solid ${FAINT}` : "none",
            display: "flex", justifyContent: "space-between",
          }}>
            <span style={{ fontWeight: 700 }}>{x.m}</span>
            <span>{x.label}</span>
          </div>
        ))}
      </div>

      {/* rainbow stripe across bottom */}
      <div style={{
        position: "absolute", bottom: 24, left: 32, right: 32, height: 4,
        display: "flex",
      }}>
        {["red", "yellow", "green", "cyan"].map(c => (
          <div key={c} style={{ flex: 1, background: STRIPE[c] }} />
        ))}
      </div>

      {/* keyboard — strict grid */}
      <div style={{ marginTop: 130, display: "flex", flexDirection: "column", gap: 6, alignItems: "center" }}>
        <div style={{ display: "flex", gap: 4 }}>{ROW_1.map((k, i) => drawKey(k, 1, i))}</div>
        <div style={{ display: "flex", gap: 4, paddingLeft: cell * 0.25 }}>{ROW_2.map((k, i) => drawKey(k, 2, i))}</div>
        <div style={{ display: "flex", gap: 4, paddingLeft: cell * 0.5 }}>
          {ROW_3.slice(0, 9).map((k, i) => drawKey(k, 3, i))}
          {drawKey(ROW_3[9], 3, 9, 1.5)}
        </div>
        <div style={{ display: "flex", gap: 4 }}>
          {drawKey(ROW_4[0], 4, 0, 1.5)}
          {ROW_4.slice(1, 8).map((k, i) => drawKey(k, 4, i + 1))}
          {drawKey(ROW_4[8], 4, 8, 1.5)}
          {drawKey(ROW_4[9], 4, 9, 1.5)}
        </div>
      </div>

      <div style={{
        position: "absolute", bottom: 40, right: 40,
        fontFamily: HAND_FONT, fontSize: 16, color: STRIPE.cyan,
      }}>
        uniform = predictable tap targets ✓
      </div>
    </div>
  );
}

// ============================================================================
// APPROACH 3 — COMPACT WITH OUTPUT STRIP
// Keyboard + live "sent keys" output strip on top + prominent WS bar.
// Focuses on feedback — you see what the ESP32 got.
// ============================================================================

function Approach3Output() {
  const keyW = 54, keyH = 50, gap = 6;
  const drawKey = (k, row, i) => {
    const w = k.wide ? keyW * k.wide : keyW;
    return (
      <div key={`${row}-${i}`} style={{
        position: "relative",
        width: w, height: keyH,
        display: "flex", alignItems: "center", justifyContent: "center",
        color: INK,
      }}>
        <SketchyRect w={w} h={keyH} stroke={INK} strokeWidth={1.2} />
        {k.keywordAbove && (
          <div style={{
            position: "absolute", top: 3, left: 4,
            fontFamily: MONO_FONT, fontSize: 7,
            color: k.stripeColor ? STRIPE[k.stripeColor] : DIM,
          }}>{k.keywordAbove}</div>
        )}
        {k.symbolRed && (
          <div style={{
            position: "absolute", top: 3, right: 4,
            fontFamily: MONO_FONT, fontSize: 8, color: STRIPE.red,
          }}>{k.symbolRed}</div>
        )}
        <div style={{
          fontFamily: MONO_FONT, fontSize: k.main?.length > 2 ? 9 : 18,
          fontWeight: 600,
        }}>{k.main}</div>
        {k.keyword && (
          <div style={{
            position: "absolute", bottom: 3, left: 0, right: 0,
            textAlign: "center", fontFamily: MONO_FONT, fontSize: 7,
            color: STRIPE.green,
          }}>{k.keyword}</div>
        )}
      </div>
    );
  };

  return (
    <div style={{ background: BG, padding: 24, width: "100%", height: "100%", color: INK, boxSizing: "border-box", position: "relative" }}>
      <div style={{ position: "absolute", top: 14, left: 20, fontFamily: HAND_FONT, fontSize: 20 }}>
        approach 3 — compact + output strip
      </div>
      <div style={{ position: "absolute", top: 40, left: 20, fontFamily: HAND_FONT, fontSize: 14, color: DIM, maxWidth: 380 }}>
        live output of sent keys above the keyboard — instant visual confirmation
      </div>

      {/* full status bar at top */}
      <div style={{
        position: "absolute", top: 80, left: 24, right: 24, height: 32,
        border: `1.2px solid ${INK}`,
        display: "flex", alignItems: "center", padding: "0 14px",
        fontFamily: MONO_FONT, fontSize: 11, gap: 20, color: INK,
      }}>
        <span style={{ display: "flex", alignItems: "center", gap: 6 }}>
          <span style={{ width: 8, height: 8, borderRadius: "50%", background: STRIPE.green }} />
          CONNECTED
        </span>
        <span style={{ color: DIM }}>ws://192.168.1.42:81</span>
        <span style={{ color: DIM }}>│</span>
        <span>RTT <span style={{ color: STRIPE.yellow }}>12ms</span></span>
        <span style={{ color: DIM }}>│</span>
        <span>TX <span style={{ color: INK }}>1,482</span> keys</span>
        <span style={{ color: DIM }}>│</span>
        <span>ESP32S3 <span style={{ color: STRIPE.cyan }}>online</span></span>
        <span style={{ marginLeft: "auto", display: "flex", gap: 12 }}>
          <span style={{ padding: "2px 8px", border: `1px solid ${DIM}` }}>DISCONNECT</span>
          <span style={{ padding: "2px 8px", border: `1px solid ${DIM}` }}>⛶ FULL</span>
        </span>
      </div>

      {/* output strip — shows recent sent keys */}
      <div style={{
        position: "absolute", top: 124, left: 24, right: 24, height: 54,
        border: `1.2px solid ${FAINT}`,
        padding: "8px 14px",
        fontFamily: MONO_FONT, fontSize: 12, color: INK,
        display: "flex", flexDirection: "column", gap: 4,
      }}>
        <div style={{ fontSize: 9, color: DIM, letterSpacing: 1 }}>SENT →</div>
        <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
          <span style={{ color: DIM }}>10 PRINT "HELLO WORLD"</span>
          <span style={{ color: INK, background: "rgba(255,255,255,0.08)", padding: "1px 6px" }}>▮</span>
        </div>
      </div>

      {/* mode strip inline with pills */}
      <div style={{
        position: "absolute", top: 190, left: 24,
        display: "flex", gap: 6, fontFamily: MONO_FONT, fontSize: 10,
      }}>
        <span style={{ color: DIM, alignSelf: "center" }}>MODE</span>
        {["K", "L", "C", "E", "G"].map((m, i) => (
          <span key={m} style={{
            padding: "3px 10px",
            border: `1px solid ${i === 0 ? INK : FAINT}`,
            background: i === 0 ? INK : "transparent",
            color: i === 0 ? BG : DIM,
            fontWeight: 700,
          }}>{m}</span>
        ))}
        <span style={{ color: STRIPE.green, alignSelf: "center", marginLeft: 8 }}>• keyword</span>
      </div>

      {/* sticky modifier indicator */}
      <div style={{
        position: "absolute", top: 190, right: 24,
        display: "flex", gap: 8, fontFamily: MONO_FONT, fontSize: 10,
      }}>
        <span style={{ padding: "3px 10px", border: `1px solid ${FAINT}`, color: DIM }}>CAPS ○</span>
        <span style={{ padding: "3px 10px", border: `1px solid ${STRIPE.yellow}`, color: STRIPE.yellow }}>SYM ●</span>
      </div>

      {/* rainbow corner */}
      <div style={{
        position: "absolute", bottom: 20, right: 24,
        display: "flex", gap: 0, height: 24,
      }}>
        {["red", "yellow", "green", "cyan", "magenta", "blue"].map(c => (
          <div key={c} style={{ width: 14, background: STRIPE[c] }} />
        ))}
      </div>

      {/* keyboard */}
      <div style={{ marginTop: 240, display: "flex", flexDirection: "column", gap: 10, alignItems: "center" }}>
        <div style={{ display: "flex", gap }}>{ROW_1.map((k, i) => drawKey(k, 1, i))}</div>
        <div style={{ display: "flex", gap, marginLeft: 20 }}>{ROW_2.map((k, i) => drawKey(k, 2, i))}</div>
        <div style={{ display: "flex", gap, marginLeft: 30 }}>{ROW_3.map((k, i) => drawKey(k, 3, i))}</div>
        <div style={{ display: "flex", gap }}>{ROW_4.map((k, i) => drawKey(k, 4, i))}</div>
      </div>

      <div style={{
        position: "absolute", bottom: 50, left: 28,
        fontFamily: HAND_FONT, fontSize: 16, color: STRIPE.yellow,
        maxWidth: 200,
      }}>
        ↑ sticky SYM on — next tap emits symbol
      </div>
    </div>
  );
}

// ============================================================================
// APPROACH 4 — LEGENDS ON DEMAND
// Clean primary keys by default. Modifier taps reveal overlays with keywords,
// E-mode, symbols. Minimal default state → less visual noise.
// ============================================================================

function Approach4OnDemand() {
  const keyW = 64, keyH = 60, gap = 8;
  const drawKey = (k, row, i, mode = "K") => {
    const w = k.wide ? keyW * k.wide : keyW;
    // decide what label to show based on mode
    let sub = null, subColor = DIM;
    if (mode === "K" && k.keyword) { sub = k.keyword; subColor = STRIPE.green; }
    else if (mode === "E" && k.eModeRed) { sub = k.eModeRed; subColor = STRIPE.red; }
    else if (mode === "E" && k.eModeGreen) { sub = k.eModeGreen; subColor = STRIPE.green; }
    else if (mode === "SYM" && k.symbolRed) { sub = k.symbolRed; subColor = STRIPE.red; }

    return (
      <div key={`${row}-${i}`} style={{
        position: "relative",
        width: w, height: keyH,
        display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center",
        color: INK, gap: 4,
      }}>
        <SketchyRect w={w} h={keyH} stroke={INK} strokeWidth={1.3} />
        <div style={{
          fontFamily: MONO_FONT, fontSize: k.main?.length > 2 ? 10 : 22,
          fontWeight: 600,
        }}>{k.main}</div>
        {sub && (
          <div style={{
            fontFamily: MONO_FONT, fontSize: 9, color: subColor,
          }}>{sub}</div>
        )}
      </div>
    );
  };

  return (
    <div style={{ background: BG, padding: 32, width: "100%", height: "100%", color: INK, boxSizing: "border-box", position: "relative" }}>
      <div style={{ position: "absolute", top: 14, left: 20, fontFamily: HAND_FONT, fontSize: 20 }}>
        approach 4 — legends on demand
      </div>
      <div style={{ position: "absolute", top: 40, left: 20, fontFamily: HAND_FONT, fontSize: 14, color: DIM, maxWidth: 420 }}>
        clean keys default. tap a mode (K / E / SYM) → labels swap in per-key
      </div>

      {/* status bar */}
      <div style={{ position: "absolute", top: 14, right: 20, display: "flex", gap: 14, alignItems: "center", fontFamily: MONO_FONT, fontSize: 10, color: DIM }}>
        <span style={{ display: "flex", gap: 5, alignItems: "center" }}>
          <span style={{ width: 6, height: 6, borderRadius: "50%", background: STRIPE.green }} />CONNECTED
        </span>
        <span>12ms</span>
        <span style={{ padding: "2px 8px", border: `1px solid ${FAINT}` }}>⛶</span>
      </div>

      {/* BIG mode toggle strip */}
      <div style={{
        position: "absolute", top: 78, left: "50%", transform: "translateX(-50%)",
        display: "flex", gap: 6, fontFamily: MONO_FONT, fontSize: 12,
      }}>
        {[
          { m: "K", label: "keyword", on: true, color: STRIPE.green },
          { m: "L", label: "letter", color: INK },
          { m: "E", label: "extended", color: STRIPE.red },
          { m: "SYM", label: "symbol", color: STRIPE.red },
          { m: "G", label: "graphics", color: STRIPE.magenta },
        ].map(x => (
          <div key={x.m} style={{
            padding: "6px 14px",
            border: `1.2px solid ${x.on ? x.color : FAINT}`,
            background: x.on ? "rgba(47,168,74,0.15)" : "transparent",
            color: x.on ? x.color : DIM,
            display: "flex", gap: 6, alignItems: "baseline",
          }}>
            <span style={{ fontWeight: 700 }}>{x.m}</span>
            <span style={{ fontSize: 9, color: x.on ? x.color : DIM, opacity: 0.7 }}>{x.label}</span>
          </div>
        ))}
      </div>

      {/* annotation callout */}
      <div style={{
        position: "absolute", top: 76, right: 60,
        fontFamily: HAND_FONT, fontSize: 14, color: STRIPE.yellow,
      }}>
        ← current mode = K<br/>so keys show green keywords
      </div>

      {/* rainbow stripe along top of keyboard */}
      <div style={{
        position: "absolute", top: 140, left: 60, right: 60, height: 3,
        display: "flex",
      }}>
        {["red", "yellow", "green", "cyan", "magenta", "blue"].map(c => (
          <div key={c} style={{ flex: 1, background: STRIPE[c] }} />
        ))}
      </div>

      {/* keyboard */}
      <div style={{ marginTop: 160, display: "flex", flexDirection: "column", gap: 10, alignItems: "center" }}>
        <div style={{ display: "flex", gap }}>{ROW_1.map((k, i) => drawKey(k, 1, i, "K"))}</div>
        <div style={{ display: "flex", gap, marginLeft: 16 }}>{ROW_2.map((k, i) => drawKey(k, 2, i, "K"))}</div>
        <div style={{ display: "flex", gap, marginLeft: 28 }}>{ROW_3.map((k, i) => drawKey(k, 3, i, "K"))}</div>
        <div style={{ display: "flex", gap }}>{ROW_4.map((k, i) => drawKey(k, 4, i, "K"))}</div>
      </div>

      <div style={{
        position: "absolute", bottom: 20, left: 40,
        fontFamily: HAND_FONT, fontSize: 15, color: STRIPE.cyan, maxWidth: 400,
      }}>
        trade-off: less busy, but you lose the "everything visible at once" encyclopedia feel
      </div>

      {/* stripe label row 1 colors — minimal ribbon */}
      <div style={{
        position: "absolute", bottom: 80, right: 40,
        display: "flex", gap: 2, fontFamily: MONO_FONT, fontSize: 8, color: DIM,
      }}>
        <span>COLOR KEYS:</span>
        {["blue","red","magenta","green","cyan","yellow","white","black"].map(c => (
          <span key={c} style={{ width: 12, height: 12, background: STRIPE[c], border: c === "black" ? `1px solid ${DIM}` : "none" }} />
        ))}
      </div>
    </div>
  );
}

// ============================================================================
// APPROACH 5 — SPLIT ERGONOMIC
// Left and right halves split for tablet thumb reach.
// Modifiers at center bottom. Reconfigures for thumb typing.
// ============================================================================

function Approach5Split() {
  const keyW = 52, keyH = 52, gap = 6;

  const drawKey = (k, row, i) => {
    const w = k.wide ? keyW * k.wide : keyW;
    return (
      <div key={`${row}-${i}`} style={{
        position: "relative",
        width: w, height: keyH,
        display: "flex", alignItems: "center", justifyContent: "center",
        color: INK,
      }}>
        <SketchyRect w={w} h={keyH} stroke={INK} strokeWidth={1.2} />
        {k.keywordAbove && (
          <div style={{
            position: "absolute", top: -13, left: 0, right: 0,
            textAlign: "center", fontFamily: MONO_FONT, fontSize: 7,
            color: k.stripeColor ? STRIPE[k.stripeColor] : DIM,
          }}>{k.keywordAbove}</div>
        )}
        {k.symbolRed && (
          <div style={{
            position: "absolute", top: 3, right: 4,
            fontFamily: MONO_FONT, fontSize: 8, color: STRIPE.red,
          }}>{k.symbolRed}</div>
        )}
        <div style={{
          fontFamily: MONO_FONT, fontSize: k.main?.length > 2 ? 9 : 17,
          fontWeight: 600,
        }}>{k.main}</div>
        {k.keyword && (
          <div style={{
            position: "absolute", bottom: 2, left: 0, right: 0,
            textAlign: "center", fontFamily: MONO_FONT, fontSize: 7,
            color: STRIPE.green,
          }}>{k.keyword}</div>
        )}
      </div>
    );
  };

  // split each row roughly in half (5/5, 5/5, 5/5, balanced for row 4)
  const splitRow1L = ROW_1.slice(0, 5);
  const splitRow1R = ROW_1.slice(5);
  const splitRow2L = ROW_2.slice(0, 5);
  const splitRow2R = ROW_2.slice(5);
  const splitRow3L = ROW_3.slice(0, 5);
  const splitRow3R = ROW_3.slice(5);
  const splitRow4L = ROW_4.slice(1, 5); // Z X C V
  const splitRow4R = ROW_4.slice(5, 8); // B N M

  return (
    <div style={{ background: BG, padding: 28, width: "100%", height: "100%", color: INK, boxSizing: "border-box", position: "relative" }}>
      <div style={{ position: "absolute", top: 14, left: 20, fontFamily: HAND_FONT, fontSize: 20 }}>
        approach 5 — split ergonomic
      </div>
      <div style={{ position: "absolute", top: 40, left: 20, fontFamily: HAND_FONT, fontSize: 14, color: DIM, maxWidth: 380 }}>
        two halves pushed to thumb zones — made for holding a tablet in both hands
      </div>

      {/* status bar */}
      <div style={{ position: "absolute", top: 14, right: 20, display: "flex", gap: 14, alignItems: "center", fontFamily: MONO_FONT, fontSize: 10, color: DIM }}>
        <span>● CONN</span><span>12ms</span><span>ESP32S3</span><span>⛶</span>
      </div>

      {/* mode strip */}
      <div style={{
        position: "absolute", top: 76, left: "50%", transform: "translateX(-50%)",
        display: "flex", gap: 4, fontFamily: MONO_FONT, fontSize: 10,
      }}>
        <span style={{ alignSelf: "center", color: DIM, marginRight: 6 }}>MODE</span>
        {["K","L","C","E","G"].map((m, i) => (
          <span key={m} style={{
            padding: "3px 10px",
            border: `1px solid ${i === 0 ? INK : FAINT}`,
            color: i === 0 ? INK : DIM,
            background: i === 0 ? "rgba(255,255,255,0.06)" : "transparent",
          }}>{m}</span>
        ))}
      </div>

      {/* LEFT HALF */}
      <div style={{
        position: "absolute", left: 40, top: 140,
        display: "flex", flexDirection: "column", gap: 20,
      }}>
        <div style={{ display: "flex", gap }}>{splitRow1L.map((k, i) => drawKey(k, 1, i))}</div>
        <div style={{ display: "flex", gap }}>{splitRow2L.map((k, i) => drawKey(k, 2, i))}</div>
        <div style={{ display: "flex", gap, marginLeft: 8 }}>{splitRow3L.map((k, i) => drawKey(k, 3, i))}</div>
        <div style={{ display: "flex", gap, marginLeft: 16 }}>{splitRow4L.map((k, i) => drawKey(k, 4, i))}</div>
      </div>

      {/* RIGHT HALF */}
      <div style={{
        position: "absolute", right: 40, top: 140,
        display: "flex", flexDirection: "column", gap: 20, alignItems: "flex-end",
      }}>
        <div style={{ display: "flex", gap }}>{splitRow1R.map((k, i) => drawKey(k, 1, i))}</div>
        <div style={{ display: "flex", gap }}>{splitRow2R.map((k, i) => drawKey(k, 2, i))}</div>
        <div style={{ display: "flex", gap, marginRight: 8 }}>{splitRow3R.map((k, i) => drawKey(k, 3, i))}</div>
        <div style={{ display: "flex", gap, marginRight: 16 }}>{splitRow4R.map((k, i) => drawKey(k, 4, i))}</div>
      </div>

      {/* CENTER — modifiers & space & rainbow */}
      <div style={{
        position: "absolute", left: "50%", top: 180,
        transform: "translateX(-50%)",
        display: "flex", flexDirection: "column", gap: 10, alignItems: "center",
        fontFamily: MONO_FONT,
      }}>
        <div style={{
          width: 80, height: 46,
          border: `1.3px solid ${INK}`,
          display: "flex", alignItems: "center", justifyContent: "center",
          fontSize: 9, fontWeight: 700, textAlign: "center",
        }}>CAPS<br/>SHIFT</div>
        <div style={{
          width: 80, height: 46,
          border: `1.3px solid ${STRIPE.yellow}`,
          color: STRIPE.yellow,
          display: "flex", alignItems: "center", justifyContent: "center",
          fontSize: 9, fontWeight: 700, textAlign: "center",
          background: "rgba(233,193,58,0.08)",
        }}>SYMBOL<br/>SHIFT ●</div>
        <div style={{
          width: 80, height: 46,
          border: `1.3px solid ${INK}`,
          display: "flex", alignItems: "center", justifyContent: "center",
          fontSize: 9, fontWeight: 700,
        }}>SPACE</div>
        <div style={{
          width: 80, height: 46,
          border: `1.3px solid ${STRIPE.red}`,
          color: STRIPE.red,
          display: "flex", alignItems: "center", justifyContent: "center",
          fontSize: 9, fontWeight: 700,
        }}>BREAK</div>
        <div style={{
          width: 80, height: 46,
          border: `1.3px solid ${INK}`,
          display: "flex", alignItems: "center", justifyContent: "center",
          fontSize: 11, fontWeight: 700,
        }}>ENTER ↵</div>
      </div>

      {/* rainbow accents on outer edges */}
      <div style={{ position: "absolute", left: 0, top: 140, bottom: 80, width: 6, display: "flex", flexDirection: "column" }}>
        {["red","yellow","green","cyan"].map(c => <div key={c} style={{ flex: 1, background: STRIPE[c] }} />)}
      </div>
      <div style={{ position: "absolute", right: 0, top: 140, bottom: 80, width: 6, display: "flex", flexDirection: "column" }}>
        {["red","yellow","green","cyan"].map(c => <div key={c} style={{ flex: 1, background: STRIPE[c] }} />)}
      </div>

      <div style={{
        position: "absolute", bottom: 16, left: "50%", transform: "translateX(-50%)",
        fontFamily: HAND_FONT, fontSize: 14, color: STRIPE.yellow, textAlign: "center",
      }}>
        ↑ modifiers & space in thumb-reach zone
      </div>

      <div style={{
        position: "absolute", bottom: 16, right: 30,
        fontFamily: HAND_FONT, fontSize: 13, color: DIM, textAlign: "right", maxWidth: 200,
      }}>
        note: breaks from original<br/>layout the most
      </div>
    </div>
  );
}

// ============================================================================
// OVERVIEW — design notes artboard
// ============================================================================

function OverviewNotes() {
  return (
    <div style={{ background: BG, padding: 40, width: "100%", height: "100%", color: INK, boxSizing: "border-box", fontFamily: HAND_FONT, position: "relative" }}>
      <div style={{ fontSize: 38, marginBottom: 8 }}>virtual zx-spectrum keyboard</div>
      <div style={{ fontSize: 20, color: DIM, marginBottom: 30 }}>esp32s3 · websocket · dark wireframe · original design</div>

      <div style={{ fontSize: 22, marginBottom: 12 }}>design principles</div>
      <ul style={{ fontSize: 18, lineHeight: 1.6, color: INK, paddingLeft: 24, marginBottom: 30 }}>
        <li>dark background, thin wireframe keys — no skeuomorphism</li>
        <li>authentic 40-key layout (4 rows × 10) — matches original exactly</li>
        <li>color stripes preserved: row-1 color names + rainbow accent</li>
        <li>all legends: keyword-above, red symbol, green command-below, E-mode</li>
        <li>sticky modifiers — tap CAPS / SYM once, next keystroke is modified</li>
        <li>mode indicator strip: K / L / C / E / G (Spectrum cursor modes)</li>
        <li>full status bar: connect/disconnect · ip · rtt · tx count · fullscreen</li>
        <li>haptic buzz on press (mobile)</li>
      </ul>

      <div style={{ fontSize: 22, marginBottom: 12 }}>5 directions →</div>
      <ol style={{ fontSize: 17, lineHeight: 1.7, color: INK, paddingLeft: 24 }}>
        <li><b>faithful clone</b> — original staggered layout, all legends outside keys</li>
        <li><b>terminal grid</b> — uniform cells, CRT scanlines, monospace everything</li>
        <li><b>compact + output strip</b> — live "sent" preview above keys, prominent WS bar</li>
        <li><b>legends on demand</b> — clean keys, labels swap per-mode</li>
        <li><b>split ergonomic</b> — two halves for tablet thumb-typing, modifiers centered</li>
      </ol>

      <div style={{
        position: "absolute", bottom: 40, right: 40,
        display: "flex", gap: 4,
      }}>
        {["blue","red","magenta","green","cyan","yellow","white","black"].map(c => (
          <div key={c} style={{
            width: 28, height: 28, background: STRIPE[c],
            border: c === "black" ? `1px solid ${DIM}` : "none",
          }} />
        ))}
      </div>
      <div style={{
        position: "absolute", bottom: 76, right: 40,
        fontSize: 12, fontFamily: MONO_FONT, color: DIM, letterSpacing: 1,
      }}>the 8 spectrum colors</div>
    </div>
  );
}

// ============================================================================
// MOUNT
// ============================================================================

const { DesignCanvas, DCSection, DCArtboard } = window;

function App() {
  return (
    <DesignCanvas title="ZX-style Keyboard — wireframe exploration">
      <DCSection id="notes" title="Notes & principles">
        <DCArtboard id="overview" label="Overview" width={900} height={720}>
          <OverviewNotes />
        </DCArtboard>
      </DCSection>

      <DCSection id="approaches" title="5 wireframe approaches">
        <DCArtboard id="a1" label="1 · Faithful clone" width={1000} height={620}>
          <Approach1Faithful />
        </DCArtboard>
        <DCArtboard id="a2" label="2 · Terminal grid" width={1000} height={620}>
          <Approach2Terminal />
        </DCArtboard>
        <DCArtboard id="a3" label="3 · Compact + output strip" width={1000} height={680}>
          <Approach3Output />
        </DCArtboard>
        <DCArtboard id="a4" label="4 · Legends on demand" width={1000} height={620}>
          <Approach4OnDemand />
        </DCArtboard>
        <DCArtboard id="a5" label="5 · Split ergonomic" width={1100} height={640}>
          <Approach5Split />
        </DCArtboard>
      </DCSection>
    </DesignCanvas>
  );
}

const root = ReactDOM.createRoot(document.getElementById("root"));
root.render(<App />);
