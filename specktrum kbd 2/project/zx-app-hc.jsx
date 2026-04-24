/* global React */
(function(){
const { useState, useEffect, useCallback } = React;
const { BG, INK, DIM, MONO, STRIPE } = window.ZX_STYLE_HC;
const StatusBar = window.ZXStatusBarHC;
const OutputStrip = window.ZXOutputStripHC;
const ModeStrip = window.ZXModeStripHC;
const Keyboard = window.ZXKeyboardHC;

const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "host": "ws://192.168.1.42:81",
  "autoConnect": true
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

  useEffect(() => {
    if (TWEAK_DEFAULTS.autoConnect) {
      const t = setTimeout(() => setConnected(true), 700);
      return () => clearTimeout(t);
    }
  }, []);

  useEffect(() => {
    if (!connected) return;
    const t = setInterval(() => setLatency(8 + Math.floor(Math.random() * 14)), 1500);
    return () => clearInterval(t);
  }, [connected]);

  const emit = useCallback((k) => {
    let out = "";
    if (k.special === "caps")   { setStickyCaps(s => !s); return; }
    if (k.special === "symbol") { setStickySym(s => !s); return; }
    if (k.special === "enter")  { out = "\n"; }
    else if (k.special === "space") {
      if (stickyCaps && stickySym) { out = "[BREAK]"; setStickyCaps(false); setStickySym(false); }
      else out = " ";
    } else {
      if (mode === "K" && k.keyword) {
        out = k.keyword + " ";
        setMode("L");
      } else if (stickySym && k.symbolRed) {
        out = k.symbolRed;
        setStickySym(false);
      } else if (mode === "E" && k.eModeRed) {
        out = k.eModeRed + " ";
        setMode("L");
      } else {
        out = stickyCaps ? k.main.toUpperCase() : k.main.toLowerCase();
        if (k.main.length === 1 && /[A-Z]/.test(k.main) && !stickyCaps) out = k.main;
        setStickyCaps(false);
      }
    }
    setBuffer(b => (b + out).slice(-120));
    setTx(n => n + 1);
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
      padding: 24,
      boxSizing: "border-box",
      display: "flex", flexDirection: "column", gap: 20,
    }}>
      {/* header */}
      <div style={{ display: "flex", alignItems: "center", gap: 18 }}>
        <div style={{
          display: "flex", alignItems: "baseline", gap: 14,
          padding: "0 4px",
        }}>
          <div style={{ fontSize: 24, fontWeight: 900, letterSpacing: 5, color: INK }}>ZX/KBD</div>
          <div style={{ fontSize: 13, color: INK, letterSpacing: 2, fontWeight: 700 }}>HC · v1.0</div>
          <div style={{ display: "flex", gap: 0, marginLeft: 8 }}>
            {["red","yellow","green","cyan"].map(c => (
              <div key={c} style={{ width: 18, height: 6, background: STRIPE[c] }} />
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

      <OutputStrip buffer={buffer} />
      <ModeStrip
        mode={mode} setMode={setMode}
        stickyCaps={stickyCaps} stickySym={stickySym}
      />

      <div style={{ display: "flex", justifyContent: "center", marginTop: 6 }}>
        <Keyboard
          onPress={emit}
          stickyCaps={stickyCaps} stickySym={stickySym}
          mode={mode}
        />
      </div>

      <div style={{
        display: "flex", justifyContent: "space-between", alignItems: "center",
        fontSize: 13, color: INK, padding: "4px 6px", letterSpacing: 0.5, fontWeight: 700,
      }}>
        <span>HIGH CONTRAST · large keys · thick borders · yellow flash on press</span>
        <span>haptic feedback on supported devices</span>
      </div>

      <style>{`
        @keyframes zx-blink {
          0%, 50% { opacity: 1; }
          50.01%, 100% { opacity: 0; }
        }
        button:focus-visible { outline: 4px solid ${STRIPE.yellow}; outline-offset: 2px; }
        body, html { background: ${BG}; margin: 0; }
        * { -webkit-tap-highlight-color: transparent; }
      `}</style>
    </div>
  );
}

const root = ReactDOM.createRoot(document.getElementById("root"));
root.render(<App />);
})();
