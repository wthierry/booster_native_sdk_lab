const batteryLevel = document.getElementById("battery-level");
const batteryPercent = document.getElementById("battery-percent");
const batteryDetails = document.getElementById("battery-details");
const batteryPill = document.getElementById("battery-pill");
const debugWindow = document.getElementById("debug-window");
const heardWindow = document.getElementById("heard-window");
const copyDebugButton = document.getElementById("copy-debug");
const backendOptions = Array.from(document.querySelectorAll('input[name="speech-backend"]'));
const rtcStartButton = document.getElementById("rtc-start");
const rtcStopButton = document.getElementById("rtc-stop");
const whisperLiveStartButton = document.getElementById("whisperlive-start");
const whisperLiveStopButton = document.getElementById("whisperlive-stop");
const whisperLiveModel = document.getElementById("whisperlive-model");
const rtcStatus = document.getElementById("rtc-status");
const whisperLiveStatus = document.getElementById("whisperlive-status");
const whisperLiveNote = document.getElementById("whisperlive-note");
const volumeSlider = document.getElementById("volume-slider");
const volumeValue = document.getElementById("volume-value");
const enableVideo = document.getElementById("enable-video");
const videoPreview = document.getElementById("video-preview");
const videoPlaceholder = document.getElementById("video-placeholder");
const videoStatus = document.getElementById("video-status");
const videoFrame = videoPreview.closest(".video-frame");

const defaultInterruptSpeechDurationMs = 700;
let volumeUpdateTimer = null;
let copyButtonTimer = null;
let videoRefreshTimer = null;
let lastHeardText = "";
let lastSpokenText = "";
let selectedBackend = "rtc";
const whisperLiveModelStorageKey = "booster.whisperlive.model";

function setBackendStatus(element, label, active) {
  element.textContent = label;
  element.classList.toggle("backend-status-idle", !active);
}

function updateBackendControls() {
  const rtcSelected = selectedBackend === "rtc";
  const whisperLiveSelected = selectedBackend === "whisperlive_asr";

  rtcStartButton.disabled = !rtcSelected;
  rtcStopButton.disabled = !rtcSelected;
  whisperLiveStartButton.disabled = !whisperLiveSelected;
  whisperLiveStopButton.disabled = !whisperLiveSelected;
  whisperLiveModel.disabled = !whisperLiveSelected;

  setBackendStatus(rtcStatus, rtcSelected ? "Active" : "Inactive", rtcSelected);
  setBackendStatus(whisperLiveStatus, whisperLiveSelected ? "Active" : "Inactive", whisperLiveSelected);
  whisperLiveNote.textContent = whisperLiveSelected
    ? "WhisperLive ASR listens on the robot mic and posts transcripts into the debug log."
    : "Select WhisperLive ASR to enable robot-side transcription controls.";
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

function appendDebug(title, payload) {
  const block = [
    `[${new Date().toLocaleTimeString()}] ${title}`,
    typeof payload === "string" ? payload : JSON.stringify(payload, null, 2),
  ].join("\n");

  debugWindow.textContent = debugWindow.textContent
    ? `${block}\n\n${debugWindow.textContent}`
    : block;
}

function setCopyButtonState(label, copied = false) {
  copyDebugButton.textContent = label;
  copyDebugButton.classList.toggle("is-copied", copied);
  if (copyButtonTimer) {
    window.clearTimeout(copyButtonTimer);
  }
  if (label !== "Copy Log") {
    copyButtonTimer = window.setTimeout(() => {
      copyDebugButton.textContent = "Copy Log";
      copyDebugButton.classList.remove("is-copied");
      copyButtonTimer = null;
    }, 1600);
  }
}

async function copyText(text) {
  if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
    await navigator.clipboard.writeText(text);
    return;
  }

  const temp = document.createElement("textarea");
  temp.value = text;
  temp.setAttribute("readonly", "");
  temp.style.position = "fixed";
  temp.style.top = "-9999px";
  temp.style.left = "-9999px";
  document.body.appendChild(temp);
  temp.focus();
  temp.select();

  try {
    const copied = document.execCommand("copy");
    if (!copied) {
      throw new Error("document.execCommand('copy') returned false");
    }
  } finally {
    document.body.removeChild(temp);
  }
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

async function refreshSpeechDebug() {
  const response = await fetch("/health");
  const data = await response.json();
  const speech = data?.wrapper?.speech_debug || {};
  const heard = typeof speech.last_heard === "string" ? speech.last_heard.trim() : "";
  const spoken = typeof speech.last_spoken === "string" ? speech.last_spoken.trim() : "";

  if (heard && heard !== lastHeardText) {
    lastHeardText = heard;
    heardWindow.textContent = heard;
  }

  if (spoken && spoken !== lastSpokenText) {
    lastSpokenText = spoken;
    appendDebug("Robot said", spoken);
  }
}

async function refreshBattery() {
  const response = await fetch("/battery");
  const data = await response.json();
  renderBattery(data.battery || {});
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

function setVideoPreviewState(isLive, message = "Video preview is off.") {
  videoFrame.classList.toggle("is-live", isLive);
  videoPlaceholder.textContent = message;
}

function refreshVideoPreview() {
  videoPreview.src = `/camera/preview.jpg?t=${Date.now()}`;
}

function stopVideoPreview() {
  if (videoRefreshTimer) {
    window.clearInterval(videoRefreshTimer);
    videoRefreshTimer = null;
  }
  videoPreview.removeAttribute("src");
  videoStatus.textContent = "Off";
  setVideoPreviewState(false, "Video preview is off.");
}

function startVideoPreview() {
  if (videoRefreshTimer) {
    return;
  }
  videoStatus.textContent = "Connecting...";
  setVideoPreviewState(false, "Waiting for robot camera preview...");
  refreshVideoPreview();
  videoRefreshTimer = window.setInterval(refreshVideoPreview, 1200);
}

videoPreview.addEventListener("load", () => {
  videoStatus.textContent = "Live";
  setVideoPreviewState(true);
});

videoPreview.addEventListener("error", () => {
  videoStatus.textContent = "No signal";
  setVideoPreviewState(false, "Robot camera preview is not ready.");
});

rtcStartButton.addEventListener("click", async () => {
  const payload = {
    interrupt_speech_duration: defaultInterruptSpeechDurationMs,
  };
  appendDebug("[Native RTC] /rtc/tts/start request", payload);
  try {
    await postJson("/rtc/tts/start", payload);
  } catch (error) {
    appendDebug("[Native RTC] Start listening error", String(error));
  }
});

rtcStopButton.addEventListener("click", async () => {
  const payload = {};
  appendDebug("[Native RTC] /rtc/tts/stop request", payload);
  try {
    await postJson("/rtc/tts/stop", payload);
  } catch (error) {
    appendDebug("[Native RTC] Stop listening error", String(error));
  }
});

whisperLiveStartButton.addEventListener("click", () => {
  const payload = {
    model: whisperLiveModel.value,
  };
  appendDebug("[WhisperLive ASR] /whisperlive/asr/start request", payload);
  postJson("/whisperlive/asr/start", payload).catch((error) => {
    appendDebug("[WhisperLive ASR] Start listening error", String(error));
  });
});

whisperLiveStopButton.addEventListener("click", () => {
  const payload = {};
  appendDebug("[WhisperLive ASR] /whisperlive/asr/stop request", payload);
  postJson("/whisperlive/asr/stop", payload).catch((error) => {
    appendDebug("[WhisperLive ASR] Stop listening error", String(error));
  });
});

whisperLiveModel.addEventListener("change", () => {
  window.localStorage.setItem(whisperLiveModelStorageKey, whisperLiveModel.value);
  appendDebug("WhisperLive model selected", whisperLiveModel.value);
});

for (const option of backendOptions) {
  option.addEventListener("change", () => {
    if (!option.checked) {
      return;
    }
    selectedBackend = option.value;
    updateBackendControls();
    appendDebug("Speech backend selected", selectedBackend === "rtc" ? "Native RTC" : "WhisperLive ASR");
  });
}

enableVideo.addEventListener("change", () => {
  if (enableVideo.checked) {
    startVideoPreview();
  } else {
    stopVideoPreview();
  }
});

copyDebugButton.addEventListener("click", async () => {
  const text = debugWindow.textContent.trim();
  if (!text) {
    setCopyButtonState("Nothing to copy");
    return;
  }

  try {
    await copyText(text);
    setCopyButtonState("Copied", true);
  } catch (error) {
    setCopyButtonState("Copy failed");
    appendDebug("Copy error", String(error));
  }
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
    console.error("Battery error", error);
  });
  refreshSpeechDebug().catch((error) => {
    console.error("Speech debug error", error);
  });
}, 1000);

refreshBattery().catch((error) => {
  appendDebug("Initial battery error", String(error));
});

refreshVolume().catch((error) => {
  appendDebug("Initial volume error", String(error));
});

refreshSpeechDebug().catch((error) => {
  appendDebug("Initial speech debug error", String(error));
});

if (!lastHeardText) {
  heardWindow.textContent = "Waiting for speech...";
}

setVideoPreviewState(false);
videoStatus.textContent = "Off";
{
  const savedModel = window.localStorage.getItem(whisperLiveModelStorageKey);
  if (savedModel) {
    whisperLiveModel.value = savedModel;
  }
}
updateBackendControls();
setCopyButtonState("Copy Log");
