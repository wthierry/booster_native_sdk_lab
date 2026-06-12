const batteryLevel = document.getElementById("battery-level");
const batteryPercent = document.getElementById("battery-percent");
const batteryDetails = document.getElementById("battery-details");
const batteryPill = document.getElementById("battery-pill");
const debugWindow = document.getElementById("debug-window");
const heardWindow = document.getElementById("heard-window");
const openAiBridgeWindow = document.getElementById("openai-bridge-window");
const simpleChatStatus = document.getElementById("simplechat-status");
const simpleChatInput = document.getElementById("simplechat-input");
const simpleChatResetButton = document.getElementById("simplechat-reset");
const simpleChatOutput = document.getElementById("simplechat-output");
const copyDebugButton = document.getElementById("copy-debug");
const copyBridgeButton = document.getElementById("copy-bridge");
const clearLogsButton = document.getElementById("clear-logs");
const backendOptions = Array.from(document.querySelectorAll('input[name="speech-backend"]'));
const backendOptionLabels = {
  rtc: document.getElementById("backend-option-rtc"),
  whisperlive_asr: document.getElementById("backend-option-whisperlive"),
  moonshine_asr: document.getElementById("backend-option-moonshine"),
  openai_realtime: document.getElementById("backend-option-openai-realtime"),
};
const backendPanels = {
  whisperlive_asr: document.getElementById("backend-panel-whisperlive"),
  moonshine_asr: document.getElementById("backend-panel-moonshine"),
  openai_realtime: document.getElementById("backend-panel-openai-realtime"),
};
const rtcStartButton = document.getElementById("rtc-start");
const rtcStopButton = document.getElementById("rtc-stop");
const whisperLiveStartButton = document.getElementById("whisperlive-start");
const whisperLiveStopButton = document.getElementById("whisperlive-stop");
const whisperLiveModel = document.getElementById("whisperlive-model");
const moonshineStartButton = document.getElementById("moonshine-start");
const moonshineStopButton = document.getElementById("moonshine-stop");
const moonshineModel = document.getElementById("moonshine-model");
const moonshineUpdateInterval = document.getElementById("moonshine-update-interval");
const moonshineUpdateIntervalValue = document.getElementById("moonshine-update-interval-value");
const moonshineVadThreshold = document.getElementById("moonshine-vad-threshold");
const moonshineVadThresholdValue = document.getElementById("moonshine-vad-threshold-value");
const openAiRealtimeStartButton = document.getElementById("openai-realtime-start");
const openAiRealtimeStopButton = document.getElementById("openai-realtime-stop");
const openAiRealtimeModel = document.getElementById("openai-realtime-model");
const rtcStatus = document.getElementById("rtc-status");
const whisperLiveStatus = document.getElementById("whisperlive-status");
const whisperLiveNote = document.getElementById("whisperlive-note");
const moonshineStatus = document.getElementById("moonshine-status");
const moonshineNote = document.getElementById("moonshine-note");
const openAiRealtimeStatus = document.getElementById("openai-realtime-status");
const openAiRealtimeNote = document.getElementById("openai-realtime-note");
const volumeSlider = document.getElementById("volume-slider");
const volumeValue = document.getElementById("volume-value");
const enableVideo = document.getElementById("enable-video");
const videoPreview = document.getElementById("video-preview");
const videoPlaceholder = document.getElementById("video-placeholder");
const videoStatus = document.getElementById("video-status");
const videoFrame = videoPreview.closest(".video-frame");
const robotPoseCanvas = document.getElementById("robot-pose-canvas");
const robotPoseStatus = document.getElementById("robot-pose-status");
const robotPoseMeta = document.getElementById("robot-pose-meta");
const robotPoseUseMeshes = document.getElementById("robot-pose-use-meshes");

let volumeUpdateTimer = null;
let copyButtonTimer = null;
let copyBridgeButtonTimer = null;
let videoRefreshTimer = null;
let lastHeardText = "";
let lastHeardWindowText = "";
let lastWhisperLiveDebugText = "";
let lastMoonshineDebugText = "";
let lastOpenAiBridgeText = "";
let lastOpenAiRealtimeLogText = "";
let lastSimpleChatHealthText = "";
let hasSpeechDebugBaseline = false;
let speechDebugRefreshInFlight = false;
let selectedBackend = "openai_realtime";
let backendAvailability = {
  rtc: true,
  whisperlive_asr: false,
  moonshine_asr: false,
  openai_realtime: false,
};
const whisperLiveModelStorageKey = "booster.whisperlive.model";
const moonshineModelStorageKey = "booster.moonshine.model";
const moonshineUpdateIntervalStorageKey = "booster.moonshine.updateInterval";
const moonshineVadThresholdStorageKey = "booster.moonshine.vadThreshold";
const openAiRealtimeModelStorageKey = "booster.openai.realtime.model";
const robotPoseUseMeshesStorageKey = "booster.pose.useMeshes";

const moonshineDefaults = {
  updateInterval: "0.2",
  vadThreshold: "0.5",
};

const robotPoseViewer = window.BoosterPoseViewer
  ? window.BoosterPoseViewer.create({
      canvas: robotPoseCanvas,
      statusElement: robotPoseStatus,
      metaElement: robotPoseMeta,
      controllerElement: robotPoseUseMeshes,
    })
  : { update() {}, setUseMeshes() {} };

if (robotPoseUseMeshes) {
  const savedValue = window.localStorage.getItem(robotPoseUseMeshesStorageKey);
  if (savedValue !== null) {
    robotPoseUseMeshes.checked = savedValue === "true";
  }
  robotPoseViewer.setUseMeshes(robotPoseUseMeshes.checked);
  robotPoseUseMeshes.addEventListener("change", () => {
    window.localStorage.setItem(robotPoseUseMeshesStorageKey, String(robotPoseUseMeshes.checked));
    robotPoseViewer.setUseMeshes(robotPoseUseMeshes.checked);
  });
}

function resetHeardWindow(text = "Waiting for speech...") {
  lastHeardText = "";
  lastHeardWindowText = text;
  heardWindow.textContent = text;
}

function resetRealtimeLogState() {
  lastOpenAiRealtimeLogText = "";
}

function summarizeWhisperLiveState(whisperLive) {
  if (!whisperLive || typeof whisperLive !== "object") {
    return "";
  }
  const summary = {
    available: Boolean(whisperLive.available),
    running: Boolean(whisperLive.running),
    state: whisperLive.state || "",
    pid: Number(whisperLive.pid || 0),
    model: whisperLive.model || "",
    language: whisperLive.language || "",
    source: whisperLive.source || "",
    server_backend: whisperLive.server_backend || "",
    server_host: whisperLive.server_host || "",
    server_port: Number(whisperLive.server_port || 0),
    last_heard: whisperLive.last_heard || "",
    last_partial: whisperLive.last_partial || "",
    last_error: whisperLive.last_error || "",
    last_rms: Number(whisperLive.last_rms || 0),
    peak_rms: Number(whisperLive.peak_rms || 0),
    segment_count: Number(whisperLive.segment_count || 0),
    updated_at: whisperLive.updated_at || "",
  };
  return JSON.stringify(summary, null, 2);
}

function updateMoonshineControlLabels() {
  moonshineUpdateIntervalValue.textContent = `${Number(moonshineUpdateInterval.value).toFixed(1)}s`;
  moonshineVadThresholdValue.textContent = Number(moonshineVadThreshold.value).toFixed(1);
}

function setBackendStatus(element, label, active) {
  element.textContent = label;
  element.classList.toggle("backend-status-idle", !active);
}

function updateBackendControls() {
  const rtcSelected = selectedBackend === "rtc";
  const whisperLiveEnabled = Boolean(backendAvailability.whisperlive_asr);
  const moonshineEnabled = Boolean(backendAvailability.moonshine_asr);
  const openAiRealtimeEnabled = Boolean(backendAvailability.openai_realtime);
  const whisperLiveSelected = whisperLiveEnabled && selectedBackend === "whisperlive_asr";
  const moonshineSelected = moonshineEnabled && selectedBackend === "moonshine_asr";
  const openAiRealtimeSelected = openAiRealtimeEnabled && selectedBackend === "openai_realtime";

  rtcStartButton.disabled = !rtcSelected;
  rtcStopButton.disabled = !rtcSelected;
  whisperLiveStartButton.disabled = !whisperLiveSelected;
  whisperLiveStopButton.disabled = !whisperLiveSelected;
  whisperLiveModel.disabled = !whisperLiveSelected;
  moonshineStartButton.disabled = !moonshineSelected;
  moonshineStopButton.disabled = !moonshineSelected;
  moonshineModel.disabled = !moonshineSelected;
  openAiRealtimeStartButton.disabled = !openAiRealtimeSelected;
  openAiRealtimeStopButton.disabled = !openAiRealtimeSelected;
  openAiRealtimeModel.disabled = !openAiRealtimeSelected;

  setBackendStatus(rtcStatus, rtcSelected ? "Active" : "Inactive", rtcSelected);
  setBackendStatus(whisperLiveStatus, whisperLiveSelected ? "Active" : "Inactive", whisperLiveSelected);
  setBackendStatus(moonshineStatus, moonshineSelected ? "Active" : "Inactive", moonshineSelected);
  setBackendStatus(openAiRealtimeStatus, openAiRealtimeSelected ? "Active" : "Inactive", openAiRealtimeSelected);
  whisperLiveNote.textContent = whisperLiveSelected
    ? "WhisperLive ASR listens on the robot mic and posts transcripts into the debug log."
    : "Select WhisperLive ASR to enable robot-side transcription controls.";
  moonshineNote.textContent = moonshineSelected
    ? "Moonshine ASR listens on the robot mic and posts transcripts into the heard window."
    : "Select Moonshine ASR to enable robot-side transcription controls.";
  openAiRealtimeNote.textContent = openAiRealtimeSelected
    ? "OpenAI Realtime streams the robot mic and requests a text response from the assistant."
    : "Select OpenAI Realtime to enable the experimental live voice path.";
}

function applyBackendAvailability(speechBackends) {
  const nextAvailability = {
    rtc: true,
    whisperlive_asr: Boolean(speechBackends?.whisperlive_asr?.available),
    moonshine_asr: Boolean(speechBackends?.moonshine_asr?.available),
    openai_realtime: Boolean(speechBackends?.openai_realtime?.available),
  };
  backendAvailability = nextAvailability;

  for (const [backend, label] of Object.entries(backendOptionLabels)) {
    if (label) {
      label.hidden = !nextAvailability[backend];
    }
  }
  for (const [backend, panel] of Object.entries(backendPanels)) {
    if (panel) {
      panel.hidden = !nextAvailability[backend];
    }
  }

  if (!nextAvailability[selectedBackend]) {
    selectedBackend = nextAvailability.openai_realtime ? "openai_realtime" : "rtc";
    const fallbackOption = document.getElementById(
      selectedBackend === "openai_realtime" ? "backend-openai-realtime" : "backend-rtc",
    );
    if (fallbackOption) {
      fallbackOption.checked = true;
    }
  }

  updateBackendControls();
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

function filterNativeBridgeLines(lines) {
  return lines.filter((line) => {
    const text = String(line || "").trim();
    if (!text) {
      return false;
    }
    return (
      text.includes("connection_open") ||
      text.includes("connection_closed") ||
      text.includes("recv_config_json=") ||
      text.includes("openai_request_json=") ||
      text.includes("openai_response_json=") ||
      text.includes("sent_result_json=")
    );
  });
}

function formatBridgeJsonLine(text, prefix, label) {
  if (!text.startsWith(prefix)) {
    return null;
  }
  const raw = text.slice(prefix.length).trim();
  try {
    const parsed = JSON.parse(raw);
    return `${label}\n${JSON.stringify(parsed, null, 2)}`;
  } catch (_error) {
    return `${label}\n${raw}`;
  }
}

function formatNativeBridgeLine(line) {
  const text = String(line || "").trim();
  if (!text) {
    return "";
  }

  const stamped = text.replace(/^\d{4}-\d{2}-\d{2}T[^\s]+\s+/, "");
  if (stamped.includes("connection_open")) {
    return "ASR Start\nconnection_open";
  }
  if (stamped.includes("connection_closed")) {
    return "ASR Stop\nconnection_closed";
  }

  return (
    formatBridgeJsonLine(stamped, "recv_config_json=", "Received Config JSON") ||
    formatBridgeJsonLine(stamped, "openai_request_json=", "OpenAI Request JSON") ||
    formatBridgeJsonLine(stamped, "openai_response_json=", "OpenAI Response JSON") ||
    formatBridgeJsonLine(stamped, "sent_result_json=", "Sent Result JSON") ||
    stamped
  );
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

function setCopyBridgeButtonState(label, copied = false) {
  copyBridgeButton.textContent = label;
  copyBridgeButton.classList.toggle("is-copied", copied);
  if (copyBridgeButtonTimer) {
    window.clearTimeout(copyBridgeButtonTimer);
  }
  if (label !== "Copy JSON") {
    copyBridgeButtonTimer = window.setTimeout(() => {
      copyBridgeButton.textContent = "Copy JSON";
      copyBridgeButton.classList.remove("is-copied");
      copyBridgeButtonTimer = null;
    }, 1600);
  }
}

function clearLogWindows() {
  debugWindow.textContent = "";
  openAiBridgeWindow.textContent = "Waiting for native OpenAI bridge output...";
  lastOpenAiBridgeText = "";
  setCopyButtonState("Copy Log");
  setCopyBridgeButtonState("Copy JSON");
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

async function fetchJson(path) {
  const response = await fetch(path);
  const data = await response.json();
  return data;
}

function renderSimpleChatStatus(data) {
  const ok = Boolean(data?.ok);
  const turns = Number(data?.turns || 0);
  const label = ok ? `Ready (${turns})` : "Offline";
  setBackendStatus(simpleChatStatus, label, ok);
  simpleChatInput.disabled = !ok;
  simpleChatResetButton.disabled = !ok;
  if (!ok) {
    simpleChatOutput.textContent = data?.error
      ? `Local chat unavailable\n${data.error}`
      : "Local chat unavailable";
  }
}

async function refreshSimpleChatHealth() {
  const data = await fetchJson("/simplechat/health");
  renderSimpleChatStatus(data);
  const summary = JSON.stringify({
    ok: Boolean(data?.ok),
    status: data?.status || "",
    turns: Number(data?.turns || 0),
    error: data?.error || "",
  });
  if (summary !== lastSimpleChatHealthText) {
    lastSimpleChatHealthText = summary;
    appendDebug("[SimpleChat] Health", data);
  }
}

async function refreshSpeechDebug() {
  if (speechDebugRefreshInFlight) {
    return;
  }
  speechDebugRefreshInFlight = true;
  try {
  const response = await fetch("/health");
  const data = await response.json();
  applyBackendAvailability(data?.wrapper?.speech_backends || {});
  const speech = data?.wrapper?.speech_debug || {};
  const whisperLive = data?.wrapper?.whisperlive_asr || {};
  const moonshine = data?.wrapper?.moonshine_asr || {};
  const openAiRealtime = data?.wrapper?.openai_realtime || {};
  const nativeOpenAiBridge = data?.wrapper?.native_openai_bridge || {};
  const robotJointState = data?.wrapper?.robot_joint_state || {};
  robotPoseViewer.update(robotJointState);
  const openAiRealtimeRunning = Boolean(openAiRealtime.running);
  const speechSource = selectedBackend === "openai_realtime"
    ? openAiRealtime
    : selectedBackend === "whisperlive_asr"
      ? whisperLive
      : selectedBackend === "moonshine_asr"
        ? moonshine
        : speech;
  const heard = typeof speechSource.last_heard === "string" ? speechSource.last_heard.trim() : "";
  const spoken = typeof speechSource.last_spoken === "string" ? speechSource.last_spoken.trim() : "";
  let heardWindowText = "Waiting for speech...";

  if (heard && spoken) {
    heardWindowText = `User Said\n${heard}\n\nRobot Response\n${spoken}`;
  } else if (heard) {
    heardWindowText = `User Said\n${heard}`;
  } else if (spoken) {
    heardWindowText = `Robot Response\n${spoken}`;
  }

  const whisperLiveDebugText = summarizeWhisperLiveState(whisperLive);
  if (whisperLiveDebugText && whisperLiveDebugText !== lastWhisperLiveDebugText) {
    lastWhisperLiveDebugText = whisperLiveDebugText;
    appendDebug("[WhisperLive ASR] State", whisperLive);
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
  const filteredBridgeLines = filterNativeBridgeLines(bridgeLines);
  const bridgeText = filteredBridgeLines
    .map(formatNativeBridgeLine)
    .filter(Boolean)
    .join("\n\n")
    .trim();

  if (!hasSpeechDebugBaseline) {
    hasSpeechDebugBaseline = true;
    lastHeardText = heard;
    lastHeardWindowText = heardWindowText;
    heardWindow.textContent = heardWindowText;
    if (Array.isArray(moonshine.debug_tail) && moonshine.debug_tail.length > 0) {
      const filtered = filterMoonshineDebugLines(moonshine.debug_tail);
      lastMoonshineDebugText = filtered.join("\n").trim();
    }
    lastWhisperLiveDebugText = whisperLiveDebugText;
    if (openAiRealtimeRunning && Array.isArray(openAiRealtime.log_tail) && openAiRealtime.log_tail.length > 0) {
      lastOpenAiRealtimeLogText = openAiRealtime.log_tail.join("\n").trim();
    } else {
      lastOpenAiRealtimeLogText = "";
    }
    lastOpenAiBridgeText = bridgeText;
    openAiBridgeWindow.textContent = bridgeText || "Waiting for native OpenAI bridge output...";
    return;
  }

  if (heard !== lastHeardText) {
    lastHeardText = heard;
  }

  if (heardWindowText !== lastHeardWindowText) {
    lastHeardWindowText = heardWindowText;
    heardWindow.textContent = heardWindowText;
  }

  if (openAiRealtimeRunning && Array.isArray(openAiRealtime.log_tail) && openAiRealtime.log_tail.length > 0) {
    const joined = openAiRealtime.log_tail.join("\n").trim();
    if (joined && joined !== lastOpenAiRealtimeLogText) {
      lastOpenAiRealtimeLogText = joined;
      appendDebug("[OpenAI Realtime] Log", joined);
    }
  } else {
    lastOpenAiRealtimeLogText = "";
  }

  if (bridgeText && bridgeText !== lastOpenAiBridgeText) {
    lastOpenAiBridgeText = bridgeText;
    openAiBridgeWindow.textContent = bridgeText;
    appendDebug("[OpenAI Bridge] Log", bridgeText);
  } else if (!bridgeText && !lastOpenAiBridgeText) {
    openAiBridgeWindow.textContent = "Waiting for native OpenAI bridge output...";
  }
  } finally {
    speechDebugRefreshInFlight = false;
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
  const payload = {};
  appendDebug("[Native ASR] /rtc/tts/start request", payload);
  try {
    await postJson("/rtc/tts/start", payload);
  } catch (error) {
    appendDebug("[Native ASR] Start listening error", String(error));
  }
});

rtcStopButton.addEventListener("click", async () => {
  const payload = {};
  appendDebug("[Native ASR] /rtc/tts/stop request", payload);
  try {
    await postJson("/rtc/tts/stop", payload);
  } catch (error) {
    appendDebug("[Native ASR] Stop listening error", String(error));
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
    update_interval: Number(moonshineUpdateInterval.value),
    vad_threshold: Number(moonshineVadThreshold.value),
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

moonshineUpdateInterval.addEventListener("input", () => {
  updateMoonshineControlLabels();
  window.localStorage.setItem(moonshineUpdateIntervalStorageKey, moonshineUpdateInterval.value);
});

moonshineVadThreshold.addEventListener("input", () => {
  updateMoonshineControlLabels();
  window.localStorage.setItem(moonshineVadThresholdStorageKey, moonshineVadThreshold.value);
});

openAiRealtimeStartButton.addEventListener("click", () => {
  resetHeardWindow("Waiting for OpenAI Realtime...");
  resetRealtimeLogState();
  hasSpeechDebugBaseline = false;
  const payload = {
    model: openAiRealtimeModel.value,
  };
  appendDebug("[OpenAI Realtime] /openai/realtime/start request", payload);
  postJson("/openai/realtime/start", payload).catch((error) => {
    appendDebug("[OpenAI Realtime] Start listening error", String(error));
  });
});

openAiRealtimeStopButton.addEventListener("click", () => {
  resetHeardWindow();
  resetRealtimeLogState();
  hasSpeechDebugBaseline = false;
  const payload = {};
  appendDebug("[OpenAI Realtime] /openai/realtime/stop request", payload);
  postJson("/openai/realtime/stop", payload).catch((error) => {
    appendDebug("[OpenAI Realtime] Stop listening error", String(error));
  });
});

openAiRealtimeModel.addEventListener("change", () => {
  window.localStorage.setItem(openAiRealtimeModelStorageKey, openAiRealtimeModel.value);
  appendDebug("OpenAI Realtime model selected", openAiRealtimeModel.value);
});

async function sendSimpleChatMessage() {
  const text = simpleChatInput.value.trim();
  if (!text) {
    simpleChatOutput.textContent = "Enter a message first.";
    return;
  }
  simpleChatInput.disabled = true;
  simpleChatResetButton.disabled = true;
  simpleChatOutput.textContent = "Waiting for local reply...";
  try {
    const data = await postJson("/simplechat/reply", { text });
    renderSimpleChatStatus(data.ok ? { ok: true, turns: data.turns } : { ok: false, error: data.error });
    simpleChatOutput.textContent = data.ok ? data.reply || "" : `Error\n${data.error || "Unknown error"}`;
    if (data.ok) {
      simpleChatInput.value = "";
    }
  } catch (error) {
    simpleChatOutput.textContent = `Error\n${String(error)}`;
    setBackendStatus(simpleChatStatus, "Offline", false);
    appendDebug("[SimpleChat] Reply error", String(error));
  } finally {
    simpleChatInput.disabled = false;
    simpleChatResetButton.disabled = false;
    simpleChatInput.focus();
  }
}

simpleChatResetButton.addEventListener("click", async () => {
  simpleChatResetButton.disabled = true;
  simpleChatInput.disabled = true;
  try {
    const data = await postJson("/simplechat/reset", {});
    renderSimpleChatStatus(data.ok ? { ok: true, turns: 0 } : { ok: false, error: data.error });
    simpleChatOutput.textContent = data.ok ? "Chat reset." : `Error\n${data.error || "Unknown error"}`;
  } catch (error) {
    simpleChatOutput.textContent = `Error\n${String(error)}`;
    setBackendStatus(simpleChatStatus, "Offline", false);
    appendDebug("[SimpleChat] Reset error", String(error));
  } finally {
    simpleChatResetButton.disabled = false;
    simpleChatInput.disabled = false;
    simpleChatInput.focus();
  }
});

simpleChatInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    sendSimpleChatMessage();
  }
});

for (const option of backendOptions) {
  option.addEventListener("change", () => {
    if (!option.checked) {
      return;
    }
    if (!backendAvailability[option.value]) {
      option.checked = false;
      selectedBackend = backendAvailability.openai_realtime ? "openai_realtime" : "rtc";
      const fallbackOption = document.getElementById(
        selectedBackend === "openai_realtime" ? "backend-openai-realtime" : "backend-rtc",
      );
      if (fallbackOption) {
        fallbackOption.checked = true;
      }
      updateBackendControls();
      return;
    }
    selectedBackend = option.value;
    resetHeardWindow();
    hasSpeechDebugBaseline = false;
    updateBackendControls();
    appendDebug(
      "Speech backend selected",
      selectedBackend === "rtc"
        ? "Native ASR"
        : selectedBackend === "whisperlive_asr"
          ? "WhisperLive ASR"
          : selectedBackend === "moonshine_asr"
            ? "Moonshine ASR"
            : "OpenAI Realtime",
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

copyBridgeButton.addEventListener("click", async () => {
  const text = openAiBridgeWindow.textContent.trim();
  if (!text || text === "Waiting for native OpenAI bridge output...") {
    setCopyBridgeButtonState("Nothing to copy");
    return;
  }

  try {
    await copyText(text);
    setCopyBridgeButtonState("Copied", true);
  } catch (error) {
    setCopyBridgeButtonState("Copy failed");
    appendDebug("Bridge copy error", String(error));
  }
});

clearLogsButton.addEventListener("click", async () => {
  try {
    await postJson("/logs/clear", {});
  } catch (error) {
    appendDebug("Clear logs error", String(error));
  }
  clearLogWindows();
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
  refreshSimpleChatHealth().catch((error) => {
    console.error("SimpleChat health error", error);
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

refreshSimpleChatHealth().catch((error) => {
  appendDebug("Initial simplechat error", String(error));
  setBackendStatus(simpleChatStatus, "Offline", false);
  simpleChatInput.disabled = true;
  simpleChatResetButton.disabled = true;
  simpleChatOutput.textContent = "Local chat unavailable\nStart a local booster_simplechat_service on port 8092.";
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
  const savedUpdateInterval = window.localStorage.getItem(moonshineUpdateIntervalStorageKey);
  moonshineUpdateInterval.value = savedUpdateInterval || moonshineDefaults.updateInterval;
}
{
  const savedVadThreshold = window.localStorage.getItem(moonshineVadThresholdStorageKey);
  moonshineVadThreshold.value = savedVadThreshold || moonshineDefaults.vadThreshold;
}
{
  const savedModel = window.localStorage.getItem(openAiRealtimeModelStorageKey);
  if (savedModel) {
    openAiRealtimeModel.value = savedModel;
  } else {
    openAiRealtimeModel.value = "gpt-realtime-2";
  }
}
updateMoonshineControlLabels();
updateBackendControls();
setCopyButtonState("Copy Log");
