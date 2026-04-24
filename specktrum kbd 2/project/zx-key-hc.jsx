/* global React */
(function(){
const { useState } = React;

// High-contrast palette
const BG = "#000000";
const PANEL = "#000000";
const INK = "#ffffff";
const DIM = "#b8b6b0";
const FAINT = "#4a4a4a";
const LINE = "#ffffff";
const MONO = `"JetBrains Mono", "IBM Plex Mono", ui-monospace, monospace`;

// Pure/bright Spectrum colors for max legibility
const HC_STRIPE = {
  blue:    "#4da3ff",
  red:     "#ff5555",
  magenta: "#ff66cc",
  green:   "#66ff66",
  cyan:    "#66ffff",
  yellow:  "#ffff33",
  white:   "#ffffff",
  black:   "#cccccc",  // render "black" as light grey so it shows on black bg
};

const { STRIPE: _srcStripe } = window.ZX_LAYOUT;
const STRIPE = HC_STRIPE;

// Larger key dimensions for older users
const KEY_W = 92;
const KEY_H = 92;
const BORDER = 3;

function Key({ k, onPress, pressed, stickyCaps, stickySym, mode }) {
  const w = k.wide ? KEY_W * k.wide : KEY_W;
  const h = KEY_H;
  const [flash, setFlash] = useState(false);

  const isSpecial = !!k.special;
  const isSticky = (k.special === "caps" && stickyCaps) || (k.special === "symbol" && stickySym);

  const handleDown = (e) => {
    e.preventDefault();
    setFlash(true);
    onPress(k);
    if (navigator.vibrate) navigator.vibrate(15);
    setTimeout(() => setFlash(false), 160);
  };

  const borderColor = isSticky ? STRIPE.yellow : (flash ? STRIPE.yellow : INK);
  const bodyBg = flash
    ? STRIPE.yellow
    : (isSticky ? "rgba(255,255,51,0.22)" : "transparent");
  const mainColor = flash ? "#000" : (isSticky ? STRIPE.yellow : INK);

  return (
    <div
      onMouseDown={handleDown}
      onTouchStart={handleDown}
      role="button"
      aria-label={k.main}
      style={{
        position: "relative",
        width: w, height: h,
        boxSizing: "border-box",
        border: `${BORDER}px solid ${borderColor}`,
        background: bodyBg,
        display: "flex", alignItems: "center", justifyContent: "center",
        color: INK, userSelect: "none", cursor: "pointer",
        transition: "border-color 80ms, background 80ms, color 80ms",
      }}
    >
      {/* keyword above */}
      {k.keywordAbove && (
        <div style={{
          position: "absolute", top: -24, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 13,
          color: k.stripeColor ? STRIPE[k.stripeColor] : INK,
          letterSpacing: 0.3, fontWeight: 800,
          opacity: mode === "K" ? 1 : 0.75,
        }}>{k.keywordAbove}</div>
      )}
      {/* stripe label above keywordAbove */}
      {k.stripeLabel && (
        <div style={{
          position: "absolute", top: -42, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 11,
          color: k.stripeColor ? STRIPE[k.stripeColor] : INK,
          letterSpacing: 0.5, fontWeight: 800,
        }}>{k.stripeLabel}</div>
      )}
      {/* red symbol top-right */}
      {k.symbolRed && (
        <div style={{
          position: "absolute", top: 6, right: 8,
          fontFamily: MONO, fontSize: 15, color: STRIPE.red,
          fontWeight: 800,
          opacity: stickySym || mode === "E" ? 1 : 0.85,
        }}>{k.symbolRed}</div>
      )}
      {/* main letter/label */}
      <div style={{
        fontFamily: MONO,
        fontSize: isSpecial ? 14 : 36,
        fontWeight: 800,
        letterSpacing: isSpecial ? 0.5 : 0,
        textAlign: "center",
        lineHeight: 1.05,
        color: mainColor,
      }}>
        {k.main.split(" ").map((word, i) => <div key={i}>{word}</div>)}
      </div>
      {/* green keyword on body */}
      {k.keyword && (
        <div style={{
          position: "absolute", bottom: 5, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 12,
          color: STRIPE.green, fontWeight: 800,
          opacity: mode === "K" ? 1 : 0.7,
        }}>{k.keyword}</div>
      )}
      {/* command below */}
      {k.commandBelow && (
        <div style={{
          position: "absolute", bottom: -22, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 12,
          color: STRIPE.green, letterSpacing: 0.3, fontWeight: 800,
        }}>{k.commandBelow}</div>
      )}
      {/* E-mode labels below */}
      {k.eModeRed && (
        <div style={{
          position: "absolute", bottom: -22, left: 4,
          fontFamily: MONO, fontSize: 12, color: STRIPE.red,
          fontWeight: 800,
          opacity: mode === "E" ? 1 : 0.75,
        }}>{k.eModeRed}</div>
      )}
      {k.eModeGreen && (
        <div style={{
          position: "absolute", bottom: -22, right: 4,
          fontFamily: MONO, fontSize: 12, color: STRIPE.green,
          fontWeight: 800,
          opacity: mode === "E" ? 1 : 0.75,
        }}>{k.eModeGreen}</div>
      )}
    </div>
  );
}

window.ZXKeyHC = Key;
window.ZX_STYLE_HC = { BG, PANEL, INK, DIM, FAINT, LINE, MONO, STRIPE };
})();
