// handleConfig.h
#pragma once

#include <string>

class handleConfig {
public:
    // liest die Configdatei und schreibt die Werte in die DB
    bool run() noexcept;

private:
    bool parseConfigFile(const std::string& path) noexcept;

    std::string callsign_;
    std::string dnsDomain_;
    int         defaultTg_   = 0;
    std::string monitorTgs_;
};
