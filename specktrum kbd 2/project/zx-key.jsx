/* global React */
(function(){
const { useState } = React;

// Colors
const BG = "#0a0a0b";
const PANEL = "#111113";
const INK = "#e8e6df";
const DIM = "#6e6c66";
const FAINT = "#26252a";
const LINE = "#3b3a40";

const MONO = `"JetBrains Mono", "IBM Plex Mono", ui-monospace, monospace`;
const { ROW_1, ROW_2, ROW_3, ROW_4, STRIPE } = window.ZX_LAYOUT;

// -----------------------------------------------------------------------------
// Key component — wireframe rectangle with authentic legends
// -----------------------------------------------------------------------------

function Key({ k, onPress, pressed, stickyCaps, stickySym, mode }) {
  const w = k.wide ? 60 * k.wide : 60;
  const h = 60;
  const [flash, setFlash] = useState(false);

  const isSpecial = !!k.special;
  const isSticky = (k.special === "caps" && stickyCaps) || (k.special === "symbol" && stickySym);

  const handleDown = (e) => {
    e.preventDefault();
    setFlash(true);
    onPress(k);
    if (navigator.vibrate) navigator.vibrate(8);
    setTimeout(() => setFlash(false), 120);
  };

  // determine border/label colors for this key in current mode
  const borderColor = isSticky ? STRIPE.yellow : (flash ? INK : LINE);
  const bodyBg = flash ? "rgba(255,255,255,0.12)" : (isSticky ? "rgba(255,204,30,0.08)" : "transparent");

  return (
    <div
      onMouseDown={handleDown}
      onTouchStart={handleDown}
      style={{
        position: "relative",
        width: w, height: h,
        boxSizing: "border-box",
        border: `1.2px solid ${borderColor}`,
        background: bodyBg,
        display: "flex", alignItems: "center", justifyContent: "center",
        color: INK, userSelect: "none", cursor: "pointer",
        transition: "border-color 80ms, background 80ms",
      }}
    >
      {/* keyword above (above the key — positioned outside) */}
      {k.keywordAbove && (
        <div style={{
          position: "absolute", top: -17, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 9,
          color: k.stripeColor ? STRIPE[k.stripeColor] : DIM,
          letterSpacing: 0.3, fontWeight: 600,
          opacity: mode === "K" || mode === "KEYWORD" ? 1 : 0.5,
        }}>{k.keywordAbove}</div>
      )}
      {/* stripe label (above keywordAbove for row 1) */}
      {k.stripeLabel && (
        <div style={{
          position: "absolute", top: -30, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 8,
          color: k.stripeColor ? STRIPE[k.stripeColor] : DIM,
          letterSpacing: 0.5, fontWeight: 700,
        }}>{k.stripeLabel}</div>
      )}
      {/* red symbol top-right */}
      {k.symbolRed && (
        <div style={{
          position: "absolute", top: 4, right: 5,
          fontFamily: MONO, fontSize: 10, color: STRIPE.red,
          opacity: stickySym || mode === "E" ? 1 : 0.7,
          fontWeight: stickySym ? 700 : 400,
        }}>{k.symbolRed}</div>
      )}
      {/* main letter */}
      <div style={{
        fontFamily: MONO,
        fontSize: isSpecial ? 10 : 22,
        fontWeight: 600,
        letterSpacing: isSpecial ? 0.5 : 0,
        textAlign: "center",
        lineHeight: 1.15,
        color: isSticky ? STRIPE.yellow : INK,
      }}>
        {k.main.split(" ").map((w, i, arr) => (
          <div key={i}>{w}</div>
        ))}
      </div>
      {/* green keyword on body (below letter) */}
      {k.keyword && (
        <div style={{
          position: "absolute", bottom: 3, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 8,
          color: STRIPE.green,
          opacity: mode === "K" ? 1 : 0.55,
          fontWeight: mode === "K" ? 700 : 400,
        }}>{k.keyword}</div>
      )}
      {/* command-below outside key */}
      {k.commandBelow && (
        <div style={{
          position: "absolute", bottom: -15, left: 0, right: 0,
          textAlign: "center", fontFamily: MONO, fontSize: 8,
          color: STRIPE.green, letterSpacing: 0.3,
        }}>{k.commandBelow}</div>
      )}
      {/* E-mode labels below */}
      {(k.eModeRed || k.eModeGreen) && (
        <>
          {k.eModeRed && (
            <div style={{
              position: "absolute", bottom: -15, left: 4,
              fontFamily: MONO, fontSize: 8, color: STRIPE.red,
              opacity: mode === "E" ? 1 : 0.6,
              fontWeight: mode === "E" ? 700 : 400,
            }}>{k.eModeRed}</div>
          )}
          {k.eModeGreen && (
            <div style={{
              position: "absolute", bottom: -15, right: 4,
              fontFamily: MONO, fontSize: 8, color: STRIPE.green,
              opacity: mode === "E" ? 1 : 0.6,
              fontWeight: mode === "E" ? 700 : 400,
            }}>{k.eModeGreen}</div>
          )}
        </>
      )}
    </div>
  );
}

window.ZXKey = Key;
window.ZX_STYLE = { BG, PANEL, INK, DIM, FAINT, LINE, MONO, STRIPE };
})();
