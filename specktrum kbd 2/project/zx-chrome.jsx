/* global React */
(function(){
const { ROW_1, ROW_2, ROW_3, ROW_4, STRIPE } = window.ZX_LAYOUT;
const { PANEL, INK, DIM, FAINT, LINE, MONO } = window.ZX_STYLE;
const Key = window.ZXKey;

// -----------------------------------------------------------------------------
// Status bar — WS connection state + Output buffer (Compact)
// -----------------------------------------------------------------------------
function StatusBar({ connected, latency, txCount, host, onToggleConn, onFullscreen, buffer }) {
  return (
    <div style={{
      height: 38,
      border: `1px solid ${LINE}`,
      background: PANEL,
      display: "flex", alignItems: "center",
      padding: "0 12px", gap: 20,
      fontFamily: MONO, fontSize: 11, color: INK,
    }}>
      <span style={{
        width: 10, height: 10, borderRadius: "50%",
        background: connected ? STRIPE.green : "#333",
        boxShadow: connected ? `0 0 8px ${STRIPE.green}` : "none",
        transition: "all 0.2s",
      }} title={connected ? "Connected" : "Offline"} />
      
      {/* Logo in place of WS URL */}
      <div style={{ height: 20, width: "auto", display: "block" }}>
        <LogoSvg />
      </div>
      
      <span style={{ color: INK, fontWeight: 800 }}>TX {txCount.toLocaleString()}</span>

      {/* Buffer Display - fills remaining space */}
      <div style={{
        flex: 1, display: "flex", alignItems: "center",
        minWidth: 0, overflow: "hidden", justifyContent: "flex-end",
      }}>
        <div style={{
          color: DIM, whiteSpace: "nowrap", overflow: "hidden",
          textOverflow: "ellipsis", direction: "rtl", textAlign: "left",
          fontSize: 12, opacity: 0.8,
        }}>
          <span style={{ direction: "ltr", unicodeBidi: "plaintext" }}>
            {buffer || "—"}
          </span>
          <span style={{
            display: "inline-block", width: 6, height: 10,
            background: INK, marginLeft: 2, verticalAlign: "middle",
            animation: "zx-blink 1s steps(2) infinite",
          }} />
        </div>
      </div>

      <span style={{ marginLeft: "auto", display: "flex", gap: 8 }}>
        <button onClick={onToggleConn} style={btnStyle}>{connected ? "OFF" : "ON"}</button>
        <button onClick={onFullscreen} style={btnStyle}>⛶</button>
      </span>
    </div>
  );
}

function LogoSvg() {
  return (
    <svg id="Layer_1" xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 1318.41 183.18" style={{ height: "100%", width: "auto" }}>
      <defs>
        <style>{`
          .st0 { fill: #fbe8b1; font-family: HelveticaNeueLTPro-Roman, 'Helvetica Neue LT Pro'; font-size: 23.02px; letter-spacing: .1em; }
          .st1 { fill: #fff; }
          .st2 { fill: #02b07b; }
          .st3 { fill: #015dbe; }
          .st4 { fill: #fdcb02; }
          .st5 { fill: #fd2f2c; }
        `}</style>
      </defs>
      <g id="TEXT">
        <path className="st1" d="M289.95,92.61c-.61,11.21-8.98,21.15-20.77,21.2l-88.51.3c-.26-8.26-1.17-15.16-2.65-23.87l83-.58c4.88-.03,6.26-19.14-1.03-19.24l-63.35-.87c-11.93-.16-21.28-10.1-22.2-21.36-.67-8.11-.64-15.21.09-23.25,1.06-11.64,10.75-21.45,23.06-21.52l87.76-.48-.38,30.36c-4.55-2.28-6.7-5.21-10.82-6.17l-64.79-.1c-2.71,0-4.96,1.07-6.26,2.36-2.04,2.02-2.08,4.82-2.13,7.65-.09,4.99,2.07,8.95,7.37,8.97l57.51.23c13.15.05,23.22,8.51,24.02,21.96.47,7.92.54,15.58.06,24.4Z"/>
        <path className="st1" d="M911.72,113.54c-23.17,1.04-45.47.91-67.73-.02-14.04-.58-23.47-11.48-25.68-24.47-.38-20.04-.72-38.97.06-59.07.58-14.79,12.99-26.7,28.25-26.68l61.57.06c16.56.02,28.43,13.67,28.44,29.64v52.2c.01,13.63-9.49,27.64-24.91,28.34ZM902.73,89.94c4.84-.01,8.07-4.23,8.07-8.49l.07-43.95c0-5.78-4.34-9.38-9.78-10.41l-46.77.07c-5.38,0-9.88,4.62-9.88,9.96l-.04,42.94c0,5.69,4.26,10.01,9.93,10l48.41-.11Z"/>
        <path className="st1" d="M694.05,56.59l57.6,57.31-34.93.1-44.07-44.45c-5.26-.11-9.47-.18-14.91.37l-.2,44.08h-26.02s0-110.78,0-110.78l26.06-.11.14,43.18c5.01.97,9.06.68,13.79.17l43.17-43.27,32.84.12-53.48,53.27Z"/>
        <path className="st1" d="M1267.31,54.09l-.13,59.91-24.84.04-.04-53.9c-.65-3.78-3.12-6.12-6.93-6.12h-21.88s-.03,59.96-.03,59.96l-24.13.07V33.95s106.76.41,106.76.41c13.98.05,23.21,12.54,22.25,25.66v54.06s-24.98-.02-24.98-.02l-.13-53.78c0-3.7-3.39-6.19-6.77-6.19h-19.14Z"/>
        <path className="st1" d="M376.69,113.63l-47.08.6-.4,23.73c-.04,2.45-1.09,4.5-.87,7.16l-23.99-.11V34.02s67.8.07,67.8.07c14.12.01,25.53,8.24,26.4,22.97-.09,11.31.58,22.49-.29,34.01s-9.3,22.4-21.56,22.55ZM372.41,61.06c0-4.83-3.27-7.94-8.07-7.94h-28.93c-4.15.25-6.17,2.96-6.16,6.89l.1,32.87,34.08.22c12.6.08,8.95-11,8.98-32.04Z"/>
        <path className="st1" d="M437.52,88.75c.4,2.68,3.09,4.47,5.89,4.48l67.18.1-.06,20.83-77.08-.39c-12.95-.07-21.78-11.51-21.78-23.59v-33.25c.02-11.69,9-22.46,21.25-22.5l56.41-.22c12.61-.05,21.9,10.66,22.16,22.67.18,8.34.51,15.84-.22,24.2h-73.11c-1.32,2.22-1.04,4.96-.63,7.67ZM485.39,63.88c2.46-2.31.88-11.31-4.44-11.46-12.8-.35-25.4-.46-38.22,0-5.97.21-6.53,9.97-4.43,11.36l47.09.1Z"/>
        <path className="st1" d="M1087.33,34.17l24.99-.06.16,52.53c.01,3.75,3.43,6.41,6.86,6.41l26.88-.02c3.07,0,5.65-2.7,5.66-5.98l.16-52.96,25.95-.03-.1,56.04c-.02,12.64-9.52,23.55-22.55,23.77-16.13.27-31.85.74-47.78-.24-12.39-.76-20.72-11.8-20.23-23.5v-55.96Z"/>
        <path className="st1" d="M556.84,92.97l60.48.11v20.95s-67.99.16-67.99.16c-13.1.03-24.01-8.77-24.95-22.17-.83-11.83-.35-23.17-.3-35.12s8.79-22.5,21.41-22.56l71.73-.38.03,20.4-58.91-.16c-4.8-.01-8.81,2.9-8.84,7.95l-.14,22.97c-.02,3.97,2.75,7.84,7.47,7.85Z"/>
        <path className="st1" d="M1006.78,93.15l.05,20.96c-12.19-.53-24.17.79-36.27-.52-10.68-1.16-18.13-10.19-18.18-20.45l-.21-39.03c-3.14-.11-5.65.46-8.83-.27v-19.62s8.89-.42,8.89-.42l.13-30.74,25.2.06.08,30.67,29.29.35c-.03,7.05.47,13.59-.26,19.97l-28.88.37-.09,31.61c-.01,3.95,3.01,6.95,6.79,6.96l22.29.1Z"/>
        <path className="st1" d="M779.83,114.1l-.28-81.38-24.55,14c-.58-5.59-.26-10.77-.18-16.41l27.85-27.06,21.68-.16v110.94s-24.52.06-24.52.06Z"/>
        <path className="st1" d="M1046,61.53l-.19,52.34-25.54.25.36-58.95c.07-11.57,9.46-20.67,20.75-20.79l35.95-.39-.05,20.15-24.75.18c-3.59.03-6.52,2.96-6.54,7.22Z"/>
      </g>
      <g id="COLOR">
        <path className="st4" d="M70.72,182.48c-9.83,1.37-19.79.23-30.35.58V.19s30.28-.06,30.28-.06l.07,182.36Z"/>
        <polygon className="st3" points="154.35 182.82 119.43 183.1 119.33 .16 154.34 .07 154.35 182.82"/>
        <polygon className="st5" points="33.4 182.79 0 183.05 .04 .09 33.42 .12 33.4 182.79"/>
        <path className="st2" d="M112.33.12l.02,182.69-33,.18.02-182.78c2.99,1.18,5.36.23,8.02.06,8.12-.53,15.67-.08,24.95-.15Z"/>
      </g>
      <text className="st0" transform="translate(173.96 180.98) scale(1.91 1)"><tspan x="0" y="0">ZX SPECTRUM EMULATOR FOR UNIHIKER H10</tspan></text>
    </svg>
  );
}

const btnStyle = {
  background: "rgba(255,255,255,0.03)",
  border: `1px solid ${LINE}`,
  color: DIM,
  padding: "3px 8px",
  fontFamily: MONO, fontSize: 9, fontWeight: 600,
  cursor: "pointer",
  borderRadius: 3,
};

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
      <div style={{ display: "flex", gap: 4 }}>
        {modes.map(x => {
          const on = mode === x.m;
          return (
            <button key={x.m} onClick={() => setMode(x.m)} style={{
              padding: "5px 12px",
              border: `1px solid ${on ? x.color : "#222"}`,
              background: on ? `${x.color}15` : "transparent",
              color: on ? x.color : DIM,
              fontFamily: MONO, fontSize: 10, fontWeight: 700,
              cursor: "pointer",
              borderRadius: 4,
            }}>
              {x.m}
            </button>
          );
        })}
      </div>
      <span style={{ flex: 1 }} />
      <div style={{ display: "flex", gap: 8 }}>
        <Indicator label="CAPS" on={stickyCaps} />
        <Indicator label="SYM" on={stickySym} />
      </div>
    </div>
  );
}

function Indicator({ label, on }) {
  return (
    <div style={{
      display: "flex", alignItems: "center", gap: 6,
      padding: "4px 10px",
      border: `1px solid ${on ? STRIPE.yellow : "#222"}`,
      background: on ? `${STRIPE.yellow}08` : "transparent",
      borderRadius: 4,
      transition: "all 0.1s",
    }}>
      <span style={{
        width: 6, height: 6, borderRadius: "50%",
        background: on ? STRIPE.yellow : "#111",
        boxShadow: on ? `0 0 6px ${STRIPE.yellow}` : "none",
        transition: "all 0.1s",
      }} />
      <span style={{
        fontFamily: MONO, fontSize: 10, fontWeight: 800,
        color: on ? STRIPE.yellow : DIM,
        letterSpacing: 0.5,
      }}>{label}</span>
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
      border: `1px solid ${LINE}`,
      background: "#111",
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
window.ZXModeStrip = ModeStrip;
window.ZXKeyboard = Keyboard;
})();
