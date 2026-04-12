from __future__ import annotations

from dataclasses import dataclass

import requests


COMMAND_MAP = {
    "FORWARD": "f",
    "BACKWARD": "b",
    "LEFT": "l",
    "RIGHT": "r",
    "STOP": "s",
}


class ESP32ClientError(RuntimeError):
    """Raised when the ESP32 request cannot be completed successfully."""


@dataclass(slots=True)
class ESP32CommandRequest:
    """Represents a transport-agnostic command request."""

    command: str
    speed: int
    endpoint: str

    @property
    def params(self) -> dict[str, int | str]:
        return {"c": COMMAND_MAP[self.command], "s": self.speed}

    @property
    def json_payload(self) -> dict[str, int | str]:
        return {"command": self.command, "code": COMMAND_MAP[self.command], "speed": self.speed}


class ESP32Client:
    """Small HTTP client for forwarding robot commands to an ESP32."""

    def __init__(
        self,
        base_url: str,
        command_endpoint: str = "/cmd",
        timeout: float = 2.0,
        session: requests.Session | None = None,
    ) -> None:
        self.base_url = base_url.rstrip("/")
        self.command_endpoint = self._normalize_endpoint(command_endpoint)
        self.timeout = timeout
        self.session = session or requests.Session()

    def send_command(self, command: str, speed: int) -> str:
        """Send a movement command using the current HTTP GET prototype."""
        request_data = self.build_command_request(command, speed)
        url = f"{self.base_url}{request_data.endpoint}"

        try:
            response = self.session.get(
                url,
                params=request_data.params,
                timeout=self.timeout,
            )
            response.raise_for_status()
        except requests.RequestException as exc:
            raise ESP32ClientError(f"ESP32 request failed: {exc}") from exc

        return response.text.strip() or "OK"

    def build_command_request(self, command: str, speed: int) -> ESP32CommandRequest:
        """Keep request formatting separate so transport changes stay easy later."""
        normalized_command = self.normalize_command(command)
        return ESP32CommandRequest(
            command=normalized_command,
            speed=int(speed),
            endpoint=self.command_endpoint,
        )

    @staticmethod
    def normalize_command(command: str) -> str:
        normalized_command = str(command).strip().upper()
        if normalized_command not in COMMAND_MAP:
            raise ValueError(f"Unsupported command: {command}")
        return normalized_command

    @staticmethod
    def _normalize_endpoint(endpoint: str) -> str:
        if endpoint.startswith("/"):
            return endpoint
        return f"/{endpoint}"
