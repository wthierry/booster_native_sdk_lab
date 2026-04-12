#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#if !BOOSTER_DEV_MODE
#include <booster/idl/ai/Subtitle.h>
#include <booster/idl/b1/BatteryState.h>
#include <booster/idl/b1/RobotStates.h>
#include <booster/robot/ai/const.hpp>
#include <booster/robot/b1/b1_api_const.hpp>
#include <booster/robot/b1/b1_loco_client.hpp>
#include <booster/robot/channel/channel_factory.hpp>
#include <booster/robot/channel/channel_subscriber.hpp>
#include <booster/robot/common/robot_shared.hpp>
#include <booster/third_party/nlohmann_json/json.hpp>
#else
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

namespace {

constexpr char kDefaultBindAddress[] = "0.0.0.0";
constexpr unsigned short kDefaultPort = 8080;
constexpr int kDefaultDomainId = 0;
constexpr char kDefaultNetworkInterface[] = "lo";
constexpr char kDefaultVoiceType[] = "zh_male_wennuanahu_moon_bigtts";
constexpr char kRosSetupScript[] = "/home/booster/Workspace/booster_robotics_sdk_ros2/install/setup.bash";
constexpr char kRosTtsHelper[] = "scripts/ros_rtc_tts.py";
constexpr char kWhisperLiveAsrHelper[] = "scripts/whisperlive_asr_daemon.py";
constexpr char kMoonshineAsrHelper[] = "scripts/moonshine_asr_daemon.py";
constexpr char kOpenAiAsrHelper[] = "scripts/openai_asr_daemon.py";
constexpr int kDefaultInterruptSpeechDurationMs = 700;
constexpr char kBackendRtc[] = "rtc";
constexpr char kBackendWhisperLiveAsr[] = "whisperlive_asr";
constexpr char kBackendMoonshineAsr[] = "moonshine_asr";
constexpr char kBackendOpenAiAsr[] = "openai_asr";
constexpr char kWhisperLiveAsrStatePath[] = "/tmp/booster_whisperlive_asr_state.json";
constexpr char kWhisperLiveAsrLogPath[] = "/tmp/booster_whisperlive_asr.log";
constexpr char kMoonshineAsrStatePath[] = "/tmp/booster_moonshine_asr_state.json";
constexpr char kMoonshineAsrLogPath[] = "/tmp/booster_moonshine_asr.log";
constexpr char kMoonshineAsrDebugLogPath[] = "/tmp/booster_moonshine_asr_debug.log";
constexpr char kMoonshineAsrInputWavPath[] = "/tmp/booster_moonshine_asr_input.wav";
constexpr char kOpenAiAsrStatePath[] = "/tmp/booster_openai_asr_state.json";
constexpr char kOpenAiAsrLogPath[] = "/tmp/booster_openai_asr.log";
constexpr char kFakeBytedanceOpenAiLogPath[] = "/var/log/fake_bytedance_openai_asr.log";

#if BOOSTER_DEV_MODE
constexpr char kDefaultCameraPreviewPath[] = "tmp/booster_camera_preview.jpg";
enum class RobotMode {
    kUnknown = -1,
    kDamping = 0,
    kPrepare = 1,
    kWalking = 2,
    kCustom = 3,
    kSoccer = 4,
};
#else
constexpr char kTopicBatteryState[] = "rt/battery_state";
constexpr char kDefaultCameraPreviewPath[] = "/tmp/booster_camera_preview.jpg";
using RobotMode = booster::robot::RobotMode;
#endif

constexpr RobotMode kDefaultRobotMode = RobotMode::kUnknown;

struct RobotModeOption {
    const char *id;
    const char *label;
    const char *description;
    RobotMode value;
};

constexpr std::array<RobotModeOption, 5> kRobotModes{{
    {"damping", "Damp", "Low-level damping mode.", RobotMode::kDamping},
    {"prepare", "Prepare", "Balanced standing mode.", RobotMode::kPrepare},
    {"walking", "Walking", "Walking locomotion mode.", RobotMode::kWalking},
    {"custom", "Custom", "Custom behavior mode.", RobotMode::kCustom},
    {"soccer", "Soccer", "Soccer behavior mode.", RobotMode::kSoccer},
}};

struct RuntimeOptions {
    std::string bind_address = kDefaultBindAddress;
    unsigned short port = kDefaultPort;
    int domain_id = kDefaultDomainId;
    std::string network_interface = kDefaultNetworkInterface;
    std::string web_root = "web";
};

json ParseJsonOrRawString(const std::string &body);

std::string Trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() &&
           (value[start] == ' ' || value[start] == '\t' || value[start] == '\n' || value[start] == '\r')) {
        ++start;
    }
    return value.substr(start);
}

std::string ResolveRtcVoiceType() {
    const char *configured = std::getenv("BOOSTER_RTC_VOICE_TYPE");
    if (configured != nullptr) {
        const std::string trimmed = Trim(configured);
        if (!trimmed.empty()) {
            return trimmed;
        }
    }
    return kDefaultVoiceType;
}

std::string ReadFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

json ReadJsonFileIfPresent(const std::string &path) {
    std::ifstream input(path);
    if (!input) {
        return json::object();
    }

    try {
        json payload;
        input >> payload;
        if (payload.is_object()) {
            return payload;
        }
    } catch (...) {
    }
    return json::object();
}

json ReadTailLinesIfPresent(const std::string &path, std::size_t max_lines) {
    std::ifstream input(path);
    if (!input) {
        return json::array();
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    json result = json::array();
    const std::size_t start = lines.size() > max_lines ? lines.size() - max_lines : 0;
    for (std::size_t i = start; i < lines.size(); ++i) {
        result.push_back(lines[i]);
    }
    return result;
}

std::string ExtractQuotedSuffix(const std::string &line, const std::string &marker) {
    const auto marker_pos = line.find(marker);
    if (marker_pos == std::string::npos) {
        return {};
    }
    const auto start = line.find('"', marker_pos + marker.size());
    const auto end = line.rfind('"');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return {};
    }

    const auto quoted = line.substr(start, end - start + 1);
    const auto parsed = ParseJsonOrRawString(quoted);
    if (parsed.is_string()) {
        return parsed.get<std::string>();
    }
    return {};
}

void SetEnvVar(const std::string &key, const std::string &value) {
#if defined(_WIN32)
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 0);
#endif
}

std::optional<std::pair<std::string, std::string>> ParseEnvAssignment(const std::string &line) {
    const auto trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
        return std::nullopt;
    }

    const auto equals = trimmed.find('=');
    if (equals == std::string::npos) {
        return std::nullopt;
    }

    auto key = Trim(trimmed.substr(0, equals));
    auto value = Trim(trimmed.substr(equals + 1));
    if (key.rfind("export ", 0) == 0) {
        key = Trim(key.substr(7));
    }
    if (key.empty()) {
        return std::nullopt;
    }

    if (value.size() >= 2) {
        const char quote = value.front();
        if ((quote == '"' || quote == '\'') && value.back() == quote) {
            value = value.substr(1, value.size() - 2);
        }
    }

    return std::make_pair(key, value);
}

void LoadDotEnvIfPresent(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) {
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto assignment = ParseEnvAssignment(line);
        if (!assignment) {
            continue;
        }
        SetEnvVar(assignment->first, assignment->second);
    }
}

std::string ContentTypeForPath(const std::string &path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") {
        return "text/html; charset=utf-8";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") {
        return "text/css; charset=utf-8";
    }
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") {
        return "application/javascript; charset=utf-8";
    }
    return "text/plain; charset=utf-8";
}

json ParseOptionalJsonBody(const std::string &body) {
    if (Trim(body).empty()) {
        return json::object();
    }
    return json::parse(body);
}

std::string ShellEscape(const std::string &value) {
    std::string escaped = "'";
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

json ParseJsonOrRawString(const std::string &body) {
    const auto trimmed = Trim(body);
    if (trimmed.empty()) {
        return json();
    }
    try {
        return json::parse(trimmed);
    } catch (...) {
        return trimmed;
    }
}

std::string RunCommand(const std::string &command, int *exit_status = nullptr) {
    std::array<char, 512> buffer{};
    std::string output;

    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run command");
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (exit_status) {
        *exit_status = status;
    }
    return output;
}

int LaunchDetachedProcess(
    const std::vector<std::string> &argv,
    const std::string &log_path,
    const std::vector<std::pair<std::string, std::string>> &env_updates,
    int *child_exit_status = nullptr) {
    if (argv.empty()) {
        if (child_exit_status) {
            *child_exit_status = -1;
        }
        return -1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        if (child_exit_status) {
            *child_exit_status = -1;
        }
        return -1;
    }

    if (pid == 0) {
        setsid();

        const int log_fd = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        const int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }

        for (const auto &[key, value] : env_updates) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<char *> exec_argv;
        exec_argv.reserve(argv.size() + 1);
        for (const auto &arg : argv) {
            exec_argv.push_back(const_cast<char *>(arg.c_str()));
        }
        exec_argv.push_back(nullptr);

        execvp(exec_argv[0], exec_argv.data());
        std::perror("execvp");
        _exit(127);
    }

    if (child_exit_status) {
        *child_exit_status = 0;
        int status = 0;
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            *child_exit_status = status;
        }
    }
    return static_cast<int>(pid);
}

bool IsSystemServiceActive(const std::string &service_name) {
    int status = 0;
    RunCommand("systemctl is-active --quiet " + ShellEscape(service_name), &status);
    return status == 0;
}

std::string QuerylessPath(const std::string &target) {
    const auto query_pos = target.find('?');
    return target.substr(0, query_pos);
}

std::string ResolveScriptPath(const char *env_var, const char *relative_path) {
    if (const char *override_path = std::getenv(env_var)) {
        const std::string configured = Trim(override_path);
        if (!configured.empty()) {
            return configured;
        }
    }

    const std::filesystem::path repo_helper =
        std::filesystem::path(BOOSTER_NATIVE_SDK_LAB_SOURCE_DIR) / relative_path;
    if (std::filesystem::exists(repo_helper)) {
        return repo_helper.string();
    }

    return relative_path;
}

std::string ResolveMoonshineHelperPath() {
    if (const char *override_path = std::getenv("BOOSTER_MOONSHINE_ASR_HELPER")) {
        const std::string configured = Trim(override_path);
        if (!configured.empty()) {
            return configured;
        }
    }

#ifdef BOOSTER_MOONSHINE_NATIVE_HELPER_PATH
    const std::filesystem::path native_helper(BOOSTER_MOONSHINE_NATIVE_HELPER_PATH);
    if (std::filesystem::exists(native_helper)) {
        return native_helper.string();
    }
#endif

    return ResolveScriptPath("BOOSTER_MOONSHINE_ASR_HELPER", kMoonshineAsrHelper);
}

std::string ResolveOpenAiAsrHelperPath() {
    return ResolveScriptPath("BOOSTER_OPENAI_ASR_HELPER", kOpenAiAsrHelper);
}

std::string BuildHelperLaunchTarget(const std::string &helper_path) {
    const std::filesystem::path helper(helper_path);
    if (helper.extension() == ".py") {
        return "python3 " + ShellEscape(helper_path);
    }
    return ShellEscape(helper_path);
}

bool IsProcessRunning(int pid);

bool CommandPathExists(const std::string &path) {
    return !path.empty() && std::filesystem::exists(path);
}

std::vector<int> FindProcessesMatching(const std::string &pattern) {
    std::vector<int> pids;
    if (pattern.empty()) {
        return pids;
    }

    int status = 0;
    const std::string output = RunCommand("pgrep -f " + ShellEscape(pattern), &status);
    if (status != 0) {
        return pids;
    }

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        try {
            pids.push_back(std::stoi(line));
        } catch (...) {
        }
    }
    return pids;
}

void AppendUniquePid(std::vector<int> *pids, int pid) {
    if (pids == nullptr || pid <= 0) {
        return;
    }
    if (std::find(pids->begin(), pids->end(), pid) == pids->end()) {
        pids->push_back(pid);
    }
}

std::vector<int> FindMoonshineHelperPids() {
    std::vector<int> pids;
    AppendUniquePid(&pids, ReadJsonFileIfPresent(kMoonshineAsrStatePath).value("pid", 0));

    const std::string helper_path = ResolveMoonshineHelperPath();
    for (int pid : FindProcessesMatching(helper_path)) {
        AppendUniquePid(&pids, pid);
    }

    const std::string python_helper = ResolveScriptPath("BOOSTER_MOONSHINE_ASR_HELPER", kMoonshineAsrHelper);
    if (helper_path != python_helper) {
        for (int pid : FindProcessesMatching(python_helper)) {
            AppendUniquePid(&pids, pid);
        }
    }

    for (int pid : FindProcessesMatching("booster_moonshine_asr_native")) {
        AppendUniquePid(&pids, pid);
    }

    return pids;
}

void StopMoonshineHelpers() {
    const std::vector<int> pids = FindMoonshineHelperPids();
    for (int pid : pids) {
        RunCommand("kill -TERM " + std::to_string(pid));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (int pid : pids) {
        if (IsProcessRunning(pid)) {
            RunCommand("kill -KILL " + std::to_string(pid));
        }
    }
}

std::string ResolveCameraPreviewPath() {
    if (const char *override_path = std::getenv("BOOSTER_CAMERA_PREVIEW_PATH")) {
        const std::string configured = Trim(override_path);
        if (!configured.empty()) {
            return configured;
        }
    }

    return (std::filesystem::path(BOOSTER_NATIVE_SDK_LAB_SOURCE_DIR) / kDefaultCameraPreviewPath).string();
}

int ParsePercentValue(const std::string &text) {
    std::string digits;
    for (char ch : text) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            digits.push_back(ch);
        } else if (!digits.empty()) {
            break;
        }
    }
    if (digits.empty()) {
        throw std::runtime_error("Failed to parse volume percent");
    }
    return std::stoi(digits);
}

bool IsProcessRunning(int pid) {
    if (pid <= 0) {
        return false;
    }

    int status = 0;
    RunCommand("kill -0 " + std::to_string(pid) + " 2>/dev/null", &status);
    return status == 0;
}

const RobotModeOption *FindRobotModeById(const std::string &id) {
    for (const auto &option : kRobotModes) {
        if (id == option.id) {
            return &option;
        }
    }
    return nullptr;
}

const RobotModeOption *FindRobotModeByValue(RobotMode mode) {
    for (const auto &option : kRobotModes) {
        if (mode == option.value) {
            return &option;
        }
    }
    return nullptr;
}

json RobotModeOptionsJson() {
    json options = json::array();
    for (const auto &option : kRobotModes) {
        options.push_back({
            {"id", option.id},
            {"label", option.label},
            {"description", option.description},
            {"value", static_cast<int>(option.value)},
        });
    }
    return options;
}

json RobotModeStateJson(RobotMode mode) {
    const auto *option = FindRobotModeByValue(mode);
    if (option) {
        return {
            {"id", option->id},
            {"label", option->label},
            {"description", option->description},
            {"value", static_cast<int>(option->value)},
        };
    }

    return {
        {"id", "unknown"},
        {"label", "Unknown"},
        {"description", "Robot mode is not available yet."},
        {"value", static_cast<int>(mode)},
    };
}

class BatteryMonitor {
public:
#if BOOSTER_DEV_MODE
    BatteryMonitor() = default;

    json ToJson() const {
        return {
            {"available", false},
            {"soc_percent", 0.0},
            {"voltage_v", 0.0},
            {"current_a", 0.0},
            {"average_voltage_v", 0.0},
            {"source", "dev_mode"},
            {"note", "Robot battery telemetry is unavailable in dev mode."},
        };
    }
#else
    BatteryMonitor()
        : subscriber_(
              kTopicBatteryState,
              [this](const void *msg) {
                  const auto *battery = static_cast<const booster_interface::msg::BatteryState *>(msg);
                  std::scoped_lock lock(mutex_);
                  last_voltage_ = battery->voltage();
                  last_current_ = battery->current();
                  last_soc_ = battery->soc();
                  last_average_voltage_ = battery->average_voltage();
                  has_data_ = true;
              }) {
        subscriber_.InitChannel();
    }

    json ToJson() const {
        std::scoped_lock lock(mutex_);
        return {
            {"available", has_data_},
            {"soc_percent", last_soc_},
            {"voltage_v", last_voltage_},
            {"current_a", last_current_},
            {"average_voltage_v", last_average_voltage_},
        };
    }

private:
    mutable std::mutex mutex_;
    booster::robot::ChannelSubscriber<booster_interface::msg::BatteryState> subscriber_;
    bool has_data_ = false;
    float last_voltage_ = 0.0f;
    float last_current_ = 0.0f;
    float last_soc_ = 0.0f;
    float last_average_voltage_ = 0.0f;
#endif
};

class BatteryWrapper {
public:
    explicit BatteryWrapper(std::string network_interface, int domain_id)
        : network_interface_(std::move(network_interface)), domain_id_(domain_id) {
#if BOOSTER_DEV_MODE
        battery_monitor_ = std::make_unique<BatteryMonitor>();
#else
        booster::robot::ChannelFactory::Instance()->Init(domain_id_, network_interface_);
        battery_monitor_ = std::make_unique<BatteryMonitor>();
        asr_monitor_ = std::make_unique<booster::robot::ChannelSubscriber<booster_interface::msg::Subtitle>>(
            booster::robot::kTopicAiSubtitle,
            [this](const void *msg) {
                const auto *subtitle = static_cast<const booster_interface::msg::Subtitle *>(msg);
                if (subtitle) {
                    HandleSubtitle(*subtitle);
                }
            });
        asr_monitor_->InitChannel();
        EnsureSpeechIdle();
#endif
    }

    json StatusJson() const {
        const json whisperlive_asr = WhisperLiveAsrStatus();
        const json moonshine_asr = MoonshineAsrStatus();
        const json openai_asr = OpenAiAsrStatus();
        const json native_openai_bridge = NativeOpenAiBridgeStatus();
        std::scoped_lock lock(speech_mutex_);
        std::string last_heard = last_heard_text_;
        std::string last_spoken = last_spoken_text_;
        if (whisperlive_asr.is_object()) {
            const auto helper_last_heard = Trim(whisperlive_asr.value("last_heard", std::string()));
            if (whisperlive_asr.value("running", false) && !helper_last_heard.empty()) {
                last_heard = helper_last_heard;
            }
            if (whisperlive_asr.value("running", false)) {
                last_spoken.clear();
            }
        }
        if (moonshine_asr.is_object()) {
            const auto helper_last_heard = Trim(moonshine_asr.value("last_heard", std::string()));
            if (moonshine_asr.value("running", false) && !helper_last_heard.empty()) {
                last_heard = helper_last_heard;
            }
            if (moonshine_asr.value("running", false)) {
                last_spoken.clear();
            }
        }
        if (openai_asr.is_object()) {
            const auto helper_last_heard = Trim(openai_asr.value("last_heard", std::string()));
            if (openai_asr.value("running", false) && !helper_last_heard.empty()) {
                last_heard = helper_last_heard;
            }
            if (openai_asr.value("running", false)) {
                last_spoken.clear();
            }
        }
        if (native_openai_bridge.is_object()) {
            const auto bridge_last_heard = Trim(native_openai_bridge.value("last_result", std::string()));
            if (!bridge_last_heard.empty()) {
                last_heard = bridge_last_heard;
                last_spoken.clear();
            }
        }
        return {
            {"network_interface", network_interface_},
            {"domain_id", domain_id_},
            {"dev_mode", static_cast<bool>(BOOSTER_DEV_MODE)},
            {"battery", battery_monitor_ ? battery_monitor_->ToJson() : json::object()},
            {"speech_backends", {
                {"rtc", {
                    {"id", kBackendRtc},
                    {"label", "Native ASR"},
                    {"available", true},
                }},
                {"whisperlive_asr", {
                    {"id", kBackendWhisperLiveAsr},
                    {"label", "WhisperLive ASR"},
                    {"available", whisperlive_asr.value("available", false)},
                }},
                {"moonshine_asr", {
                    {"id", kBackendMoonshineAsr},
                    {"label", "Moonshine ASR"},
                    {"available", moonshine_asr.value("available", false)},
                }},
                {"openai_asr", {
                    {"id", kBackendOpenAiAsr},
                    {"label", "OpenAI ASR"},
                    {"available", openai_asr.value("available", false)},
                }},
            }},
            {"tts_transport", BOOSTER_DEV_MODE ? "mock" : "lui_asr_only"},
            {"speech_debug", {
                {"last_heard", last_heard},
                {"last_spoken", last_spoken},
            }},
            {"native_openai_bridge", native_openai_bridge},
            {"whisperlive_asr", whisperlive_asr},
            {"moonshine_asr", moonshine_asr},
            {"openai_asr", openai_asr},
        };
    }

    json StartTts(const std::string &voice_type,
                  int interrupt_speech_duration_ms = kDefaultInterruptSpeechDurationMs) const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "lui_start_asr"},
            {"backend", kBackendRtc},
            {"dev_mode", true},
            {"note", "Native ASR start is mocked in dev mode."},
        };
#else
        if (!IsSystemServiceActive("fake-bytedance-openai-asr.service")) {
            return {
                {"ok", false},
                {"action", "lui_start_asr"},
                {"backend", kBackendRtc},
                {"code", 503},
                {"error", "fake-bytedance-openai-asr.service is not running"},
                {"service_name", "fake-bytedance-openai-asr.service"},
            };
        }
        if (!IsSystemServiceActive("booster-lui.service")) {
            return {
                {"ok", false},
                {"action", "lui_start_asr"},
                {"backend", kBackendRtc},
                {"code", 503},
                {"error", "booster-lui.service is not running"},
                {"service_name", "booster-lui.service"},
            };
        }

        (void)voice_type;
        (void)interrupt_speech_duration_ms;

        json result = RunRosTtsHelper("lui_start_asr", json::object());
        const std::string response_body = Trim(result.value("response_body", std::string()));
        if (result.value("ok", false) || response_body != "Start ASR failed") {
            return result;
        }

        const json stop_result = RunRosTtsHelper("lui_stop_asr", json::object());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        json retry_result = RunRosTtsHelper("lui_start_asr", json::object());
        retry_result["recovered_after_restart"] = retry_result.value("ok", false);
        retry_result["auto_stop_result"] = stop_result;
        return retry_result;
#endif
    }

    json StopTts() {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "lui_stop_asr"},
            {"backend", kBackendRtc},
            {"dev_mode", true},
            {"note", "Native ASR stop is mocked in dev mode."},
        };
#else
        {
            std::scoped_lock lock(speech_mutex_);
            last_spoken_text_.clear();
        }
        return RunRosTtsHelper("lui_stop_asr", json::object());
#endif
    }

    json StartWhisperLiveAsr(const std::string &model = std::string()) const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "whisperlive_asr_start"},
            {"backend", kBackendWhisperLiveAsr},
            {"dev_mode", true},
            {"note", "WhisperLive ASR start is mocked in dev mode."},
        };
#else
        const json stop_result = StopWhisperLiveAsr();
        const std::string helper_path = ResolveScriptPath("BOOSTER_WHISPERLIVE_ASR_HELPER", kWhisperLiveAsrHelper);
        const std::string launcher =
            "import os,subprocess,sys;"
            "os.environ['BOOSTER_WHISPERLIVE_ASR_LOG_PATH']=sys.argv[2];"
            "os.environ['BOOSTER_WHISPERLIVE_ASR_STATE_PATH']=sys.argv[3];"
            "os.environ['BOOSTER_WHISPERLIVE_MODEL']=sys.argv[4];"
            "log=open(sys.argv[2],'ab', buffering=0);"
            "proc=subprocess.Popen(['python3', sys.argv[1]], stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True);"
            "print(proc.pid)";
        const std::string command =
            "python3 -c " + ShellEscape(launcher) + " " + ShellEscape(helper_path) + " " +
            ShellEscape(kWhisperLiveAsrLogPath) + " " + ShellEscape(kWhisperLiveAsrStatePath) + " " +
            ShellEscape(model.empty() ? std::string("base.en") : model);

        int status = 0;
        const std::string output = RunCommand("bash -lc " + ShellEscape(command), &status);
        const std::string pid_text = Trim(output);
        int spawned_pid = 0;
        try {
            if (!pid_text.empty()) {
                spawned_pid = std::stoi(pid_text);
            }
        } catch (...) {
        }
        json result = {
            {"ok", status == 0 && !pid_text.empty()},
            {"action", "whisperlive_asr_start"},
            {"backend", kBackendWhisperLiveAsr},
            {"requested_model", model.empty() ? std::string("base.en") : model},
            {"helper_exit_status", status},
            {"log_path", kWhisperLiveAsrLogPath},
            {"state_path", kWhisperLiveAsrStatePath},
            {"spawned_pid", spawned_pid},
            {"auto_stop_result", stop_result},
        };
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        json helper_status = WhisperLiveAsrStatus();
        for (auto it = helper_status.begin(); it != helper_status.end(); ++it) {
            result[it.key()] = it.value();
        }
        return result;
#endif
    }

    json StopWhisperLiveAsr() const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "whisperlive_asr_stop"},
            {"backend", kBackendWhisperLiveAsr},
            {"dev_mode", true},
            {"note", "WhisperLive ASR stop is mocked in dev mode."},
        };
#else
        const json before = WhisperLiveAsrStatus();
        const int pid = before.value("pid", 0);
        if (pid > 0) {
            RunCommand("kill -TERM " + std::to_string(pid));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        json result = WhisperLiveAsrStatus();
        result["ok"] = true;
        result["action"] = "whisperlive_asr_stop";
        result["backend"] = kBackendWhisperLiveAsr;
        result["stopped_pid"] = pid;
        return result;
#endif
    }

    json StartMoonshineAsr(const std::string &model = std::string()) const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "moonshine_asr_start"},
            {"backend", kBackendMoonshineAsr},
            {"dev_mode", true},
            {"note", "Moonshine ASR start is mocked in dev mode."},
        };
#else
        const json stop_result = StopMoonshineAsr();
        const std::string helper_path = ResolveMoonshineHelperPath();
        const std::string helper_target = BuildHelperLaunchTarget(helper_path);
        const std::string requested_model = model.empty() ? std::string("medium-streaming") : model;
        const std::string command =
            "BOOSTER_MOONSHINE_ASR_LOG_PATH=" + ShellEscape(kMoonshineAsrLogPath) + " " +
            "BOOSTER_MOONSHINE_ASR_DEBUG_LOG_PATH=" + ShellEscape(kMoonshineAsrDebugLogPath) + " " +
            "BOOSTER_MOONSHINE_ASR_STATE_PATH=" + ShellEscape(kMoonshineAsrStatePath) + " " +
            "BOOSTER_MOONSHINE_ASR_INPUT_WAV_PATH=" + ShellEscape(kMoonshineAsrInputWavPath) + " " +
            "BOOSTER_MOONSHINE_ASR_MODEL=" + ShellEscape(requested_model) + " " +
            helper_target + " >> " + ShellEscape(kMoonshineAsrLogPath) + " 2>&1 & echo $!";

        int status = 0;
        const std::string output = RunCommand("bash -lc " + ShellEscape(command), &status);
        const std::string pid_text = Trim(output);
        int spawned_pid = 0;
        try {
            if (!pid_text.empty()) {
                spawned_pid = std::stoi(pid_text);
            }
        } catch (...) {
        }
        json result = {
            {"ok", status == 0 && !pid_text.empty()},
            {"action", "moonshine_asr_start"},
            {"backend", kBackendMoonshineAsr},
            {"requested_model", requested_model},
            {"helper_exit_status", status},
            {"log_path", kMoonshineAsrLogPath},
            {"debug_log_path", kMoonshineAsrDebugLogPath},
            {"state_path", kMoonshineAsrStatePath},
            {"input_wav_path", kMoonshineAsrInputWavPath},
            {"spawned_pid", spawned_pid},
            {"auto_stop_result", stop_result},
        };
        json helper_status = json::object();
        for (int attempt = 0; attempt < 10; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            helper_status = MoonshineAsrStatus();
            if (spawned_pid <= 0) {
                break;
            }
            if (helper_status.value("pid", 0) == spawned_pid) {
                break;
            }
        }
        for (auto it = helper_status.begin(); it != helper_status.end(); ++it) {
            result[it.key()] = it.value();
        }
        result["spawned_pid"] = spawned_pid;
        if (spawned_pid > 0 && result.value("pid", 0) != spawned_pid) {
            result["pid"] = spawned_pid;
        }
        return result;
#endif
    }

    json StopMoonshineAsr() const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "moonshine_asr_stop"},
            {"backend", kBackendMoonshineAsr},
            {"dev_mode", true},
            {"note", "Moonshine ASR stop is mocked in dev mode."},
        };
#else
        const json before = MoonshineAsrStatus();
        const std::vector<int> pids = FindMoonshineHelperPids();
        StopMoonshineHelpers();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        json result = MoonshineAsrStatus();
        result["ok"] = true;
        result["action"] = "moonshine_asr_stop";
        result["backend"] = kBackendMoonshineAsr;
        result["stopped_pid"] = before.value("pid", 0);
        result["stopped_pids"] = pids;
        return result;
#endif
    }

    json StartOpenAiAsr(const std::string &model = std::string()) const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "openai_asr_start"},
            {"backend", kBackendOpenAiAsr},
            {"dev_mode", true},
            {"note", "OpenAI ASR start is mocked in dev mode."},
        };
#else
        const json stop_result = StopOpenAiAsr();
        const std::string requested_model = model.empty() ? std::string("gpt-4o-mini-transcribe") : model;
        const std::vector<std::pair<std::string, std::string>> envs = {
            {"BOOSTER_OPENAI_ASR_LOG_PATH", kOpenAiAsrLogPath},
            {"BOOSTER_OPENAI_ASR_STATE_PATH", kOpenAiAsrStatePath},
            {"BOOSTER_OPENAI_ASR_MODEL", requested_model},
        };
        const std::vector<std::string> argv = {"python3", ResolveOpenAiAsrHelperPath()};
        int status = 0;
        const int spawned_pid = LaunchDetachedProcess(argv, kOpenAiAsrLogPath, envs, &status);
        json result = {
            {"ok", spawned_pid > 0 && status == 0},
            {"action", "openai_asr_start"},
            {"backend", kBackendOpenAiAsr},
            {"requested_model", requested_model},
            {"helper_exit_status", status},
            {"log_path", kOpenAiAsrLogPath},
            {"state_path", kOpenAiAsrStatePath},
            {"spawned_pid", spawned_pid},
            {"auto_stop_result", stop_result},
        };
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        json helper_status = OpenAiAsrStatus();
        for (auto it = helper_status.begin(); it != helper_status.end(); ++it) {
            result[it.key()] = it.value();
        }
        return result;
#endif
    }

    json StartOpenAiAsr(const json &config) const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "openai_asr_start"},
            {"backend", kBackendOpenAiAsr},
            {"dev_mode", true},
            {"note", "OpenAI ASR start is mocked in dev mode."},
        };
#else
        const json stop_result = StopOpenAiAsr();
        const std::string requested_model = Trim(config.value("model", std::string("gpt-4o-mini-transcribe")));
        std::vector<std::pair<std::string, std::string>> envs = {
            {"BOOSTER_OPENAI_ASR_LOG_PATH", kOpenAiAsrLogPath},
            {"BOOSTER_OPENAI_ASR_STATE_PATH", kOpenAiAsrStatePath},
            {"BOOSTER_OPENAI_ASR_MODEL", requested_model.empty() ? std::string("gpt-4o-mini-transcribe") : requested_model},
        };
        const std::array<std::pair<const char *, const char *>, 9> mappings{{
            {"language", "BOOSTER_OPENAI_ASR_LANGUAGE"},
            {"prompt", "BOOSTER_OPENAI_ASR_PROMPT"},
            {"sample_rate", "BOOSTER_OPENAI_ASR_SAMPLE_RATE"},
            {"channels", "BOOSTER_OPENAI_ASR_CHANNELS"},
            {"frame_samples", "BOOSTER_OPENAI_ASR_FRAME_SAMPLES"},
            {"rms_threshold", "BOOSTER_OPENAI_ASR_RMS_THRESHOLD"},
            {"start_threshold", "BOOSTER_OPENAI_ASR_START_THRESHOLD"},
            {"continue_threshold", "BOOSTER_OPENAI_ASR_CONTINUE_THRESHOLD"},
            {"silence_frames", "BOOSTER_OPENAI_ASR_SILENCE_FRAMES"},
        }};
        for (const auto &[json_key, env_key] : mappings) {
            if (!config.contains(json_key) || config.at(json_key).is_null()) {
                continue;
            }
            std::string value;
            if (config.at(json_key).is_string()) {
                value = Trim(config.at(json_key).get<std::string>());
            } else {
                value = Trim(config.at(json_key).dump());
            }
            if (value.empty()) {
                continue;
            }
            envs.push_back({env_key, value});
        }
        if (config.contains("min_voiced_frames") && !config.at("min_voiced_frames").is_null()) {
            envs.push_back({"BOOSTER_OPENAI_ASR_MIN_VOICED_FRAMES", Trim(config.at("min_voiced_frames").dump())});
        }
        if (config.contains("prefix_frames") && !config.at("prefix_frames").is_null()) {
            envs.push_back({"BOOSTER_OPENAI_ASR_PREFIX_FRAMES", Trim(config.at("prefix_frames").dump())});
        }
        if (config.contains("max_frames") && !config.at("max_frames").is_null()) {
            envs.push_back({"BOOSTER_OPENAI_ASR_MAX_FRAMES", Trim(config.at("max_frames").dump())});
        }
        const std::vector<std::string> argv = {"python3", ResolveOpenAiAsrHelperPath()};
        int status = 0;
        const int spawned_pid = LaunchDetachedProcess(argv, kOpenAiAsrLogPath, envs, &status);
        json result = {
            {"ok", spawned_pid > 0 && status == 0},
            {"action", "openai_asr_start"},
            {"backend", kBackendOpenAiAsr},
            {"requested_model", requested_model.empty() ? std::string("gpt-4o-mini-transcribe") : requested_model},
            {"requested_config", config},
            {"helper_exit_status", status},
            {"log_path", kOpenAiAsrLogPath},
            {"state_path", kOpenAiAsrStatePath},
            {"spawned_pid", spawned_pid},
            {"auto_stop_result", stop_result},
        };
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        json helper_status = OpenAiAsrStatus();
        for (auto it = helper_status.begin(); it != helper_status.end(); ++it) {
            result[it.key()] = it.value();
        }
        return result;
#endif
    }

    json StopOpenAiAsr() const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "openai_asr_stop"},
            {"backend", kBackendOpenAiAsr},
            {"dev_mode", true},
            {"note", "OpenAI ASR stop is mocked in dev mode."},
        };
#else
        const json before = OpenAiAsrStatus();
        const int pid = before.value("pid", 0);
        if (pid > 0) {
            RunCommand("kill -TERM " + std::to_string(pid));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        json result = OpenAiAsrStatus();
        result["ok"] = true;
        result["action"] = "openai_asr_stop";
        result["backend"] = kBackendOpenAiAsr;
        result["stopped_pid"] = pid;
        return result;
#endif
    }

    json GetVolume() const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"volume_percent", volume_percent_},
            {"dev_mode", true},
        };
#else
        int status = 0;
        const auto output = RunCommand("pactl get-sink-volume @DEFAULT_SINK@", &status);
        if (status != 0) {
            return {
                {"ok", false},
                {"code", status},
                {"error", "Failed to read volume"},
            };
        }

        const auto percent_pos = output.find('%');
        if (percent_pos == std::string::npos) {
            return {
                {"ok", false},
                {"code", 500},
                {"error", "Unexpected volume output"},
                {"raw_output", Trim(output)},
            };
        }

        const auto start = output.rfind(' ', percent_pos);
        const auto fragment = output.substr(start == std::string::npos ? 0 : start + 1, percent_pos - start);
        return {
            {"ok", true},
            {"volume_percent", ParsePercentValue(fragment)},
        };
#endif
    }

    json SetVolume(int percent) const {
#if BOOSTER_DEV_MODE
        volume_percent_ = std::max(0, std::min(150, percent));
        return {
            {"ok", true},
            {"volume_percent", volume_percent_},
            {"requested_percent", volume_percent_},
            {"dev_mode", true},
        };
#else
        const int clamped = std::max(0, std::min(150, percent));
        int status = 0;
        RunCommand("pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(clamped) + "%", &status);
        if (status != 0) {
            return {
                {"ok", false},
                {"code", status},
                {"error", "Failed to set volume"},
                {"requested_percent", clamped},
            };
        }

        auto current = GetVolume();
        current["requested_percent"] = clamped;
        return current;
#endif
    }

private:
#if !BOOSTER_DEV_MODE
    void HandleSubtitle(const booster_interface::msg::Subtitle &subtitle) {
        const auto text = Trim(subtitle.text());
        if (text.empty()) {
            return;
        }

        const auto user_id = Trim(subtitle.user_id());
        const bool from_robot = user_id == booster::robot::kBoosterRobotUserId;

        {
            std::scoped_lock lock(speech_mutex_);
            if (from_robot) {
                last_spoken_text_ = text;
            } else {
                last_heard_text_ = text;
            }
        }
    }
#endif

    json WhisperLiveAsrStatus() const {
        json status = ReadJsonFileIfPresent(kWhisperLiveAsrStatePath);
        if (!status.is_object() || status.empty()) {
            return {
                {"available", std::filesystem::exists(ResolveScriptPath(
                    "BOOSTER_WHISPERLIVE_ASR_HELPER", kWhisperLiveAsrHelper))},
                {"running", false},
                {"state", "stopped"},
                {"last_heard", ""},
                {"last_error", ""},
                {"log_path", kWhisperLiveAsrLogPath},
                {"state_path", kWhisperLiveAsrStatePath},
            };
        }
        const int pid = status.value("pid", 0);
        if (!IsProcessRunning(pid)) {
            status["running"] = false;
            if (status.value("state", std::string()) != "error") {
                status["state"] = "stopped";
            }
        }
        return status;
    }

    json MoonshineAsrStatus() const {
        json status = ReadJsonFileIfPresent(kMoonshineAsrStatePath);
        if (!status.is_object() || status.empty()) {
            return {
                {"available", std::filesystem::exists(ResolveMoonshineHelperPath())},
                {"running", false},
                {"state", "stopped"},
                {"last_heard", ""},
                {"last_error", ""},
                {"log_path", kMoonshineAsrLogPath},
                {"debug_log_path", kMoonshineAsrDebugLogPath},
                {"state_path", kMoonshineAsrStatePath},
                {"input_wav_path", kMoonshineAsrInputWavPath},
                {"debug_tail", json::array()},
            };
        }
        const int pid = status.value("pid", 0);
        if (!IsProcessRunning(pid)) {
            status["running"] = false;
            if (status.value("state", std::string()) != "error") {
                status["state"] = "stopped";
            }
        }
        status["debug_tail"] = ReadTailLinesIfPresent(kMoonshineAsrLogPath, 12);
        return status;
    }

    json OpenAiAsrStatus() const {
        json status = ReadJsonFileIfPresent(kOpenAiAsrStatePath);
        if (!status.is_object() || status.empty()) {
            const bool has_api_key =
                !Trim(std::getenv("OPENAI_API_KEY") ? std::getenv("OPENAI_API_KEY") : "").empty() ||
                !Trim(std::getenv("CHATGPT_API_KEY") ? std::getenv("CHATGPT_API_KEY") : "").empty() ||
                !Trim(std::getenv("CHAT_GPT_API") ? std::getenv("CHAT_GPT_API") : "").empty();
            return {
                {"available", std::filesystem::exists(ResolveOpenAiAsrHelperPath()) && has_api_key},
                {"running", false},
                {"state", "stopped"},
                {"last_heard", ""},
                {"last_error", ""},
                {"log_path", kOpenAiAsrLogPath},
                {"state_path", kOpenAiAsrStatePath},
            };
        }
        const int pid = status.value("pid", 0);
        if (!IsProcessRunning(pid)) {
            status["running"] = false;
            if (status.value("state", std::string()) != "error") {
                status["state"] = "stopped";
            }
        }
        return status;
    }

    json NativeOpenAiBridgeStatus() const {
        json status = {
            {"available", std::filesystem::exists(kFakeBytedanceOpenAiLogPath)},
            {"log_path", kFakeBytedanceOpenAiLogPath},
            {"debug_tail", ReadTailLinesIfPresent(kFakeBytedanceOpenAiLogPath, 200)},
            {"last_result", ""},
            {"last_result_event", ""},
            {"last_segment", ""},
        };

        if (!status["debug_tail"].is_array()) {
            return status;
        }

        for (auto it = status["debug_tail"].rbegin(); it != status["debug_tail"].rend(); ++it) {
            const std::string line = it->is_string() ? it->get<std::string>() : std::string();
            if (status["last_result"].get<std::string>().empty() && line.find("sent_result ") != std::string::npos) {
                const std::string extracted = Trim(ExtractQuotedSuffix(line, "text="));
                if (!extracted.empty()) {
                    status["last_result"] = extracted;
                    status["last_result_event"] = line;
                }
            }
            if (status["last_segment"].get<std::string>().empty() && line.find("segment_done ") != std::string::npos) {
                const std::string extracted = Trim(ExtractQuotedSuffix(line, "text="));
                if (!extracted.empty()) {
                    status["last_segment"] = extracted;
                }
            }
            if (!status["last_result"].get<std::string>().empty() &&
                !status["last_segment"].get<std::string>().empty()) {
                break;
            }
        }

        return status;
    }

    void EnsureSpeechIdle() {
#if !BOOSTER_DEV_MODE
        {
            std::scoped_lock lock(speech_mutex_);
            last_spoken_text_.clear();
        }
        (void)RunRosTtsHelper("lui_stop_asr", json::object());
#endif
    }

#if !BOOSTER_DEV_MODE
    json RunRosTtsHelper(const std::string &action, const json &payload) const {
        const std::string command =
            "source /opt/ros/humble/setup.bash >/dev/null 2>&1 && "
            "source " +
            ShellEscape(kRosSetupScript) +
            " >/dev/null 2>&1 && "
            "python3 " +
            ShellEscape(ResolveScriptPath("BOOSTER_ROS_TTS_HELPER", kRosTtsHelper)) + " " +
            ShellEscape(action) + " " + ShellEscape(payload.dump());

        std::array<char, 512> buffer{};
        std::string output;
        FILE *pipe = popen((std::string("bash -lc ") + ShellEscape(command)).c_str(), "r");
        if (!pipe) {
            return {
                {"ok", false},
                {"code", 500},
                {"action", action},
                {"error", "Failed to launch ROS TTS helper"},
            };
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        const int status = pclose(pipe);
        json result = ParseJsonOrRawString(output);
        if (!result.is_object()) {
            return {
                {"ok", false},
                {"code", status},
                {"action", action},
                {"error", "Unexpected helper output"},
                {"raw_output", Trim(output)},
            };
        }

        result["helper_exit_status"] = status;
        if (!result.contains("ok")) {
            result["ok"] = status == 0;
        }
        if (!result.contains("action")) {
            result["action"] = action;
        }
        return result;
    }
#endif

    std::string network_interface_;
    int domain_id_;
#if !BOOSTER_DEV_MODE
    std::unique_ptr<booster::robot::ChannelSubscriber<booster_interface::msg::Subtitle>> asr_monitor_;
#endif
    std::unique_ptr<BatteryMonitor> battery_monitor_;
    mutable std::mutex speech_mutex_;
    mutable int volume_percent_ = 100;
    mutable std::string last_heard_text_;
    mutable std::string last_spoken_text_;
};

RuntimeOptions ParseArgs(int argc, char **argv) {
    RuntimeOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const char *flag) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + flag);
            }
            return argv[++i];
        };

        if (arg == "--bind") {
            options.bind_address = require_value("--bind");
        } else if (arg == "--port") {
            options.port = static_cast<unsigned short>(std::stoi(require_value("--port")));
        } else if (arg == "--iface") {
            options.network_interface = require_value("--iface");
        } else if (arg == "--domain-id") {
            options.domain_id = std::stoi(require_value("--domain-id"));
        } else if (arg == "--web-root") {
            options.web_root = require_value("--web-root");
        } else if (arg == "--help" || arg == "-h") {
            std::ostringstream oss;
            oss << "Usage: " << argv[0]
                << " [--bind 0.0.0.0] [--port 8080] [--iface lo] [--domain-id 0] [--web-root web]";
            throw std::runtime_error(oss.str());
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (const char *env_iface = std::getenv("BOOSTER_NETWORK_INTERFACE"); env_iface && *env_iface) {
        options.network_interface = env_iface;
    }
    if (const char *env_bind = std::getenv("BOOSTER_BIND_ADDRESS"); env_bind && *env_bind) {
        options.bind_address = env_bind;
    }
    if (const char *env_port = std::getenv("BOOSTER_PORT"); env_port && *env_port) {
        options.port = static_cast<unsigned short>(std::stoi(env_port));
    }
    if (const char *env_web_root = std::getenv("BOOSTER_WEB_ROOT"); env_web_root && *env_web_root) {
        options.web_root = env_web_root;
    }

    return options;
}

http::response<http::string_body> JsonResponse(http::status status, const json &body) {
    http::response<http::string_body> res{status, 11};
    res.set(http::field::content_type, "application/json");
    res.set(http::field::cache_control, "no-store");
    res.body() = body.dump(2) + "\n";
    res.prepare_payload();
    return res;
}

http::response<http::string_body> TextResponse(
    http::status status,
    const std::string &body,
    const std::string &content_type) {
    http::response<http::string_body> res{status, 11};
    res.set(http::field::content_type, content_type);
    res.set(http::field::cache_control, "no-store");
    res.body() = body;
    res.prepare_payload();
    return res;
}

http::response<http::string_body> BinaryResponse(
    http::status status,
    std::string body,
    const std::string &content_type) {
    http::response<http::string_body> res{status, 11};
    res.set(http::field::content_type, content_type);
    res.set(http::field::cache_control, "no-store");
    res.body() = std::move(body);
    res.prepare_payload();
    return res;
}

http::response<http::string_body> HandleRequest(
    const http::request<http::string_body> &req,
    BatteryWrapper &wrapper,
    const RuntimeOptions &options) {
    const std::string target = std::string(req.target());
    const std::string path = QuerylessPath(target);

    if (req.method() == http::verb::get && (path == "/" || path == "/index.html")) {
        const auto file_path = options.web_root + "/index.html";
        return TextResponse(http::status::ok, ReadFile(file_path), ContentTypeForPath(file_path));
    }

    if (req.method() == http::verb::get && (path == "/app.js" || path == "/styles.css")) {
        const auto file_path = options.web_root + path;
        return TextResponse(http::status::ok, ReadFile(file_path), ContentTypeForPath(file_path));
    }

    if (req.method() == http::verb::get && path == "/health") {
        return JsonResponse(http::status::ok, {
            {"ok", true},
            {"service", "booster_battery_console"},
            {"wrapper", wrapper.StatusJson()},
        });
    }

    if (req.method() == http::verb::get && path == "/battery") {
        return JsonResponse(http::status::ok, {
            {"ok", true},
            {"battery", wrapper.StatusJson().value("battery", json::object())},
        });
    }

    if (req.method() == http::verb::get && path == "/audio/volume") {
        return JsonResponse(http::status::ok, wrapper.GetVolume());
    }

    if (req.method() == http::verb::get && path == "/camera/preview.jpg") {
        try {
            const std::string output_path = ResolveCameraPreviewPath();
            std::ifstream image_file(output_path, std::ios::binary);
            if (!image_file) {
                return JsonResponse(http::status::service_unavailable, {
                    {"ok", false},
                    {"error", "Camera preview not ready"},
                    {"path", path},
                    {"image_path", output_path},
                });
            }
            return BinaryResponse(http::status::ok, ReadFile(output_path), "image/jpeg");
        } catch (const std::exception &e) {
            return JsonResponse(http::status::service_unavailable, {
                {"ok", false},
                {"error", e.what()},
                {"path", path},
            });
        }
    }

    if (req.method() == http::verb::post && path == "/rtc/tts/start") {
        try {
            const json body = ParseOptionalJsonBody(req.body());
            const auto voice_type = Trim(body.value("voice_type", ResolveRtcVoiceType()));
            const int interrupt_speech_duration_ms =
                std::max(0, body.value("interrupt_speech_duration", kDefaultInterruptSpeechDurationMs));
            return JsonResponse(http::status::ok, wrapper.StartTts(voice_type, interrupt_speech_duration_ms));
        } catch (const std::exception &e) {
            return JsonResponse(http::status::bad_request, {
                {"ok", false},
                {"error", e.what()},
                {"path", path},
            });
        }
    }

    if (req.method() == http::verb::post && path == "/rtc/tts/stop") {
        return JsonResponse(http::status::ok, wrapper.StopTts());
    }

    if (req.method() == http::verb::post && path == "/whisperlive/asr/start") {
        try {
            const json body = ParseOptionalJsonBody(req.body());
            const auto model = Trim(body.value("model", std::string("base.en")));
            return JsonResponse(http::status::ok, wrapper.StartWhisperLiveAsr(model));
        } catch (const std::exception &e) {
            return JsonResponse(http::status::bad_request, {
                {"ok", false},
                {"error", e.what()},
                {"path", path},
            });
        }
    }

    if (req.method() == http::verb::post && path == "/whisperlive/asr/stop") {
        return JsonResponse(http::status::ok, wrapper.StopWhisperLiveAsr());
    }

    if (req.method() == http::verb::post && path == "/moonshine/asr/start") {
        try {
            const json body = ParseOptionalJsonBody(req.body());
            const auto model = Trim(body.value("model", std::string("medium-streaming")));
            return JsonResponse(http::status::ok, wrapper.StartMoonshineAsr(model));
        } catch (const std::exception &e) {
            return JsonResponse(http::status::bad_request, {
                {"ok", false},
                {"error", e.what()},
                {"path", path},
            });
        }
    }

    if (req.method() == http::verb::post && path == "/moonshine/asr/stop") {
        return JsonResponse(http::status::ok, wrapper.StopMoonshineAsr());
    }

    if (req.method() == http::verb::post && path == "/openai/asr/start") {
        try {
            const json body = ParseOptionalJsonBody(req.body());
            return JsonResponse(http::status::ok, wrapper.StartOpenAiAsr(body));
        } catch (const std::exception &e) {
            return JsonResponse(http::status::bad_request, {
                {"ok", false},
                {"error", e.what()},
                {"path", path},
            });
        }
    }

    if (req.method() == http::verb::post && path == "/openai/asr/stop") {
        return JsonResponse(http::status::ok, wrapper.StopOpenAiAsr());
    }

    if (req.method() == http::verb::post && path == "/audio/volume") {
        try {
            const json body = ParseOptionalJsonBody(req.body());
            const int volume_percent = body.at("volume_percent").get<int>();
            return JsonResponse(http::status::ok, wrapper.SetVolume(volume_percent));
        } catch (const std::exception &e) {
            return JsonResponse(http::status::bad_request, {
                {"ok", false},
                {"error", e.what()},
                {"path", path},
            });
        }
    }

    return JsonResponse(http::status::not_found, {
        {"ok", false},
        {"error", "Unknown endpoint"},
        {"path", path},
    });
}

void RunServer(const RuntimeOptions &options, BatteryWrapper &wrapper) {
    asio::io_context ioc{1};
    tcp::acceptor acceptor{ioc, {asio::ip::make_address(options.bind_address), options.port}};

    std::cout << "Listening on http://" << options.bind_address << ":" << options.port
              << " using interface '" << options.network_interface << "'" << std::endl;

    for (;;) {
        tcp::socket socket{ioc};
        acceptor.accept(socket);
        std::thread(
            [&wrapper, &options](tcp::socket client_socket) mutable {
                try {
                    beast::flat_buffer buffer;
                    http::request_parser<http::string_body> parser;
                    parser.body_limit(10 * 1024 * 1024);
                    http::read(client_socket, buffer, parser);
                    auto req = parser.release();

                    auto res = HandleRequest(req, wrapper, options);
                    res.set(http::field::server, "booster-battery-console");
                    http::write(client_socket, res);

                    beast::error_code ec;
                    client_socket.shutdown(tcp::socket::shutdown_send, ec);
                } catch (const std::exception &e) {
                    std::cerr << "HTTP session error: " << e.what() << std::endl;
                    try {
                        auto res = JsonResponse(http::status::internal_server_error, {
                            {"ok", false},
                            {"error", e.what()},
                        });
                        res.set(http::field::server, "booster-battery-console");
                        http::write(client_socket, res);
                        beast::error_code ec;
                        client_socket.shutdown(tcp::socket::shutdown_send, ec);
                    } catch (...) {
                    }
                }
            },
            std::move(socket))
            .detach();
    }
}

}  // namespace

int main(int argc, char **argv) {
    try {
        LoadDotEnvIfPresent(std::filesystem::path(BOOSTER_NATIVE_SDK_LAB_SOURCE_DIR) / ".env");
        const auto options = ParseArgs(argc, argv);
        BatteryWrapper wrapper(options.network_interface, options.domain_id);
        RunServer(options, wrapper);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
