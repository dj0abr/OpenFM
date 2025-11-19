#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include "MqttListener.h"
#include "handleConfig.h"
#include "node_info_writer.h"

static std::atomic<bool> g_running{true};

void sigHandler(int)
{
    g_running = false;
}

int main(){

    // Starte FM Funknetz Abfragen als Thread
    MqttListener::init();
    std::signal(SIGINT,  sigHandler);
    std::signal(SIGTERM, sigHandler);
    MqttListener::start();

    handleConfig cfg;
    cfg.run();

    NodeInfoWriter nodeInfoWriter("/etc/svxlink/node_info.json");

    while(g_running) {
        nodeInfoWriter.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    MqttListener::stop();
    return 0;
}
