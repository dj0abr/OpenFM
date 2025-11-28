// node_info_writer.cpp
#include "node_info_writer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <thread>    // std::thread
#include <chrono>    // std::chrono::seconds, std::this_thread::sleep_for
#include <cstdlib>   // std::system
#include <unistd.h>  // ::sync()

NodeInfoWriter::NodeInfoWriter(const std::string& outputPath)
    : outputPath_(outputPath),
      lastRun_(std::chrono::steady_clock::now() - std::chrono::seconds(10))
{
}

void NodeInfoWriter::tick()
{
    using namespace std::chrono;
    auto now = steady_clock::now();

    // nur alle 2 Sekunden wirklich arbeiten
    if (now - lastRun_ < seconds(2)) {
        return;
    }
    lastRun_ = now;

    updateIfNeeded();
}

bool rebootInProgress = false;

void NodeInfoWriter::updateIfNeeded()
{
    FMDatabase::ConfigRow cfg;
    if (!db_.getConfig(cfg)) {
        return;
    }

    // führe einen reboot aus, falls angefordert
    if (cfg.rebootRequested && !rebootInProgress) {
        rebootInProgress = true;

        std::fprintf(stderr, "[MAIN] Reboot requested via config table – rebooting...\n");

        // optional: in eigenen Thread, falls du den Main-Loop "sauber" beenden willst
        std::thread([]{
            ::sync();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::system("sudo /usr/sbin/shutdown -r now");
        }).detach();
    }

    // svxlink.conf aktualisieren
    bool confChanged = updateSvxlinkConf(cfg);

    // JSON-String generieren
    std::string json = buildJsonFromConfig(cfg);
    bool jsonChanged = (json != lastJson_);

    // Wenn weder JSON noch Config geändert -> nichts tun
    if (!jsonChanged && !confChanged) {
        return;
    }

    // JSON-Datei schreiben, wenn nötig
    if (jsonChanged) {
        std::ofstream ofs(outputPath_, std::ios::trunc);
        if (!ofs) {
            std::cerr << "[NodeInfoWriter] Could not open " << outputPath_ << " for writing\n";
        } else {
            ofs << json << std::endl;
            if (!ofs.good()) {
                std::cerr << "[NodeInfoWriter] Error while writing " << outputPath_ << "\n";
            } else {
                lastJson_ = std::move(json);
            }
        }
    }

    // Service neu starten, wenn sich svxlink.conf geändert hat
    if (confChanged) {
        if (!restartFmparserService()) {
            std::cerr << "[NodeInfoWriter] Failed to restart fmparser.service\n";
        }
    }
}

// einfache JSON-Escaping-Funktion
std::string NodeInfoWriter::escapeJson(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);

    for (char c : in) {
        switch (c) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                // Steuerzeichen als \u00XX
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
                out += buf;
            } else {
                out += c;
            }
        }
    }

    return out;
}

static std::string hzToMhzString(const std::string& hzStr)
{
    if (hzStr.empty()) {
        return "";
    }

    try {
        long long hz = std::stoll(hzStr);
        double mhz = hz / 1'000'000.0;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << mhz; // z.B. 439.050
        return oss.str();
    }
    catch (...) {
        // Fallback: wenn Parsing schiefgeht, original String zurück
        return hzStr;
    }
}

std::string NodeInfoWriter::buildJsonFromConfig(const FMDatabase::ConfigRow& cfg)
{
    std::string txMhz = hzToMhzString(cfg.TXFREQ);
    std::string rxMhz = hzToMhzString(cfg.RXFREQ);

    // "Sysop" (zweiter Key, klein geschrieben) zusammenbauen
    // z.B. "439.050 MHz @DJ0ABR DG6RCH"
    std::string sysopLabel;

    if (!txMhz.empty()) {
        sysopLabel += txMhz;
        sysopLabel += " MHz";
    }
    if (!cfg.SysOp.empty()) {
        if (!sysopLabel.empty()) {
            sysopLabel += " @";
        }
        sysopLabel += cfg.SysOp;
    }

    // DefaultTG als String
    std::string defaultTgStr = std::to_string(cfg.defaultTg);

    // JSON zusammensetzen
    std::string json;
    json.reserve(512);

    json += "{\n";
    json += "  \"Location\": \""     + escapeJson(cfg.Location)      + "\",\n";
    json += "  \"Locator\": \""      + escapeJson(cfg.Locator)       + "\",\n";
    json += "  \"SysOp\": \""        + escapeJson(cfg.SysOp)         + "\",\n";
    json += "  \"LAT\": \""          + escapeJson(cfg.LAT)           + "\",\n";
    json += "  \"LONG\": \""         + escapeJson(cfg.LON)           + "\",\n";
    json += "  \"TXFREQ\": \""       + escapeJson(txMhz)             + "\",\n";
    json += "  \"RXFREQ\": \""       + escapeJson(rxMhz)             + "\",\n";
    json += "  \"Website\": \""      + escapeJson(cfg.Website)       + "\",\n";
    json += "  \"Mode\": \"FM\",\n";
    json += "  \"Type\": \"1\",\n";
    json += "  \"Echolink\": \"0\",\n";
    json += "  \"nodeLocation\": \"" + escapeJson(cfg.nodeLocation)  + "\",\n";
    json += "  \"Sysop\": \""        + escapeJson(sysopLabel)        + "\",\n";
    json += "  \"Verbund\": \""      + escapeJson(cfg.dnsDomain)     + "\",\n";
    json += "  \"CTCSS\": \""        + escapeJson(cfg.CTCSS)         + "\",\n";
    json += "  \"DefaultTG\": \""    + escapeJson(defaultTgStr)      + "\"\n";
    json += "}\n";

    return json;
}

bool NodeInfoWriter::updateSvxlinkConf(const FMDatabase::ConfigRow& cfg)
{
    const std::string confPath = "/etc/svxlink/svxlink.conf";

    std::ifstream ifs(confPath);
    if (!ifs) {
        std::cerr << "[NodeInfoWriter] Could not open " << confPath << " for reading\n";
        return false;
    }

    std::vector<std::string> lines;
    lines.reserve(512);

    std::string line;
    std::string currentSection;
    bool changed = false;

    auto trim = [](const std::string& s) -> std::string {
        const char* ws = " \t\r\n";
        auto start = s.find_first_not_of(ws);
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(ws);
        return s.substr(start, end - start + 1);
    };

    while (std::getline(ifs, line)) {
        std::string originalLine = line;

        // Sektion erkennen: [SectionName]
        if (!line.empty() && line.front() == '[') {
            auto pos = line.find(']');
            if (pos != std::string::npos) {
                currentSection = line.substr(1, pos - 1);
            }
        } else {
            // Nur Zeilen mit KEY=VALUE bearbeiten
            auto eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = trim(line.substr(0, eqPos));
                std::string value = trim(line.substr(eqPos + 1));

                auto setValue = [&](const std::string& newVal) {
                    std::string newLine = key + "=" + newVal;
                    if (newLine != line) {
                        line = newLine;
                        changed = true;
                    }
                };

                if (currentSection == "SimplexLogic") {
                    if (key == "CALLSIGN") {
                        setValue(cfg.callsign);
                    } else if (key == "REPORT_CTCSS") {
                        setValue(cfg.CTCSS);
                    }
                } else if (currentSection == "RepeaterLogic") {
                    if (key == "CALLSIGN") {
                        setValue(cfg.callsign);
                    } else if (key == "REPORT_CTCSS") {
                        setValue(cfg.CTCSS);
                    }
                } else if (currentSection == "ReflectorLogic") {
                    if (key == "DNS_DOMAIN") {
                        setValue(cfg.dnsDomain);
                    } else if (key == "CALLSIGN") {
                        // hier in Anführungszeichen
                        setValue("\"" + cfg.callsign + "\"");
                    } else if (key == "DEFAULT_TG") {
                        setValue(std::to_string(cfg.defaultTg));
                    } else if (key == "MONITOR_TGS") {
                        setValue(cfg.monitorTgs);
                    }
                } else if (currentSection == "Tx1") {
                    if (key == "CTCSS_FQ") {
                        setValue(cfg.CTCSS);
                    }
                }
            }
        }

        lines.push_back(line);
    }

    if (!changed) {
        // nichts zu tun
        return false;
    }

    // Datei neu schreiben (optional: temp-Datei + rename für mehr Sicherheit)
    std::ofstream ofs(confPath, std::ios::trunc);
    if (!ofs) {
        std::cerr << "[NodeInfoWriter] Could not open " << confPath << " for writing\n";
        return false;
    }

    for (const auto& l : lines) {
        ofs << l << "\n";
    }

    if (!ofs.good()) {
        std::cerr << "[NodeInfoWriter] Error while writing " << confPath << "\n";
        return false;
    }

    return true; // Config wurde geändert
}

bool NodeInfoWriter::restartFmparserService()
{
    // Achtung: dafür muss in /etc/sudoers ein Eintrag vorhanden sein, damit
    // der User svxlink ohne Passwort systemctl ausführen darf, siehe install.sh
    int ret = std::system("sudo /usr/bin/systemctl restart svxlink.service");

    if (ret != 0) {
        std::cerr << "[NodeInfoWriter] systemctl restart svxlink.service failed, ret=" << ret << "\n";
        return false;
    }

    return true;
}
