const batteryLevel = document.getElementById("battery-level");
const batteryPercent = document.getElementById("battery-percent");
const batteryDetails = document.getElementById("battery-details");
const batteryPill = document.getElementById("battery-pill");
const debugWindow = document.getElementById("debug-window");
const copyDebugButton = document.getElementById("copy-debug");
const ttsText = document.getElementById("tts-text");
const modeSelect = document.getElementById("mode-select");
const modeNote = document.getElementById("mode-note");
const volumeSlider = document.getElementById("volume-slider");
const volumeValue = document.getElementById("volume-value");
const enableVideo = document.getElementById("enable-video");
const videoPreview = document.getElementById("video-preview");
const videoPlaceholder = document.getElementById("video-placeholder");
const videoStatus = document.getElementById("video-status");
const videoFrame = videoPreview.closest(".video-frame");
const visionSummary = document.getElementById("vision-summary");
const defaultVoiceType = "zh_female_shuangkuaisisi_emo_v2_mars_bigtts";
const defaultInterruptSpeechDurationMs = 200;
let volumeUpdateTimer = null;
let videoRefreshTimer = null;
let visionRefreshTimer = null;
let lastHeardText = "";
let lastSpokenText = "";
let lastOpenAiVisionText = "";
let lastOpenAiErrorText = "";
let copyButtonTimer = null;
let currentMode = "";
let isDevMode = false;

function modeLabel(mode) {
  return mode?.label || mode?.id || "Unknown";
}

function setModeNote(text) {
  modeNote.textContent = text;
}

function renderModeOptions(data) {
  const modes = Array.isArray(data?.modes) ? data.modes : [];
  const selectedMode = typeof data?.current_mode?.id === "string" ? data.current_mode.id : currentMode;
  currentMode = selectedMode;

  modeSelect.innerHTML = "";
  for (const mode of modes) {
    const option = document.createElement("option");
    option.value = mode.id;
    option.textContent = modeLabel(mode);
    option.selected = mode.id === selectedMode;
    modeSelect.appendChild(option);
  }

  modeSelect.disabled = modes.length === 0;
  if (!modes.length) {
    setModeNote("No robot modes available.");
    return;
  }

  const activeMode =
    modes.find((mode) => mode.id === selectedMode) ||
    (data?.current_mode && typeof data.current_mode === "object" ? data.current_mode : null);
  if (activeMode?.description) {
    setModeNote(`Current mode: ${modeLabel(activeMode)}. ${activeMode.description}`);
  } else {
    setModeNote(selectedMode ? `Current mode: ${selectedMode}` : "Current mode unavailable.");
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

function setVisionSummary(text, isError = false) {
  visionSummary.textContent = text;
  visionSummary.classList.toggle("is-empty", !text);
  visionSummary.classList.toggle("is-error", Boolean(text) && isError);
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

async function refreshSpeechDebug() {
  const response = await fetch("/health");
  const data = await response.json();
  isDevMode = Boolean(data?.wrapper?.dev_mode);
  const speech = data?.wrapper?.speech_debug || {};
  const heard = typeof speech.last_heard === "string" ? speech.last_heard.trim() : "";
  const spoken = typeof speech.last_spoken === "string" ? speech.last_spoken.trim() : "";
  const openAiVision =
    typeof speech.last_openai_vision === "string" ? speech.last_openai_vision.trim() : "";
  const openAiError =
    typeof speech.last_openai_error === "string" ? speech.last_openai_error.trim() : "";

  if (heard && heard !== lastHeardText) {
    lastHeardText = heard;
    appendDebug("Robot heard", heard);
  }

  if (spoken && spoken !== lastSpokenText) {
    lastSpokenText = spoken;
    appendDebug("Robot said", spoken);
  }

  if (openAiVision && openAiVision !== lastOpenAiVisionText) {
    lastOpenAiVisionText = openAiVision;
    setVisionSummary(openAiVision, false);
  }

  if (openAiError && openAiError !== lastOpenAiErrorText) {
    lastOpenAiErrorText = openAiError;
    setVisionSummary(`Vision error: ${openAiError}`, true);
  }

  if (!openAiVision && !openAiError && !visionSummary.textContent.trim()) {
    setVisionSummary("No image description yet.");
  }
}

async function refreshRobotMode() {
  const response = await fetch("/robot/mode");
  const data = await response.json();
  if (!data.ok) {
    appendDebug("/robot/mode response", data);
    modeSelect.disabled = true;
    setModeNote("Robot mode is unavailable.");
    return;
  }
  renderModeOptions(data);
}

async function refreshVisualContext() {
  const response = await fetch("/vision/refresh", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: "{}",
  });
  const data = await response.json();
  if (!data.ok) {
    appendDebug("Vision refresh error", data);
  }
  await refreshSpeechDebug();
  return data;
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
  if (visionRefreshTimer) {
    window.clearInterval(visionRefreshTimer);
    visionRefreshTimer = null;
  }
  videoPreview.removeAttribute("src");
  videoStatus.textContent = "Off";
  setVideoPreviewState(false, "Video preview is off.");
}

function startVideoPreview() {
  if (!videoRefreshTimer) {
    videoStatus.textContent = "Connecting...";
    setVideoPreviewState(false, "Waiting for robot camera preview...");
    refreshVideoPreview();
    videoRefreshTimer = window.setInterval(refreshVideoPreview, 1200);
  }

  if (!visionRefreshTimer) {
    refreshVisualContext().catch((error) => {
      appendDebug("Vision refresh error", String(error));
    });
    visionRefreshTimer = window.setInterval(() => {
      refreshVisualContext().catch((error) => {
        appendDebug("Vision refresh error", String(error));
      });
    }, 6000);
  }
}

videoPreview.addEventListener("load", () => {
  videoStatus.textContent = "Live";
  setVideoPreviewState(true);
});

videoPreview.addEventListener("error", () => {
  videoStatus.textContent = "No signal";
  setVideoPreviewState(false, "Robot camera preview is not ready.");
});

document.getElementById("refresh-battery").addEventListener("click", () => {
  refreshBattery().catch((error) => {
    console.error("Refresh error", error);
  });
});

document.getElementById("start-tts").addEventListener("click", async () => {
  const payload = {
    voice_type: defaultVoiceType,
    interrupt_speech_duration: defaultInterruptSpeechDurationMs,
    video_enabled: enableVideo.checked,
  };
  appendDebug("/rtc/tts/start request", payload);
  try {
    await postJson("/rtc/tts/start", payload);
  } catch (error) {
    appendDebug("Start listening error", String(error));
  }
});

document.getElementById("speak-tts").addEventListener("click", () => {
  const payload = {
    text: ttsText.value.trim(),
    video_enabled: enableVideo.checked,
  };
  appendDebug("/rtc/tts/speak request", payload);

  if (!payload.text) {
    appendDebug("Speak TTS error", "Please enter text before speaking.");
    return;
  }

  postJson("/rtc/tts/speak", payload).catch((error) => {
    appendDebug("Speak TTS error", String(error));
  });
});

document.getElementById("stop-tts").addEventListener("click", async () => {
  const payload = {};
  appendDebug("/rtc/tts/stop request", payload);
  try {
    await postJson("/rtc/tts/stop", payload);
  } catch (error) {
    appendDebug("Stop TTS error", String(error));
  }
});

document.getElementById("wave-hand").addEventListener("click", async () => {
  const payload = {};
  appendDebug("/robot/wave-hand request", payload);
  try {
    await postJson("/robot/wave-hand", payload);
  } catch (error) {
    appendDebug("Wave hand error", String(error));
  }
});

modeSelect.addEventListener("change", async () => {
  const previousMode = currentMode;
  const payload = { mode: modeSelect.value };
  const selectedOption = modeSelect.options[modeSelect.selectedIndex];
  const selectedLabel = selectedOption?.textContent || payload.mode;

  if (payload.mode !== previousMode) {
    const confirmed = window.confirm(`Switch robot mode to ${selectedLabel}?`);
    if (!confirmed) {
      modeSelect.value = previousMode;
      return;
    }
  }

  appendDebug("/robot/mode request", payload);

  try {
    modeSelect.disabled = true;
    setModeNote("Updating mode...");
    const data = await postJson("/robot/mode", payload);
    if (data.ok) {
      renderModeOptions(data);
    } else {
      setModeNote("Unable to update mode.");
      await refreshRobotMode();
    }
  } catch (error) {
    appendDebug("Mode change error", String(error));
    setModeNote("Unable to update mode.");
    await refreshRobotMode();
  } finally {
    modeSelect.disabled = false;
  }
});

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
    console.error("Refresh error", error);
  });

  refreshRobotMode().catch((error) => {
    console.error("Robot mode error", error);
  });
}, 5000);

setInterval(() => {
  refreshSpeechDebug().catch((error) => {
    console.error("Speech debug error", error);
  });
}, 1000);

refreshBattery().catch((error) => {
  console.error("Initial refresh error", error);
});

refreshRobotMode().catch((error) => {
  appendDebug("Initial robot mode error", String(error));
});

refreshVolume().catch((error) => {
  appendDebug("Initial volume error", String(error));
});

refreshSpeechDebug().catch((error) => {
  appendDebug("Initial speech debug error", String(error));
});

setVideoPreviewState(false);
videoStatus.textContent = "Off";
setVisionSummary("No image description yet.");
setCopyButtonState("Copy Log");
