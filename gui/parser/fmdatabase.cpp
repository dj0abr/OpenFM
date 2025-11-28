// fmdatabase.cpp
#include "fmdatabase.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <tuple>
#include <cstdint>

FMDatabase::FMDatabase()
{
    if (!connect()) {
        std::fprintf(stderr, "[FMDB] initial connect() failed: %s\n", lastError_.c_str());
        std::exit(EXIT_FAILURE);
    }
}

FMDatabase::~FMDatabase()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool FMDatabase::connect() noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);

    lastError_.clear();

    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        lastError_ = "mysql_init failed";
        std::fprintf(stderr, "[FMDB] %s\n", lastError_.c_str());
        return false;
    }

    // Auto-Reconnect
    {
        bool rc = true;
        mysql_options(conn_, MYSQL_OPT_RECONNECT, &rc);
    }

    // Unix-Socket forcieren (optional)
    {
        unsigned int proto = MYSQL_PROTOCOL_SOCKET;
        mysql_options(conn_, MYSQL_OPT_PROTOCOL, &proto);
    }

    if (!mysql_real_connect(conn_,
                            nullptr,                   // host (nullptr = lokal)
                            dbUser_.c_str(),
                            dbPass_.c_str(),
                            dbName_.c_str(),
                            dbPort_,
                            dbUnixSocket_.c_str(),
                            0)) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] mysql_real_connect failed: %s\n", lastError_.c_str());
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }

    if (!ensureSchema()) {
        std::fprintf(stderr, "[FMDB] ensureSchema failed: %s\n", lastError_.c_str());
        return false;
    }

    return true;
}

bool FMDatabase::ensureSchema() noexcept
{
    if (!conn_) {
        lastError_ = "ensureSchema without connection";
        return false;
    }

    // fmlastheard: Historie, per Zeitfenster (z.B. 50 Tage) begrenzt
    static const char* q1 =
        "CREATE TABLE IF NOT EXISTS fmlastheard ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  event_time DATETIME NOT NULL,"
        "  talk       VARCHAR(8)  NOT NULL,"       // 'start' / 'stop'
        "  callsign   VARCHAR(32) NOT NULL,"
        "  tg         INT         NOT NULL,"
        "  server     VARCHAR(8)  NOT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_event_time (event_time),"
        "  INDEX idx_callsign   (callsign),"
        "  INDEX idx_tg         (tg)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    if (mysql_query(conn_, q1) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] create fmlastheard failed: %s\n", lastError_.c_str());
        return false;
    }

    // fmstatus: nur aktive Stationen (start -> eintragen, stop/Timeout -> löschen)
    static const char* q2 =
        "CREATE TABLE IF NOT EXISTS fmstatus ("
        "  callsign   VARCHAR(32) NOT NULL PRIMARY KEY,"
        "  event_time DATETIME    NOT NULL,"         // Zeitpunkt des letzten start-Events
        "  tg         INT         NOT NULL,"
        "  server     VARCHAR(8)  NOT NULL,"
        "  last_update TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "  INDEX idx_event_time (event_time),"
        "  INDEX idx_tg         (tg)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    if (mysql_query(conn_, q2) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] create fmstatus failed: %s\n", lastError_.c_str());
        return false;
    }

    // nodes
    static const char* q3 = R"SQL(
        CREATE TABLE IF NOT EXISTS nodes (
          callsign  VARCHAR(32) NOT NULL PRIMARY KEY,
          location  VARCHAR(255) NULL,
          locator   VARCHAR(16)  NULL,
          lat       DOUBLE       NULL,
          lon       DOUBLE       NULL,
          rx_freq   VARCHAR(32)  NULL,
          tx_freq   VARCHAR(32)  NULL,
          updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )SQL";

    if (mysql_query(conn_, q3) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] create nodes failed: %s\n", lastError_.c_str());
        return false;
    }

    // config-Tabelle (immer genau eine Zeile, id=1)
    static const char* q4 = R"SQL(
        CREATE TABLE IF NOT EXISTS config (
        id           TINYINT UNSIGNED NOT NULL PRIMARY KEY,
        callsign     VARCHAR(32)   NOT NULL,
        dns_domain   VARCHAR(255)  NOT NULL,
        default_tg   INT           NOT NULL,
        monitor_tgs  TEXT          NOT NULL,

        Location     VARCHAR(255)  NULL,
        Locator      VARCHAR(64)   NULL,
        SysOp        VARCHAR(255)  NULL,
        LAT          VARCHAR(64)   NULL,
        LON          VARCHAR(64)   NULL,
        TXFREQ       VARCHAR(64)   NULL,
        RXFREQ       VARCHAR(64)   NULL,
        Website      VARCHAR(255)  NULL,
        nodeLocation VARCHAR(255)  NULL,
        CTCSS        VARCHAR(64)   NULL,
        setup_password VARCHAR(255) NULL,
        reboot_requested TINYINT(1)   NOT NULL DEFAULT 0,
        updated_at   TIMESTAMP     NOT NULL
                    DEFAULT CURRENT_TIMESTAMP
                    ON UPDATE CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )SQL";


    if (mysql_query(conn_, q4) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] create config failed: %s\n", lastError_.c_str());
        return false;
    }

    // fmstats: aggregierte Statistiken für GUI
    static const char* q5 = R"SQL(
        CREATE TABLE IF NOT EXISTS fmstats (
          id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
          metric        VARCHAR(32) NOT NULL,   -- z.B. 'top_calls_qso', 'heatmap_week'
          rank          TINYINT UNSIGNED NULL,  -- 1..10 für Top-Listen, sonst NULL
          callsign      VARCHAR(32) NULL,
          tg            INT NULL,
          weekday       TINYINT UNSIGNED NULL,  -- 0=Mo .. 6=So (für heatmap)
          hour          TINYINT UNSIGNED NULL,  -- 0..23 (für heatmap)
          qso_count     BIGINT UNSIGNED NULL,
          total_seconds DOUBLE NULL,
          score         DOUBLE NULL,
          metric_value  DOUBLE NULL,            -- generischer Wert (z.B. qso_count, Dauer, Score)
          updated_at    TIMESTAMP NOT NULL
                        DEFAULT CURRENT_TIMESTAMP
                        ON UPDATE CURRENT_TIMESTAMP,
          INDEX idx_metric (metric),
          INDEX idx_metric_rank (metric, rank),
          INDEX idx_metric_wh (metric, weekday, hour)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )SQL";

    if (mysql_query(conn_, q5) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] create fmstats failed: %s\n", lastError_.c_str());
        return false;
    }

    return true;
}

bool FMDatabase::ensureConn() noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (!conn_) {
        return connect();
    }

    if (mysql_ping(conn_) == 0) return true;

    std::fprintf(stderr, "[FMDB] ping failed: %s -> reconnect\n", mysql_error(conn_));
    return connect();
}

std::string FMDatabase::escape(const std::string& in) noexcept
{
    if (!conn_) return std::string();

    std::string out;
    out.resize(in.size() * 2 + 1);
    unsigned long n = mysql_real_escape_string(conn_,
                                               &out[0],
                                               in.c_str(),
                                               static_cast<unsigned long>(in.size()));
    out.resize(n);
    return out;
}

// timeStr: "HH:MM:SS" oder "YYYY-MM-DD HH:MM:SS"
std::string FMDatabase::makeDateTime(const std::string& timeStr) noexcept
{
    if (timeStr.size() > 8 && timeStr.find(' ') != std::string::npos) {
        return timeStr;  // schon ein volles Datum
    }

    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d") << " " << timeStr;
    return oss.str();
}

bool FMDatabase::pruneIfNeeded() noexcept
{
    if (!conn_) return false;

    // alles löschen, was älter als 50 Tage ist
    static const char* q =
        "DELETE FROM fmlastheard "
        "WHERE event_time < (NOW() - INTERVAL 365 DAY)";

    if (mysql_query(conn_, q) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] pruneIfNeeded (365 days) failed: %s\n",
                     lastError_.c_str());
        return false;
    }

    return true;
}

// fmstatus pflegen: start -> eintragen/aktualisieren, stop -> löschen
bool FMDatabase::updateStatus(const std::string& dt,
                              const std::string& talk,
                              const std::string& call,
                              int tg,
                              const std::string& server) noexcept
{
    if (!conn_) return false;

    // nur start/stop relevant
    if (talk == "start") {
        std::string callE = escape(call);
        std::string srvE  = escape(server);
        std::string dtE   = escape(dt);

        // REPLACE INTO -> callsign ist PRIMARY KEY, also immer max. 1 Zeile pro Callsign
        std::ostringstream oss;
        oss << "REPLACE INTO fmstatus (callsign, event_time, tg, server) VALUES ("
            << "'" << callE << "',"
            << "'" << dtE   << "',"
            <<      tg      << ","
            << "'" << srvE  << "')";
        if (mysql_query(conn_, oss.str().c_str()) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] updateStatus REPLACE failed: %s\n", lastError_.c_str());
            return false;
        }
    } else if (talk == "stop") {
        std::string callE = escape(call);
        std::string q = "DELETE FROM fmstatus WHERE callsign='" + callE + "'";
        if (mysql_query(conn_, q.c_str()) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] updateStatus DELETE failed: %s\n", lastError_.c_str());
            return false;
        }
    }

    return true;
}

// alles löschen, was seit > 3 Minuten nicht aktualisiert wurde
bool FMDatabase::cleanupStatus() noexcept
{
    if (!conn_) return false;

    static const char* q =
        "DELETE FROM fmstatus "
        "WHERE last_update < (NOW() - INTERVAL 3 MINUTE)";

    if (mysql_query(conn_, q) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] cleanupStatus failed: %s\n", lastError_.c_str());
        return false;
    }
    return true;
}

bool FMDatabase::insertEvent(const std::string& timeStr,
                             const std::string& talk,
                             const std::string& call,
                             const std::string& tg,
                             const std::string& server) noexcept
{
    if (!ensureConn()) {
        std::fprintf(stderr, "[FMDB] insertEvent: no connection: %s\n", lastError_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);

    std::string dt     = makeDateTime(timeStr);
    std::string talkE  = escape(talk);
    std::string callE  = escape(call);
    std::string srvE   = escape(server);

    int tgInt = 0;
    try {
        tgInt = std::stoi(tg);
    } catch (...) {
        tgInt = 0;
    }

    //
    // doppelte "stop"-Events für ein Callsign verhindern
    //
    if (talk == "stop") {
        std::string qLast =
            "SELECT talk FROM fmlastheard "
            "WHERE callsign='" + callE + "' "
            "ORDER BY id DESC "
            "LIMIT 1";

        if (mysql_query(conn_, qLast.c_str()) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] insertEvent: query last talk failed: %s\n",
                         lastError_.c_str());
            // im Zweifel lieber trotzdem weitermachen und den Stop loggen
        } else {
            MYSQL_RES* res = mysql_store_result(conn_);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    std::string lastTalk = row[0];
                    if (lastTalk == "stop") {
                        // Zweiter stop hintereinander -> ignorieren
                        mysql_free_result(res);
                        // fmstatus ist ohnehin schon "nicht aktiv", also updateStatus hier NICHT aufrufen
                        return true;
                    }
                }
                mysql_free_result(res);
            }
        }
    }

    //
    // normaler INSERT, wenn wir hier sind
    //
    if (callE.rfind("TG", 0) == std::string::npos) {
        // beginnt NICHT mit "TG"
        // verhindert dass die lästigen TG2328 die Liste verstopfen
        std::ostringstream oss;
        oss << "INSERT INTO fmlastheard (event_time, talk, callsign, tg, server) VALUES ("
            << "'" << escape(dt)   << "',"
            << "'" << talkE        << "',"
            << "'" << callE        << "',"
            <<      tgInt          << ","
            << "'" << srvE         << "')";

        if (mysql_query(conn_, oss.str().c_str()) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] INSERT fmlastheard failed: %s\n", lastError_.c_str());
            return false;
        }
    }

    // fmstatus für "start"/"stop" pflegen
    if (!updateStatus(dt, talk, call, tgInt, server)) {
        std::fprintf(stderr, "[FMDB] updateStatus failed: %s\n", lastError_.c_str());
        // kein harter Fehler für insertEvent
    }

    // fmlastheard begrenzen
    if (!pruneIfNeeded()) {
        std::fprintf(stderr, "[FMDB] pruneIfNeeded failed: %s\n", lastError_.c_str());
    }

    // fmstatus Timeout (alles, was > 3min alt ist, entfernen)
    if (!cleanupStatus()) {
        std::fprintf(stderr, "[FMDB] cleanupStatus failed: %s\n", lastError_.c_str());
    }

    return true;
}

bool FMDatabase::upsertNode(const std::string& callsign,
                            const std::string& location,
                            const std::string& locator,
                            double lat,
                            double lon,
                            const std::string& rx_freq,
                            const std::string& tx_freq) noexcept
{
    if (!ensureConn()) {
        std::fprintf(stderr, "[FMDB] upsertNode: no connection: %s\n", lastError_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);

    std::string callE     = escape(callsign);
    std::string locE      = escape(location);
    std::string locatorE  = escape(locator);
    std::string rxE       = escape(rx_freq);
    std::string txE       = escape(tx_freq);

    // lat/lon -> NULL falls NaN (z.B. wenn nicht gesetzt)
    bool latValid = !std::isnan(lat);
    bool lonValid = !std::isnan(lon);

    std::ostringstream oss;
    oss << "REPLACE INTO nodes (callsign, location, locator, lat, lon, rx_freq, tx_freq) VALUES ("
        << "'" << callE   << "',"
        << "'" << locE    << "',"
        << "'" << locatorE<< "',";

    if (latValid)
        oss << lat;
    else
        oss << "NULL";

    oss << ",";

    if (lonValid)
        oss << lon;
    else
        oss << "NULL";

    oss << ","
        << "'" << rxE << "',"
        << "'" << txE << "')";
    
    if (mysql_query(conn_, oss.str().c_str()) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] upsertNode REPLACE failed: %s\n", lastError_.c_str());
        return false;
    }

    return true;
}

bool FMDatabase::upsertConfig(const std::string& callsign,
                              const std::string& dnsDomain,
                              int defaultTg,
                              const std::string& monitorTgs) noexcept
{
    if (!ensureConn()) {
        std::fprintf(stderr, "[FMDB] upsertConfig: no connection: %s\n", lastError_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);

    // Prüfen, ob es bereits einen Eintrag mit id=1 gibt
    {
        const char* q = "SELECT COUNT(*) FROM config WHERE id=1";

        if (mysql_query(conn_, q) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] upsertConfig COUNT failed: %s\n", lastError_.c_str());
            return false;
        }

        MYSQL_RES* res = mysql_store_result(conn_);
        if (!res) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] upsertConfig store_result failed: %s\n", lastError_.c_str());
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        unsigned long long cnt = (row && row[0]) ? std::strtoull(row[0], nullptr, 10) : 0ULL;
        mysql_free_result(res);

        if (cnt > 0) {
            // config existiert schon -> NICHTS ändern!
            // (GUI oder anderes Tool darf das pflegen, wir fassen es nicht mehr an)
            return true;
        }
    }

    // Wenn wir hier sind, gibt es noch keinen Eintrag mit id=1 -> Defaultwerte anlegen
    std::string callE  = escape(callsign);
    std::string dnsE   = escape(dnsDomain);
    std::string monsE  = escape(monitorTgs);

    std::ostringstream oss;
    oss << "INSERT INTO config (id, callsign, dns_domain, default_tg, monitor_tgs) VALUES ("
        << "1,"
        << "'" << callE << "',"
        << "'" << dnsE  << "',"
        <<      defaultTg << ","
        << "'" << monsE << "')";

    if (mysql_query(conn_, oss.str().c_str()) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] upsertConfig INSERT failed: %s\n", lastError_.c_str());
        return false;
    }

    return true;
}

bool FMDatabase::getConfig(ConfigRow& out) noexcept
{
    if (!ensureConn()) {
        std::fprintf(stderr, "[FMDB] getConfig: no connection: %s\n", lastError_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);

    // Nur die Zeile mit id=1
    const char* q =
        "SELECT "
        "  id, callsign, dns_domain, default_tg, monitor_tgs, "
        "  Location, Locator, SysOp, LAT, LON, TXFREQ, RXFREQ, "
        "  Website, nodeLocation, CTCSS, reboot_requested,"
        "  DATE_FORMAT(updated_at,'%Y-%m-%d %H:%i:%s') AS updated_at "
        "FROM config "
        "WHERE id=1 "
        "LIMIT 1";

    if (mysql_query(conn_, q) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] getConfig query failed: %s\n", lastError_.c_str());
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] getConfig store_result failed: %s\n", lastError_.c_str());
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        // keine config-Zeile vorhanden
        mysql_free_result(res);
        lastError_ = "getConfig: no row with id=1";
        return false;
    }

    auto getInt = [](const char* s, int defVal = 0) {
        if (!s) return defVal;
        try {
            return std::stoi(s);
        } catch (...) {
            return defVal;
        }
    };

    out.id          = getInt(row[0], 0);
    out.callsign    = row[1]  ? row[1]  : "";
    out.dnsDomain   = row[2]  ? row[2]  : "";
    out.defaultTg   = getInt(row[3], 0);
    out.monitorTgs  = row[4]  ? row[4]  : "";

    out.Location    = row[5]  ? row[5]  : "";
    out.Locator     = row[6]  ? row[6]  : "";
    out.SysOp       = row[7]  ? row[7]  : "";
    out.LAT         = row[8]  ? row[8]  : "";
    out.LON         = row[9]  ? row[9]  : "";
    out.TXFREQ      = row[10] ? row[10] : "";
    out.RXFREQ      = row[11] ? row[11] : "";
    out.Website     = row[12] ? row[12] : "";
    out.nodeLocation= row[13] ? row[13] : "";
    out.CTCSS       = row[14] ? row[14] : "";
    const int rebootRequestedInt = getInt(row[15], 0);
    out.rebootRequested          = (rebootRequestedInt != 0);
    out.updatedAt   = row[16] ? row[16] : "";

    mysql_free_result(res);

    // Wenn ein Reboot angefordert ist: Flag sofort wieder löschen,
    // damit wir es nur "einmal" sehen.
    if (rebootRequestedInt != 0) {
        const char* upd = "UPDATE config SET reboot_requested = 0 WHERE id = 1";
        if (mysql_query(conn_, upd) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] getConfig: clear reboot_requested failed: %s\n",
                         lastError_.c_str());
            // hier nicht false zurückgeben – Config ist trotzdem lesbar
        }
    }
    
    return true;
}

// ---------------- Statistik ----------------
bool FMDatabase::parseDateTimeToTimeT(const char* s, std::time_t& out) noexcept
{
    if (!s) return false;

    std::tm tm{};
    tm.tm_isdst = -1;

    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return false;
    }

    std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) {
        return false;
    }

    out = t;
    return true;
}

bool FMDatabase::computeQsoAggregatesLast30Days(CallAggMap&   perCall,
                                                TgAggMap&     perTg,
                                                TgCountMap&   perTgCount,
                                                FMQsoHeatmap& heatmapWeek) noexcept
{
    perCall.clear();
    perTg.clear();
    perTgCount.clear();

    // Heatmap initialisieren
    for (auto& day : heatmapWeek) {
        day.fill(0);
    }

    if (!ensureConn()) {
        std::fprintf(stderr, "[FMDB] computeQsoAggregatesLast30Days: no connection: %s\n",
                     lastError_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);

    std::ostringstream oss;
    oss << "SELECT "
           "DATE_FORMAT(event_time,'%Y-%m-%d %H:%i:%s') AS et, "
           "talk, callsign, tg "
           "FROM fmlastheard "
           "WHERE event_time >= (NOW() - INTERVAL 30 DAY) "
           "ORDER BY callsign, event_time, id";

    if (mysql_query(conn_, oss.str().c_str()) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] computeQsoAggregatesLast30Days query failed: %s\n",
                     lastError_.c_str());
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] computeQsoAggregatesLast30Days store_result failed: %s\n",
                     lastError_.c_str());
        return false;
    }

    const std::time_t now          = std::time(nullptr);
    const std::time_t sevenDaysAgo = now - 7 * 24 * 60 * 60;

    std::string currentCall;
    bool        hasOpenStart = false;
    std::time_t currentStart = 0;
    int         currentTg    = 0;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        const char* etStr = row[0];
        const char* talk  = row[1];
        const char* call  = row[2];
        const char* tgStr = row[3];

        if (!etStr || !talk || !call || !tgStr) {
            continue;
        }

        std::time_t t{};
        if (!parseDateTimeToTimeT(etStr, t)) {
            continue;
        }

        int tg = std::atoi(tgStr);

        std::string callStr(call);
        std::string talkStr(talk);

        // bei neuem Callsign State zurücksetzen
        if (callStr != currentCall) {
            currentCall  = callStr;
            hasOpenStart = false;
        }

        if (talkStr == "start") {
            // mehrfaches start -> letztes gewinnt, alte werden ignoriert
            currentStart  = t;
            currentTg     = tg;
            hasOpenStart  = true;
        } else if (talkStr == "stop") {
            if (!hasOpenStart) {
                // stop ohne passendes start -> ignorieren
                continue;
            }

            double dt = std::difftime(t, currentStart);
            // QSOs < 5s ignorieren
            if (dt < 5.0) {
                hasOpenStart = false;
                continue;
            }

            // Aggregation pro Callsign
            auto& agg = perCall[currentCall];
            agg.qsoCount     += 1;
            agg.totalSeconds += dt;

            // Aggregation pro TG
            perTg[currentTg] += dt;
            perTgCount[currentTg] += 1;

            // Heatmap (nur QSOs mit Startzeit in letzter Woche)
            if (currentStart >= sevenDaysAgo) {
                std::tm tm{};
                localtime_r(&currentStart, &tm);

                int hour = tm.tm_hour;          // 0..23
                int wday = tm.tm_wday;          // 0=Sonntag..6=Samstag

                // auf 0=Montag..6=Sonntag umbiegen
                int weekdayIndex = (wday == 0) ? 6 : (wday - 1);

                if (weekdayIndex >= 0 && weekdayIndex < 7 &&
                    hour >= 0 && hour < 24) {
                    heatmapWeek[weekdayIndex][hour] += 1;
                }
            }

            hasOpenStart = false;
        }
    }

    mysql_free_result(res);
    return true;
}

std::vector<FMCallQsoCount>
FMDatabase::makeTop10ByQsoCount(const CallAggMap& perCall) const
{
    std::vector<FMCallQsoCount> result;
    result.reserve(perCall.size());

    for (const auto& kv : perCall) {
        FMCallQsoCount e;
        e.callsign = kv.first;
        e.qsoCount = kv.second.qsoCount;
        result.push_back(std::move(e));
    }

    std::sort(result.begin(), result.end(),
              [](const FMCallQsoCount& a, const FMCallQsoCount& b) {
                  return a.qsoCount > b.qsoCount;
              });

    if (result.size() > 10) result.resize(10);
    return result;
}

std::vector<FMCallDuration>
FMDatabase::makeTop10ByDuration(const CallAggMap& perCall) const
{
    std::vector<FMCallDuration> result;
    result.reserve(perCall.size());

    for (const auto& kv : perCall) {
        FMCallDuration e;
        e.callsign     = kv.first;
        e.totalSeconds = kv.second.totalSeconds;
        result.push_back(std::move(e));
    }

    std::sort(result.begin(), result.end(),
              [](const FMCallDuration& a, const FMCallDuration& b) {
                  return a.totalSeconds > b.totalSeconds;
              });

    if (result.size() > 10) result.resize(10);
    return result;
}

std::vector<FMCallScore>
FMDatabase::makeTop10ByScore(const CallAggMap& perCall) const
{
    std::vector<FMCallScore> result;
    result.reserve(perCall.size());

    for (const auto& kv : perCall) {
        FMCallScore e;
        e.callsign     = kv.first;
        e.qsoCount     = kv.second.qsoCount;
        e.totalSeconds = kv.second.totalSeconds;
        e.score        = (e.qsoCount * e.totalSeconds) / 100.0;
        result.push_back(std::move(e));
    }

    std::sort(result.begin(), result.end(),
              [](const FMCallScore& a, const FMCallScore& b) {
                  return a.score > b.score;
              });

    if (result.size() > 10) result.resize(10);
    return result;
}

std::vector<FMTgDuration>
FMDatabase::makeTop10TgByDuration(const TgAggMap& perTg,
                                  const TgCountMap& perTgCount) const
{
    std::vector<FMTgDuration> result;
    result.reserve(perTg.size());

    for (const auto& kv : perTg) {
        int tg          = kv.first;
        double totalSec = kv.second;

        std::uint64_t cnt = 0;
        if (auto it = perTgCount.find(tg); it != perTgCount.end()) {
            cnt = it->second;
        }

        FMTgDuration e;
        e.tg           = tg;
        e.totalSeconds = totalSec;
        e.qsoCount     = cnt;    // <--- hier wird qsoCount gesetzt
        result.push_back(std::move(e));
    }

    std::sort(result.begin(), result.end(),
              [](const FMTgDuration& a, const FMTgDuration& b) {
                  return a.totalSeconds > b.totalSeconds;
              });

    if (result.size() > 10) result.resize(10);
    return result;
}

void FMDatabase::statistics() noexcept
{
    // Nur alle 10 Minuten neu berechnen
    using Clock = std::chrono::steady_clock;
    static Clock::time_point lastRun;
    static bool hasLastRun = false;

    Clock::time_point now = Clock::now();
    if (hasLastRun) {
        auto diff = std::chrono::duration_cast<std::chrono::minutes>(now - lastRun);
        if (diff < std::chrono::minutes(10)) {
            // Letzte Berechnung ist noch keine 10 Minuten her -> ignorieren
            return;
        }
    }

    hasLastRun = true;
    lastRun = now;

    CallAggMap   perCall;
    TgAggMap     perTg;
    TgCountMap   perTgCount;
    FMQsoHeatmap heatmapWeek;

    if (!computeQsoAggregatesLast30Days(perCall, perTg, perTgCount, heatmapWeek)) {
        std::fprintf(stderr, "[FMDB] statistics: computeQsoAggregatesLast30Days failed: %s\n",
                     lastError_.c_str());
        return;
    }

    // 1–4: Top-Listen bilden
    auto topCallsByCount    = makeTop10ByQsoCount(perCall);
    auto topCallsByDuration = makeTop10ByDuration(perCall);
    auto topCallsByScore    = makeTop10ByScore(perCall);
    auto topTgByDuration    = makeTop10TgByDuration(perTg, perTgCount);

    // Ergebnisse in fmstats schreiben
    if (!writeStatisticsToDb(topCallsByCount,
                             topCallsByDuration,
                             topCallsByScore,
                             topTgByDuration,
                             heatmapWeek)) {
        std::fprintf(stderr, "[FMDB] statistics: writeStatisticsToDb failed: %s\n",
                     lastError_.c_str());
    }
}

bool FMDatabase::writeStatisticsToDb(const std::vector<FMCallQsoCount>& topCallsByCount,
                                     const std::vector<FMCallDuration>& topCallsByDuration,
                                     const std::vector<FMCallScore>&   topCallsByScore,
                                     const std::vector<FMTgDuration>&  topTgByDuration,
                                     const FMQsoHeatmap&               heatmapWeek) noexcept
{
    if (!ensureConn()) {
        std::fprintf(stderr, "[FMDB] writeStatisticsToDb: no connection: %s\n",
                     lastError_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);

    auto execSimple = [&](const char* q) -> bool {
        if (mysql_query(conn_, q) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] writeStatisticsToDb query failed: %s\n",
                         lastError_.c_str());
            return false;
        }
        return true;
    };

    if (!execSimple("START TRANSACTION")) {
        return false;
    }

    if (!execSimple("DELETE FROM fmstats")) {
        execSimple("ROLLBACK");
        return false;
    }

    auto insertRow =
        [&](const std::string& metric,
            int                rank,
            const std::string* callsign,
            const int*         tg,
            const int*         weekday,
            const int*         hour,
            const std::uint64_t* qsoCount,
            const double*      totalSeconds,
            const double*      score,
            const double*      value) -> bool
    {
        std::ostringstream oss;
        oss << "INSERT INTO fmstats "
               "(metric, rank, callsign, tg, weekday, hour, "
               " qso_count, total_seconds, score, metric_value) VALUES (";

        // metric
        oss << "'" << escape(metric) << "',";

        // rank
        if (rank >= 0) oss << rank;
        else           oss << "NULL";
        oss << ",";

        // callsign
        if (callsign) oss << "'" << escape(*callsign) << "'";
        else          oss << "NULL";
        oss << ",";

        // tg
        if (tg) oss << *tg;
        else    oss << "NULL";
        oss << ",";

        // weekday
        if (weekday) oss << *weekday;
        else         oss << "NULL";
        oss << ",";

        // hour
        if (hour) oss << *hour;
        else      oss << "NULL";
        oss << ",";

        // qso_count
        if (qsoCount) oss << static_cast<unsigned long long>(*qsoCount);
        else          oss << "NULL";
        oss << ",";

        // total_seconds
        if (totalSeconds) oss << *totalSeconds;
        else              oss << "NULL";
        oss << ",";

        // score
        if (score) oss << *score;
        else       oss << "NULL";
        oss << ",";

        // metric_value
        if (value) oss << *value;
        else       oss << "NULL";

        oss << ")";

        if (mysql_query(conn_, oss.str().c_str()) != 0) {
            lastError_ = mysql_error(conn_);
            std::fprintf(stderr, "[FMDB] writeStatisticsToDb INSERT failed: %s\n",
                         lastError_.c_str());
            return false;
        }
        return true;
    };

    // 1) Top 10 Callsigns nach QSO-Anzahl
    for (std::size_t i = 0; i < topCallsByCount.size(); ++i) {
        const auto& e = topCallsByCount[i];
        int rank      = static_cast<int>(i + 1);
        std::uint64_t qso = e.qsoCount;
        double value  = static_cast<double>(qso);

        if (!insertRow("top_calls_qso",
                       rank,
                       &e.callsign,
                       nullptr,
                       nullptr,
                       nullptr,
                       &qso,
                       nullptr,
                       nullptr,
                       &value)) {
            execSimple("ROLLBACK");
            return false;
        }
    }

    // 2) Top 10 Callsigns nach Gesamtdauer (Sekunden)
    for (std::size_t i = 0; i < topCallsByDuration.size(); ++i) {
        const auto& e = topCallsByDuration[i];
        int rank      = static_cast<int>(i + 1);
        double total  = e.totalSeconds;
        double value  = total;

        if (!insertRow("top_calls_duration",
                       rank,
                       &e.callsign,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
                       &total,
                       nullptr,
                       &value)) {
            execSimple("ROLLBACK");
            return false;
        }
    }

    // 3) Top 10 Callsigns nach Score
    for (std::size_t i = 0; i < topCallsByScore.size(); ++i) {
        const auto& e = topCallsByScore[i];
        int rank      = static_cast<int>(i + 1);
        std::uint64_t qso = e.qsoCount;
        double total  = e.totalSeconds;
        double sc     = e.score;
        double value  = sc;

        if (!insertRow("top_calls_score",
                       rank,
                       &e.callsign,
                       nullptr,
                       nullptr,
                       nullptr,
                       &qso,
                       &total,
                       &sc,
                       &value)) {
            execSimple("ROLLBACK");
            return false;
        }
    }

    // 4) Top 10 TG nach Dauer
    for (std::size_t i = 0; i < topTgByDuration.size(); ++i) {
        const auto& e = topTgByDuration[i];
        int rank      = static_cast<int>(i + 1);
        int tg        = e.tg;
        double total  = e.totalSeconds;
        double value  = total;

        // NEU: QSO-Anzahl für diese TG
        std::uint64_t qso = e.qsoCount;

        if (!insertRow("top_tg_duration",
                    rank,          // rank
                    nullptr,       // callsign
                    &tg,           // tg
                    nullptr,       // weekday
                    nullptr,       // hour
                    &qso,          // qso_count
                    &total,        // total_seconds
                    nullptr,       // score
                    &value)) {     // metric_value (hier = total)
            execSimple("ROLLBACK");
            return false;
        }
    }

    // 5) Heatmap 24 x 7 (Anzahl QSOs pro Stunde, letzte Woche)
    // weekday: 0=Mo..6=So, hour: 0..23
    for (int wd = 0; wd < 7; ++wd) {
        for (int h = 0; h < 24; ++h) {
            std::uint64_t cnt = heatmapWeek[wd][h];
            double value      = static_cast<double>(cnt);
            int weekday       = wd;
            int hour          = h;

            if (!insertRow("heatmap_week",
                           -1,         // kein Ranking
                           nullptr,
                           nullptr,
                           &weekday,
                           &hour,
                           &cnt,
                           nullptr,
                           nullptr,
                           &value)) {
                execSimple("ROLLBACK");
                return false;
            }
        }
    }

    if (!execSimple("COMMIT")) {
        execSimple("ROLLBACK");
        return false;
    }

    return true;
}
