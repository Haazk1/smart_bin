import io
import time
from threading import Thread, Lock
from tkinter import Tk, Label, Text
from PIL import Image, ImageTk, ImageFile
from flask import Flask, request
from google import genai
from google.genai import types

ImageFile.LOAD_TRUNCATED_IMAGES = True

app = Flask(__name__)

GEMINI_API_KEY = "AIzaSyABkO2Xa6Gg3gEVCHKEK16auiQiIVxVmBs"
GEMINI_MODEL = "gemini-2.5-flash"
client = genai.Client(api_key=GEMINI_API_KEY)

root = Tk()
root.title("ESP32-CAM Gemini Detection")

result_text = Text(root, height=10, width=60)
result_text.pack(pady=10)

image_label = Label(root)
image_label.pack()

latest_image = None
lock = Lock()
processing = False
trigger_ready = True
gemini_result = "Waiting..."


# -------------------------------------
# Utility: Ensure full JPEG is loaded
# -------------------------------------
def wait_for_full_jpeg(img_bytes, max_retries=5):
    for attempt in range(max_retries):
        try:
            img = Image.open(io.BytesIO(img_bytes))
            img.load()
            return img.copy()
        except:
            print(f"⚠️ JPEG not fully loaded ({attempt+1}/{max_retries})")
            time.sleep(0.02)
    print("❌ Failed to decode JPEG")
    return None


# -------------------------------------
# Gemini AI classification
# -------------------------------------
def analyze_image(img_bytes):
    global processing, trigger_ready, gemini_result

    try:
        result_text.insert("1.0", "🤖 Sending to Gemini...\n")

        # Decode JPEG
        img = Image.open(io.BytesIO(img_bytes))
        img.load()

        # 🔥 Rotate image upside-down
        flipped = img.rotate(180)

        # Convert rotated image back to JPEG bytes
        buf = io.BytesIO()
        flipped.save(buf, format="JPEG")
        flipped_bytes = buf.getvalue()

        # Ask Gemini
        response = client.models.generate_content(
            model=GEMINI_MODEL,
            contents=[
                types.Part.from_text(
                    text= "Identify EXACTLY what item is in the image just interprete the item and just whats the basic interpretation of that item like if nuts then metal (e.g., Coca-Cola can, plastic bottle, tissue, cardboard box). "
                    "Then on the next line, output ONLY its recycling category as one word: Plastic, Paper, Metal, or Trash.If not any just put trash but try to fit the items in one of the3 not put everything trash pls "
                    "Format:\n"
                    "Item: <name>\n"
                    "Category: <one word>"

                ),
                types.Part.from_bytes(
                    data=flipped_bytes,
                    mime_type="image/jpeg"
                )
            ]
        )

        gemini_result = response.text

        result_text.insert("1.0", "✅ Response:\n" + gemini_result + "\n\n")

    except Exception as e:
        gemini_result = f"Error: {e}"
        result_text.insert("1.0", f"❌ Gemini error: {e}\n\n")

    finally:
        processing = False
        trigger_ready = True


# -------------------------------------
# GUI live preview of ESP32-CAM image
# -------------------------------------
def update_gui():
    global latest_image

    with lock:
        if latest_image is None:
            root.after(30, update_gui)
            return
        img_bytes = latest_image

    img = wait_for_full_jpeg(img_bytes)
    if img is None:
        root.after(30, update_gui)
        return

    # 🔥 Rotate preview
    img = img.rotate(180)

    img.thumbnail((400, 300))
    img_tk = ImageTk.PhotoImage(img)

    image_label.config(image=img_tk)
    image_label.image = img_tk

    root.after(30, update_gui)


# -------------------------------------
# Web server endpoint for ESP32
# -------------------------------------
@app.route("/upload", methods=["POST"])
def upload_image():
    global latest_image, trigger_ready, processing, gemini_result

    if not trigger_ready:
        return "BUSY", 200

    img_bytes = request.get_data(cache=False, parse_form_data=False)

    with lock:
        latest_image = img_bytes
        processing = True
        trigger_ready = False

    Thread(target=analyze_image, args=(img_bytes,), daemon=True).start()

    while processing:
        time.sleep(0.01)

    return gemini_result, 200


# -------------------------------------
# Run Flask in a background thread
# -------------------------------------
def run_flask():
    app.run(host="0.0.0.0", port=8000, debug=False, use_reloader=False)

Thread(target=run_flask, daemon=True).start()

root.after(50, update_gui)
root.mainloop()
