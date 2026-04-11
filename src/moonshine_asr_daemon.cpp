#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#elif __has_include(<booster/third_party/nlohmann_json/json.hpp>)
#include <booster/third_party/nlohmann_json/json.hpp>
#else
#error "nlohmann json header not found"
#endif

#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>

#include "moonshine-c-api.h"

using json = nlohmann::json;

namespace {

std::string Trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  return value.substr(start);
}

std::string GetEnvOrDefault(const char *name, const char *fallback) {
  if (const char *value = std::getenv(name)) {
    const std::string trimmed = Trim(value);
    if (!trimmed.empty()) {
      return trimmed;
    }
  }
  return fallback;
}

double GetEnvDouble(const char *name, double fallback) {
  if (const char *value = std::getenv(name)) {
    return std::atof(value);
  }
  return fallback;
}

int GetEnvInt(const char *name, int fallback) {
  if (const char *value = std::getenv(name)) {
    return std::atoi(value);
  }
  return fallback;
}

std::string NormalizeInputWavPath(std::string path) {
  path = Trim(path);
  if (path.empty()) {
    return "/tmp/booster_moonshine_asr_input.wav";
  }

  std::error_code ec;
  const std::filesystem::path fs_path(path);
  if (std::filesystem::is_directory(fs_path, ec)) {
    return (fs_path / "input.wav").string();
  }
  return path;
}

const std::string kStatePath =
    GetEnvOrDefault("BOOSTER_MOONSHINE_ASR_STATE_PATH", "/tmp/booster_moonshine_asr_state.json");
const std::string kLogPath =
    GetEnvOrDefault("BOOSTER_MOONSHINE_ASR_LOG_PATH", "/tmp/booster_moonshine_asr.log");
const std::string kDebugLogPath =
    GetEnvOrDefault("BOOSTER_MOONSHINE_ASR_DEBUG_LOG_PATH", "/tmp/booster_moonshine_asr_debug.log");
const std::string kInputWavPath =
    NormalizeInputWavPath(
        GetEnvOrDefault("BOOSTER_MOONSHINE_ASR_INPUT_WAV_PATH", "/tmp/booster_moonshine_asr_input.wav"));
const std::string kLanguage = GetEnvOrDefault("BOOSTER_MOONSHINE_ASR_LANGUAGE", "en");
const std::string kModelName = GetEnvOrDefault("BOOSTER_MOONSHINE_ASR_MODEL", "medium-streaming");
const std::string kDeviceName = GetEnvOrDefault("BOOSTER_MOONSHINE_ASR_DEVICE", "default");
const double kUpdateIntervalSec = GetEnvDouble("BOOSTER_MOONSHINE_ASR_UPDATE_INTERVAL_SEC", 0.2);
const double kPrerollSec = GetEnvDouble("BOOSTER_MOONSHINE_ASR_PREROLL_SEC", 0.5);
const int kSampleRate = GetEnvInt("BOOSTER_MOONSHINE_ASR_SAMPLE_RATE", 16000);
const int kBlockSize = GetEnvInt("BOOSTER_MOONSHINE_ASR_BLOCKSIZE", 1024);
const int kChannels = GetEnvInt("BOOSTER_MOONSHINE_ASR_CHANNELS", 1);

std::atomic<bool> gRunning{true};
std::atomic<bool> gResetRequested{false};
std::mutex gStateMutex;
json gState = json::object();

std::string IsoNow() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const std::time_t time = clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&time, &tm);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

void WriteStateUnlocked() {
  gState["updated_at"] = IsoNow();
  const std::filesystem::path path(kStatePath);
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  const std::string tmp_path = kStatePath + ".tmp";
  std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
  out << gState.dump(2) << "\n";
  out.close();
  std::filesystem::rename(tmp_path, kStatePath);
}

template <typename Fn>
void UpdateState(Fn &&fn) {
  std::scoped_lock lock(gStateMutex);
  fn(gState);
  WriteStateUnlocked();
}

void LogLine(const std::string &message) {
  const std::string line = IsoNow() + " " + message + "\n";
  std::ofstream session_out(kLogPath, std::ios::app);
  session_out << line;
  std::ofstream debug_out(kDebugLogPath, std::ios::app);
  debug_out << line;
}

std::optional<uint32_t> ResolveModelArch(const std::string &name) {
  static const std::map<std::string, uint32_t> kModelMap = {
      {"tiny", MOONSHINE_MODEL_ARCH_TINY},
      {"base", MOONSHINE_MODEL_ARCH_BASE},
      {"tiny-streaming", MOONSHINE_MODEL_ARCH_TINY_STREAMING},
      {"base-streaming", MOONSHINE_MODEL_ARCH_BASE_STREAMING},
      {"small-streaming", MOONSHINE_MODEL_ARCH_SMALL_STREAMING},
      {"medium-streaming", MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING},
  };
  const auto it = kModelMap.find(Trim(name));
  if (it == kModelMap.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string DefaultModelRoot() {
  if (const char *configured = std::getenv("BOOSTER_MOONSHINE_ASR_MODEL_ROOT")) {
    const std::string root = Trim(configured);
    if (!root.empty()) {
      return root;
    }
  }
  if (const char *home = std::getenv("HOME")) {
    return std::string(home) + "/.cache/moonshine_voice/download.moonshine.ai/model";
  }
  return "/tmp";
}

std::string ResolveModelPath(const std::string &model_name, const std::string &language) {
  if (const char *configured = std::getenv("BOOSTER_MOONSHINE_ASR_MODEL_PATH")) {
    const std::string path = Trim(configured);
    if (!path.empty()) {
      return path;
    }
  }
  return DefaultModelRoot() + "/" + model_name + "-" + language + "/quantized";
}

std::string MoonshineError(int32_t code) {
  const char *error = moonshine_error_to_string(code);
  if (error != nullptr) {
    return error;
  }
  return "unknown error";
}

std::optional<std::string> ExecReadFirstLine(const char *command) {
  FILE *pipe = popen(command, "r");
  if (pipe == nullptr) {
    return std::nullopt;
  }

  char buffer[512];
  std::string line;
  if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    line = Trim(buffer);
  }
  pclose(pipe);

  if (line.empty()) {
    return std::nullopt;
  }
  return line;
}

std::optional<std::string> ResolvePulseSourceFromEnv() {
  if (const char *value = std::getenv("BOOSTER_MOONSHINE_ASR_SOURCE")) {
    const std::string trimmed = Trim(value);
    if (!trimmed.empty()) {
      return trimmed;
    }
  }
  return std::nullopt;
}

std::optional<std::string> ResolvePulseSourceFromPactl() {
  static constexpr const char *kHardwarePreferredCommand =
      "pactl list short sources | awk '/iflytek|XFM-DP/ {print $2; exit}'";
  static constexpr const char *kDefaultSourceCommand =
      "pactl info | sed -n 's/^Default Source: //p' | head -n 1";

  if (const auto preferred = ExecReadFirstLine(kHardwarePreferredCommand)) {
    return preferred;
  }
  return ExecReadFirstLine(kDefaultSourceCommand);
}

std::string DefaultPulseSource() {
  return "alsa_input.usb-iflytek_XFM-DP-V0.0.18_bc00144082144751c10-01.mono-fallback";
}

std::string ResolvePulseSource() {
  if (const auto configured = ResolvePulseSourceFromEnv()) {
    return *configured;
  }
  if (const auto detected = ResolvePulseSourceFromPactl()) {
    return *detected;
  }
  return DefaultPulseSource();
}

void RequireMoonshine(int32_t code, const std::string &context) {
  if (code < 0) {
    throw std::runtime_error(context + ": " + MoonshineError(code));
  }
}

struct CaptureProcess {
  int fd = -1;
  pid_t pid = -1;
};

CaptureProcess StartCapture(const std::string &pulse_source) {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    throw std::runtime_error("pipe() failed");
  }

  const pid_t child = fork();
  if (child < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    throw std::runtime_error("fork() failed");
  }

  if (child == 0) {
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    execlp("parec", "parec",
           "--device", pulse_source.c_str(),
           "--format=s16le",
           "--rate", std::to_string(kSampleRate).c_str(),
           "--channels", std::to_string(kChannels).c_str(),
           "--raw",
           static_cast<char *>(nullptr));
    _exit(127);
  }

  close(pipe_fds[1]);
  return {pipe_fds[0], child};
}

void StopCapture(CaptureProcess &capture) {
  if (capture.pid > 0) {
    kill(capture.pid, SIGTERM);
    int status = 0;
    waitpid(capture.pid, &status, 0);
    capture.pid = -1;
  }
  if (capture.fd >= 0) {
    close(capture.fd);
    capture.fd = -1;
  }
}

class WavWriter {
 public:
  explicit WavWriter(const std::string &path) : path_(path) {
    const std::filesystem::path fs_path(path_);
    if (fs_path.has_parent_path()) {
      std::filesystem::create_directories(fs_path.parent_path());
    }
    out_.open(path_, std::ios::binary | std::ios::trunc);
    if (!out_) {
      throw std::runtime_error("Unable to open wav path: " + path_);
    }
    WriteHeader(0);
  }

  ~WavWriter() { Finalize(); }

  void Append(const char *data, std::size_t size) {
    out_.write(data, static_cast<std::streamsize>(size));
    data_bytes_ += static_cast<uint32_t>(size);
  }

  void Finalize() {
    if (!out_.is_open()) {
      return;
    }
    out_.flush();
    out_.seekp(0, std::ios::beg);
    WriteHeader(data_bytes_);
    out_.close();
  }

 private:
  void WriteUint16(uint16_t value) { out_.write(reinterpret_cast<const char *>(&value), sizeof(value)); }
  void WriteUint32(uint32_t value) { out_.write(reinterpret_cast<const char *>(&value), sizeof(value)); }

  void WriteHeader(uint32_t data_bytes) {
    const uint32_t byte_rate = static_cast<uint32_t>(kSampleRate * kChannels * 2);
    const uint16_t block_align = static_cast<uint16_t>(kChannels * 2);
    out_.write("RIFF", 4);
    WriteUint32(36 + data_bytes);
    out_.write("WAVE", 4);
    out_.write("fmt ", 4);
    WriteUint32(16);
    WriteUint16(1);
    WriteUint16(static_cast<uint16_t>(kChannels));
    WriteUint32(static_cast<uint32_t>(kSampleRate));
    WriteUint32(byte_rate);
    WriteUint16(block_align);
    WriteUint16(16);
    out_.write("data", 4);
    WriteUint32(data_bytes);
  }

  std::string path_;
  std::ofstream out_;
  uint32_t data_bytes_ = 0;
};

struct MoonshineContext {
  int32_t transcriber_handle = -1;
  int32_t stream_handle = -1;
};

void ProcessTranscript(const transcript_t *transcript);

MoonshineContext StartMoonshine(const std::string &model_path, uint32_t model_arch) {
  static const moonshine_option_t options[] = {
      {"identify_speakers", "false"},
      {"word_timestamps", "false"},
      {"log_api_calls", "false"},
  };

  MoonshineContext context;
  context.transcriber_handle = moonshine_load_transcriber_from_files(
      model_path.c_str(), model_arch, options, 3, MOONSHINE_HEADER_VERSION);
  RequireMoonshine(context.transcriber_handle, "moonshine_load_transcriber_from_files");
  context.stream_handle = moonshine_create_stream(context.transcriber_handle, 0);
  RequireMoonshine(context.stream_handle, "moonshine_create_stream");
  RequireMoonshine(moonshine_start_stream(context.transcriber_handle, context.stream_handle),
                   "moonshine_start_stream");
  return context;
}

void FreeMoonshine(MoonshineContext &context) {
  if (context.stream_handle >= 0 && context.transcriber_handle >= 0) {
    moonshine_free_stream(context.transcriber_handle, context.stream_handle);
    context.stream_handle = -1;
  }
  if (context.transcriber_handle >= 0) {
    moonshine_free_transcriber(context.transcriber_handle);
    context.transcriber_handle = -1;
  }
}

void FinalizeStream(MoonshineContext &context) {
  if (context.transcriber_handle < 0 || context.stream_handle < 0) {
    return;
  }
  RequireMoonshine(
      moonshine_stop_stream(context.transcriber_handle, context.stream_handle),
      "moonshine_stop_stream");
  transcript_t *transcript = nullptr;
  RequireMoonshine(
      moonshine_transcribe_stream(
          context.transcriber_handle, context.stream_handle, 0, &transcript),
      "moonshine_transcribe_stream");
  ProcessTranscript(transcript);
}

void RestartMoonshine(
    MoonshineContext &context, const std::string &model_path, uint32_t model_arch) {
  FinalizeStream(context);
  FreeMoonshine(context);
  context = StartMoonshine(model_path, model_arch);
}

void AddAudioToMoonshine(MoonshineContext &context, const std::vector<float> &audio) {
  if (audio.empty()) {
    return;
  }
  RequireMoonshine(
      moonshine_transcribe_add_audio_to_stream(
          context.transcriber_handle, context.stream_handle, audio.data(),
          audio.size(), kSampleRate, 0),
      "moonshine_transcribe_add_audio_to_stream");
}

void ProcessTranscript(const transcript_t *transcript) {
  if (transcript == nullptr) {
    return;
  }

  for (uint64_t i = 0; i < transcript->line_count; ++i) {
    const transcript_line_t &line = transcript->lines[i];
    const std::string text = line.text != nullptr ? Trim(line.text) : "";

    if (line.has_text_changed && !text.empty()) {
      LogLine("partial: " + text);
      UpdateState([&](json &state) {
        state["state"] = "hearing";
        state["last_partial"] = text;
        state["last_error"] = "";
      });
    }

    if (line.is_complete && line.is_updated) {
      if (text.empty()) {
        LogLine("completed: <empty>");
        UpdateState([](json &state) {
          state["state"] = "listening";
          state["last_error"] = "";
        });
        continue;
      }

      LogLine("heard: " + text);
      UpdateState([&](json &state) {
        state["state"] = "listening";
        state["last_heard"] = text;
        state["last_partial"] = text;
        state["last_error"] = "";
      });
      gResetRequested = true;
    }
  }
}

void HandleSignal(int) { gRunning = false; }

std::string CaptureCommandForLog(const std::string &pulse_source) {
  std::ostringstream command;
  command << "parec --device " << pulse_source << " --format=s16le --rate "
          << kSampleRate << " --channels " << kChannels << " --raw";
  return command.str();
}

}  // namespace

int main() {
  std::signal(SIGTERM, HandleSignal);
  std::signal(SIGINT, HandleSignal);

  std::ofstream(kLogPath, std::ios::trunc).close();

  const auto model_arch = ResolveModelArch(kModelName);
  const std::string model_path = ResolveModelPath(kModelName, kLanguage);
  const std::string pulse_source = ResolvePulseSource();

  UpdateState([&](json &state) {
    state = {
        {"ok", true},
        {"available", true},
        {"running", true},
        {"pid", getpid()},
        {"state", "starting"},
        {"started_at", IsoNow()},
        {"last_error", ""},
        {"last_heard", ""},
        {"last_partial", ""},
        {"last_spoken", ""},
        {"log_path", kLogPath},
        {"debug_log_path", kDebugLogPath},
        {"input_wav_path", kInputWavPath},
        {"backend", "moonshine_asr"},
        {"label", "Moonshine ASR"},
        {"language", kLanguage},
        {"model", kModelName},
        {"model_path", model_path},
        {"device", kDeviceName},
        {"source", pulse_source},
        {"sample_rate_hz", kSampleRate},
        {"blocksize", kBlockSize},
        {"channels", kChannels},
    };
    if (model_arch) {
      state["model_arch"] = static_cast<int>(*model_arch);
    }
  });

  if (!model_arch) {
    LogLine("fatal: unsupported model '" + kModelName + "'");
    UpdateState([](json &state) {
      state["ok"] = false;
      state["running"] = false;
      state["state"] = "error";
      state["last_error"] = "unsupported model";
    });
    return 1;
  }

  if (!std::filesystem::exists(model_path)) {
    LogLine("fatal: model path not found: " + model_path);
    UpdateState([](json &state) {
      state["ok"] = false;
      state["running"] = false;
      state["state"] = "error";
      state["last_error"] = "model path not found";
    });
    return 1;
  }

  MoonshineContext moonshine;
  CaptureProcess capture;

  try {
    LogLine("starting model=" + kModelName + " arch=" + std::to_string(static_cast<int>(*model_arch)) +
            " device=" + kDeviceName + " samplerate=" + std::to_string(kSampleRate) +
            " blocksize=" + std::to_string(kBlockSize) + " channels=" + std::to_string(kChannels));
    LogLine("resolved source: " + pulse_source);

    moonshine = StartMoonshine(model_path, *model_arch);
    capture = StartCapture(pulse_source);
    WavWriter wav_writer(kInputWavPath);
    std::vector<char> raw_buffer(static_cast<std::size_t>(kBlockSize * kChannels * 2));
    const std::size_t preroll_max_samples =
        static_cast<std::size_t>(std::max(0.0, kPrerollSec) * static_cast<double>(kSampleRate));
    std::vector<float> preroll_buffer;
    if (preroll_max_samples > 0) {
      preroll_buffer.reserve(preroll_max_samples);
    }
    double audio_seconds_since_update = 0.0;

    LogLine("capture command: " + CaptureCommandForLog(pulse_source));
    LogLine("listening");
    UpdateState([](json &state) {
      state["state"] = "listening";
      state["last_error"] = "";
    });

    while (gRunning) {
      int child_status = 0;
      const pid_t child = waitpid(capture.pid, &child_status, WNOHANG);
      if (child == capture.pid) {
        throw std::runtime_error("parec exited");
      }

      pollfd fd = {capture.fd, POLLIN, 0};
      const int poll_result = poll(&fd, 1, 100);
      if (poll_result < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error("poll() failed");
      }
      if (poll_result == 0) {
        continue;
      }
      if ((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        throw std::runtime_error("capture pipe closed");
      }

      const ssize_t bytes_read = read(capture.fd, raw_buffer.data(), raw_buffer.size());
      if (bytes_read < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error("read() failed");
      }
      if (bytes_read == 0) {
        continue;
      }

      const std::size_t usable_bytes = static_cast<std::size_t>(bytes_read - (bytes_read % 2));
      if (usable_bytes == 0) {
        continue;
      }

      wav_writer.Append(raw_buffer.data(), usable_bytes);

      const auto *samples = reinterpret_cast<const int16_t *>(raw_buffer.data());
      const std::size_t sample_count = usable_bytes / sizeof(int16_t);
      std::vector<float> audio(sample_count);
      for (std::size_t i = 0; i < sample_count; ++i) {
        audio[i] = static_cast<float>(samples[i]) / 32768.0f;
      }
      if (preroll_max_samples > 0) {
        if (audio.size() >= preroll_max_samples) {
          preroll_buffer.assign(audio.end() - static_cast<std::ptrdiff_t>(preroll_max_samples), audio.end());
        } else {
          const std::size_t overflow =
              preroll_buffer.size() + audio.size() > preroll_max_samples
                  ? preroll_buffer.size() + audio.size() - preroll_max_samples
                  : 0;
          if (overflow > 0) {
            preroll_buffer.erase(preroll_buffer.begin(),
                                 preroll_buffer.begin() + static_cast<std::ptrdiff_t>(overflow));
          }
          preroll_buffer.insert(preroll_buffer.end(), audio.begin(), audio.end());
        }
      }
      AddAudioToMoonshine(moonshine, audio);
      audio_seconds_since_update += static_cast<double>(audio.size()) / static_cast<double>(kSampleRate);

      if (audio_seconds_since_update >= kUpdateIntervalSec) {
        transcript_t *transcript = nullptr;
        RequireMoonshine(
            moonshine_transcribe_stream(
                moonshine.transcriber_handle, moonshine.stream_handle, MOONSHINE_FLAG_FORCE_UPDATE, &transcript),
            "moonshine_transcribe_stream");
        ProcessTranscript(transcript);
        audio_seconds_since_update = 0.0;
      }

      if (gResetRequested && gRunning) {
        gResetRequested = false;
        LogLine("restarting transcriber after heard");
        RestartMoonshine(moonshine, model_path, *model_arch);
        if (!preroll_buffer.empty()) {
          LogLine("replaying preroll after restart samples=" + std::to_string(preroll_buffer.size()));
          AddAudioToMoonshine(moonshine, preroll_buffer);
        }
        LogLine("transcriber restart complete");
        audio_seconds_since_update = 0.0;
        UpdateState([](json &state) {
          state["state"] = "listening";
          state["last_error"] = "";
        });
      }
    }

    FinalizeStream(moonshine);

    StopCapture(capture);
    FreeMoonshine(moonshine);
    wav_writer.Finalize();
    LogLine("stopped");
    UpdateState([](json &state) {
      state["running"] = false;
      state["state"] = "stopped";
      state["last_error"] = "";
    });
    return 0;
  } catch (const std::exception &error) {
    StopCapture(capture);
    FreeMoonshine(moonshine);
    LogLine("fatal: " + Trim(error.what()));
    UpdateState([&](json &state) {
      state["ok"] = false;
      state["running"] = false;
      state["state"] = "error";
      state["last_error"] = Trim(error.what());
    });
    return 1;
  }
}
