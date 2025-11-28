// fmdatabase.h
#pragma once

#include <string>
#include <mysql/mysql.h>
#include <mutex>
#include <vector>
#include <array>
#include <cstdint>
#include <unordered_map>

struct FMCallQsoCount {
    std::string   callsign;
    std::uint64_t qsoCount;
};

struct FMCallDuration {
    std::string callsign;
    double      totalSeconds;
};

struct FMCallScore {
    std::string   callsign;
    std::uint64_t qsoCount;
    double        totalSeconds;
    double        score;
};

struct FMTgDuration {
    int    tg  = 0;
    std::uint64_t qsoCount = 0;
    double totalSeconds = 0.0;
};

using FMQsoHeatmap = std::array<std::array<std::uint32_t, 24>, 7>; // [weekday][hour], weekday: 0=Mo..6=So

class FMDatabase {
public:
    FMDatabase();
    ~FMDatabase();

    FMDatabase(const FMDatabase&) = delete;
    FMDatabase& operator=(const FMDatabase&) = delete;

    // Ein einzelnes MQTT-Event eintragen + fmstatus pflegen
    bool insertEvent(const std::string& timeStr,
                     const std::string& talk,
                     const std::string& call,
                     const std::string& tg,
                     const std::string& server) noexcept;

    bool upsertNode(const std::string& callsign,
                    const std::string& location,
                    const std::string& locator,
                    double lat,
                    double lon,
                    const std::string& rx_freq,
                    const std::string& tx_freq) noexcept;

    // Config-Tabelle (immer nur eine Zeile, id=1)
    bool upsertConfig(const std::string& callsign,
                      const std::string& dnsDomain,
                      int defaultTg,
                      const std::string& monitorTgs) noexcept;

    // Haupt-Statistikfunktion, aus main loop aufrufbar
    void statistics() noexcept;

    // Struktur für die config-Zeile
    struct ConfigRow {
        int         id = 0;
        std::string callsign;
        std::string dnsDomain;
        int         defaultTg = 0;
        std::string monitorTgs;

        std::string Location;
        std::string Locator;
        std::string SysOp;
        std::string LAT;
        std::string LON;
        std::string TXFREQ;
        std::string RXFREQ;
        std::string Website;
        std::string nodeLocation;
        std::string CTCSS;

        std::string updatedAt; // "YYYY-MM-DD HH:MM:SS"

        bool rebootRequested = false;
    };

    // NEU: config lesen (id=1)
    bool getConfig(ConfigRow& out) noexcept;

private:
    bool connect() noexcept;
    bool ensureSchema() noexcept;
    bool ensureConn() noexcept;

    // fmlastheard begrenzen
    bool pruneIfNeeded() noexcept;

    // fmstatus: aktive Stationen pflegen
    bool updateStatus(const std::string& dt,
                      const std::string& talk,
                      const std::string& call,
                      int tg,
                      const std::string& server) noexcept;

    // Einträge, die länger als 3 Minuten nicht aktualisiert wurden, löschen
    bool cleanupStatus() noexcept;

    // Hilfsfunktionen
    std::string makeDateTime(const std::string& timeStr) noexcept;
    std::string escape(const std::string& in) noexcept;

    MYSQL* conn_ = nullptr;
    std::string lastError_;

    std::mutex mtx_;

    const std::string dbUser_       = "svxlink";
    const std::string dbPass_       = "";
    const std::string dbName_       = "mmdvmdb";
    const std::string dbUnixSocket_ = "/run/mysqld/mysqld.sock";
    const unsigned int dbPort_      = 0; // 0 = über Unix-Socket

    // für die Statistik
    // Aggregation der QSOs der letzten 30 Tage
    struct CallAggregate {
        std::uint64_t qsoCount     = 0;
        double        totalSeconds = 0.0;
    };

    using CallAggMap = std::unordered_map<std::string, CallAggregate>;
    using TgAggMap   = std::unordered_map<int, double>;
    using TgCountMap = std::unordered_map<int, std::uint64_t>;

    bool computeQsoAggregatesLast30Days(CallAggMap&   perCall,
                                    TgAggMap&     perTg,
                                    TgCountMap&   perTgCount,
                                    FMQsoHeatmap& heatmapWeek) noexcept;

    std::vector<FMCallQsoCount>
    makeTop10ByQsoCount(const CallAggMap& perCall) const;

    std::vector<FMCallDuration>
    makeTop10ByDuration(const CallAggMap& perCall) const;

    std::vector<FMCallScore>
    makeTop10ByScore(const CallAggMap& perCall) const;

    std::vector<FMTgDuration>
    makeTop10TgByDuration(const TgAggMap& perTg,
                          const TgCountMap& perTgCount) const;


    // kleiner Helper für DATETIME → time_t
    static bool parseDateTimeToTimeT(const char* s, std::time_t& out) noexcept;

    bool writeStatisticsToDb(const std::vector<FMCallQsoCount>& topCallsByCount,
                             const std::vector<FMCallDuration>& topCallsByDuration,
                             const std::vector<FMCallScore>&   topCallsByScore,
                             const std::vector<FMTgDuration>&  topTgByDuration,
                             const FMQsoHeatmap&               heatmapWeek) noexcept;
};
