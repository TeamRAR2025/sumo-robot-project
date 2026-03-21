from flask import Flask, jsonify, render_template, request
from paho.mqtt import publish


app = Flask(__name__)

MQTT_HOST = "127.0.0.1"
MQTT_PORT = 1883
MQTT_TOPIC = "robot/command"
CAMERA_STREAM_URL = "http://127.0.0.1:8080/video"
VALID_COMMANDS = {"FORWARD", "BACKWARD", "LEFT", "RIGHT", "STOP"}

# Keep the latest robot command in memory for the status endpoint and UI.
last_command = "STOP"


@app.get("/")
def index():
    """Render the main control page."""
    return render_template(
        "index.html",
        camera_stream_url=CAMERA_STREAM_URL,
        last_command=last_command,
    )


@app.post("/api/command")
def send_command():
    """Validate and publish a robot command over MQTT."""
    global last_command

    data = request.get_json(silent=True) or {}
    command = str(data.get("command", "")).upper()

    if command not in VALID_COMMANDS:
        return (
            jsonify(
                {
                    "error": "Invalid command",
                    "valid_commands": sorted(VALID_COMMANDS),
                }
            ),
            400,
        )

    publish.single(
        MQTT_TOPIC,
        payload=command,
        hostname=MQTT_HOST,
        port=MQTT_PORT,
    )
    last_command = command

    return jsonify({"success": True, "last_command": last_command})


@app.get("/api/status")
def status():
    """Return the server status and latest known command."""
    return jsonify(
        {
            "status": "running",
            "last_command": last_command,
            "camera_stream_url": CAMERA_STREAM_URL,
        }
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
