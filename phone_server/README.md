# Phone Server

Minimal Flask web application for sending robot control commands from a phone browser.

## Project contents

```text
phone_server/
├── app.py
├── requirements.txt
├── README.md
├── templates/
│   └── index.html
└── static/
    ├── css/
    └── js/
```

## Run in Termux

1. Install packages:

```sh
pkg update
pkg install python mosquitto
```

2. Go to the project folder:

```sh
cd /path/to/sumo-robot-project/phone_server
```

3. Install Python dependencies:

```sh
pip install -r requirements.txt
```

## Start the MQTT broker

Run a local Mosquitto broker in Termux:

```sh
mosquitto -p 1883
```

The Flask app publishes robot commands to:

```text
robot/command
```

## Start the Flask server

In a second Termux session:

```sh
cd /path/to/sumo-robot-project/phone_server
python app.py
```

The server listens on:

```text
http://0.0.0.0:5000
```

## Open the UI in a browser

Open the phone browser and go to:

```text
http://127.0.0.1:5000
```

If you want to open it from another device on the same network, replace `127.0.0.1` with the phone IP address.

## Camera stream

The UI displays a camera stream from:

```text
http://127.0.0.1:8080/video
```

Update the `CAMERA_STREAM_URL` constant in `app.py` if your stream uses a different address.
