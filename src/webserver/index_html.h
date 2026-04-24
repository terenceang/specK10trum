#pragma once

static const char* INDEX_HTML_START = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>speck10trum Virtual Keyboard</title>
    <style>
        body { font-family: sans-serif; background: #222; color: #eee; text-align: center; }
        .keyboard { display: grid; grid-template-columns: repeat(10, 1fr); gap: 5px; max-width: 600px; margin: 20px auto; padding: 10px; background: #444; border-radius: 10px; }
        .key { padding: 15px 5px; background: #666; border-radius: 5px; cursor: pointer; user-select: none; font-weight: bold; }
        .key:active { background: #999; }
        .key.pressed { background: #aaa; color: #000; }
        .status { margin-top: 10px; font-size: 0.8em; color: #aaa; }
    </style>
</head>
<body>
    <h2>speck10trum Keyboard</h2>
    <div id="status" class="status">Connecting...</div>
    <div class="keyboard" id="kb">
        <!-- Row 0: 1-5 (Left) -->
        <div class="key" data-row="3" data-bit="0">1</div>
        <div class="key" data-row="3" data-bit="1">2</div>
        <div class="key" data-row="3" data-bit="2">3</div>
        <div class="key" data-row="3" data-bit="3">4</div>
        <div class="key" data-row="3" data-bit="4">5</div>
        <!-- Row 4: 6-0 (Right) -->
        <div class="key" data-row="4" data-bit="4">6</div>
        <div class="key" data-row="4" data-bit="3">7</div>
        <div class="key" data-row="4" data-bit="2">8</div>
        <div class="key" data-row="4" data-bit="1">9</div>
        <div class="key" data-row="4" data-bit="0">0</div>

        <!-- Row 2: Q-T (Left) -->
        <div class="key" data-row="2" data-bit="0">Q</div>
        <div class="key" data-row="2" data-bit="1">W</div>
        <div class="key" data-row="2" data-bit="2">E</div>
        <div class="key" data-row="2" data-bit="3">R</div>
        <div class="key" data-row="2" data-bit="4">T</div>
        <!-- Row 5: Y-P (Right) -->
        <div class="key" data-row="5" data-bit="4">Y</div>
        <div class="key" data-row="5" data-bit="3">U</div>
        <div class="key" data-row="5" data-bit="2">I</div>
        <div class="key" data-row="5" data-bit="1">O</div>
        <div class="key" data-row="5" data-bit="0">P</div>

        <!-- Row 1: A-G (Left) -->
        <div class="key" data-row="1" data-bit="0">A</div>
        <div class="key" data-row="1" data-bit="1">S</div>
        <div class="key" data-row="1" data-bit="2">D</div>
        <div class="key" data-row="1" data-bit="3">F</div>
        <div class="key" data-row="1" data-bit="4">G</div>
        <!-- Row 6: H-ENT (Right) -->
        <div class="key" data-row="6" data-bit="4">H</div>
        <div class="key" data-row="6" data-bit="3">J</div>
        <div class="key" data-row="6" data-bit="2">K</div>
        <div class="key" data-row="6" data-bit="1">L</div>
        <div class="key" data-row="6" data-bit="0">ENT</div>

        <!-- Row 0: SHIFT-V (Left) -->
        <div class="key" data-row="0" data-bit="0">CAPS</div>
        <div class="key" data-row="0" data-bit="1">Z</div>
        <div class="key" data-row="0" data-bit="2">X</div>
        <div class="key" data-row="0" data-bit="3">C</div>
        <div class="key" data-row="0" data-bit="4">V</div>
        <!-- Row 7: B-SPACE (Right) -->
        <div class="key" data-row="7" data-bit="4">B</div>
        <div class="key" data-row="7" data-bit="3">N</div>
        <div class="key" data-row="7" data-bit="2">M</div>
        <div class="key" data-row="0" data-bit="1">SYM</div>
        <div class="key" data-row="7" data-bit="0">SPC</div>
    </div>

    <script>
        const status = document.getElementById('status');
        const ws = new WebSocket('ws://' + location.host + '/ws');
        
        ws.onopen = () => status.innerText = 'Connected';
        ws.onclose = () => status.innerText = 'Disconnected';
        
        function sendKey(row, bit, isPressed) {
            if (ws.readyState === WebSocket.OPEN) {
                const data = new Uint8Array([row, bit, isPressed ? 1 : 0]);
                ws.send(data);
            }
        }

        document.querySelectorAll('.key').forEach(el => {
            const row = parseInt(el.dataset.row);
            const bit = parseInt(el.dataset.bit);
            
            const handlePress = (e) => {
                e.preventDefault();
                el.classList.add('pressed');
                sendKey(row, bit, true);
            };
            const handleRelease = (e) => {
                e.preventDefault();
                el.classList.remove('pressed');
                sendKey(row, bit, false);
            };

            el.addEventListener('mousedown', handlePress);
            el.addEventListener('mouseup', handleRelease);
            el.addEventListener('touchstart', handlePress);
            el.addEventListener('touchend', handleRelease);
        });
    </script>
</body>
</html>
)rawliteral";
