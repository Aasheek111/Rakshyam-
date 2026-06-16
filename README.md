# Rakshyam-
This is the project which combines the hardware with the software 

# Smart Home Security and Access Control System

## Project Documentation

### 1. Project Title

**IoT-Based Smart Home Security and Access Control System with Face Recognition**

---

# 2. Abstract

The Smart Home Security and Access Control System is an integrated hardware and software solution designed to improve home security through automation, face recognition, motion detection, remote monitoring, and intelligent access control.

The system uses an ESP32-S3 microcontroller, PIR motion sensor, DHT11 temperature and humidity sensor, keypad, servo motor, and a Flask-based web application. When motion is detected, the system captures images, performs face recognition, records attendance logs, and allows authorized users to remotely monitor and control door access through a web dashboard.

The project combines Internet of Things (IoT), Computer Vision, Web Development, and Embedded Systems into a single platform.

---

# 3. Objectives

### Main Objectives

* Provide secure door access.
* Detect intruders automatically.
* Recognize known and unknown visitors.
* Monitor environmental conditions.
* Allow remote door control.
* Maintain visitor logs and attendance records.
* Enable live camera monitoring.

### Specific Objectives

* Open door using keypad password.
* Open/close door remotely from website.
* Detect motion using PIR sensor.
* Capture visitor images automatically.
* Perform face recognition.
* Store visitor records in Excel database.
* Monitor temperature and humidity.
* Provide live video streaming.
* Support real-time communication through chat.

---

# 4. Problem Statement

Traditional home security systems often lack intelligent visitor identification, remote monitoring, and automated logging. Manual security systems cannot distinguish between authorized and unauthorized visitors.

This project addresses these limitations by integrating IoT devices, computer vision, and web technologies to create a smart, automated, and remotely accessible security solution.

---

# 5. Technologies Used

## Hardware

| Component                     | Purpose                           |
| ----------------------------- | --------------------------------- |
| ESP32-S3                      | Main Controller                   |
| PIR Sensor                    | Motion Detection                  |
| DHT11 Sensor                  | Temperature & Humidity Monitoring |
| 3x3 Keypad                    | Password Entry                    |
| Servo Motor                   | Door Lock Control                 |
| Camera/Webcam                 | Face Recognition                  |
| Wi-Fi Module (Built-in ESP32) | Communication                     |
| Power Supply                  | System Power                      |

---

## Software

| Technology               | Purpose             |
| ------------------------ | ------------------- |
| Python                   | Backend Development |
| Flask                    | Web Framework       |
| Flask-SocketIO           | Real-Time Chat      |
| OpenCV                   | Image Processing    |
| Face Recognition Library | Facial Recognition  |
| Pandas                   | Data Handling       |
| Excel                    | Attendance Database |
| HTML/CSS/JavaScript      | Frontend            |
| Arduino IDE              | ESP32 Programming   |

---

# 6. System Architecture

```text
                    ┌───────────────┐
                    │     User      │
                    └──────┬────────┘
                           │
                           ▼
                 ┌─────────────────┐
                 │ Flask Web Server│
                 └──────┬──────────┘
                        │
       ┌────────────────┼─────────────────┐
       │                │                 │
       ▼                ▼                 ▼

 Face Recognition   Live Stream      Door Control
       │                │                 │
       └──────────┬─────┘                 │
                  │                       │
                  ▼                       ▼

             Attendance Log          ESP32-S3
                                         │
        ┌────────────────────────────────┼────────────────┐
        │                │               │                │
        ▼                ▼               ▼                ▼

   PIR Sensor      DHT11 Sensor      Keypad         Servo Motor
(Motion Detect)   (Environment)  (Password Entry) (Door Lock)
```

---

# 7. Hardware Description

## ESP32-S3

Acts as the brain of the system.

Responsibilities:

* Connects to Wi-Fi.
* Reads sensor data.
* Hosts web server.
* Controls servo motor.
* Sends HTTP requests to Flask server.

---

## PIR Sensor

Detects human motion.

Working:

1. Detects infrared radiation changes.
2. Sends HIGH signal to ESP32.
3. ESP32 triggers image capture.

---

## DHT11 Sensor

Measures:

* Temperature
* Humidity

Updates readings every 2 seconds.

---

## Keypad

Used for password authentication.

Password:

```text
1234
```

If password is correct:

```text
Door Opens
```

Otherwise:

```text
Access Denied
```

---

## Servo Motor

Functions as smart door lock.

Position:

```text
0°   = Door Closed
180° = Door Open
```

---

# 8. Software Modules

---

## Module 1: User Authentication

Features:

* Login Page
* Session Management
* User Verification

Authorized users:

```python
users = {
'admin1@gmail.com':'password1',
'admin2@gmail.com':'password2'
}
```

---

## Module 2: Motion Detection

Process:

```text
Motion Detected
      ↓
ESP32 Sends Request
      ↓
Flask Captures Image
      ↓
Face Recognition
      ↓
Log Attendance
```

---

## Module 3: Face Recognition

### Known Faces

Stored inside:

```text
known_faces/
```

### Recognition Process

1. Capture image.
2. Detect face.
3. Generate encoding.
4. Compare with database.
5. Identify visitor.

Outputs:

```text
Known Visitor
```

or

```text
Unknown Visitor
```

---

## Module 4: Attendance Logging

Data stored in:

```text
attendance_log.xlsx
```

Fields:

| Name    | Time     | Status  |
| ------- | -------- | ------- |
| John    | 10:20 AM | Known   |
| Unknown | 10:30 AM | Unknown |

---

## Module 5: Live Video Streaming

Features:

* Real-time camera feed
* Face detection
* Face labeling
* Browser-based monitoring

---

## Module 6: Smart Door Control

### Local Access

Using keypad.

### Remote Access

Using website.

Endpoints:

```http
/open
/close
```

---

## Module 7: Environmental Monitoring

Provides:

* Temperature
* Humidity

Endpoint:

```http
/dht
```

Example:

```text
Temp: 28°C
Humidity: 65%
```

---

## Module 8: Real-Time Chat

Implemented using:

```text
Flask SocketIO
```

Allows communication between connected users.

---

# 9. Working Procedure

### Step 1

ESP32 connects to Wi-Fi.

### Step 2

System starts monitoring motion.

### Step 3

PIR detects movement.

### Step 4

Flask captures image.

### Step 5

Face Recognition runs.

### Step 6

Visitor identified.

### Step 7

Attendance stored.

### Step 8

User can:

* View live stream.
* Open/close door.
* View logs.
* Monitor environment.

---

# 10. Database Structure

## Attendance Log

| Field  | Description    |
| ------ | -------------- |
| Name   | Visitor Name   |
| Time   | Detection Time |
| Status | Known/Unknown  |

---

# 11. Features

### Security Features

✅ Face Recognition

✅ Password Protected Entry

✅ Motion Detection

✅ Visitor Logging

✅ Unknown Person Detection

✅ Session Authentication

---

### Smart Features

✅ IoT-Based Control

✅ Real-Time Monitoring

✅ Remote Door Control

✅ Environmental Monitoring

✅ Live Streaming

✅ Chat System

---

# 12. Advantages

* Low Cost
* Easy Installation
* Automated Security
* Remote Monitoring
* Real-Time Detection
* Expandable Architecture
* User Friendly

---

# 13. Limitations

* Requires Wi-Fi connection.
* DHT11 has limited accuracy.
* Face recognition performance depends on lighting.
* Camera must remain connected.
* Password can be guessed if weak.

---

# 14. Future Enhancements

### Recommended Upgrades

* ESP32-CAM integration.
* Mobile Application.
* Email Notifications.
* SMS Alerts.
* Telegram Alerts.
* Cloud Database (MySQL/Firebase).
* Fingerprint Authentication.
* RFID Authentication.
* Voice Assistant Integration.
* AI-based Intrusion Detection.
* Visitor Image Gallery.
* Multi-Factor Authentication.
* Face Recognition Door Unlock.
* Emergency Alarm System.
* Battery Backup System.

---

# 15. Expected Output

The system successfully:

* Detects motion.
* Identifies visitors.
* Records attendance.
* Monitors temperature and humidity.
* Streams live video.
* Controls door lock remotely.
* Provides secure smart-home access.

---

# 16. Conclusion

The Smart Home Security and Access Control System demonstrates the integration of IoT, Embedded Systems, Web Development, and Artificial Intelligence into a practical real-world application. The project provides enhanced security through face recognition, motion detection, password authentication, environmental monitoring, and remote access control. It offers a scalable foundation for future smart-home and smart-campus security solutions.
