from __future__ import annotations

from typing import Any

from flask import Flask, current_app, jsonify, render_template, request

import config
from services.esp32_client import COMMAND_MAP, ESP32Client, ESP32ClientError


VALID_COMMANDS = tuple(COMMAND_MAP.keys())


def create_app(test_config: dict[str, Any] | None = None) -> Flask:
    app = Flask(__name__)
    app.config.from_object(config)

    if test_config:
        app.config.update(test_config)

    app.extensions["runtime_state"] = {
        "last_command": "STOP",
        "last_speed": app.config["DEFAULT_SPEED"],
        "connection_status": "Idle",
        "last_error": None,
    }

    if "ESP32_CLIENT" not in app.config:
        app.config["ESP32_CLIENT"] = ESP32Client(
            base_url=app.config["ESP32_BASE_URL"],
            command_endpoint=app.config["ESP32_COMMAND_ENDPOINT"],
            timeout=app.config["REQUEST_TIMEOUT"],
        )

    @app.get("/")
    def index():
        """Render the main control page."""
        state = _get_state()
        return render_template(
            "index.html",
            camera_stream_url=app.config["CAMERA_STREAM_URL"],
            phone_ap_ip=app.config["PHONE_AP_IP"],
            connection_status=state["connection_status"],
            last_command=state["last_command"],
            last_speed=state["last_speed"],
        )

    @app.post("/api/command")
    def send_command():
        """Validate a command and forward it to the ESP32."""
        payload = request.get_json(silent=True)
        if payload is None:
            return jsonify({"status": "error", "error": "Request body must be valid JSON."}), 400

        try:
            command, speed = _validate_command_payload(payload)
        except ValueError as exc:
            return (
                jsonify(
                    {
                        "status": "error",
                        "error": str(exc),
                        "valid_commands": list(VALID_COMMANDS),
                        "speed_range": {
                            "min": app.config["MIN_SPEED"],
                            "max": app.config["MAX_SPEED"],
                        },
                    }
                ),
                400,
            )

        state = _get_state()
        esp32_client = current_app.config["ESP32_CLIENT"]

        try:
            esp32_response = esp32_client.send_command(command, speed)
        except ESP32ClientError as exc:
            state["connection_status"] = "ESP32 unavailable"
            state["last_error"] = str(exc)
            return (
                jsonify(
                    {
                        "status": "error",
                        "error": "Failed to send command to ESP32.",
                        "details": str(exc),
                        "sent_command": command,
                        "speed": speed,
                    }
                ),
                502,
            )

        state["last_command"] = command
        state["last_speed"] = speed
        state["connection_status"] = "Connected to ESP32"
        state["last_error"] = None

        return jsonify(
            {
                "status": "ok",
                "sent_command": command,
                "speed": speed,
                "esp32_response": esp32_response,
                "connection_status": state["connection_status"],
            }
        )

    @app.get("/api/status")
    def status():
        """Return the server status and latest known command."""
        state = _get_state()
        return jsonify(
            {
                "status": "running",
                "phone_ap_ip": app.config["PHONE_AP_IP"],
                "camera_stream_url": app.config["CAMERA_STREAM_URL"],
                "esp32_base_url": app.config["ESP32_BASE_URL"],
                "last_command": state["last_command"],
                "last_speed": state["last_speed"],
                "connection_status": state["connection_status"],
                "last_error": state["last_error"],
            }
        )

    @app.get("/health")
    def health():
        """Return a simple health response."""
        return jsonify({"status": "ok"})

    return app


def _get_state() -> dict[str, Any]:
    return current_app.extensions["runtime_state"]


def _validate_command_payload(payload: Any) -> tuple[str, int]:
    if not isinstance(payload, dict):
        raise ValueError("JSON body must be an object.")

    raw_command = payload.get("command")
    if raw_command is None:
        raise ValueError("Missing 'command' field.")

    command = str(raw_command).strip().upper()
    if command not in VALID_COMMANDS:
        raise ValueError(f"Invalid command '{command}'.")

    raw_speed = payload.get("speed", current_app.config["DEFAULT_SPEED"])
    try:
        speed = int(raw_speed)
    except (TypeError, ValueError) as exc:
        raise ValueError("Speed must be an integer.") from exc

    min_speed = current_app.config["MIN_SPEED"]
    max_speed = current_app.config["MAX_SPEED"]
    if not min_speed <= speed <= max_speed:
        raise ValueError(f"Speed must be between {min_speed} and {max_speed}.")

    return command, speed


app = create_app()


if __name__ == "__main__":
    app.run(
        host=app.config["FLASK_HOST"],
        port=app.config["FLASK_PORT"],
        debug=app.config["DEBUG"],
    )
