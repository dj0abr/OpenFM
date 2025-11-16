// fmdatabase.h
#pragma once

#include <string>
#include <mysql/mysql.h>
#include <mutex>

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

    static constexpr unsigned long MAX_ROWS_ = 5000;      // Limit fmlastheard
};
