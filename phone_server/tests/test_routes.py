from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app import create_app


class FakeESP32Client:
    def __init__(self) -> None:
        self.calls: list[tuple[str, int]] = []

    def send_command(self, command: str, speed: int) -> str:
        self.calls.append((command, speed))
        return f"OK:{command[0].lower()}"


def build_test_client():
    fake_client = FakeESP32Client()
    app = create_app(
        {
            "TESTING": True,
            "DEBUG": False,
            "ESP32_CLIENT": fake_client,
        }
    )
    return app.test_client(), fake_client


def test_health_route():
    client, _ = build_test_client()

    response = client.get("/health")

    assert response.status_code == 200
    assert response.get_json() == {"status": "ok"}


def test_status_route_returns_defaults():
    client, _ = build_test_client()

    response = client.get("/api/status")
    payload = response.get_json()

    assert response.status_code == 200
    assert payload["status"] == "running"
    assert payload["phone_ap_ip"] == "192.168.43.1"
    assert payload["camera_stream_url"] == "http://192.168.43.1:8080/video"
    assert payload["esp32_base_url"] == "http://192.168.43.2"
    assert payload["last_command"] == "STOP"
    assert payload["last_speed"] == 150


def test_command_route_forwards_to_esp32():
    client, fake_client = build_test_client()

    response = client.post(
        "/api/command",
        json={"command": "FORWARD", "speed": 150},
    )
    payload = response.get_json()

    assert response.status_code == 200
    assert fake_client.calls == [("FORWARD", 150)]
    assert payload["status"] == "ok"
    assert payload["sent_command"] == "FORWARD"
    assert payload["speed"] == 150
    assert payload["esp32_response"] == "OK:f"


def test_invalid_command_is_rejected():
    client, fake_client = build_test_client()

    response = client.post(
        "/api/command",
        json={"command": "JUMP", "speed": 150},
    )
    payload = response.get_json()

    assert response.status_code == 400
    assert fake_client.calls == []
    assert payload["status"] == "error"
    assert "Invalid command" in payload["error"]


def test_invalid_speed_is_rejected():
    client, fake_client = build_test_client()

    response = client.post(
        "/api/command",
        json={"command": "FORWARD", "speed": 999},
    )
    payload = response.get_json()

    assert response.status_code == 400
    assert fake_client.calls == []
    assert payload["status"] == "error"
    assert "Speed must be between" in payload["error"]
