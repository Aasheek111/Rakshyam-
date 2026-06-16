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


# Email configuration. Gmail requires an app password, not the normal account password.
mail_use_tls = env_bool("MAIL_USE_TLS", False)
app.config["MAIL_SERVER"] = os.getenv("MAIL_SERVER", "smtp.gmail.com")
app.config["MAIL_PORT"] = env_int("MAIL_PORT", 465)
app.config["MAIL_USE_TLS"] = mail_use_tls
app.config["MAIL_USE_SSL"] = env_bool("MAIL_USE_SSL", not mail_use_tls)
app.config["MAIL_USERNAME"] = os.getenv("MAIL_USERNAME")
app.config["MAIL_PASSWORD"] = os.getenv("MAIL_PASSWORD")
app.config["MAIL_DEFAULT_SENDER"] = os.getenv("MAIL_DEFAULT_SENDER") or os.getenv("MAIL_USERNAME")

socketio = SocketIO(app)
mail = Mail(app)

CAPTURES_DIR = "captures"
KNOWN_FACES_DIR = "known_faces"
ATTENDANCE_FILE = "attendance_log.xlsx"

FACE_MATCH_TOLERANCE = float(os.getenv("FACE_MATCH_TOLERANCE", "0.6"))
ATTENDANCE_UPDATE_INTERVAL_SECONDS = env_int("ATTENDANCE_UPDATE_INTERVAL_SECONDS", 60)
UNKNOWN_ALERT_COOLDOWN_SECONDS = env_int("UNKNOWN_ALERT_COOLDOWN_SECONDS", 60)
DOOR_UNLOCK_COOLDOWN_SECONDS = env_int("DOOR_UNLOCK_COOLDOWN_SECONDS", 10)
DOOR_REQUEST_TIMEOUT_SECONDS = env_int("DOOR_REQUEST_TIMEOUT_SECONDS", 3)
DOOR_CONTROLLER_URL = os.getenv(
    "DOOR_CONTROLLER_URL",
    f"http://{os.getenv('NODEMCU_IP', '192.168.1.108')}",
).rstrip("/")

os.makedirs(CAPTURES_DIR, exist_ok=True)
os.makedirs(KNOWN_FACES_DIR, exist_ok=True)

# Define a list of users (username, password) pairs.
users = {
    "gautamabhiyan51@gmail.com": "gammaboy",
    "vvs16@gmail.com": "vvss",
    "gautamaashik111@gmail.com": "techbro",
}

known_face_encodings = []
known_face_names = []

attendance_lock = Lock()
door_lock = Lock()
alert_lock = Lock()
last_attendance_updates = {}
last_auto_unlock_at = 0
last_unknown_alert_at = 0


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


def ensure_attendance_file():
    if not os.path.exists(ATTENDANCE_FILE):
        df = pd.DataFrame(columns=["Name", "Time", "Status"])
        df.to_excel(ATTENDANCE_FILE, index=False)
        print(f"Created '{ATTENDANCE_FILE}' file.")


def unique_names(names):
    seen = set()
    result = []
    for name in names:
        if name in seen:
            continue
        seen.add(name)
        result.append(name)
    return result


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
                best_match_index = np.argmin(face_distances)
                if matches[best_match_index]:
                    name = known_face_names[best_match_index]

        face_names.append(name)

    return face_locations, face_names


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

        with attendance_lock:
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


def parse_recipients(value):
    if not value:
        return []
    return [email.strip() for email in value.replace(";", ",").split(",") if email.strip()]


def get_alert_recipients(preferred_recipient=None):
    recipients = []
    if preferred_recipient:
        recipients.append(preferred_recipient)

    configured = os.getenv("SECURITY_ALERT_RECIPIENTS") or os.getenv("ALERT_EMAILS") or os.getenv("ALERT_EMAIL")
    recipients.extend(parse_recipients(configured))

    if not recipients:
        recipients.extend(users.keys())

    deduped = []
    for email in recipients:
        if email not in deduped:
            deduped.append(email)
    return deduped


def mail_is_configured():
    missing = [
        key
        for key in ("MAIL_USERNAME", "MAIL_PASSWORD", "MAIL_DEFAULT_SENDER")
        if not app.config.get(key)
    ]
    if missing:
        print(f"Email alert skipped. Missing mail config: {', '.join(missing)}")
        return False
    return True


def send_unknown_face_email(recipients, detected_at, image_bytes=None):
    if not mail_is_configured():
        return

    with app.app_context():
        try:
            msg = Message(
                subject="Rakshyam alert: unknown face detected",
                recipients=recipients,
            )
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
            socketio.emit(
                "security-alert",
                {"message": "Unknown face alert email sent", "time": detected_at},
            )
        except Exception as e:
            print(f"Error sending unknown-face email: {e}")
            socketio.emit(
                "security-alert",
                {"message": "Unknown face email failed", "time": detected_at},
            )


def queue_unknown_face_alert(frame, preferred_recipient=None):
    global last_unknown_alert_at

    now = time.monotonic()
    with alert_lock:
        if now - last_unknown_alert_at < UNKNOWN_ALERT_COOLDOWN_SECONDS:
            return False
        last_unknown_alert_at = now

    detected_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    snapshot_path = os.path.join(
        CAPTURES_DIR,
        f"unknown_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jpg",
    )

    image_bytes = None
    try:
        cv2.imwrite(snapshot_path, frame)
        ok, jpeg = cv2.imencode(".jpg", frame)
        if ok:
            image_bytes = jpeg.tobytes()
    except Exception as e:
        print(f"Could not save unknown-face snapshot: {e}")

    recipients = get_alert_recipients(preferred_recipient)
    socketio.emit(
        "security-alert",
        {"message": "Unknown face detected", "time": detected_at},
    )

    Thread(
        target=send_unknown_face_email,
        args=(recipients, detected_at, image_bytes),
        daemon=True,
    ).start()
    return True


def send_door_command(command):
    try:
        response = requests.get(
            f"{DOOR_CONTROLLER_URL}/{command}",
            timeout=DOOR_REQUEST_TIMEOUT_SECONDS,
        )
        ok = response.status_code == 200
        detail = response.text.strip() or response.reason
        return ok, detail
    except RequestException as e:
        return False, str(e)


def queue_auto_unlock(name):
    global last_auto_unlock_at

    now = time.monotonic()
    with door_lock:
        if now - last_auto_unlock_at < DOOR_UNLOCK_COOLDOWN_SECONDS:
            return False
        last_auto_unlock_at = now

    Thread(target=auto_unlock_door, args=(name,), daemon=True).start()
    return True


def auto_unlock_door(name):
    success, detail = send_door_command("open")
    message = f"Door auto-unlocked for {name}" if success else f"Auto-unlock failed for {name}"
    print(f"{message}: {detail}")
    socketio.emit(
        "door-event",
        {
            "status": "open" if success else "error",
            "message": message,
            "detail": detail,
            "time": datetime.now().strftime("%H:%M:%S"),
        },
    )


def handle_face_events(face_names, frame, preferred_recipient=None):
    logged_rows = log_attendance_for_faces(face_names)

    known_names = [name for name in unique_names(face_names) if name != "Unknown"]
    if known_names:
        queue_auto_unlock(known_names[0])

    if "Unknown" in face_names:
        queue_unknown_face_alert(frame, preferred_recipient)

    return logged_rows


def draw_face_labels(frame, face_locations, face_names):
    for (top, right, bottom, left), name in zip(face_locations, face_names):
        color = (35, 158, 47) if name != "Unknown" else (43, 57, 192)
        cv2.rectangle(frame, (left, top), (right, bottom), color, 2)
        cv2.putText(frame, name, (left, top - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)


load_known_faces()
ensure_attendance_file()

camera = cv2.VideoCapture(0)
if not camera.isOpened():
    print("Error: Camera not initialized properly. Ensure it is connected.")
else:
    print("Camera initialized successfully.")

camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)


@app.route("/", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        entered_username = request.form.get("username", "").strip()
        entered_password = request.form.get("password", "")

        if entered_username in users and users[entered_username] == entered_password:
            session["username"] = entered_username
            append_attendance_rows(
                [
                    {
                        "Name": entered_username,
                        "Time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                        "Status": "Logged In",
                    }
                ]
            )
            print(f"User '{entered_username}' logged in successfully.")
            return redirect(url_for("dashboard"))

        print("Invalid username or password.")
        flash("Invalid username or password", "error")

    return render_template("login.html")


@app.route("/dashboard")
def dashboard():
    if "username" not in session:
        print("Access denied: User not logged in.")
        return redirect(url_for("login"))

    return render_template("index.html", username=session["username"])


@app.route("/capture_motion", methods=["GET"])
@app.route("/captures", methods=["GET"])
def capture_motion():
    try:
        ret, frame = camera.read()
        if not ret:
            print("Error: Failed to capture frame.")
            return jsonify(error="Camera error"), 500

        face_locations, face_names = recognize_faces(frame)
        print(f"Motion capture detected {len(face_names)} faces: {face_names}")

        if not face_names:
            return jsonify(message="No faces detected", faces=[]), 200

        logged_rows = handle_face_events(face_names, frame, session.get("username"))
        return jsonify(
            message="Image captured and processed successfully",
            faces=face_names,
            logged=logged_rows,
        ), 200
    except Exception as e:
        print(f"Error in capture_motion: {e}")
        return jsonify(error="Internal server error"), 500


@app.route("/video_feed")
def video_feed():
    preferred_recipient = session.get("username")

    def gen():
        while True:
            ret, frame = camera.read()
            if not ret:
                print("Error: Failed to read from camera.")
                break

            try:
                face_locations, face_names = recognize_faces(frame)
                if face_names:
                    print(f"Detected in live feed: {face_names}")
                    handle_face_events(face_names, frame, preferred_recipient)
                draw_face_labels(frame, face_locations, face_names)
            except Exception as e:
                print(f"Error processing live frame: {e}")

            ret, jpeg = cv2.imencode(".jpg", frame)
            if ret:
                yield (
                    b"--frame\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n" + jpeg.tobytes() + b"\r\n"
                )

    return Response(gen(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/open_door")
def open_door():
    success, detail = send_door_command("open")
    socketio.emit(
        "door-event",
        {
            "status": "open" if success else "error",
            "message": "Door opened" if success else "Door open failed",
            "detail": detail,
            "time": datetime.now().strftime("%H:%M:%S"),
        },
    )
    return jsonify(ok=success, status="Door Opened" if success else "Failed", detail=detail), 200 if success else 502


@app.route("/close_door")
def close_door():
    success, detail = send_door_command("close")
    socketio.emit(
        "door-event",
        {
            "status": "closed" if success else "error",
            "message": "Door closed" if success else "Door close failed",
            "detail": detail,
            "time": datetime.now().strftime("%H:%M:%S"),
        },
    )
    return jsonify(ok=success, status="Door Closed" if success else "Failed", detail=detail), 200 if success else 502


@app.route("/status")
def door_status():
    try:
        response = requests.get(
            f"{DOOR_CONTROLLER_URL}/status",
            timeout=DOOR_REQUEST_TIMEOUT_SECONDS,
        )
        if response.status_code == 200:
            try:
                status = response.json()
            except ValueError:
                status = {"status": response.text.strip()}
            return jsonify(status=status)
    except RequestException as e:
        return jsonify(status={"status": "Error fetching status", "detail": str(e)}), 502

    return jsonify(status={"status": "Error fetching status"}), 502


@app.route("/logout")
def logout():
    session.pop("username", None)
    print("User logged out.")
    return redirect(url_for("login"))


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

    socketio.emit(
        "message",
        {"message": message, "username": username, "type": "user"},
    )


if __name__ == "__main__":
    socketio.run(
        app,
        host=os.getenv("FLASK_HOST", "127.0.0.1"),
        port=env_int("FLASK_PORT", 5000),
        debug=True,
    )
