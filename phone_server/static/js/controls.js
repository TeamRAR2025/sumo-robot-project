const commandButtons = Array.from(document.querySelectorAll("[data-command]"));
const connectionStatus = document.getElementById("connection-status");
const lastCommand = document.getElementById("last-command");
const speedInput = document.getElementById("speed");
const errorMessage = document.getElementById("error-message");
const cameraStream = document.getElementById("camera-stream");
const cameraFallback = document.getElementById("camera-fallback");

function setConnectionStatus(message) {
    connectionStatus.textContent = message;
}

function showError(message) {
    errorMessage.textContent = message;
    errorMessage.hidden = false;
}

function clearError() {
    errorMessage.textContent = "";
    errorMessage.hidden = true;
}

function setButtonsDisabled(disabled) {
    commandButtons.forEach((button) => {
        button.disabled = disabled;
    });
}

function getSpeedValue() {
    const parsed = Number.parseInt(speedInput.value, 10);
    if (Number.isNaN(parsed)) {
        return Number.parseInt(speedInput.defaultValue, 10) || 150;
    }
    return parsed;
}

async function refreshStatus() {
    try {
        const response = await fetch("/api/status");
        if (!response.ok) {
            throw new Error(`Status request failed with ${response.status}`);
        }

        const data = await response.json();
        lastCommand.textContent = data.last_command;
        setConnectionStatus(data.connection_status || "Running");

        if (document.activeElement !== speedInput) {
            speedInput.value = data.last_speed;
        }

        if (data.last_error) {
            showError(data.last_error);
            return;
        }

        clearError();
    } catch (error) {
        setConnectionStatus("Status refresh failed");
        showError(error.message);
    }
}

async function sendCommand(command) {
    clearError();
    setConnectionStatus(`Sending ${command}...`);
    setButtonsDisabled(true);

    try {
        const response = await fetch("/api/command", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify({
                command,
                speed: getSpeedValue(),
            }),
        });

        const data = await response.json();
        if (!response.ok) {
            throw new Error(data.error || "Command request failed.");
        }

        lastCommand.textContent = data.sent_command;
        setConnectionStatus(data.connection_status || "Connected to ESP32");
    } catch (error) {
        setConnectionStatus("ESP32 unavailable");
        showError(error.message);
    } finally {
        setButtonsDisabled(false);
    }
}

commandButtons.forEach((button) => {
    button.addEventListener("click", () => {
        void sendCommand(button.dataset.command);
    });
});

cameraStream.addEventListener("error", () => {
    cameraStream.classList.add("is-hidden");
    cameraFallback.hidden = false;
});

void refreshStatus();
window.setInterval(refreshStatus, 10000);
