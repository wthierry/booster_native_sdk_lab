#include <booster/robot/channel/channel_subscriber.hpp>
#include <booster/robot/channel/channel_factory.hpp>
#include <booster/robot/ai/const.hpp>
#include <booster/idl/ai/AsrChunk.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using booster::robot::ChannelFactory;
using booster::robot::ChannelSubscriber;
using booster_interface::msg::AsrChunk;

namespace {

std::atomic<bool> g_running{true};
std::atomic<int> g_count{0};

void HandleSignal(int) {
    g_running = false;
}

void HandleChunk(const void* msg) {
    const auto* chunk = static_cast<const AsrChunk*>(msg);
    ++g_count;
    std::cout << "[LUI_CHUNK " << g_count.load() << "] " << chunk->text() << std::endl;
    std::cout.flush();
}

}  // namespace

int main() {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    std::cout << "Initializing DDS on loopback and subscribing to "
              << booster::robot::kTopicLuiAsrChunk << std::endl;
    ChannelFactory::Instance()->Init(0, "lo");

    ChannelSubscriber<AsrChunk> subscriber(booster::robot::kTopicLuiAsrChunk,
                                           HandleChunk);
    subscriber.InitChannel();

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "Exiting after receiving " << g_count.load() << " chunks" << std::endl;
    return 0;
}
