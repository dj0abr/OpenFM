// fmdatabase.cpp
#include "fmdatabase.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <sstream>
#include <iomanip>

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

    // fmlastheard: komplette Historie (limitiert über MAX_ROWS_)
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

    if (mysql_query(conn_, "SELECT COUNT(*) FROM fmlastheard") != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] COUNT(*) failed: %s\n", lastError_.c_str());
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] store_result failed: %s\n", lastError_.c_str());
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    unsigned long long cnt = (row && row[0]) ? std::strtoull(row[0], nullptr, 10) : 0ULL;
    mysql_free_result(res);

    if (cnt <= MAX_ROWS_) {
        return true;
    }

    unsigned long long toDelete = cnt - MAX_ROWS_;

    std::ostringstream oss;
    oss << "DELETE FROM fmlastheard ORDER BY event_time ASC LIMIT " << toDelete;

    if (mysql_query(conn_, oss.str().c_str()) != 0) {
        lastError_ = mysql_error(conn_);
        std::fprintf(stderr, "[FMDB] prune DELETE failed: %s\n", lastError_.c_str());
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
        "  Website, nodeLocation, CTCSS, "
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
    out.updatedAt   = row[15] ? row[15] : "";

    mysql_free_result(res);
    return true;
}
