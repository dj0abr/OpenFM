// MqttListener.cpp
#include "MqttListener.h"
#include "fmdatabase.h"

#include <iostream>
#include <cstring>
#include <nlohmann/json.hpp>
using nlohmann::json;

bool MqttListener::init()
{
    if (s_initialized.load()) {
        std::cerr << "[MqttListener] Already initialized\n";
        return false;
    }

    mosquitto_lib_init();

    s_mosq = mosquitto_new(s_clientId.c_str(), true, nullptr);
    if (!s_mosq) {
        std::cerr << "[MqttListener] mosquitto_new() failed\n";
        mosquitto_lib_cleanup();
        return false;
    }

    // optional: Auto-Reconnect-Parameter
    mosquitto_reconnect_delay_set(s_mosq,
                                  2,    // min delay
                                  30,   // max delay
                                  true  // exponential backoff
    );

    mosquitto_connect_callback_set(s_mosq, &MqttListener::onConnect);
    mosquitto_message_callback_set(s_mosq, &MqttListener::onMessage);
    mosquitto_disconnect_callback_set(s_mosq, &MqttListener::onDisconnect);
    mosquitto_log_callback_set(s_mosq, &MqttListener::onLog);

    // eigene DB
    s_db = new FMDatabase();

    s_initialized = true;
    return true;
}

void MqttListener::start()
{
    if (!s_initialized.load()) {
        std::cerr << "[MqttListener] Not initialized\n";
        return;
    }
    if (s_running.load()) {
        std::cerr << "[MqttListener] Already running\n";
        return;
    }

    // asynchron verbinden
    int rc = mosquitto_connect_async(s_mosq, s_host.c_str(), s_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MqttListener] mosquitto_connect_async failed: "
                  << mosquitto_strerror(rc) << "\n";
        return;
    }

    // internen mosquitto-Thread starten
    rc = mosquitto_loop_start(s_mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MqttListener] mosquitto_loop_start failed: "
                  << mosquitto_strerror(rc) << "\n";
        return;
    }

    s_running = true;
    std::cout << "[MqttListener] loop started, waiting for messages...\n";
}

void MqttListener::stop()
{
    if (!s_running.load())
        return;

    s_running = false;

    if (s_mosq) {
        mosquitto_disconnect(s_mosq);
        mosquitto_loop_stop(s_mosq, true); // true = Thread blockierend beenden
        mosquitto_destroy(s_mosq);
        s_mosq = nullptr;
    }

    mosquitto_lib_cleanup();

    delete s_db;
    s_db = nullptr;

    s_initialized = false;

    std::cout << "[MqttListener] stopped\n";
}

void MqttListener::onConnect(struct mosquitto* /*mosq*/, void* /*userdata*/, int rc)
{
    std::cout << "[MqttListener] onConnect rc=" << rc << "\n";
    if (rc == 0) {
        // Talker-Events
        std::cout << "[MqttListener] Subscribing to topic: /server/statethr/1\n";
        int subRc1 = mosquitto_subscribe(s_mosq, nullptr, "/server/statethr/1", 0);
        if (subRc1 != MOSQ_ERR_SUCCESS) {
            std::cerr << "[MqttListener] subscribe statethr failed: "
                      << mosquitto_strerror(subRc1) << "\n";
        }

        // Node-Infos
        std::cout << "[MqttListener] Subscribing to topic: /server/state/nodes/#\n";
        int subRc2 = mosquitto_subscribe(s_mosq, nullptr, "/server/state/nodes/#", 0);
        if (subRc2 != MOSQ_ERR_SUCCESS) {
            std::cerr << "[MqttListener] subscribe nodes failed: "
                      << mosquitto_strerror(subRc2) << "\n";
        }
    } else {
        std::cerr << "[MqttListener] Connect failed, rc=" << rc << "\n";
    }
}

void MqttListener::onDisconnect(struct mosquitto* /*mosq*/,
                                void* /*userdata*/,
                                int rc)
{
    std::cerr << "[MqttListener] onDisconnect rc=" << rc << "\n";
    // rc == 0 -> sauber getrennt
    // rc > 0  -> unerwartet (vom Broker oder Fehler)
}

void MqttListener::onLog(struct mosquitto* /*mosq*/,
                         void* /*userdata*/,
                         int level,
                         const char* str)
{
    // ruhig alles loggen, erstmal zum Debuggen
    //std::cerr << "[mosq-log " << level << "] " << (str ? str : "") << "\n";
}

void MqttListener::onMessage(struct mosquitto* /*mosq*/,
                             void* /*userdata*/,
                             const struct mosquitto_message* msg)
{
    std::string topic = msg->topic ? msg->topic : "";

    std::string payload;
    if (msg->payload && msg->payloadlen > 0) {
        payload.assign(static_cast<char*>(msg->payload),
                       static_cast<size_t>(msg->payloadlen));
    }

    /*
    std::cout << "[MQTT] Topic: " << topic
              << " | QoS: " << msg->qos
              << " | Retain: " << msg->retain
              << " | Payload: " << payload << "\n";
    */
    if (!s_db) return;

    // Whitespace vorne weg
    std::string trimmed = payload;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t' ||
                                trimmed[0] == '\r' || trimmed[0] == '\n')) {
        trimmed.erase(trimmed.begin());
    }

    // 1) Talker-Events (/server/statethr...)
    if (topic.rfind("/server/statethr", 0) == 0) {
        if (!trimmed.empty() && trimmed[0] == '{') {
            try {
                json j = json::parse(trimmed);

                std::string timeStr  = j.value("time",   "");
                std::string talkStr  = j.value("talk",   "");
                std::string callStr  = j.value("call",   "");
                std::string tgStr    = j.value("tg",     "");
                std::string srvStr   = j.value("server", "");

                if (!timeStr.empty() && !talkStr.empty() &&
                    !callStr.empty() && !tgStr.empty()) {
                    if (!s_db->insertEvent(timeStr, talkStr, callStr, tgStr, srvStr)) {
                        std::cerr << "[MqttListener] insertEvent failed\n";
                    }
                } else {
                    std::cerr << "[MqttListener] JSON (statethr) missing required fields\n";
                }

            } catch (const std::exception& e) {
                std::cerr << "[MqttListener] JSON parse error (statethr): " << e.what() << "\n";
            }
        }
    }
    // 2) Node-Infos (/server/state/nodes/...)
    else if (topic.rfind("/server/state/nodes/", 0) == 0) {
        if (!trimmed.empty() && trimmed[0] == '{') {
            try {
                json j = json::parse(trimmed);

                std::string call     = j.value("call",     "");
                std::string location = j.value("location", "");
                std::string locator  = j.value("locator",  "");
                std::string rx_freq  = j.value("rx_freq",  "");
                std::string tx_freq  = j.value("tx_freq",  "");

                double lat = std::numeric_limits<double>::quiet_NaN();
                double lon = std::numeric_limits<double>::quiet_NaN();

                // lat/lon können null sein
                if (j.contains("lat") && !j["lat"].is_null()) {
                    if (j["lat"].is_number_float() || j["lat"].is_number_integer()) {
                        lat = j["lat"].get<double>();
                    } else if (j["lat"].is_string()) {
                        try {
                            lat = std::stod(j["lat"].get<std::string>());
                        } catch (...) {}
                    }
                }
                if (j.contains("lon") && !j["lon"].is_null()) {
                    if (j["lon"].is_number_float() || j["lon"].is_number_integer()) {
                        lon = j["lon"].get<double>();
                    } else if (j["lon"].is_string()) {
                        try {
                            lon = std::stod(j["lon"].get<std::string>());
                        } catch (...) {}
                    }
                }

                if (!call.empty()) {
                    if (!s_db->upsertNode(call, location, locator, lat, lon, rx_freq, tx_freq)) {
                        std::cerr << "[MqttListener] upsertNode failed\n";
                    }
                } else {
                    std::cerr << "[MqttListener] nodes JSON without call - ignored\n";
                }

            } catch (const std::exception& e) {
                std::cerr << "[MqttListener] JSON parse error (nodes): " << e.what() << "\n";
            }
        }
    }
}
