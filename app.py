import os
import time
from datetime import datetime
from threading import Lock, Thread

import cv2
import face_recognition
import numpy as np
import pandas as pd
import requests
from dotenv import load_dotenv
from flask import Flask, Response, flash, jsonify, redirect, render_template, request, session, url_for
from flask_mail import Mail, Message
from flask_socketio import SocketIO
from requests import RequestException


load_dotenv()

app = Flask(__name__)
app.secret_key = os.getenv("SECRET_KEY", "1234")


def env_bool(name, default=False):
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def env_int(name, default):
    value = os.getenv(name)
    if not value:
        return default
    try:
        return int(value)
    except ValueError:
        print(f"Invalid integer for {name}: {value}. Using {default}.")
        return default


# Email configuration
app.config["MAIL_SERVER"] = os.getenv("MAIL_SERVER", "smtp.gmail.com")
app.config["MAIL_PORT"] = env_int("MAIL_PORT", 465)
app.config["MAIL_USE_SSL"] = env_bool("MAIL_USE_SSL", True)
app.config["MAIL_USE_TLS"] = env_bool("MAIL_USE_TLS", False)
app.config["MAIL_USERNAME"] = os.getenv("MAIL_USERNAME")
app.config["MAIL_PASSWORD"] = os.getenv("MAIL_PASSWORD")
app.config["MAIL_DEFAULT_SENDER"] = os.getenv("MAIL_DEFAULT_SENDER") or os.getenv("MAIL_USERNAME")

socketio = SocketIO(app, cors_allowed_origins="*")
mail = Mail(app)

# Paths
CAPTURES_DIR = "captures"
KNOWN_FACES_DIR = "known_faces"
ATTENDANCE_FILE = "attendance_log.xlsx"

# Tunable constants
FACE_MATCH_TOLERANCE = float(os.getenv("FACE_MATCH_TOLERANCE", "0.55"))
ATTENDANCE_UPDATE_INTERVAL_SECONDS = env_int("ATTENDANCE_UPDATE_INTERVAL_SECONDS", 60)
UNKNOWN_ALERT_COOLDOWN_SECONDS = env_int("UNKNOWN_ALERT_COOLDOWN_SECONDS", 60)

DOOR_UNLOCK_COOLDOWN_SECONDS = env_int("DOOR_UNLOCK_COOLDOWN_SECONDS", 15)
DOOR_AUTO_RELOCK_SECONDS = env_int("DOOR_AUTO_RELOCK_SECONDS", 10)
DOOR_COMMAND_COOLDOWN_SECONDS = env_int("DOOR_COMMAND_COOLDOWN_SECONDS", 2)

DOOR_REQUEST_TIMEOUT_SECONDS = env_int("DOOR_REQUEST_TIMEOUT_SECONDS", 5)
DOOR_REQUEST_RETRIES = env_int("DOOR_REQUEST_RETRIES", 2)

FACE_RECOGNITION_FRAME_SKIP = env_int("FACE_RECOGNITION_FRAME_SKIP", 3)

# Telegram alerts
TELEGRAM_BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN")
TELEGRAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID")
TELEGRAM_CHAT_IDS = os.getenv("TELEGRAM_CHAT_IDS")
TELEGRAM_TIMEOUT_SECONDS = env_int("TELEGRAM_TIMEOUT_SECONDS", 10)
TELEGRAM_ALERT_PHONE_NUMBER = os.getenv("TELEGRAM_ALERT_PHONE_NUMBER", "+9779805898412")

# Network settings
SERVER_HOST = os.getenv("SERVER_HOST", "0.0.0.0")
SERVER_PORT = env_int("SERVER_PORT", 5001)

DOOR_CONTROLLER_URL = os.getenv(
    "DOOR_CONTROLLER_URL",
    f"http://{os.getenv('NODEMCU_IP', '192.168.43.19')}",
).rstrip("/")

os.makedirs(CAPTURES_DIR, exist_ok=True)
os.makedirs(KNOWN_FACES_DIR, exist_ok=True)

# Users
users = {
    "gautamabhiyan51@gmail.com": "gammaboy",
    "vvs16@gmail.com": "vvss",
    "gautamaashik111@gmail.com": "techbro",
}

# Face data
known_face_encodings: list = []
known_face_names: list = []

# Locks
attendance_lock = Lock()
updates_lock = Lock()
door_lock = Lock()
alert_lock = Lock()

# Shared state
last_attendance_updates: dict = {}
last_auto_unlock_at: float = 0.0
last_unknown_alert_at: float = 0.0
last_door_command_at: float = 0.0

_door_state = "unknown"
door_state_lock = Lock()


def _set_door_state(state: str):
    global _door_state
    with door_state_lock:
        _door_state = state


def _get_door_state() -> str:
    with door_state_lock:
        return _door_state


# Face loading
def load_known_faces():
    for filename in os.listdir(KNOWN_FACES_DIR):
        if not filename.lower().endswith((".jpg", ".jpeg", ".png")):
            continue
        filepath = os.path.join(KNOWN_FACES_DIR, filename)
        try:
            image = face_recognition.load_image_file(filepath)
            encodings = face_recognition.face_encodings(image)
            if not encodings:
                print(f"No face found in {filename}.")
                continue
            known_face_encodings.append(encodings[0])
            known_face_names.append(os.path.splitext(filename)[0])
            print(f"Loaded face encoding for {filename}.")
        except Exception as e:
            print(f"Error loading {filename}: {e}")


# Attendance
def ensure_attendance_file():
    if not os.path.exists(ATTENDANCE_FILE):
        pd.DataFrame(columns=["Name", "Time", "Status"]).to_excel(ATTENDANCE_FILE, index=False)
        print(f"Created '{ATTENDANCE_FILE}'.")


def unique_names(names):
    seen, result = set(), []
    for name in names:
        if name not in seen:
            seen.add(name)
            result.append(name)
    return result


def append_attendance_rows(rows):
    if not rows:
        return []
    with attendance_lock:
        try:
            df = pd.read_excel(ATTENDANCE_FILE)
        except Exception as e:
            print(f"Error reading attendance file: {e}")
            return []
        df = pd.concat([df, pd.DataFrame(rows)], ignore_index=True)
        try:
            df.to_excel(ATTENDANCE_FILE, index=False)
        except Exception as e:
            print(f"Error writing attendance file: {e}")
            return []
    return rows


def log_attendance_for_faces(face_names):
    rows = []
    now = time.monotonic()
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    for name in unique_names(face_names):
        status = "Known" if name != "Unknown" else "Unknown"
        interval = (
            ATTENDANCE_UPDATE_INTERVAL_SECONDS
            if status == "Known"
            else UNKNOWN_ALERT_COOLDOWN_SECONDS
        )
        with updates_lock:
            last_logged_at = last_attendance_updates.get(name, 0)
            if now - last_logged_at < interval:
                continue
            last_attendance_updates[name] = now

        rows.append({"Name": name, "Time": timestamp, "Status": status})

    written_rows = append_attendance_rows(rows)
    for row in written_rows:
        print(f"Attendance logged: {row['Name']} ({row['Status']}) at {row['Time']}")
        socketio.emit("attendance-event", row)
    return written_rows


# Email and Telegram alerts
def parse_recipients(value):
    if not value:
        return []
    return [item.strip() for item in value.replace(";", ",").split(",") if item.strip()]


def get_alert_recipients(preferred_recipient=None):
    recipients = []
    if preferred_recipient:
        recipients.append(preferred_recipient)
    configured = (
        os.getenv("SECURITY_ALERT_RECIPIENTS")
        or os.getenv("ALERT_EMAILS")
        or os.getenv("ALERT_EMAIL")
    )
    recipients.extend(parse_recipients(configured))
    if not recipients:
        recipients.extend(users.keys())
    deduped = []
    for email in recipients:
        if email not in deduped:
            deduped.append(email)
    return deduped


def mail_is_configured():
    missing = [k for k in ("MAIL_USERNAME", "MAIL_PASSWORD", "MAIL_DEFAULT_SENDER") if not app.config.get(k)]
    if missing:
        print(f"Email alert skipped - missing: {', '.join(missing)}")
        return False
    return True


def telegram_chat_ids():
    ids = parse_recipients(TELEGRAM_CHAT_IDS) or parse_recipients(TELEGRAM_CHAT_ID)
    deduped = []
    for chat_id in ids:
        if chat_id not in deduped:
            deduped.append(chat_id)
    return deduped


def telegram_is_configured():
    missing = []
    if not TELEGRAM_BOT_TOKEN:
        missing.append("TELEGRAM_BOT_TOKEN")
    if not telegram_chat_ids():
        missing.append("TELEGRAM_CHAT_ID")
    if missing:
        print(
            "Telegram alert skipped - missing: "
            + ", ".join(missing)
            + f". The phone {TELEGRAM_ALERT_PHONE_NUMBER} must first start your bot, then use that chat_id here."
        )
        return False
    return True


def send_telegram_request(method, data=None, files=None):
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/{method}"
    response = requests.post(
        url,
        data=data,
        files=files,
        timeout=TELEGRAM_TIMEOUT_SECONDS,
    )
    response.raise_for_status()
    payload = response.json()
    if not payload.get("ok"):
        raise RequestException(payload)
    return payload


def send_unknown_face_telegram(detected_at, image_bytes=None):
    if not telegram_is_configured():
        return

    caption = (
        "Rakshyam alert: unknown face detected\n\n"
        f"Detected at: {detected_at}\n"
        f"Notify: {TELEGRAM_ALERT_PHONE_NUMBER}"
    )

    for chat_id in telegram_chat_ids():
        try:
            if image_bytes:
                send_telegram_request(
                    "sendPhoto",
                    data={"chat_id": chat_id, "caption": caption},
                    files={"photo": ("unknown_face.jpg", image_bytes, "image/jpeg")},
                )
            else:
                send_telegram_request(
                    "sendMessage",
                    data={"chat_id": chat_id, "text": caption},
                )
            print(f"Unknown-face Telegram alert sent to chat {chat_id}.")
            socketio.emit("security-alert", {"message": "Unknown face Telegram alert sent", "time": detected_at})
        except Exception as e:
            print(f"Error sending Telegram unknown-face alert to chat {chat_id}: {e}")
            socketio.emit("security-alert", {"message": "Unknown face Telegram alert failed", "time": detected_at})


def send_unknown_face_email(recipients, detected_at, image_bytes=None):
    if not mail_is_configured():
        return
    with app.app_context():
        try:
            msg = Message(subject="Rakshyam alert: unknown face detected", recipients=recipients)
            msg.body = (
                "An unknown visitor was detected by Rakshyam.\n\n"
                f"Detected at: {detected_at}\n"
                "Please check the live camera feed or the attached snapshot."
            )
            if image_bytes:
                filename = f"unknown_face_{detected_at.replace(':', '-').replace(' ', '_')}.jpg"
                msg.attach(filename, "image/jpeg", image_bytes)
            mail.send(msg)
            print(f"Unknown-face email sent to {', '.join(recipients)}.")
            socketio.emit("security-alert", {"message": "Unknown face alert email sent", "time": detected_at})
        except Exception as e:
            print(f"Error sending unknown-face email: {e}")
            socketio.emit("security-alert", {"message": "Unknown face email failed", "time": detected_at})

# Separate, short cooldown just for taking snapshots (not for email/Telegram)
SNAPSHOT_COOLDOWN_SECONDS = env_int("SNAPSHOT_COOLDOWN_SECONDS", 2)
last_snapshot_at = 0.0
snapshot_lock = Lock()


def capture_unknown_face_snapshot(frame):
    """Save a snapshot of an unknown face. First detection is instant;
    after that, capture is throttled by SNAPSHOT_COOLDOWN_SECONDS.
    """
    global last_snapshot_at

    now = time.monotonic()
    with snapshot_lock:
        if now - last_snapshot_at < SNAPSHOT_COOLDOWN_SECONDS:
            return None, None, None
        last_snapshot_at = now

    detected_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    snapshot_path = os.path.join(
        CAPTURES_DIR,
        f"unknown_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.jpg",
    )

    image_bytes = None
    try:
        cv2.imwrite(snapshot_path, frame)
        ok, jpeg = cv2.imencode(".jpg", frame)
        if ok:
            image_bytes = jpeg.tobytes()
        print(f"Unknown-face snapshot captured: {snapshot_path}")
        socketio.emit("security-alert", {"message": "Unknown face detected - photo captured", "time": detected_at})
    except Exception as e:
        print(f"Could not save unknown-face snapshot: {e}")
        return None, None, detected_at

    return snapshot_path, image_bytes, detected_at


def queue_unknown_face_alert(frame, preferred_recipient=None):
    """Capture a snapshot (instant on first hit, then throttled by
    SNAPSHOT_COOLDOWN_SECONDS). Email/Telegram notifications are throttled
    separately by UNKNOWN_ALERT_COOLDOWN_SECONDS so you don't get spammed.
    """
    global last_unknown_alert_at

    snapshot_path, image_bytes, detected_at = capture_unknown_face_snapshot(frame)
    if snapshot_path is None:
        # Either save failed, or we're inside the 2s snapshot cooldown - skip entirely.
        return False

    now = time.monotonic()
    with alert_lock:
        if now - last_unknown_alert_at < UNKNOWN_ALERT_COOLDOWN_SECONDS:
            return True
        last_unknown_alert_at = now

    recipients = get_alert_recipients(preferred_recipient)
    Thread(target=send_unknown_face_email, args=(recipients, detected_at, image_bytes), daemon=True).start()
    Thread(target=send_unknown_face_telegram, args=(detected_at, image_bytes), daemon=True).start()
    return True

# Door control
def send_door_command(command: str):
    last_error = "Unknown error"
    for attempt in range(max(1, DOOR_REQUEST_RETRIES)):
        try:
            response = requests.get(
                f"{DOOR_CONTROLLER_URL}/{command}",
                timeout=DOOR_REQUEST_TIMEOUT_SECONDS,
            )
            ok = response.status_code < 500
            detail = response.text.strip() or response.reason
            if ok:
                return True, detail
            last_error = f"HTTP {response.status_code}: {detail}"
        except RequestException as e:
            last_error = str(e)
            print(f"Door command '{command}' attempt {attempt + 1} failed: {e}")
            if attempt < DOOR_REQUEST_RETRIES - 1:
                time.sleep(0.5)

    return False, last_error


def _emit_door_event(status: str, message: str, detail: str = ""):
    socketio.emit("door-event", {
        "status": status,
        "message": message,
        "detail": detail,
        "time": datetime.now().strftime("%H:%M:%S"),
    })


def _relock_after_delay(name: str, delay: float):
    time.sleep(delay)
    print(f"Auto-relocking door after {delay}s (was unlocked for {name}).")
    success, detail = send_door_command("open_door")
    if success:
        _set_door_state("locked")
        _emit_door_event("closed", f"Door auto-locked after {int(delay)}s", detail)
        print(f"Auto-relock succeeded: {detail}")
    else:
        _emit_door_event("error", "Auto-relock failed - check NodeMCU", detail)
        print(f"Auto-relock FAILED: {detail}")


def queue_auto_unlock(name: str):
    global last_auto_unlock_at

    now = time.monotonic()
    with door_lock:
        if now - last_auto_unlock_at < DOOR_UNLOCK_COOLDOWN_SECONDS:
            return False
        last_auto_unlock_at = now

    Thread(target=_auto_unlock_door, args=(name,), daemon=True).start()
    return True


def _auto_unlock_door(name: str):
    success, detail = send_door_command("close_door")
    if success:
        _set_door_state("unlocked")
        message = f"Door auto-unlocked for {name}"
        _emit_door_event("open", message, detail)
        print(f"{message}: {detail}")
        Thread(target=_relock_after_delay, args=(name, DOOR_AUTO_RELOCK_SECONDS), daemon=True).start()
    else:
        message = f"Auto-unlock failed for {name}"
        _emit_door_event("error", message, detail)
        print(f"{message}: {detail}")


# Face recognition helpers
def recognize_faces(frame):
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    face_locations = face_recognition.face_locations(rgb_frame)
    face_encodings = face_recognition.face_encodings(rgb_frame, face_locations)

    face_names = []
    for face_encoding in face_encodings:
        name = "Unknown"
        if known_face_encodings:
            matches = face_recognition.compare_faces(
                known_face_encodings,
                face_encoding,
                tolerance=FACE_MATCH_TOLERANCE,
            )
            face_distances = face_recognition.face_distance(known_face_encodings, face_encoding)
            if len(face_distances) > 0:
                best = int(np.argmin(face_distances))
                if matches[best]:
                    name = known_face_names[best]
        face_names.append(name)

    return face_locations, face_names


def draw_face_labels(frame, face_locations, face_names):
    for (top, right, bottom, left), name in zip(face_locations, face_names):
        color = (35, 158, 47) if name != "Unknown" else (43, 57, 192)
        cv2.rectangle(frame, (left, top), (right, bottom), color, 2)
        cv2.putText(frame, name, (left, top - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)


def handle_face_events(face_names, frame, preferred_recipient=None):
    logged_rows = log_attendance_for_faces(face_names)
    known_names = [n for n in unique_names(face_names) if n != "Unknown"]
    # if known_names:
        # queue_auto_unlock(known_names[0])
    if "Unknown" in face_names:
        queue_unknown_face_alert(frame, preferred_recipient)
    return logged_rows


# Startup
load_known_faces()
ensure_attendance_file()

camera = cv2.VideoCapture(0)
if not camera.isOpened():
    print("Error: Camera not initialised. Ensure it is connected.")
else:
    print("Camera initialised successfully.")

camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)


# Routes
@app.route("/", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        username = request.form.get("username", "").strip()
        password = request.form.get("password", "")
        if username in users and users[username] == password:
            session["username"] = username
            append_attendance_rows([{
                "Name": username,
                "Time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "Status": "Logged In",
            }])
            print(f"User '{username}' logged in.")
            return redirect(url_for("dashboard"))
        flash("Invalid username or password", "error")
    return render_template("login.html")


@app.route("/dashboard")
def dashboard():
    if "username" not in session:
        return redirect(url_for("login"))
    return render_template("index.html", username=session["username"])


@app.route("/capture_motion", methods=["GET"])
@app.route("/captures", methods=["GET"])
def capture_motion():
    try:
        ret, frame = camera.read()
        if not ret:
            return jsonify(error="Camera error"), 500
        face_locations, face_names = recognize_faces(frame)
        if not face_names:
            return jsonify(message="No faces detected", faces=[]), 200
        logged_rows = handle_face_events(face_names, frame, session.get("username"))
        return jsonify(message="Captured and processed", faces=face_names, logged=logged_rows), 200
    except Exception as e:
        print(f"Error in capture_motion: {e}")
        return jsonify(error="Internal server error"), 500


@app.route("/video_feed")
def video_feed():
    preferred_recipient = session.get("username")

    def gen():
        frame_counter = 0
        last_locations = []
        last_names = []

        while True:
            ret, frame = camera.read()
            if not ret:
                print("Camera read failed - stopping stream.")
                break

            frame_counter += 1
            if frame_counter % max(1, FACE_RECOGNITION_FRAME_SKIP) == 0:
                try:
                    last_locations, last_names = recognize_faces(frame)
                    if last_names:
                        handle_face_events(last_names, frame, preferred_recipient)
                except Exception as e:
                    print(f"Error in face recognition: {e}")

            draw_face_labels(frame, last_locations, last_names)

            ret, jpeg = cv2.imencode(".jpg", frame)
            if ret:
                yield (
                    b"--frame\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n" + jpeg.tobytes() + b"\r\n"
                )

    return Response(gen(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/open")
def open_door():
    global last_door_command_at

    now = time.monotonic()
    with door_lock:
        if now - last_door_command_at < DOOR_COMMAND_COOLDOWN_SECONDS:
            remaining = round(DOOR_COMMAND_COOLDOWN_SECONDS - (now - last_door_command_at), 1)
            return jsonify(ok=False, status="Cooldown", detail=f"Wait {remaining}s before next command"), 429
        last_door_command_at = now

    success, detail = send_door_command("close_door")
    status_str = "open" if success else "error"
    msg = "Door opened" if success else "Door open failed"

    if success:
        _set_door_state("unlocked")
        Thread(
            target=_relock_after_delay,
            args=("manual open", DOOR_AUTO_RELOCK_SECONDS),
            daemon=True,
        ).start()

    _emit_door_event(status_str, msg, detail)
    return jsonify(ok=success, status="Door Opened" if success else "Failed", detail=detail), 200 if success else 502


@app.route("/close_door")
def close_door():
    global last_door_command_at

    now = time.monotonic()
    with door_lock:
        if now - last_door_command_at < DOOR_COMMAND_COOLDOWN_SECONDS:
            remaining = round(DOOR_COMMAND_COOLDOWN_SECONDS - (now - last_door_command_at), 1)
            return jsonify(ok=False, status="Cooldown", detail=f"Wait {remaining}s before next command"), 429
        last_door_command_at = now

    success, detail = send_door_command("open_door")
    status_str = "closed" if success else "error"
    msg = "Door closed" if success else "Door close failed"

    if success:
        _set_door_state("locked")

    _emit_door_event(status_str, msg, detail)
    return jsonify(ok=success, status="Door Closed" if success else "Failed", detail=detail), 200 if success else 502


@app.route("/status")
def door_status():
    nodemcu_status = None
    try:
        response = requests.get(
            f"{DOOR_CONTROLLER_URL}/status",
            timeout=DOOR_REQUEST_TIMEOUT_SECONDS,
        )
        if response.status_code < 500:
            raw = response.text.strip()
            try:
                nodemcu_status = response.json()
            except ValueError:
                nodemcu_status = {"raw": raw}
    except RequestException as e:
        print(f"/status NodeMCU unreachable: {e}")

    local_state = _get_door_state()

    if nodemcu_status is not None:
        return jsonify(status={
            "source": "nodemcu",
            "nodemcu": nodemcu_status,
            "local_state": local_state,
        })

    return jsonify(status={
        "source": "local",
        "local_state": local_state,
        "warning": "NodeMCU unreachable; showing last known state",
    }), 200


@app.route("/logout")
def logout():
    session.pop("username", None)
    print("User logged out.")
    return redirect(url_for("login"))


@app.route("/myip")
def my_ip():
    import socket

    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    lan_ips = []
    try:
        for info in socket.getaddrinfo(hostname, None):
            ip = info[4][0]
            if ip not in lan_ips and not ip.startswith("127.") and ":" not in ip:
                lan_ips.append(ip)
    except Exception:
        lan_ips = [local_ip]
    return jsonify({
        "hostname": hostname,
        "primary_ip": local_ip,
        "all_lan_ips": lan_ips,
        "access_url": [f"http://{ip}:{SERVER_PORT}" for ip in lan_ips],
    })


# SocketIO
@socketio.on("user-message")
def handle_message(data):
    if isinstance(data, dict):
        message = data.get("message", "")
        username = data.get("username", "User")
    else:
        message = str(data)
        username = session.get("username", "User")

    if not message.strip():
        return

    socketio.emit("message", {"message": message, "username": username, "type": "user"})


# Entry point
if __name__ == "__main__":
    import socket

    hostname = socket.gethostname()
    try:
        lan_ips = []
        for info in socket.getaddrinfo(hostname, None):
            ip = info[4][0]
            if ip not in lan_ips and not ip.startswith("127.") and ":" not in ip:
                lan_ips.append(ip)
    except Exception:
        lan_ips = []

    print("\n" + "=" * 55)
    print(f"  Rakshyam starting on  {SERVER_HOST}:{SERVER_PORT}")
    print("  Open on this machine :  http://127.0.0.1:" + str(SERVER_PORT))
    for ip in lan_ips:
        print(f"  Open on LAN devices  :  http://{ip}:{SERVER_PORT}")
    print("=" * 55 + "\n")

    socketio.run(
        app,
        host=SERVER_HOST,
        port=SERVER_PORT,
        debug=env_bool("FLASK_DEBUG", False),
        use_reloader=False,
        allow_unsafe_werkzeug=True,
    )
