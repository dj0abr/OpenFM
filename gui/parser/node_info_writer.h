// node_info_writer.h
#pragma once

#include <string>
#include <chrono>
#include "fmdatabase.h"

class NodeInfoWriter {
public:
    explicit NodeInfoWriter(const std::string& outputPath = "/etc/svxlink/node_info.json");

    // in der main-Loop regelmäßig aufrufen
    void tick();

private:
    void updateIfNeeded();
    static std::string escapeJson(const std::string& in);
    static std::string buildJsonFromConfig(const FMDatabase::ConfigRow& cfg);
    bool updateSvxlinkConf(const FMDatabase::ConfigRow& cfg);
    bool restartFmparserService();

    FMDatabase db_;
    std::string outputPath_;

    std::chrono::steady_clock::time_point lastRun_;
    std::string lastJson_;  // zum Vergleich
};
