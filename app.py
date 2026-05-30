import time
from flask import Flask, Response, render_template, request, redirect, url_for, session, flash
import cv2
import os
from datetime import datetime
import face_recognition
import numpy as np
from flask_mail import Mail, Message
from flask_socketio import SocketIO, send
import pandas as pd

app = Flask(__name__)

# Email Configuration
app.config['MAIL_SERVER'] = 'smtp.gmail.com'
app.config['MAIL_PORT'] = 465
app.config['MAIL_USE_SSL'] = True
app.config['MAIL_USERNAME'] = 'subscribegateway111@gmail.com'  # Replace with your email
app.config['MAIL_PASSWORD'] = 'cmdx xzeu csmk vfxh'  # Replace with your app-specific password
app.config['MAIL_DEFAULT_SENDER'] = 'subscribegateway111@gmail.com'  # Replace with your email

# Initialize SocketIO and Flask-Mail
socketio = SocketIO(app)
mail = Mail(app)

# Secret key for session management
app.secret_key = '1234'

# Initialize camera
camera = cv2.VideoCapture(0)
if not camera.isOpened():
    print("Error: Camera not initialized properly. Ensure it is connected.")
else:
    print("Camera initialized successfully.")

# Set camera resolution
camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

# Ensure the "captures" directory exists
if not os.path.exists("captures"):
    os.makedirs("captures")
    print("Created 'captures' directory.")

# Define a list of users (username, password) pairs
users = {
    'gautamabhiyan51@gmail.com': 'gammaboy',
    'vvs16@gmail.com': 'vvss',
    'gautamaashik111@gmail.com':'techbro'
}

# Known face encodings and names
known_face_encodings = []
known_face_names = []

# Load known faces
known_faces_dir = "known_faces"
if not os.path.exists(known_faces_dir):
    os.makedirs(known_faces_dir)
    print("Created 'known_faces' directory.")

for filename in os.listdir(known_faces_dir):
    if filename.endswith(".jpg") or filename.endswith(".png"):
        filepath = os.path.join(known_faces_dir, filename)
        try:
            image = face_recognition.load_image_file(filepath)
            encoding = face_recognition.face_encodings(image)[0]
            known_face_encodings.append(encoding)
            known_face_names.append(os.path.splitext(filename)[0])  # Use filename (without extension) as the name
            print(f"Loaded face encoding for {filename}.")
        except Exception as e:
            print(f"Error loading {filename}: {e}")

# File to store attendance/log data
attendance_file = 'attendance_log.xlsx'

# Create an Excel file if it doesn't exist
if not os.path.exists(attendance_file):
    df = pd.DataFrame(columns=["Name", "Time", "Status"])
    df.to_excel(attendance_file, index=False)
    print(f"Created '{attendance_file}' file.")

# Flask routes
@app.route('/', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        entered_username = request.form.get('username')
        entered_password = request.form.get('password')

        if entered_username in users and users[entered_username] == entered_password:
            session['username'] = entered_username
            print(f"User '{entered_username}' logged in successfully.")
            
            # Log user login in the attendance file
            timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            df = pd.read_excel(attendance_file)
            new_row = pd.DataFrame([{"Name": entered_username, "Time": timestamp, "Status": "Logged In"}])
            df = pd.concat([df, new_row], ignore_index=True)
            df.to_excel(attendance_file, index=False)
            print(f"User '{entered_username}' login recorded in attendance log.")
            
            return redirect(url_for('dashboard'))
        else:
            print("Invalid username or password.")
            flash("Invalid username or password", 'error')
            return render_template('login.html')
    return render_template('login.html')

@app.route('/dashboard')
def dashboard():
    if 'username' not in session:
        print("Access denied: User not logged in.")
        return redirect(url_for('login'))
    username = session['username']
    return render_template('index.html', username=username)
@app.route('/capture_motion', methods=['GET'])
def capture_motion():
    try:
        # Capture a frame
        ret, frame = camera.read()
        if not ret:
            print("Error: Failed to capture frame.")
            return "Camera error", 500

        print("Frame captured successfully.")

        # Convert frame to RGB for face recognition
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        # Detect face locations and encodings
        face_locations = face_recognition.face_locations(rgb_frame)
        face_encodings = face_recognition.face_encodings(rgb_frame, face_locations)

        print(f"Detected {len(face_encodings)} faces.")

        if not face_encodings:
            print("No faces detected.")
            return "No faces detected", 200

        face_names = []
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # Process each detected face
        for i, face_encoding in enumerate(face_encodings):
            print(f"Processing face {i + 1}/{len(face_encodings)}.")

            matches = face_recognition.compare_faces(known_face_encodings, face_encoding, tolerance=0.6)
            print(f"Matches: {matches}")

            name = "Unknown"
            face_distances = face_recognition.face_distance(known_face_encodings, face_encoding)

            if matches and len(face_distances) > 0:
                best_match_index = np.argmin(face_distances)
                if matches[best_match_index]:
                    name = known_face_names[best_match_index]
                    print(f"Face {i + 1} recognized as: {name}")

            face_names.append(name)

        # Print all detected names
        print(f"Detected names: {face_names}")

        # Log details to Excel
        try:
            df = pd.read_excel(attendance_file)
            print("Attendance file read successfully.")
        except Exception as e:
            print(f"Error reading attendance file: {e}")
            return "Attendance file error", 500

        # Check if the entry for this name exists on the same day
        for name in face_names:
            # Extract current date for comparison
            current_date = datetime.now().strftime('%Y-%m-%d')

            # Check if this name has already been recorded today
            existing_entries = df[(df['Name'] == name) & (df['Time'].str.startswith(current_date))]
            if not existing_entries.empty:
                print(f"Attendance already recorded for {name} today. Skipping update.")
                continue

            status = "Known" if name != "Unknown" else "Unknown"
            print(f"Logging attendance for: Name={name}, Status={status}")

            # Add a new row to the dataframe
            new_row = pd.DataFrame([{"Name": name, "Time": timestamp, "Status": status}])
            df = pd.concat([df, new_row], ignore_index=True)

        # Save the updated log to Excel
        try:
            df.to_excel(attendance_file, index=False)
            print("Attendance log updated successfully.")
        except Exception as e:
            print(f"Error writing attendance file: {e}")
            return "Attendance file save error", 500

        return "Image captured and attendance logged successfully", 200

    except Exception as e:
        print(f"Error in capture_motion: {e}")
        return "Internal server error", 500



# Backend (Flask + SocketIO)
@socketio.on('user-message')
def handle_message(data):
    # Data should contain the message and username
    message = data['message']
    username = data['username']
    
    # Emit to all clients (including sender) with the appropriate type
    socketio.emit('message', {'message': message, 'username': username, 'type': 'user'})


@app.route('/video_feed')
def video_feed():
    def gen():
        while True:
            ret, frame = camera.read()
            if not ret:
                print("Error: Failed to read from camera.")
                break

            rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            face_locations = face_recognition.face_locations(rgb_frame)
            face_encodings = face_recognition.face_encodings(rgb_frame, face_locations)

            face_names = []
            timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

            for face_encoding in face_encodings:
                matches = face_recognition.compare_faces(known_face_encodings, face_encoding, tolerance=0.6)
                name = "Unknown"
                face_distances = face_recognition.face_distance(known_face_encodings, face_encoding)
                if matches and len(face_distances) > 0:
                    best_match_index = np.argmin(face_distances)
                    if matches[best_match_index]:
                        name = known_face_names[best_match_index]
                face_names.append(name)

            if face_names:
                print(f"Detected in live feed: {face_names}")
                try:
                    df = pd.read_excel(attendance_file)
                    for name in face_names:
                        status = "Known" if name != "Unknown" else "Unknown"
                        new_row = pd.DataFrame([{"Name": name, "Time": timestamp, "Status": status}])
                        df = pd.concat([df, new_row], ignore_index=True)
                    df.to_excel(attendance_file, index=False)
                    print("Attendance log updated from live feed.")
                except Exception as e:
                    print(f"Error logging attendance from live feed: {e}")

            for (top, right, bottom, left), name in zip(face_locations, face_names):
                cv2.rectangle(frame, (left, top), (right, bottom), (0, 255, 0), 2)
                cv2.putText(frame, name, (left, top - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)

            ret, jpeg = cv2.imencode('.jpg', frame)
            if ret:
                yield (b'--frame\r\n'
                       b'Content-Type: image/jpeg\r\n\r\n' + jpeg.tobytes() + b'\r\n')

    return Response(gen(), mimetype='multipart/x-mixed-replace; boundary=frame')

NODEMCU_IP = '192.168.1.108'
@app.route('/open_door')
def open_door():
    # Send HTTP request to open the door
    response = requests.get("http://192.168.100.53/open")
    return jsonify(status="Door Opened" if response.status_code == 200 else "Failed")

@app.route('/close_door')
def close_door():
    # Send HTTP request to close the door
    response = requests.get("http://192.168.100.53/close")
    return jsonify(status="Door Closed" if response.status_code == 200 else "Failed")

@app.route('/status')
def door_status():
    # Send HTTP request to get the door status (this would be dynamically handled by your NodeMCU)
    response = requests.get(f"{NODEMCU_IP}/status")
    status = response.json() if response.status_code == 200 else {"status": "Error fetching status"}
    return jsonify(status=status)

@app.route('/logout')
def logout():
    session.pop('username', None)
    print("User logged out.")
    return redirect(url_for('login')) 

@socketio.on('user-message')
def handle_user_message(message):
    print(f"Received message: {message}")
    send(message, broadcast=True)

# Run the app
if __name__ == '__main__':
    socketio.run(app, debug=True)
