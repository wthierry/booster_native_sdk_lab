const batteryLevel = document.getElementById("battery-level");
const batteryPercent = document.getElementById("battery-percent");
const batteryDetails = document.getElementById("battery-details");
const batteryPill = document.getElementById("battery-pill");
const debugWindow = document.getElementById("debug-window");
const heardWindow = document.getElementById("heard-window");
const openAiBridgeWindow = document.getElementById("openai-bridge-window");
const copyDebugButton = document.getElementById("copy-debug");
const backendOptions = Array.from(document.querySelectorAll('input[name="speech-backend"]'));
const rtcStartButton = document.getElementById("rtc-start");
const rtcStopButton = document.getElementById("rtc-stop");
const whisperLiveStartButton = document.getElementById("whisperlive-start");
const whisperLiveStopButton = document.getElementById("whisperlive-stop");
const whisperLiveModel = document.getElementById("whisperlive-model");
const moonshineStartButton = document.getElementById("moonshine-start");
const moonshineStopButton = document.getElementById("moonshine-stop");
const moonshineModel = document.getElementById("moonshine-model");
const openAiStartButton = document.getElementById("openai-start");
const openAiStopButton = document.getElementById("openai-stop");
const openAiModel = document.getElementById("openai-model");
const openAiTuningControls = document.getElementById("openai-tuning-controls");
const openAiStartThreshold = document.getElementById("openai-start-threshold");
const openAiStartThresholdValue = document.getElementById("openai-start-threshold-value");
const openAiContinueThreshold = document.getElementById("openai-continue-threshold");
const openAiContinueThresholdValue = document.getElementById("openai-continue-threshold-value");
const openAiSilenceFrames = document.getElementById("openai-silence-frames");
const openAiSilenceFramesValue = document.getElementById("openai-silence-frames-value");
const openAiMaxFrames = document.getElementById("openai-max-frames");
const openAiMaxFramesValue = document.getElementById("openai-max-frames-value");
const rtcStatus = document.getElementById("rtc-status");
const whisperLiveStatus = document.getElementById("whisperlive-status");
const whisperLiveNote = document.getElementById("whisperlive-note");
const moonshineStatus = document.getElementById("moonshine-status");
const moonshineNote = document.getElementById("moonshine-note");
const openAiStatus = document.getElementById("openai-status");
const openAiNote = document.getElementById("openai-note");
const volumeSlider = document.getElementById("volume-slider");
const volumeValue = document.getElementById("volume-value");
const enableVideo = document.getElementById("enable-video");
const videoPreview = document.getElementById("video-preview");
const videoPlaceholder = document.getElementById("video-placeholder");
const videoStatus = document.getElementById("video-status");
const videoFrame = videoPreview.closest(".video-frame");

let volumeUpdateTimer = null;
let copyButtonTimer = null;
let videoRefreshTimer = null;
let lastHeardText = "";
let lastMoonshineDebugText = "";
let lastOpenAiBridgeText = "";
let selectedBackend = "rtc";
const whisperLiveModelStorageKey = "booster.whisperlive.model";
const moonshineModelStorageKey = "booster.moonshine.model";
const openAiModelStorageKey = "booster.openai.model";
const openAiTuningStorageKey = "booster.openai.tuning";
const openAiDefaults = {
  start_threshold: 1800,
  continue_threshold: 700,
  silence_frames: 8,
  max_frames: 90,
};

function getOpenAiTuning() {
  return {
    start_threshold: Number(openAiStartThreshold.value),
    continue_threshold: Number(openAiContinueThreshold.value),
    silence_frames: Number(openAiSilenceFrames.value),
    max_frames: Number(openAiMaxFrames.value),
  };
}

function renderOpenAiTuningValues() {
  openAiStartThresholdValue.textContent = String(openAiStartThreshold.value);
  openAiContinueThresholdValue.textContent = String(openAiContinueThreshold.value);
  openAiSilenceFramesValue.textContent = String(openAiSilenceFrames.value);
  openAiMaxFramesValue.textContent = String(openAiMaxFrames.value);
}

function persistOpenAiTuning() {
  window.localStorage.setItem(openAiTuningStorageKey, JSON.stringify(getOpenAiTuning()));
}

function setBackendStatus(element, label, active) {
  element.textContent = label;
  element.classList.toggle("backend-status-idle", !active);
}

function updateBackendControls() {
  const rtcSelected = selectedBackend === "rtc";
  const whisperLiveSelected = selectedBackend === "whisperlive_asr";
  const moonshineSelected = selectedBackend === "moonshine_asr";
  const openAiSelected = selectedBackend === "openai_asr";

  rtcStartButton.disabled = !rtcSelected;
  rtcStopButton.disabled = !rtcSelected;
  whisperLiveStartButton.disabled = !whisperLiveSelected;
  whisperLiveStopButton.disabled = !whisperLiveSelected;
  whisperLiveModel.disabled = !whisperLiveSelected;
  moonshineStartButton.disabled = !moonshineSelected;
  moonshineStopButton.disabled = !moonshineSelected;
  moonshineModel.disabled = !moonshineSelected;
  openAiStartButton.disabled = !openAiSelected;
  openAiStopButton.disabled = !openAiSelected;
  openAiModel.disabled = !openAiSelected;
  openAiTuningControls.hidden = !openAiSelected;
  openAiStartThreshold.disabled = !openAiSelected;
  openAiContinueThreshold.disabled = !openAiSelected;
  openAiSilenceFrames.disabled = !openAiSelected;
  openAiMaxFrames.disabled = !openAiSelected;

  setBackendStatus(rtcStatus, rtcSelected ? "Active" : "Inactive", rtcSelected);
  setBackendStatus(whisperLiveStatus, whisperLiveSelected ? "Active" : "Inactive", whisperLiveSelected);
  setBackendStatus(moonshineStatus, moonshineSelected ? "Active" : "Inactive", moonshineSelected);
  setBackendStatus(openAiStatus, openAiSelected ? "Active" : "Inactive", openAiSelected);
  whisperLiveNote.textContent = whisperLiveSelected
    ? "WhisperLive ASR listens on the robot mic and posts transcripts into the debug log."
    : "Select WhisperLive ASR to enable robot-side transcription controls.";
  moonshineNote.textContent = moonshineSelected
    ? "Moonshine ASR listens on the robot mic and posts transcripts into the heard window."
    : "Select Moonshine ASR to enable robot-side transcription controls.";
  openAiNote.textContent = openAiSelected
    ? "OpenAI ASR uploads detected speech segments for server-side transcription."
    : "Select OpenAI ASR to enable cloud transcription using the server-side API key.";
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

function filterMoonshineDebugLines(lines) {
  return lines.filter((line) => {
    const text = String(line || "").trim();
    if (!text) {
      return false;
    }
    return (
      text.includes("starting model=") ||
      text.includes("capture command:") ||
      text === "listening" ||
      text.endsWith(" listening") ||
      text.includes("partial:") ||
      text.includes("heard:") ||
      text.includes("error:") ||
      text.includes("fatal:") ||
      text.endsWith(" stopped") ||
      text === "stopped"
    );
  });
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
  const moonshine = data?.wrapper?.moonshine_asr || {};
  const nativeOpenAiBridge = data?.wrapper?.native_openai_bridge || {};
  const heard = typeof speech.last_heard === "string" ? speech.last_heard.trim() : "";

  if (heard && heard !== lastHeardText) {
    lastHeardText = heard;
    heardWindow.textContent = heard;
  }

  if (Array.isArray(moonshine.debug_tail) && moonshine.debug_tail.length > 0) {
    const filtered = filterMoonshineDebugLines(moonshine.debug_tail);
    const joined = filtered.join("\n").trim();
    if (joined && joined !== lastMoonshineDebugText) {
      lastMoonshineDebugText = joined;
      appendDebug("[Moonshine ASR] Debug", joined);
    }
  }

  const bridgeLines = Array.isArray(nativeOpenAiBridge.debug_tail) ? nativeOpenAiBridge.debug_tail : [];
  const bridgeResult = typeof nativeOpenAiBridge.last_result === "string" ? nativeOpenAiBridge.last_result.trim() : "";
  const bridgeSegment = typeof nativeOpenAiBridge.last_segment === "string" ? nativeOpenAiBridge.last_segment.trim() : "";
  const bridgeText = [
    bridgeResult ? `Last Result:\n${bridgeResult}` : "",
    bridgeSegment && bridgeSegment !== bridgeResult ? `Last Segment:\n${bridgeSegment}` : "",
    bridgeLines.length ? `Recent Log:\n${bridgeLines.join("\n")}` : "",
  ].filter(Boolean).join("\n\n");

  if (bridgeText && bridgeText !== lastOpenAiBridgeText) {
    lastOpenAiBridgeText = bridgeText;
    openAiBridgeWindow.textContent = bridgeText;
  } else if (!bridgeText && !lastOpenAiBridgeText) {
    openAiBridgeWindow.textContent = "Waiting for native OpenAI bridge output...";
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
    interrupt_speech_duration: 700,
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

moonshineStartButton.addEventListener("click", () => {
  const payload = {
    model: moonshineModel.value,
  };
  appendDebug("[Moonshine ASR] /moonshine/asr/start request", payload);
  postJson("/moonshine/asr/start", payload).catch((error) => {
    appendDebug("[Moonshine ASR] Start listening error", String(error));
  });
});

moonshineStopButton.addEventListener("click", () => {
  const payload = {};
  appendDebug("[Moonshine ASR] /moonshine/asr/stop request", payload);
  postJson("/moonshine/asr/stop", payload).catch((error) => {
    appendDebug("[Moonshine ASR] Stop listening error", String(error));
  });
});

moonshineModel.addEventListener("change", () => {
  window.localStorage.setItem(moonshineModelStorageKey, moonshineModel.value);
  appendDebug("Moonshine model selected", moonshineModel.value);
});

openAiStartButton.addEventListener("click", () => {
  const payload = {
    model: openAiModel.value,
    start_threshold: Number(openAiStartThreshold.value),
    continue_threshold: Number(openAiContinueThreshold.value),
    silence_frames: Number(openAiSilenceFrames.value),
    max_frames: Number(openAiMaxFrames.value),
  };
  appendDebug("[OpenAI ASR] /openai/asr/start request", payload);
  postJson("/openai/asr/start", payload).catch((error) => {
    appendDebug("[OpenAI ASR] Start listening error", String(error));
  });
});

openAiStopButton.addEventListener("click", () => {
  const payload = {};
  appendDebug("[OpenAI ASR] /openai/asr/stop request", payload);
  postJson("/openai/asr/stop", payload).catch((error) => {
    appendDebug("[OpenAI ASR] Stop listening error", String(error));
  });
});

openAiModel.addEventListener("change", () => {
  window.localStorage.setItem(openAiModelStorageKey, openAiModel.value);
  appendDebug("OpenAI model selected", openAiModel.value);
});

for (const [slider, render] of [
  [openAiStartThreshold, renderOpenAiTuningValues],
  [openAiContinueThreshold, renderOpenAiTuningValues],
  [openAiSilenceFrames, renderOpenAiTuningValues],
  [openAiMaxFrames, renderOpenAiTuningValues],
]) {
  slider.addEventListener("input", () => {
    render();
    persistOpenAiTuning();
  });
}

for (const option of backendOptions) {
  option.addEventListener("change", () => {
    if (!option.checked) {
      return;
    }
    selectedBackend = option.value;
    updateBackendControls();
    appendDebug(
      "Speech backend selected",
      selectedBackend === "rtc"
        ? "Native RTC"
        : selectedBackend === "whisperlive_asr"
          ? "WhisperLive ASR"
          : selectedBackend === "moonshine_asr"
            ? "Moonshine ASR"
            : "OpenAI ASR",
    );
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
if (!lastOpenAiBridgeText) {
  openAiBridgeWindow.textContent = "Waiting for native OpenAI bridge output...";
}

setVideoPreviewState(false);
videoStatus.textContent = "Off";
{
  const savedModel = window.localStorage.getItem(whisperLiveModelStorageKey);
  if (savedModel) {
    whisperLiveModel.value = savedModel;
  }
}
{
  const savedModel = window.localStorage.getItem(moonshineModelStorageKey);
  if (savedModel) {
    moonshineModel.value = savedModel;
  } else {
    moonshineModel.value = "medium-streaming";
  }
}
{
  const savedModel = window.localStorage.getItem(openAiModelStorageKey);
  if (savedModel) {
    openAiModel.value = savedModel;
  } else {
    openAiModel.value = "gpt-4o-mini-transcribe";
  }
}
{
  let tuning = openAiDefaults;
  const raw = window.localStorage.getItem(openAiTuningStorageKey);
  if (raw) {
    try {
      tuning = { ...openAiDefaults, ...JSON.parse(raw) };
    } catch (_error) {
    }
  }
  openAiStartThreshold.value = String(tuning.start_threshold);
  openAiContinueThreshold.value = String(tuning.continue_threshold);
  openAiSilenceFrames.value = String(tuning.silence_frames);
  openAiMaxFrames.value = String(tuning.max_frames);
}
renderOpenAiTuningValues();
updateBackendControls();
setCopyButtonState("Copy Log");
