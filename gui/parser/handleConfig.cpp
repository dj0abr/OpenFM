// handleConfig.cpp
#include "handleConfig.h"
#include "fmdatabase.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {

// einfache Trim-Funktion für Whitespace
std::string trim(const std::string& s)
{
    std::size_t start = 0;
    while (start < s.size() &&
           std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }

    std::size_t end = s.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    return s.substr(start, end - start);
}

} // namespace

bool handleConfig::parseConfigFile(const std::string& path) noexcept
{
    std::ifstream in(path);
    if (!in) {
        std::cerr << "[handleConfig] Kann Config-Datei nicht öffnen: "
                  << path << "\n";
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(in, line)) {
        // CR am Ende (Windows-Lines) entfernen
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::string t = trim(line);
        if (t.empty()) continue;

        // Kommentare (# oder ;)
        if (t[0] == '#' || t[0] == ';') continue;

        // Sektion [XYZ]
        if (t.front() == '[' && t.back() == ']') {
            currentSection = t.substr(1, t.size() - 2);
            continue;
        }

        // key=value
        auto posEq = t.find('=');
        if (posEq == std::string::npos) continue;

        std::string key = trim(t.substr(0, posEq));
        std::string val = trim(t.substr(posEq + 1));

        if (currentSection == "RepeaterLogic") {
            if (key == "CALLSIGN") {
                callsign_ = val;
            }
        } else if (currentSection == "ReflectorLogic") {
            if (key == "DNS_DOMAIN") {
                dnsDomain_ = val;
            } else if (key == "DEFAULT_TG") {
                try {
                    defaultTg_ = std::stoi(val);
                } catch (...) {
                    defaultTg_ = 0;
                }
            } else if (key == "MONITOR_TGS") {
                monitorTgs_ = val;
            }
        }
    }

    if (callsign_.empty()) {
        std::cerr << "[handleConfig] CALLSIGN in [RepeaterLogic] nicht gefunden\n";
        return false;
    }
    if (dnsDomain_.empty()) {
        std::cerr << "[handleConfig] DNS_DOMAIN in [ReflectorLogic] nicht gefunden\n";
        return false;
    }
    // DEFAULT_TG und MONITOR_TGS könnten theoretisch optional sein,
    // hier lassen wir defaultTg_ = 0 und monitorTgs_ = "" zu.

    return true;
}

bool handleConfig::run() noexcept
{
    const std::string path = "/etc/svxlink/svxlink.conf";

    if (!parseConfigFile(path)) {
        std::cerr << "[handleConfig] parseConfigFile fehlgeschlagen\n";
        return false;
    }

    // eigene DB-Instanz (analog zu MqttListener)
    FMDatabase db;

    if (!db.upsertConfig(callsign_, dnsDomain_, defaultTg_, monitorTgs_)) {
        std::cerr << "[handleConfig] upsertConfig fehlgeschlagen\n";
        return false;
    }

    std::cout << "[handleConfig] Config in DB geschrieben\n";
    return true;
}
