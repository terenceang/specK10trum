/* global React */
(function(){
const { ROW_1, ROW_2, ROW_3, ROW_4 } = window.ZX_LAYOUT;
const { INK, DIM, FAINT, LINE, MONO, STRIPE } = window.ZX_STYLE_HC;
const Key = window.ZXKeyHC;

// -----------------------------------------------------------------------------
// Status bar — HC
// -----------------------------------------------------------------------------
function StatusBar({ connected, latency, txCount, host, onToggleConn, onFullscreen }) {
  return (
    <div style={{
      height: 56,
      border: `3px solid ${INK}`,
      background: "#000",
      display: "flex", alignItems: "center",
      padding: "0 20px", gap: 20,
      fontFamily: MONO, fontSize: 15, color: INK, fontWeight: 700,
    }}>
      <span style={{ display: "flex", alignItems: "center", gap: 10 }}>
        <span style={{
          width: 14, height: 14, borderRadius: "50%",
          background: connected ? STRIPE.green : STRIPE.red,
          border: `2px solid ${INK}`,
        }} />
        <span style={{ fontWeight: 800, letterSpacing: 1 }}>
          {connected ? "CONNECTED" : "OFFLINE"}
        </span>
      </span>
      <span style={{ color: INK }}>│</span>
      <span style={{ color: INK }}>{host}</span>
      <span style={{ color: INK }}>│</span>
      <span>RTT <span style={{ color: connected ? STRIPE.yellow : DIM }}>{connected ? `${latency}ms` : "—"}</span></span>
      <span style={{ color: INK }}>│</span>
      <span>TX <span style={{ color: STRIPE.yellow }}>{txCount.toLocaleString()}</span></span>

      <span style={{ marginLeft: "auto", display: "flex", gap: 12 }}>
        <button onClick={onToggleConn} style={btnStyle}>{connected ? "DISCONNECT" : "CONNECT"}</button>
        <button onClick={onFullscreen} style={btnStyle}>⛶ FULLSCREEN</button>
      </span>
    </div>
  );
}

const btnStyle = {
  background: "transparent",
  border: `3px solid ${INK}`,
  color: INK,
  padding: "8px 18px",
  fontFamily: MONO, fontSize: 14, letterSpacing: 1, fontWeight: 800,
  cursor: "pointer",
};

// -----------------------------------------------------------------------------
// Output strip — HC, larger text
// -----------------------------------------------------------------------------
function OutputStrip({ buffer }) {
  return (
    <div style={{
      height: 90,
      border: `3px solid ${INK}`,
      background: "#000",
      padding: "12px 20px",
      fontFamily: MONO, fontSize: 22,
      display: "flex", flexDirection: "column", gap: 8,
      overflow: "hidden",
    }}>
      <div style={{ fontSize: 13, color: STRIPE.yellow, letterSpacing: 2, fontWeight: 800 }}>SENT → ESP32-S3</div>
      <div style={{
        color: INK, whiteSpace: "nowrap", overflow: "hidden",
        textOverflow: "ellipsis", direction: "rtl", textAlign: "left",
        fontWeight: 700,
      }}>
        <span style={{ direction: "ltr", unicodeBidi: "plaintext" }}>
          {buffer || <span style={{ color: DIM }}>— press any key to begin —</span>}
        </span>
        <span style={{
          display: "inline-block", width: 14, height: 24,
          background: INK, marginLeft: 3, verticalAlign: "middle",
          animation: "zx-blink 1s steps(2) infinite",
        }} />
      </div>
    </div>
  );
}

// -----------------------------------------------------------------------------
// Mode strip — HC
// -----------------------------------------------------------------------------
function ModeStrip({ mode, setMode, stickyCaps, stickySym }) {
  const modes = [
    { m: "K", label: "keyword",  color: STRIPE.green },
    { m: "L", label: "letter",   color: INK },
    { m: "C", label: "caps",     color: STRIPE.white },
    { m: "E", label: "extended", color: STRIPE.red },
    { m: "G", label: "graphics", color: STRIPE.magenta },
  ];
  return (
    <div style={{
      display: "flex", alignItems: "center", gap: 14,
      padding: "0 4px",
    }}>
      <span style={{ fontFamily: MONO, fontSize: 14, color: INK, letterSpacing: 1.5, fontWeight: 800 }}>MODE</span>
      <div style={{ display: "flex", gap: 6 }}>
        {modes.map(x => {
          const on = mode === x.m;
          return (
            <button key={x.m} onClick={() => setMode(x.m)} style={{
              padding: "10px 20px",
              border: `3px solid ${on ? x.color : INK}`,
              background: on ? x.color : "transparent",
              color: on ? "#000" : INK,
              fontFamily: MONO, fontSize: 14, fontWeight: 800, letterSpacing: 1,
              cursor: "pointer",
            }}>
              {x.m} <span style={{ fontWeight: 700, opacity: 0.9 }}>{x.label}</span>
            </button>
          );
        })}
      </div>
      <span style={{ flex: 1 }} />
      <div style={{ display: "flex", gap: 8, fontFamily: MONO, fontSize: 14 }}>
        <span style={{
          padding: "10px 18px",
          border: `3px solid ${stickyCaps ? STRIPE.yellow : INK}`,
          color: stickyCaps ? "#000" : INK,
          background: stickyCaps ? STRIPE.yellow : "transparent",
          fontWeight: 800, letterSpacing: 1,
        }}>CAPS {stickyCaps ? "●" : "○"}</span>
        <span style={{
          padding: "10px 18px",
          border: `3px solid ${stickySym ? STRIPE.yellow : INK}`,
          color: stickySym ? "#000" : INK,
          background: stickySym ? STRIPE.yellow : "transparent",
          fontWeight: 800, letterSpacing: 1,
        }}>SYM {stickySym ? "●" : "○"}</span>
      </div>
    </div>
  );
}

// -----------------------------------------------------------------------------
// Keyboard assembly — HC
// -----------------------------------------------------------------------------
function Keyboard({ onPress, stickyCaps, stickySym, mode }) {
  const rowStyle = (ml = 0) => ({
    display: "flex", gap: 12, marginLeft: ml,
  });
  const keyProps = { onPress, stickyCaps, stickySym, mode };
  return (
    <div style={{
      display: "flex", flexDirection: "column",
      gap: 44, alignItems: "flex-start",
      padding: "58px 32px 40px 32px",
      border: `3px solid ${INK}`,
      background: "#000",
      position: "relative",
    }}>
      {/* rainbow stripe on right — thicker for HC */}
      <div style={{
        position: "absolute", right: 0, top: 0, bottom: 0, width: 20,
        display: "flex", flexDirection: "column",
      }}>
        {["red","yellow","green","cyan"].map(c => (
          <div key={c} style={{ flex: 1, background: STRIPE[c] }} />
        ))}
      </div>

      <div style={rowStyle(0)}>{ROW_1.map((k, i) => <Key key={`1-${i}`} k={k} {...keyProps} />)}</div>
      <div style={rowStyle(24)}>{ROW_2.map((k, i) => <Key key={`2-${i}`} k={k} {...keyProps} />)}</div>
      <div style={rowStyle(48)}>{ROW_3.map((k, i) => <Key key={`3-${i}`} k={k} {...keyProps} />)}</div>
      <div style={rowStyle(0)}>{ROW_4.map((k, i) => <Key key={`4-${i}`} k={k} {...keyProps} />)}</div>
    </div>
  );
}

window.ZXStatusBarHC = StatusBar;
window.ZXOutputStripHC = OutputStrip;
window.ZXModeStripHC = ModeStrip;
window.ZXKeyboardHC = Keyboard;
})();
