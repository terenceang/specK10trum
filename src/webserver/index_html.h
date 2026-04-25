#ifndef INDEX_HTML_H
#define INDEX_HTML_H

static const char* INDEX_HTML_START = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>SpecK10trum Fallback</title>
<style>
  body { background: #000; color: #fff; font-family: monospace; padding: 20px; text-align: center; }
  .error { color: #f44; margin: 20px; border: 1px solid #444; padding: 20px; }
</style>
</head>
<body>
<div class="error">
  <h2>SPIFFS /www Missing</h2>
  <p>The virtual keyboard files were not found on the device storage.</p>
  <p>Please run: <b>pio run -t uploadfs</b></p>
</div>
</body>
</html>
)rawliteral";

#endif
