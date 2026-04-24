/* global React */
(function(){
const { useState, useEffect, useCallback } = React;
const { STRIPE } = window.ZX_LAYOUT;
const { BG, INK, DIM, MONO } = window.ZX_STYLE;
const StatusBar = window.ZXStatusBar;
const OutputStrip = window.ZXOutputStrip;
const ModeStrip = window.ZXModeStrip;
const Keyboard = window.ZXKeyboard;

// Tweakable defaults — persisted via edit-mode protocol
const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "host": "ws://192.168.1.42:81",
  "autoConnect": true,
  "audioClick": false,
  "showStripe": true
}/*EDITMODE-END*/;

function App() {
  const [connected, setConnected] = useState(false);
  const [latency, setLatency] = useState(12);
  const [tx, setTx] = useState(0);
  const [buffer, setBuffer] = useState("");
  const [mode, setMode] = useState("K");
  const [stickyCaps, setStickyCaps] = useState(false);
  const [stickySym, setStickySym] = useState(false);
  const host = TWEAK_DEFAULTS.host;

  // simulated connection (in real deploy this would be a real WebSocket)
  useEffect(() => {
    if (TWEAK_DEFAULTS.autoConnect) {
      const t = setTimeout(() => setConnected(true), 700);
      return () => clearTimeout(t);
    }
  }, []);

  // simulated latency jitter
  useEffect(() => {
    if (!connected) return;
    const t = setInterval(() => setLatency(8 + Math.floor(Math.random() * 14)), 1500);
    return () => clearInterval(t);
  }, [connected]);

  const emit = useCallback((k) => {
    // resolve what to send based on mode + stickies
    let out = "";
    if (k.special === "caps")   { setStickyCaps(s => !s); return; }
    if (k.special === "symbol") { setStickySym(s => !s); return; }
    if (k.special === "enter")  { out = "\n"; }
    else if (k.special === "space") {
      if (stickyCaps && stickySym) { out = "[BREAK]"; setStickyCaps(false); setStickySym(false); }
      else out = " ";
    } else {
      // keyword mode → if key has a keyword, send that as a token
      if (mode === "K" && k.keyword) {
        out = k.keyword + " ";
        setMode("L"); // after emitting a keyword, drop back to letter mode (authentic)
      } else if (stickySym && k.symbolRed) {
        out = k.symbolRed;
        setStickySym(false);
      } else if (mode === "E" && k.eModeRed) {
        out = k.eModeRed + " ";
        setMode("L");
      } else {
        out = stickyCaps ? k.main.toUpperCase() : k.main.toLowerCase();
        if (k.main.length === 1 && /[A-Z]/.test(k.main) && !stickyCaps) {
          // letters default uppercase on spectrum
          out = k.main;
        }
        setStickyCaps(false);
      }
    }

    setBuffer(b => (b + out).slice(-120));
    setTx(n => n + 1);

    // here a real build would do: ws.send(JSON.stringify({ code: k.code, mods: {caps: stickyCaps, sym: stickySym}, mode }));
  }, [mode, stickyCaps, stickySym]);

  const toggleConn = () => setConnected(c => !c);
  const goFullscreen = () => {
    const el = document.documentElement;
    if (!document.fullscreenElement && el.requestFullscreen) el.requestFullscreen();
    else if (document.exitFullscreen) document.exitFullscreen();
  };

  return (
    <div style={{
      minHeight: "100vh",
      background: BG,
      color: INK,
      fontFamily: MONO,
      padding: 18,
      boxSizing: "border-box",
      display: "flex", flexDirection: "column", gap: 14,
    }}>
      {/* header row — product mark + status bar */}
      <div style={{ display: "flex", alignItems: "center", gap: 14 }}>
        <div style={{
          display: "flex", alignItems: "baseline", gap: 10,
          padding: "0 4px",
        }}>
          <div style={{ fontSize: 16, fontWeight: 800, letterSpacing: 4, color: INK }}>ZX/KBD</div>
          <div style={{ fontSize: 10, color: DIM, letterSpacing: 2 }}>v1.0 · ESP32-S3 · WS</div>
          {/* mini rainbow */}
          <div style={{ display: "flex", gap: 0, marginLeft: 6 }}>
            {["red","yellow","green","cyan"].map(c => (
              <div key={c} style={{ width: 12, height: 3, background: STRIPE[c] }} />
            ))}
          </div>
        </div>
        <div style={{ flex: 1 }}>
          <StatusBar
            connected={connected} latency={latency} txCount={tx}
            host={host} onToggleConn={toggleConn} onFullscreen={goFullscreen}
          />
        </div>
      </div>

      {/* output preview */}
      <OutputStrip buffer={buffer} />

      {/* mode + sticky strip */}
      <ModeStrip
        mode={mode} setMode={setMode}
        stickyCaps={stickyCaps} stickySym={stickySym}
      />

      {/* keyboard */}
      <div style={{ display: "flex", justifyContent: "center", marginTop: 4 }}>
        <Keyboard
          onPress={emit}
          stickyCaps={stickyCaps} stickySym={stickySym}
          mode={mode}
        />
      </div>

      {/* footer hint */}
      <div style={{
        display: "flex", justifyContent: "space-between", alignItems: "center",
        fontSize: 10, color: DIM, padding: "4px 6px", letterSpacing: 0.5,
      }}>
        <span>sticky modifiers — tap CAPS or SYM, next keystroke is modified. K mode emits BASIC keywords.</span>
        <span>haptic feedback on supported devices</span>
      </div>

      <style>{`
        @keyframes zx-blink {
          0%, 50% { opacity: 1; }
          50.01%, 100% { opacity: 0; }
        }
        button:hover { border-color: ${INK} !important; color: ${INK} !important; }
        body, html { background: ${BG}; margin: 0; }
        * { -webkit-tap-highlight-color: transparent; }
      `}</style>
    </div>
  );
}

const root = ReactDOM.createRoot(document.getElementById("root"));
root.render(<App />);
})();
