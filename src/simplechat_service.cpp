#include "llama.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#elif __has_include(<booster/third_party/nlohmann_json/json.hpp>)
#include <booster/third_party/nlohmann_json/json.hpp>
#else
#error "nlohmann json header not found"
#endif

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

namespace {

struct Options {
    std::string model_path;
    std::string bind_address = "127.0.0.1";
    unsigned short port = 8092;
    int ngl = 99;
    int n_ctx = 2048;
};

void print_usage(int, char ** argv) {
    std::printf("\nexample usage:\n");
    std::printf(
        "\n    %s -m model.gguf [--bind 127.0.0.1] [--port 8092] [-c 2048] [-ngl 99]\n",
        argv[0]);
    std::printf("\n");
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    return value.substr(start);
}

Options parse_args(int argc, char ** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        try {
            if (std::strcmp(argv[i], "-m") == 0) {
                if (i + 1 >= argc) {
                    print_usage(argc, argv);
                    std::exit(1);
                }
                options.model_path = argv[++i];
            } else if (std::strcmp(argv[i], "-c") == 0) {
                if (i + 1 >= argc) {
                    print_usage(argc, argv);
                    std::exit(1);
                }
                options.n_ctx = std::stoi(argv[++i]);
            } else if (std::strcmp(argv[i], "-ngl") == 0) {
                if (i + 1 >= argc) {
                    print_usage(argc, argv);
                    std::exit(1);
                }
                options.ngl = std::stoi(argv[++i]);
            } else if (std::strcmp(argv[i], "--bind") == 0) {
                if (i + 1 >= argc) {
                    print_usage(argc, argv);
                    std::exit(1);
                }
                options.bind_address = argv[++i];
            } else if (std::strcmp(argv[i], "--port") == 0) {
                if (i + 1 >= argc) {
                    print_usage(argc, argv);
                    std::exit(1);
                }
                options.port = static_cast<unsigned short>(std::stoi(argv[++i]));
            } else {
                print_usage(argc, argv);
                std::exit(1);
            }
        } catch (const std::exception & e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            print_usage(argc, argv);
            std::exit(1);
        }
    }
    if (options.model_path.empty()) {
        print_usage(argc, argv);
        std::exit(1);
    }
    return options;
}

json parse_body_as_json_or_text(const std::string &body) {
    const std::string trimmed = trim(body);
    if (trimmed.empty()) {
        return json::object();
    }
    try {
        return json::parse(trimmed);
    } catch (...) {
        return json{{"text", trimmed}};
    }
}

http::response<http::string_body> json_response(http::status status, const json &body) {
    http::response<http::string_body> res{status, 11};
    res.set(http::field::content_type, "application/json");
    res.set(http::field::cache_control, "no-store");
    res.body() = body.dump(2) + "\n";
    res.prepare_payload();
    return res;
}

class SimpleChatEngine {
public:
    explicit SimpleChatEngine(const Options &options) {
        std::setlocale(LC_NUMERIC, "C");

        llama_log_set([](enum ggml_log_level level, const char * text, void *) {
            if (level >= GGML_LOG_LEVEL_ERROR) {
                std::fprintf(stderr, "%s", text);
            }
        }, nullptr);

        ggml_backend_load_all();

        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = options.ngl;

        model_ = llama_model_load_from_file(options.model_path.c_str(), model_params);
        if (!model_) {
            throw std::runtime_error("unable to load model");
        }

        vocab_ = llama_model_get_vocab(model_);

        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = options.n_ctx;
        ctx_params.n_batch = options.n_ctx;

        ctx_ = llama_init_from_model(model_, ctx_params);
        if (!ctx_) {
            throw std::runtime_error("failed to create the llama_context");
        }

        sampler_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.8f));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

        formatted_.resize(llama_n_ctx(ctx_));
    }

    ~SimpleChatEngine() {
        clear_messages();
        if (sampler_) {
            llama_sampler_free(sampler_);
        }
        if (ctx_) {
            llama_free(ctx_);
        }
        if (model_) {
            llama_model_free(model_);
        }
    }

    json reply(const std::string &user_text) {
        const std::string trimmed = trim(user_text);
        if (trimmed.empty()) {
            return {{"ok", false}, {"error", "text is required"}};
        }

        std::scoped_lock lock(mutex_);
        const char * tmpl = llama_model_chat_template(model_, nullptr);
        if (!tmpl) {
            return {{"ok", false}, {"error", "model has no chat template"}};
        }

        messages_.push_back(llama_chat_message{"user", ::strdup(trimmed.c_str())});
        int new_len = llama_chat_apply_template(
            tmpl,
            messages_.data(),
            messages_.size(),
            true,
            formatted_.data(),
            formatted_.size());
        if (new_len > static_cast<int>(formatted_.size())) {
            formatted_.resize(new_len);
            new_len = llama_chat_apply_template(
                tmpl,
                messages_.data(),
                messages_.size(),
                true,
                formatted_.data(),
                formatted_.size());
        }
        if (new_len < 0) {
            pop_last_message();
            return {{"ok", false}, {"error", "failed to apply the chat template"}};
        }

        const std::string prompt(formatted_.begin() + prev_len_, formatted_.begin() + new_len);
        std::string response;
        try {
            response = generate_locked(prompt);
        } catch (const std::exception &e) {
            pop_last_message();
            return {{"ok", false}, {"error", e.what()}};
        }

        messages_.push_back(llama_chat_message{"assistant", ::strdup(response.c_str())});
        prev_len_ = llama_chat_apply_template(tmpl, messages_.data(), messages_.size(), false, nullptr, 0);
        if (prev_len_ < 0) {
            return {{"ok", false}, {"error", "failed to update conversation state"}};
        }

        return {
            {"ok", true},
            {"reply", response},
            {"turns", static_cast<int>(messages_.size() / 2)},
        };
    }

    json reset() {
        std::scoped_lock lock(mutex_);
        clear_messages();
        prev_len_ = 0;
        return {
            {"ok", true},
            {"reset", true},
        };
    }

    json health() const {
        std::scoped_lock lock(mutex_);
        return {
            {"ok", true},
            {"status", "ok"},
            {"turns", static_cast<int>(messages_.size() / 2)},
        };
    }

private:
    std::string generate_locked(const std::string &prompt) {
        std::string response;

        const bool is_first = llama_memory_seq_pos_max(llama_get_memory(ctx_), 0) == -1;
        const int n_prompt_tokens =
            -llama_tokenize(vocab_, prompt.c_str(), prompt.size(), nullptr, 0, is_first, true);
        if (n_prompt_tokens <= 0) {
            throw std::runtime_error("failed to tokenize prompt");
        }

        std::vector<llama_token> prompt_tokens(n_prompt_tokens);
        if (llama_tokenize(
                vocab_,
                prompt.c_str(),
                prompt.size(),
                prompt_tokens.data(),
                prompt_tokens.size(),
                is_first,
                true) < 0) {
            throw std::runtime_error("failed to tokenize prompt");
        }

        llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
        llama_token new_token_id;
        while (true) {
            const int n_ctx = llama_n_ctx(ctx_);
            const int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(ctx_), 0) + 1;
            if (n_ctx_used + batch.n_tokens > n_ctx) {
                throw std::runtime_error("context size exceeded");
            }

            const int ret = llama_decode(ctx_, batch);
            if (ret != 0) {
                throw std::runtime_error("failed to decode");
            }

            new_token_id = llama_sampler_sample(sampler_, ctx_, -1);
            if (llama_vocab_is_eog(vocab_, new_token_id)) {
                break;
            }

            char buf[256];
            const int n = llama_token_to_piece(vocab_, new_token_id, buf, sizeof(buf), 0, true);
            if (n < 0) {
                throw std::runtime_error("failed to convert token to piece");
            }
            response.append(buf, n);
            batch = llama_batch_get_one(&new_token_id, 1);
        }

        return trim(response);
    }

    void clear_messages() {
        for (auto &msg : messages_) {
            std::free(const_cast<char *>(msg.content));
        }
        messages_.clear();
    }

    void pop_last_message() {
        if (messages_.empty()) {
            return;
        }
        std::free(const_cast<char *>(messages_.back().content));
        messages_.pop_back();
    }

    llama_model *model_ = nullptr;
    llama_context *ctx_ = nullptr;
    llama_sampler *sampler_ = nullptr;
    const llama_vocab *vocab_ = nullptr;
    std::vector<llama_chat_message> messages_;
    std::vector<char> formatted_;
    int prev_len_ = 0;
    mutable std::mutex mutex_;
};

http::response<http::string_body> handle_request(
    const http::request<http::string_body> &req,
    SimpleChatEngine &engine) {
    const std::string path = std::string(req.target());

    if (req.method() == http::verb::get && path == "/health") {
        return json_response(http::status::ok, engine.health());
    }

    if (req.method() == http::verb::post && path == "/reset") {
        return json_response(http::status::ok, engine.reset());
    }

    if (req.method() == http::verb::post && path == "/reply") {
        try {
            const json body = parse_body_as_json_or_text(req.body());
            const std::string text = trim(body.value("text", std::string()));
            if (text.empty()) {
                return json_response(http::status::bad_request, {
                    {"ok", false},
                    {"error", "text is required"},
                });
            }
            const json result = engine.reply(text);
            return json_response(
                result.value("ok", false) ? http::status::ok : http::status::unprocessable_entity,
                result);
        } catch (const std::exception &e) {
            return json_response(http::status::bad_request, {
                {"ok", false},
                {"error", e.what()},
            });
        }
    }

    return json_response(http::status::not_found, {
        {"ok", false},
        {"error", "not found"},
    });
}

void serve(SimpleChatEngine &engine, const Options &options) {
    asio::io_context io_context{1};
    tcp::acceptor acceptor(io_context, {asio::ip::make_address(options.bind_address), options.port});

    for (;;) {
        tcp::socket socket(io_context);
        acceptor.accept(socket);

        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);

        auto res = handle_request(req, engine);
        res.keep_alive(false);
        http::write(socket, res);

        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
    }
}

} // namespace

int main(int argc, char ** argv) {
    try {
        const Options options = parse_args(argc, argv);
        SimpleChatEngine engine(options);
        std::cerr << "simplechat-service listening on " << options.bind_address << ":" << options.port << "\n";
        serve(engine, options);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
