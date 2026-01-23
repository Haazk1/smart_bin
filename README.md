# SmartBin – AI-Based Waste Sorting Bin (ESP32 + Gemini)

SmartBin is an AI-assisted waste sorting prototype built for sustainability education.  
It captures an image using an **ESP32-CAM**, sends the image to a local Python server for **Gemini (Google GenAI) classification**, then moves a **stepper-driven sorting mechanism** to route the item into the correct bin.

> **Category mapping (current build):**  
> `Plastic = 1`, `Paper = 2`, `Metal = 3`, `Trash = 4`

---

## Architecture (How everything connects)

**ESP32-CAM (Camera Web Server)**  
- Hosts `GET /capture` (JPEG image)

**ESP32 Display/Controller (UI + Scan + Networking)**  
- `GET` image from ESP32-CAM (`/capture`)
- `POST` JPEG to Python server (`/upload`)
- Parses Gemini text → converts to command `1..4`
- Sends command to Motor ESP32 via UART (Serial2)

**Python Server (Flask + Tkinter GUI + Gemini API)**  
- Receives JPEG on `POST /upload`
- Sends image to Gemini model (`gemini-2.5-flash`)
- Returns a text result:
  ```
  Item: <name>
  Category: Plastic | Paper | Metal | Trash
  ```

**ESP32 Motor Controller (Stepper + Limit Switches + Servo)**  
- Receives `1\n .. 4\n` via UART (Serial2)
- Moves stepper until the correct microswitch is hit
- Triggers a servo “drop” action (dispense)

---

## Repository Files

```
.
├── esp32camcode.ino      # ESP32-CAM: Wi-Fi + /capture JPEG endpoint
├── VROOMdisp.ino         # ESP32 UI/Controller: OLED + ultrasonic + joystick + HTTP + UART
├── VROOMMOtor.ino        # ESP32 Motor: L298N stepper + 4 microswitches + servo + UART
└── ppppserv.py           # Python: Flask /upload + Tkinter preview + Gemini classification
```

---

## Hardware (as used in code)

### 1) ESP32-CAM board
- Flash LED: **GPIO4**
- Camera endpoint: `http://<CAM_IP>/capture`

### 2) ESP32 Display/Controller board
- OLED: **SSD1306 (128×64) over I2C**
- Ultrasonic: `TRIG=GPIO5`, `ECHO=GPIO18`
- Joystick: `VRX=GPIO39`, `VRY=GPIO36`, `SW=GPIO25`
- UART to Motor ESP32 (Serial2): `RX2=GPIO16`, `TX2=GPIO17`
- Reads image from: `cam_url`
- Sends image to: `server_url`

### 3) ESP32 Motor board
- Stepper driver module: **L298N (HW-095)** (4-wire stepper via IN1–IN4)
  - `IN1=GPIO14`, `IN2=GPIO27`, `IN3=GPIO26`, `IN4=GPIO25`
- Microswitches (NO type; pressed = LOW):
  - `SW1=GPIO36`, `SW2=GPIO39`, `SW3=GPIO34`, `SW4=GPIO35`
  - ⚠️ GPIO34–39 have **no internal pull-ups** → use external pull-up (e.g., 10k to 3.3V)
- Servo:
  - `SERVO_PIN=GPIO4` (Motor board)

> Note: GPIO numbers above are from the source code. Update them if you rewire.

---

## Quick Start (Recommended order)

### Step 0 — Put all devices on the same Wi‑Fi
In the `.ino` files, update:
- `ssid`
- `password`

### Step 1 — Flash ESP32‑CAM (`esp32camcode.ino`)
1. Open `esp32camcode.ino` in Arduino IDE  
2. Select the correct ESP32‑CAM board + COM port  
3. Upload
4. Check Serial Monitor for the **Camera IP address**
5. Test in a browser:
   - `http://<CAM_IP>/capture`  
   You should see/download a JPEG.

### Step 2 — Run the Python AI server (`ppppserv.py`)
#### Install dependencies
```bash
pip install flask pillow google-genai
```

#### Run
```bash
python ppppserv.py
```

The app will:
- Start Flask on `http://0.0.0.0:8000`
- Open a Tkinter window for live preview + Gemini results

### Step 3 — Flash Display/Controller ESP32 (`VROOMdisp.ino`)
1. Update:
   - `cam_url` → `http://<CAM_IP>/capture`
   - `server_url` → `http://<PC_IP>:8000/upload`
2. Upload
3. Use the OLED menu to go to **Scan**  
4. When an object is within ~15 cm (ultrasonic), it auto-scans once.

### Step 4 — Flash Motor ESP32 (`VROOMMOtor.ino`)
1. Upload to the motor-controller ESP32
2. Ensure UART wiring + common ground:
   - Display ESP32 `TX2 (GPIO17)` → Motor ESP32 `RX2 (GPIO16)`
   - GND ↔ GND
3. Send test commands via Serial Monitor on Motor ESP32:
   - Type `1`, `2`, `3`, or `4` + Enter

---

## Endpoints & Data Flow

### ESP32‑CAM
- `GET /capture` → returns **image/jpeg**

### Python server
- `POST /upload` (raw JPEG body) → returns **plain text** Gemini result

The Display ESP32 does:
1. `GET cam_url` → gets full JPEG  
2. `POST server_url` with header `Content-Type: image/jpeg`  
3. Reads response text → maps to `1..4` → sends UART command

---

## Tuning / Customisation

### Category → Command mapping
In `VROOMdisp.ino`:
```cpp
// plastic=1 paper=2 metal=3 trash=4
int categoryToCmd(String s) { ... }
```

### Stepper speed
In `VROOMMOtor.ino`:
- `stepIntervalMs` (higher = slower)
- `MAX_STEPS` (safety limit)

### Servo drop timing
In `VROOMMOtor.ino`:
- `SERVO_DROP_MS` (default 5000 ms)

---

## Security Note (IMPORTANT)
Your Python file currently contains a hardcoded Gemini API key.  
For safety:
1. **Remove the key from code**
2. Store it as an environment variable (e.g., `GEMINI_API_KEY`)
3. **Regenerate/rotate the exposed key** in Google Cloud / AI Studio

(Do not commit API keys to public repos.)

---

## Troubleshooting

### “Upload failed” / can’t reach Python server
- Confirm PC IP is correct in `server_url`
- Allow port **8000** through firewall
- Ensure ESP32 and PC are on the same network

### Camera GET fails / incomplete JPEG
- Ensure `cam_url` points to the correct IP
- Reduce camera frame size or JPEG quality if needed

### Motor doesn’t move / overshoots
- Check external motor power supply
- Verify microswitch wiring and pull-ups for GPIO34–39
- Reduce speed (`stepIntervalMs`) or adjust `MAX_STEPS`

### Category not detected
- Ensure Gemini output contains “Plastic/Paper/Metal/Trash”
- Improve lighting or adjust Gemini prompt in `ppppserv.py`

---

## License
Educational / prototype use.

