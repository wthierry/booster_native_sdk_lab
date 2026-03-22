#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <booster/idl/ai/Subtitle.h>
#include <booster/idl/b1/BatteryState.h>
#include <booster/robot/ai/const.hpp>
#include <booster/robot/channel/channel_factory.hpp>
#include <booster/robot/channel/channel_subscriber.hpp>
#include <booster/third_party/nlohmann_json/json.hpp>

#include <array>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

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
constexpr char kTopicBatteryState[] = "rt/battery_state";
constexpr char kDefaultVoiceType[] = "zh_female_shuangkuaisisi_emo_v2_mars_bigtts";
constexpr char kCameraPreviewPath[] = "/tmp/booster_camera_preview.jpg";
constexpr char kRosSetupScript[] = "/home/booster/Workspace/booster_robotics_sdk_ros2/install/setup.bash";
constexpr char kRosTtsHelper[] = "scripts/ros_rtc_tts.py";
constexpr char kOpenAiVisionHelper[] = "scripts/openai_vision.py";

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

std::string ReadFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
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

class BatteryMonitor {
public:
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
};

class BatteryWrapper {
public:
    explicit BatteryWrapper(std::string network_interface, int domain_id)
        : network_interface_(std::move(network_interface)), domain_id_(domain_id) {
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
    }

    json StatusJson() const {
        std::scoped_lock lock(speech_mutex_);
        return {
            {"network_interface", network_interface_},
            {"domain_id", domain_id_},
            {"battery", battery_monitor_ ? battery_monitor_->ToJson() : json::object()},
            {"tts_transport", "ros_rtc_service"},
            {"speech_debug", {
                {"last_heard", last_heard_text_},
                {"last_spoken", last_spoken_text_},
                {"last_openai_vision", last_openai_vision_text_},
                {"last_openai_error", last_openai_error_},
            }},
        };
    }

    json RefreshVisualContext() const {
        std::scoped_lock vision_lock(vision_mutex_);
        json vision = AnalyzeCurrentImageWithOpenAi();
        if (vision.value("ok", false)) {
            const std::string answer = Trim(vision.value("answer", std::string()));
            {
                std::scoped_lock lock(speech_mutex_);
                last_openai_vision_text_ = answer;
                last_openai_error_.clear();
            }
        } else {
            const std::string error_summary =
                Trim(vision.value("error", std::string()) + " " + vision.value("response_body", std::string()));
            std::scoped_lock lock(speech_mutex_);
            last_openai_vision_text_.clear();
            last_openai_error_ = error_summary;
        }
        vision["action"] = "refresh_visual_context";
        return vision;
    }

    json StartTts(const std::string &voice_type) const {
        return RunRosTtsHelper("start", {{"voice_type", voice_type}});
    }

    json StopTts() {
        return RunRosTtsHelper("stop", json::object());
    }

    json SpeakTts(const std::string &text) const {
        {
            std::scoped_lock lock(speech_mutex_);
            last_spoken_text_ = text;
        }
        return RunRosTtsHelper("speak", {{"text", text}});
    }

    json GetVolume() const {
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
    }

    json SetVolume(int percent) const {
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
    }

private:
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

    json AnalyzeCurrentImageWithOpenAi() const {
        const std::string prompt =
            "The robot was asked 'what do you see?'. Reply in exactly 1 or 2 short spoken sentences. "
            "Mention only the most important visible people, objects, or activity. If uncertain, say so briefly.";
        const std::string command =
            "python3 " +
            ShellEscape(ResolveScriptPath("BOOSTER_OPENAI_VISION_HELPER", kOpenAiVisionHelper)) + " analyze " +
            ShellEscape(json({
                {"prompt", prompt},
                {"image_path", kCameraPreviewPath},
            }).dump());

        int status = 0;
        const auto output = RunCommand(command, &status);
        json result = ParseJsonOrRawString(output);
        if (!result.is_object()) {
            return {
                {"ok", false},
                {"code", status},
                {"error", "Unexpected OpenAI vision helper output"},
                {"raw_output", Trim(output)},
            };
        }
        result["helper_exit_status"] = status;
        return result;
    }

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
    std::string network_interface_;
    int domain_id_;
    std::unique_ptr<BatteryMonitor> battery_monitor_;
    std::unique_ptr<booster::robot::ChannelSubscriber<booster_interface::msg::Subtitle>> asr_monitor_;
    mutable std::mutex vision_mutex_;
    mutable std::mutex speech_mutex_;
    mutable std::string last_heard_text_;
    mutable std::string last_spoken_text_;
    mutable std::string last_openai_vision_text_;
    mutable std::string last_openai_error_;
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
            const std::string output_path = kCameraPreviewPath;
            std::ifstream image_file(output_path, std::ios::binary);
            if (!image_file) {
                return JsonResponse(http::status::service_unavailable, {
                    {"ok", false},
                    {"error", "Camera preview not ready"},
                    {"path", path},
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
            const auto voice_type = body.value("voice_type", std::string(kDefaultVoiceType));
            return JsonResponse(http::status::ok, wrapper.StartTts(voice_type));
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

    if (req.method() == http::verb::post && path == "/rtc/tts/speak") {
        try {
            const json body = ParseOptionalJsonBody(req.body());
            const auto text = Trim(body.value("text", std::string()));
            if (text.empty()) {
                return JsonResponse(http::status::bad_request, {
                    {"ok", false},
                    {"error", "Missing text"},
                    {"path", path},
                });
            }
            return JsonResponse(http::status::ok, wrapper.SpeakTts(text));
        } catch (const std::exception &e) {
            return JsonResponse(http::status::bad_request, {
                {"ok", false},
                {"error", e.what()},
                {"path", path},
            });
        }
    }

    if (req.method() == http::verb::post && path == "/vision/refresh") {
        return JsonResponse(http::status::ok, wrapper.RefreshVisualContext());
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
                    http::request<http::string_body> req;
                    http::read(client_socket, buffer, req);

                    auto res = HandleRequest(req, wrapper, options);
                    res.set(http::field::server, "booster-battery-console");
                    http::write(client_socket, res);

                    beast::error_code ec;
                    client_socket.shutdown(tcp::socket::shutdown_send, ec);
                } catch (const std::exception &e) {
                    std::cerr << "HTTP session error: " << e.what() << std::endl;
                }
            },
            std::move(socket))
            .detach();
    }
}

}  // namespace

int main(int argc, char **argv) {
    try {
        const auto options = ParseArgs(argc, argv);
        BatteryWrapper wrapper(options.network_interface, options.domain_id);
        RunServer(options, wrapper);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
