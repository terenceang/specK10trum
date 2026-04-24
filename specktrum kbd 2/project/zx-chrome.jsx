/* global React */
(function(){
const { ROW_1, ROW_2, ROW_3, ROW_4, STRIPE } = window.ZX_LAYOUT;
const { PANEL, INK, DIM, FAINT, LINE, MONO } = window.ZX_STYLE;
const Key = window.ZXKey;

// -----------------------------------------------------------------------------
// Status bar — WS connection state
// -----------------------------------------------------------------------------
function StatusBar({ connected, latency, txCount, host, onToggleConn, onFullscreen }) {
  return (
    <div style={{
      height: 38,
      border: `1.2px solid ${LINE}`,
      background: PANEL,
      display: "flex", alignItems: "center",
      padding: "0 16px", gap: 18,
      fontFamily: MONO, fontSize: 11, color: INK,
    }}>
      <span style={{ display: "flex", alignItems: "center", gap: 8 }}>
        <span style={{
          width: 8, height: 8, borderRadius: "50%",
          background: connected ? STRIPE.green : STRIPE.red,
          boxShadow: connected ? `0 0 8px ${STRIPE.green}` : `0 0 8px ${STRIPE.red}`,
        }} />
        <span style={{ fontWeight: 700, letterSpacing: 0.5 }}>
          {connected ? "CONNECTED" : "OFFLINE"}
        </span>
      </span>
      <span style={{ color: DIM }}>│</span>
      <span style={{ color: DIM }}>{host}</span>
      <span style={{ color: DIM }}>│</span>
      <span>RTT <span style={{ color: connected ? STRIPE.yellow : DIM }}>{connected ? `${latency}ms` : "—"}</span></span>
      <span style={{ color: DIM }}>│</span>
      <span>TX <span style={{ color: INK }}>{txCount.toLocaleString()}</span></span>
      <span style={{ color: DIM }}>│</span>
      <span>ESP32-S3 <span style={{ color: connected ? STRIPE.cyan : DIM }}>{connected ? "online" : "—"}</span></span>

      <span style={{ marginLeft: "auto", display: "flex", gap: 10 }}>
        <button onClick={onToggleConn} style={btnStyle}>{connected ? "DISCONNECT" : "CONNECT"}</button>
        <button onClick={onFullscreen} style={btnStyle}>⛶ FULLSCREEN</button>
      </span>
    </div>
  );
}

const btnStyle = {
  background: "transparent",
  border: `1px solid ${LINE}`,
  color: INK,
  padding: "4px 12px",
  fontFamily: MONO, fontSize: 10, letterSpacing: 0.5, fontWeight: 600,
  cursor: "pointer",
};

// -----------------------------------------------------------------------------
// Output strip — live "sent keys" preview
// -----------------------------------------------------------------------------
function OutputStrip({ buffer }) {
  return (
    <div style={{
      height: 60,
      border: `1.2px solid ${LINE}`,
      background: PANEL,
      padding: "8px 16px",
      fontFamily: MONO, fontSize: 13,
      display: "flex", flexDirection: "column", gap: 4,
      overflow: "hidden",
    }}>
      <div style={{ fontSize: 9, color: DIM, letterSpacing: 1.5, fontWeight: 700 }}>SENT → ESP32-S3</div>
      <div style={{
        color: INK, whiteSpace: "nowrap", overflow: "hidden",
        textOverflow: "ellipsis", direction: "rtl", textAlign: "left",
      }}>
        <span style={{ direction: "ltr", unicodeBidi: "plaintext" }}>
          {buffer || <span style={{ color: DIM }}>— press any key to begin —</span>}
        </span>
        <span style={{
          display: "inline-block", width: 8, height: 14,
          background: INK, marginLeft: 2, verticalAlign: "middle",
          animation: "zx-blink 1s steps(2) infinite",
        }} />
      </div>
    </div>
  );
}

// -----------------------------------------------------------------------------
// Mode strip + sticky modifier badges
// -----------------------------------------------------------------------------
function ModeStrip({ mode, setMode, stickyCaps, stickySym }) {
  const modes = [
    { m: "K", label: "keyword", color: STRIPE.green },
    { m: "L", label: "letter",  color: INK },
    { m: "C", label: "caps",    color: STRIPE.white },
    { m: "E", label: "extended", color: STRIPE.red },
    { m: "G", label: "graphics", color: STRIPE.magenta },
  ];
  return (
    <div style={{
      display: "flex", alignItems: "center", gap: 12,
      padding: "0 4px",
    }}>
      <span style={{ fontFamily: MONO, fontSize: 10, color: DIM, letterSpacing: 1 }}>MODE</span>
      <div style={{ display: "flex", gap: 4 }}>
        {modes.map(x => {
          const on = mode === x.m;
          return (
            <button key={x.m} onClick={() => setMode(x.m)} style={{
              padding: "5px 14px",
              border: `1px solid ${on ? x.color : FAINT}`,
              background: on ? `${x.color}18` : "transparent",
              color: on ? x.color : DIM,
              fontFamily: MONO, fontSize: 10, fontWeight: 700, letterSpacing: 0.5,
              cursor: "pointer",
            }}>
              {x.m} <span style={{ fontWeight: 400, opacity: 0.85 }}>{x.label}</span>
            </button>
          );
        })}
      </div>
      <span style={{ flex: 1 }} />
      <div style={{ display: "flex", gap: 6, fontFamily: MONO, fontSize: 10 }}>
        <span style={{
          padding: "4px 12px",
          border: `1px solid ${stickyCaps ? STRIPE.yellow : FAINT}`,
          color: stickyCaps ? STRIPE.yellow : DIM,
          background: stickyCaps ? `${STRIPE.yellow}15` : "transparent",
          fontWeight: 700, letterSpacing: 0.5,
        }}>CAPS {stickyCaps ? "●" : "○"}</span>
        <span style={{
          padding: "4px 12px",
          border: `1px solid ${stickySym ? STRIPE.yellow : FAINT}`,
          color: stickySym ? STRIPE.yellow : DIM,
          background: stickySym ? `${STRIPE.yellow}15` : "transparent",
          fontWeight: 700, letterSpacing: 0.5,
        }}>SYM {stickySym ? "●" : "○"}</span>
      </div>
    </div>
  );
}

// -----------------------------------------------------------------------------
// Keyboard assembly
// -----------------------------------------------------------------------------
function Keyboard({ onPress, stickyCaps, stickySym, mode }) {
  const rowStyle = (ml = 0) => ({
    display: "flex", gap: 8, marginLeft: ml,
  });
  const keyProps = { onPress, stickyCaps, stickySym, mode };
  return (
    <div style={{
      display: "flex", flexDirection: "column",
      gap: 28, alignItems: "flex-start",
      padding: "38px 24px 28px 24px",
      border: `1.2px solid ${LINE}`,
      background: PANEL,
      position: "relative",
    }}>
      {/* rainbow stripe on the right, like the original */}
      <div style={{
        position: "absolute", right: 0, top: 0, bottom: 0, width: 12,
        display: "flex", flexDirection: "column",
      }}>
        {["red","yellow","green","cyan"].map(c => (
          <div key={c} style={{ flex: 1, background: STRIPE[c] }} />
        ))}
      </div>

      <div style={rowStyle(0)}>{ROW_1.map((k, i) => <Key key={`1-${i}`} k={k} {...keyProps} />)}</div>
      <div style={rowStyle(16)}>{ROW_2.map((k, i) => <Key key={`2-${i}`} k={k} {...keyProps} />)}</div>
      <div style={rowStyle(32)}>{ROW_3.map((k, i) => <Key key={`3-${i}`} k={k} {...keyProps} />)}</div>
      <div style={rowStyle(0)}>{ROW_4.map((k, i) => <Key key={`4-${i}`} k={k} {...keyProps} />)}</div>
    </div>
  );
}

window.ZXStatusBar = StatusBar;
window.ZXOutputStrip = OutputStrip;
window.ZXModeStrip = ModeStrip;
window.ZXKeyboard = Keyboard;
})();
