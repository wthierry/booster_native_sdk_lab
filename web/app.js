const batteryLevel = document.getElementById("battery-level");
const batteryPercent = document.getElementById("battery-percent");
const batteryDetails = document.getElementById("battery-details");
const batteryPill = document.getElementById("battery-pill");
const debugWindow = document.getElementById("debug-window");
const ttsText = document.getElementById("tts-text");
const volumeSlider = document.getElementById("volume-slider");
const volumeValue = document.getElementById("volume-value");
const defaultVoiceType = "zh_female_shuangkuaisisi_emo_v2_mars_bigtts";
let volumeUpdateTimer = null;

function appendDebug(title, payload) {
  const block = [
    `[${new Date().toLocaleTimeString()}] ${title}`,
    typeof payload === "string" ? payload : JSON.stringify(payload, null, 2),
  ].join("\n");

  debugWindow.textContent = debugWindow.textContent
    ? `${block}\n\n${debugWindow.textContent}`
    : block;
}

function renderBattery(battery) {
  if (!battery.available) {
    batteryLevel.style.width = "0%";
    batteryPercent.textContent = "--%";
    batteryDetails.textContent = "Waiting for battery data...";
    batteryPill.textContent = "Waiting";
    batteryPill.className = "pill";
    return;
  }

  const soc = Math.max(0, Math.min(100, Number(battery.soc_percent ?? 0)));
  batteryLevel.style.width = `${soc}%`;
  batteryPercent.textContent = `${soc.toFixed(1)}%`;
  batteryDetails.innerHTML = `
    <div><strong>Voltage:</strong> ${Number(battery.voltage_v ?? 0).toFixed(2)} V</div>
    <div><strong>Current:</strong> ${Number(battery.current_a ?? 0).toFixed(2)} A</div>
    <div><strong>Average:</strong> ${Number(battery.average_voltage_v ?? 0).toFixed(2)} V</div>
  `;

  if (soc >= 50) {
    batteryPill.textContent = "Good";
    batteryPill.className = "pill pill-good";
  } else if (soc >= 20) {
    batteryPill.textContent = "Low";
    batteryPill.className = "pill pill-warn";
  } else {
    batteryPill.textContent = "Critical";
    batteryPill.className = "pill pill-bad";
  }
}

async function refreshBattery() {
  const response = await fetch("/battery");
  const data = await response.json();
  renderBattery(data.battery || {});
}

async function postJson(path, payload) {
  const response = await fetch(path, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  appendDebug(`${path} response`, data);
  return data;
}

async function refreshVolume() {
  const response = await fetch("/audio/volume");
  const data = await response.json();
  if (data.ok) {
    volumeSlider.value = data.volume_percent;
    volumeValue.textContent = `${data.volume_percent}%`;
  } else {
    appendDebug("/audio/volume response", data);
  }
}

document.getElementById("refresh-battery").addEventListener("click", () => {
  refreshBattery().catch((error) => {
    console.error("Refresh error", error);
  });
});

document.getElementById("start-tts").addEventListener("click", () => {
  const payload = { voice_type: defaultVoiceType };
  appendDebug("/rtc/tts/start request", payload);
  postJson("/rtc/tts/start", payload).catch((error) => {
    appendDebug("Start listening error", String(error));
  });
});

document.getElementById("speak-tts").addEventListener("click", () => {
  const payload = { text: ttsText.value.trim() };
  appendDebug("/rtc/tts/speak request", payload);

  if (!payload.text) {
    appendDebug("Speak TTS error", "Please enter text before speaking.");
    return;
  }

  postJson("/rtc/tts/speak", payload).catch((error) => {
    appendDebug("Speak TTS error", String(error));
  });
});

document.getElementById("stop-tts").addEventListener("click", () => {
  const payload = {};
  appendDebug("/rtc/tts/stop request", payload);
  postJson("/rtc/tts/stop", payload).catch((error) => {
    appendDebug("Stop TTS error", String(error));
  });
});

volumeSlider.addEventListener("input", () => {
  volumeValue.textContent = `${volumeSlider.value}%`;
  if (volumeUpdateTimer) {
    window.clearTimeout(volumeUpdateTimer);
  }
  volumeUpdateTimer = window.setTimeout(() => {
    const payload = { volume_percent: Number(volumeSlider.value) };
    appendDebug("/audio/volume request", payload);
    postJson("/audio/volume", payload).catch((error) => {
      appendDebug("Volume error", String(error));
    });
  }, 120);
});

setInterval(() => {
  refreshBattery().catch((error) => {
    console.error("Refresh error", error);
  });
}, 5000);

refreshBattery().catch((error) => {
  console.error("Initial refresh error", error);
});

refreshVolume().catch((error) => {
  appendDebug("Initial volume error", String(error));
});
