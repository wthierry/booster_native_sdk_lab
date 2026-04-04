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
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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
constexpr char kDefaultVoiceType[] = "zh_female_shuangkuaisisi_emo_v2_mars_bigtts";
constexpr char kRosSetupScript[] = "/home/booster/Workspace/booster_robotics_sdk_ros2/install/setup.bash";
constexpr char kRosTtsHelper[] = "scripts/ros_rtc_tts.py";
constexpr int kDefaultInterruptSpeechDurationMs = 700;

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
#endif
    }

    json StatusJson() const {
        std::scoped_lock lock(speech_mutex_);
        return {
            {"network_interface", network_interface_},
            {"domain_id", domain_id_},
            {"dev_mode", static_cast<bool>(BOOSTER_DEV_MODE)},
            {"battery", battery_monitor_ ? battery_monitor_->ToJson() : json::object()},
            {"tts_transport", BOOSTER_DEV_MODE ? "mock" : "ros_rtc_service"},
            {"speech_debug", {
                {"last_heard", last_heard_text_},
                {"last_spoken", last_spoken_text_},
            }},
        };
    }

    json StartTts(const std::string &voice_type,
                  int interrupt_speech_duration_ms = kDefaultInterruptSpeechDurationMs) const {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "start"},
            {"interrupt_speech_duration", interrupt_speech_duration_ms},
            {"dev_mode", true},
            {"note", "TTS start is mocked in dev mode."},
        };
#else
        json payload = {
            {"interrupt_speech_duration", interrupt_speech_duration_ms},
        };
        if (!voice_type.empty()) {
            payload["voice_type"] = voice_type;
        }

        json result = RunRosTtsHelper("start", payload);
        const std::string response_body = Trim(result.value("response_body", std::string()));
        if (result.value("ok", false) || response_body != "Start chat failed") {
            return result;
        }

        const json stop_result = RunRosTtsHelper("stop", json::object());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        json retry_result = RunRosTtsHelper("start", payload);
        retry_result["recovered_after_restart"] = retry_result.value("ok", false);
        retry_result["auto_stop_result"] = stop_result;
        return retry_result;
#endif
    }

    json StopTts() {
#if BOOSTER_DEV_MODE
        return {
            {"ok", true},
            {"action", "stop"},
            {"dev_mode", true},
            {"note", "TTS stop is mocked in dev mode."},
        };
#else
        return RunRosTtsHelper("stop", json::object());
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
