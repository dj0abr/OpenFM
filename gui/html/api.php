<?php
declare(strict_types=1);

/**
 * Kleine JSON-API für MMDVM-Daten.
 * - Gibt je nach $_GET['q'] unterschiedliche Auswertungen zurück.
 * - Antwort ist immer JSON (UTF-8).
 */
header('Content-Type: application/json; charset=utf-8');

/** 
 * Ermittelt den Ländercode (ISO-3166 Alpha-2) anhand eines Rufzeichen-Präfixes.
 * Hinweis:
 * - Mapping ist bewusst statisch und unvollständig/uneinheitlich (DX real life).
 * - Reihenfolge ist wichtig: längere Präfixe müssen davor stehen (z. B. "OH0" vor "OH").
 * - Keine Logikänderung: Mapping bleibt exakt wie vorgefunden, inkl. Duplikate.
 *
 * @param mixed $call Rufzeichen (beliebiger Typ, wird intern zu String gecastet)
 * @return string|null ISO-3166-Code oder null, wenn kein Match
 */
function prefix_to_country($call) {
  static $map = null;

  if ($map === null) {
    $map = [
      // --- Europa ---
      'DL' => 'DE', 'DA' => 'DE', 'DB' => 'DE', 'DC' => 'DE', 'DD' => 'DE', 'DE' => 'DE', 'DF' => 'DE', 'DG' => 'DE', 'DH' => 'DE', 'DJ' => 'DE', 'DK' => 'DE', 'DM' => 'DE', 'DN' => 'DE', 'DO' => 'DE',
      'OE' => 'AT', 'OK' => 'CZ', 'OM' => 'SK', 'HA' => 'HU', 'SP' => 'PL', 'S5' => 'SI', '9A' => 'HR', 'YU' => 'RS', 'YT' => 'RS', 'YL' => 'LV', 'ES' => 'EE', 'LY' => 'LT',
      'OH' => 'FI', 'SM' => 'SE', 'LA' => 'NO', 'OZ' => 'DK', 'TF' => 'IS', 'EI' => 'IE', 'PA' => 'NL', 'ON' => 'BE', 'LX' => 'LU', 'HB9' => 'CH', 'HB3' => 'CH', 'HB0' => 'LI',
      'F' => 'FR', 'TM' => 'FR', 'TK' => 'FR', 'EA' => 'ES', 'EB' => 'ES', 'EC' => 'ES', 'ED' => 'ES', 'EE' => 'ES', 'EF' => 'ES', 'EG' => 'ES', 'EH' => 'ES', 'CT' => 'PT', 'CU' => 'PT',
      'I' => 'IT', 'IS' => 'IT', 'IZ' => 'IT', 'IN' => 'IT', 'IW' => 'IT', 'IV' => 'IT', 'SV' => 'GR', 'SW' => 'GR', 'SX' => 'GR', 'SY' => 'GR',
      'YO' => 'RO', 'YR' => 'RO', 'LZ' => 'BG', 'E7' => 'BA', 'Z3' => 'MK', '9H' => 'MT', 'ER' => 'MD', 'UA2' => 'RU', 'R2' => 'RU', 'R3' => 'RU', 'UA3' => 'RU', 'UA1' => 'RU', 'R1' => 'RU',
      'UA' => 'RU', 'UB' => 'RU', 'UC' => 'RU', 'UD' => 'RU', 'UE' => 'RU', 'UF' => 'RU', 'UG' => 'RU', 'UH' => 'RU', 'UI' => 'RU',
      'US' => 'UA', 'UR' => 'UA', 'UT' => 'UA', 'UU' => 'UA', 'UV' => 'UA', 'UW' => 'UA', 'UX' => 'UA', 'UY' => 'UA', 'UZ' => 'UA',
      'LY' => 'LT', 'ES' => 'EE', 'OH0' => 'AX', 'OY' => 'FO', 'OX' => 'GL', 'TF' => 'IS',
      'CN' => 'MA', 'EA8' => 'ES', 'CT9' => 'PT', 'IS0' => 'IT',
      'TA' => 'TR', 'TC' => 'TR',

      // --- Vereinigtes Königreich ---
      'G' => 'GB', 'M' => 'GB', '2E' => 'GB', 'GM' => 'GB', 'GW' => 'GB', 'GI' => 'GB', 'GD' => 'GB', 'GU' => 'GB', 'GH' => 'GB', 'GT' => 'GB', 'MB' => 'GB', 'GB' => 'GB',

      // --- Skandinavien & Ostsee ---
      'LA' => 'NO', 'LB' => 'NO', 'LC' => 'NO', 'LD' => 'NO', 'LG' => 'NO', 'LH' => 'NO', 'LI' => 'NO', 'LN' => 'NO',
      'OH' => 'FI', 'OF' => 'FI', 'OG' => 'FI', 'OJ' => 'FI', 'OH0' => 'AX',
      'SM' => 'SE', '7S' => 'SE', 'SB' => 'SE', 'SI' => 'SE', 'SL' => 'SE',
      'OZ' => 'DK', 'OV' => 'DK', '5P' => 'DK', '5Q' => 'DK',

      // --- Nordamerika ---
      'K' => 'US', 'N' => 'US', 'W' => 'US', 'AA' => 'US', 'AB' => 'US', 'AC' => 'US', 'AD' => 'US', 'AE' => 'US', 'AF' => 'US', 'AG' => 'US', 'AI' => 'US', 'AJ' => 'US', 'AK' => 'US',
      'KL' => 'US', 'KH6' => 'US', 'WH6' => 'US', 'KH7' => 'US', 'KH8' => 'AS', 'KH9' => 'UM', 'KP4' => 'PR', 'KP2' => 'VI', 'NP4' => 'PR', 'WP4' => 'PR',
      'VE' => 'CA', 'VA' => 'CA', 'VY' => 'CA', 'VO' => 'CA', 'CY' => 'CA', 'CZ' => 'CA', 'CG' => 'CA',

      // --- Mittel- & Südamerika ---
      'HC' => 'EC', 'HD' => 'EC', 'OA' => 'PE', 'OB' => 'PE', 'TI' => 'CR', 'TE' => 'CR', 'TG' => 'GT', 'YN' => 'NI', 'YS' => 'SV', 'HP' => 'PA', 'HO' => 'PA', 'HH' => 'HT',
      'HI' => 'DO', 'CP' => 'BO', 'CE' => 'CL', 'CA' => 'CL', 'CB' => 'CL', 'CC' => 'CL', 'CD' => 'CL', '3G' => 'CL',
      'CX' => 'UY', 'LU' => 'AR', 'LW' => 'AR', 'LR' => 'AR', 'LS' => 'AR', 'LT' => 'AR', 'LV' => 'AR', 'PU' => 'BR', 'PY' => 'BR', 'PP' => 'BR', 'PQ' => 'BR', 'PR' => 'BR',
      'PZ' => 'SR', 'HC8' => 'EC', 'PJ2' => 'CW', 'PJ4' => 'BQ', 'PJ5' => 'BQ', 'PJ7' => 'BQ',

      // --- Karibik ---
      'J3' => 'GD', 'J7' => 'DM', 'J8' => 'VC', '9Y' => 'TT', '9Z' => 'TT', 'VP2E' => 'AI', 'VP2M' => 'MS', 'VP2V' => 'VG', 'VP5' => 'TC', 'VP6' => 'PN', 'VP9' => 'BM', 'ZF' => 'KY',
      'CM' => 'CU', 'CO' => 'CU', 'T4' => 'CU', 'C6' => 'BS', 'PJ' => 'BQ',

      // --- Afrika ---
      'ZS' => 'ZA', 'ZR' => 'ZA', 'ZU' => 'ZA', '5R' => 'MG', '5T' => 'MR', '5U' => 'NE', '5V' => 'TG', '5X' => 'UG', '5Z' => 'KE', '6O' => 'SO', '6V' => 'SN', '6W' => 'SN', '7O' => 'YE',
      '7P' => 'LS', '7Q' => 'MW', '7X' => 'DZ', '9G' => 'GH', '9J' => 'ZM', '9L' => 'SL', '9Q' => 'CD', '9U' => 'BI', '9X' => 'RW', 'D2' => 'AO', 'D4' => 'CV', 'D6' => 'KM',
      'EL' => 'LR', 'ET' => 'ET', 'S7' => 'SC', 'ST' => 'SD', 'SU' => 'EG', 'TJ' => 'CM', 'TN' => 'CG', 'TR' => 'GA', 'TT' => 'TD', 'TZ' => 'ML', 'V5' => 'NA', 'ZD7' => 'SH', 'ZD8' => 'SH', 'ZD9' => 'SH',

      // --- Naher Osten ---
      '4X' => 'IL', '4Z' => 'IL', '5B' => 'CY', 'C4' => 'CY', 'H2' => 'CY', 'E3' => 'ER', 'EK' => 'AM', 'EP' => 'IR', 'EQ' => 'IR', 'HZ' => 'SA', '7Z' => 'SA', '8Z' => 'SA', 'A4' => 'OM',
      'A6' => 'AE', 'A7' => 'QA', 'A9' => 'BH', 'AP' => 'PK', 'YA' => 'AF', 'T6' => 'AF', 'YK' => 'SY', 'YI' => 'IQ', '9K' => 'KW',

      // --- Asien ---
      'VU' => 'IN', 'VT' => 'IN', 'AT' => 'IN', '8T' => 'IN', '8Q' => 'MV', '9N' => 'NP', 'EY' => 'TJ', 'EX' => 'KG', 'EZ' => 'TM', 'HL' => 'KR', 'DS' => 'KR', 'DT' => 'KR',
      'JA' => 'JP', 'JE' => 'JP', 'JF' => 'JP', 'JG' => 'JP', 'JH' => 'JP', 'JI' => 'JP', 'JJ' => 'JP', 'JK' => 'JP', 'JL' => 'JP', 'JM' => 'JP', 'JN' => 'JP', 'JO' => 'JP', 'JR' => 'JP',
      'BV' => 'TW', 'BX' => 'TW', 'BY' => 'CN', 'BD' => 'CN', 'BG' => 'CN', 'BH' => 'CN', 'BL' => 'CN', 'BM' => 'CN', 'BN' => 'CN', 'BT' => 'CN',
      'HS' => 'TH', 'E2' => 'TH', '9M2' => 'MY', '9M6' => 'MY', '9M8' => 'MY', '9V' => 'SG', 'YB' => 'ID', 'YC' => 'ID','YE' => 'ID', 'PK' => 'ID', 'PL' => 'ID', 'PM' => 'ID', 'PN' => 'ID',
      '9M' => 'MY', '9V' => 'SG', '9W' => 'MY', 'VR' => 'HK', 'DU' => 'PH', 'DV' => 'PH', 'DW' => 'PH', 'DX' => 'PH', 'DY' => 'PH', 'DZ' => 'PH', 'VU' => 'IN',

      // --- Ozeanien ---
      'VK' => 'AU', 'AX' => 'AU', 'VI' => 'AU', 'ZL' => 'NZ', '3D2' => 'FJ', 'A3' => 'TO', 'E5' => 'CK', 'T30' => 'KI', 'T31' => 'KI', 'T32' => 'KI', 'T33' => 'KI',
      '5W' => 'WS', 'YJ' => 'VU', 'P2' => 'PG', 'C2' => 'NR', 'T2' => 'TV', 'ZK1' => 'CK', 'ZK3' => 'TK', 'A2' => 'BW', 'H40' => 'SB', 'H44' => 'SB', 'FK' => 'NC', 'FO' => 'PF', 'FW' => 'WF',

      // --- Antarktis & Gebiete ---
      'VP8' => 'FK', 'CE9' => 'AQ', 'RI1A' => 'AQ', 'DP1' => 'AQ', 'KC4' => 'AQ', 'LU1Z' => 'AQ', 'VK0' => 'AQ', 'ZL5' => 'AQ', 'ZS7' => 'AQ',

      // --- Sonderrufe ---
      'AM' => 'ES', 'AN' => 'ES', 'AO' => 'ES', 'EG' => 'ES', 'EH' => 'ES', 'EM' => 'UA', 'EN' => 'UA', 'EO' => 'UA'
    ];
  }

  // Eingabe aufbereiten
  $call = strtoupper(trim($call ?? ''));
  if ($call === '') {
    return null;
  }

  // Präfix-Match (greedy von links nach rechts)
  foreach ($map as $pfx => $cc) {
    if (str_starts_with($call, $pfx)) {
      return $cc;
    }
  }

  return null;
}

  function bm_api_get_raw(string $endpoint, string $token, int $timeout=8, bool $allow_non2xx=false) {
    $ch = curl_init("https://api.brandmeister.network{$endpoint}");
    curl_setopt_array($ch, [
      CURLOPT_RETURNTRANSFER => true,
      CURLOPT_HTTPHEADER => ["Authorization: Bearer ".trim($token), "Accept: application/json"],
      CURLOPT_TIMEOUT => $timeout,
      CURLOPT_SSL_VERIFYPEER => true,
    ]);
    $body = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $err  = curl_error($ch);
    curl_close($ch);
    if ($body === false) throw new RuntimeException("BM cURL: $err");
    if (!$allow_non2xx && ($code < 200 || $code >= 300)) {
      throw new RuntimeException("BM HTTP $code");
    }
    return ['code'=>$code, 'json'=>json_decode($body, true)];
  }

function bm_api_get(string $endpoint, string $token, bool $debug = false): array {
  if (!function_exists('curl_init')) {
    throw new RuntimeException('php-curl fehlt (installiere php-curl)');
  }

  $url = "https://api.brandmeister.network{$endpoint}";
  $ch = curl_init($url);
  curl_setopt_array($ch, [
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_HTTPHEADER => [
      "Authorization: Bearer " . trim($token),
      "Accept: application/json",
    ],
    CURLOPT_TIMEOUT => 10,
    CURLOPT_SSL_VERIFYPEER => true,
  ]);

  $body = curl_exec($ch);
  $err  = curl_error($ch);
  $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
  curl_close($ch);

  if ($body === false) {
    throw new RuntimeException("BrandMeister API cURL error: {$err}");
  }

  // Bei Nicht-2xx: komplette Fehlantwort nach oben reichen (damit wir sie sehen)
  if ($code < 200 || $code >= 300) {
    if ($debug) {
      return ['ok' => false, 'code' => $code, 'raw' => $body];
    }
    throw new RuntimeException("BrandMeister API HTTP {$code}");
  }

  $json = json_decode($body, true);
  if ($json === null && json_last_error() !== JSON_ERROR_NONE) {
    if ($debug) {
      return ['ok' => false, 'code' => $code, 'raw' => $body];
    }
    throw new RuntimeException("BrandMeister API: ungültiges JSON");
  }

  return ['ok' => true, 'code' => $code, 'data' => $json];
}

try {
  /**
   * DB-Verbindung (Unix-Socket, kein TCP).
   * - ERRMODE_EXCEPTION: Fehler werden als Exceptions geworfen.
   * - FETCH_ASSOC: Ergebnisse als assoziative Arrays.
   * - EMULATE_PREPARES=false: native Prepared Statements, wenn verfügbar.
   */
  $pdo = new PDO(
    'mysql:unix_socket=/run/mysqld/mysqld.sock;dbname=mmdvmdb;charset=utf8mb4',
    'www-data',
    '',
    [
      PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
      PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
      PDO::ATTR_EMULATE_PREPARES   => false,
    ]
  );

  // Abfrageparameter: q=...
  $q = $_GET['q'] ?? 'status';

  /* =========================
     OpenFM: Config für setup.html
     q=config_inbox
     ========================= */
  if ($q === 'config_inbox') {
    $row = $pdo->query("
      SELECT
        id,
        callsign,
        dns_domain,
        default_tg,
        monitor_tgs,
        Location,
        Locator,
        SysOp,
        LAT,
        LON,
        TXFREQ,
        RXFREQ,
        Website,
        nodeLocation,
        CTCSS
      FROM config
      ORDER BY id ASC
      LIMIT 1
    ")->fetch(PDO::FETCH_ASSOC);

    if (!$row) {
      // leeres Objekt, damit JS nicht über ein Array stolpert
      echo json_encode(new stdClass(), JSON_UNESCAPED_UNICODE);
      exit;
    }

    // Mapping DB → Feldnamen in setup.html
    $out = [
      // STATION
      'Callsign'      => $row['callsign']     ?? '',
      'Region'        => $row['nodeLocation'] ?? '',
      'Location'      => $row['Location']     ?? '',
      'Locator'       => $row['Locator']      ?? '',
      'SYSOP'         => $row['SysOp']        ?? '',
      'Latitude'      => $row['LAT']          ?? '',
      'Longitude'     => $row['LON']          ?? '',
      'URL'           => $row['Website']      ?? '',

      // REPEATER / HOTSPOT
      'RXFrequency'   => $row['RXFREQ']       ?? '',
      'TXFrequency'   => $row['TXFREQ']       ?? '',
      'Network'       => $row['dns_domain']   ?? '',
      'CTCSSRepeater' => $row['CTCSS']        ?? '',

      // TALKGROUPS
      'TGDefault'     => $row['default_tg']   ?? '',
      'TGMonitored'   => $row['monitor_tgs']  ?? '',
    ];

    // alles als String rausgeben (Null -> leerer String)
    foreach ($out as $k => $v) {
      $out[$k] = ($v === null) ? '' : (string)$v;
    }

    echo json_encode($out, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
    LocalConfig (Einzelzeile)
    ========================= */
  if ($q === 'localconfig') {
    $row = $pdo->query("
      SELECT
        callsign,
        dns_domain,
        default_tg,
        monitor_tgs,
        RXFREQ   AS rxfreq,
        TXFREQ   AS txfreq,
        LAT      AS latitude,
        LON      AS longitude,
        DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at
      FROM config
      LIMIT 1
    ")->fetch(PDO::FETCH_ASSOC);

    echo json_encode($row ?: new stdClass(), JSON_UNESCAPED_UNICODE);
    exit;
  }

    /* =========================
     FM Heatmap (Count by weekday + hour)
     q=fmheatmap
     ========================= */
    if ($q === 'fmheatmap') {
        // Daten kommen fertig aggregiert aus fmstats (letzte 7 Tage, 24x7)
        $rows = $pdo->query("
            SELECT
              weekday,
              hour,
              COALESCE(qso_count, 0) AS count
            FROM fmstats
            WHERE metric = 'heatmap_week'
            ORDER BY weekday, hour
        ")->fetchAll(PDO::FETCH_ASSOC);

        echo json_encode($rows, JSON_UNESCAPED_UNICODE);
        exit;
    }

  /* =========================
     FM: Last Heard (nur talk = 'stop', inkl. Dauer + location)
     mit Server-Side-Filterung:
       mode=all
       mode=local&tg=123
       mode=monitored&tgs=1,2,3
     ========================= */
  if ($q === 'fmlastheard') {
    $mode = $_GET['mode'] ?? 'all';
    $sqlFilter = '';
    $params = [];

    if ($mode === 'local') {
      // Einzelne TG
      $tg = isset($_GET['tg']) ? (int)$_GET['tg'] : 0;
      if ($tg > 0) {
        $sqlFilter = " AND s.tg = :tg";
        $params[':tg'] = $tg;
      }
    } elseif ($mode === 'monitored') {
      // Mehrere TGs, CSV-Liste
      $tgList = $_GET['tgs'] ?? '';
      $tgNums = array_filter(
        array_map(
          static fn ($x) => (int)trim($x),
          explode(',', (string)$tgList)
        ),
        static fn ($n) => $n > 0
      );

      if ($tgNums) {
        $placeholders = [];
        foreach ($tgNums as $idx => $tg) {
          $ph = ":tg{$idx}";
          $placeholders[] = $ph;
          $params[$ph] = $tg;
        }
        $sqlFilter = " AND s.tg IN (" . implode(',', $placeholders) . ")";
      }
    }

    $sql = "
      SELECT
        s.callsign,
        s.tg,
        s.server,
        s.talk,
        DATE_FORMAT(s.event_time, '%Y-%m-%d %H:%i:%s') AS event_time,
        TIMESTAMPDIFF(
          SECOND,
          (
            SELECT MAX(start.event_time)
            FROM fmlastheard start
            WHERE start.callsign   = s.callsign
              AND start.tg         = s.tg
              AND start.server     = s.server
              AND start.talk       = 'start'
              AND start.event_time <= s.event_time
          ),
          s.event_time
        ) AS duration_s,
        n.location
      FROM fmlastheard s
      LEFT JOIN nodes n
        ON n.callsign = s.callsign
      WHERE s.talk = 'stop'
      {$sqlFilter}
      ORDER BY s.event_time DESC
      LIMIT 50
    ";

    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

    foreach ($rows as &$r) {
      $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
    }

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

    /* =========================
     FM: aktuelle aktive Stationen (fmstatus)
     ========================= */
  if ($q === 'fmstatus') {
    $rows = $pdo->query("
      SELECT
        s.callsign,
        s.tg,
        s.server,
        DATE_FORMAT(s.event_time, '%Y-%m-%d %H:%i:%s') AS event_time,
        n.location
      FROM fmstatus s
      LEFT JOIN nodes n
        ON n.callsign = s.callsign
      ORDER BY s.event_time DESC
    ")->fetchAll(PDO::FETCH_ASSOC);

    foreach ($rows as &$r) {
      $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
    }

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     FM: Top 10 Callsigns nach Anzahl TX
     q=fm_callsignTop10Count
     Optional: mode=all|local|monitored, tg=NUM, tgs=1,2,3
     ========================= */
  if ($q === 'fm_callsignTop10Count') {
      // Top 10 Callsigns nach QSO-Anzahl aus fmstats
      $rows = $pdo->query("
          SELECT
            callsign,
            COALESCE(qso_count, 0) AS cnt
          FROM fmstats
          WHERE metric = 'top_calls_qso'
          ORDER BY rank ASC
          LIMIT 10
      ")->fetchAll(PDO::FETCH_ASSOC);

      foreach ($rows as &$r) {
          $r['cnt'] = (int)($r['cnt'] ?? 0);
          $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
      }
      unset($r);

      echo json_encode($rows, JSON_UNESCAPED_UNICODE);
      exit;
  }

  /* =========================
     FM: Top 10 Callsigns nach Gesamtsendezeit
     q=fm_callsignTop10Duration
     Optional: mode=all|local|monitored, tg=NUM, tgs=1,2,3
     ========================= */
  if ($q === 'fm_callsignTop10Duration') {
      // Top 10 Callsigns nach Gesamtdauer (Sekunden) aus fmstats
      $rows = $pdo->query("
          SELECT
            callsign,
            COALESCE(total_seconds, 0) AS sec
          FROM fmstats
          WHERE metric = 'top_calls_duration'
          ORDER BY rank ASC
          LIMIT 10
      ")->fetchAll(PDO::FETCH_ASSOC);

      foreach ($rows as &$r) {
          $r['sec'] = (float)($r['sec'] ?? 0.0);
          $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
      }
      unset($r);

      echo json_encode($rows, JSON_UNESCAPED_UNICODE);
      exit;
  }


    /* =========================
     FM: Hall of Fame – Top Callsigns der Monat (30 Tage)
     q=fm_hallOfFameWeek
     ========================= */
  if ($q === 'fm_hallOfFameWeek') {
    // Hall of Fame: Top-Calls nach Score aus fmstats
    $rows = $pdo->query("
        SELECT
          callsign,
          COALESCE(qso_count,     0) AS qso_count,
          COALESCE(total_seconds, 0) AS total_sec,
          COALESCE(score,         0) AS score
        FROM fmstats
        WHERE metric = 'top_calls_score'
        ORDER BY rank ASC
        LIMIT 10
    ")->fetchAll(PDO::FETCH_ASSOC);

    foreach ($rows as &$r) {
        $qso = (int)($r['qso_count'] ?? 0);
        $tot = (float)($r['total_sec'] ?? 0.0);
        $r['qso_count']   = $qso;
        $r['total_sec']   = $tot;
        $r['score']       = (float)($r['score'] ?? 0.0);
        $r['avg_sec']     = $qso > 0 ? $tot / $qso : 0.0;
        $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
    }
    unset($r);

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     FM: Top Talkgroups (global)
     q=fm_topTalkgroups
     Immer global (kein Filter nach Default/Monitor)
     ========================= */
if ($q === 'fm_topTalkgroups') {
    // Top Talkgroups nach Gesamtdauer aus fmstats
    $rows = $pdo->query("
        SELECT
          tg,
          COALESCE(qso_count,     0) AS cnt,
          COALESCE(total_seconds, 0) AS total_sec
        FROM fmstats
        WHERE metric = 'top_tg_duration'
        ORDER BY rank ASC
        LIMIT 10
    ")->fetchAll(PDO::FETCH_ASSOC);

    foreach ($rows as &$r) {
        $cnt = (int)($r['cnt'] ?? 0);
        $tot = (float)($r['total_sec'] ?? 0.0);
        $r['cnt']       = $cnt;
        $r['total_sec'] = $tot;
        $r['avg_sec']   = $cnt > 0 ? $tot / $cnt : 0.0;
    }
    unset($r);

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  // Fallback: unbekannter q-Parameter
  http_response_code(400);
  echo json_encode(['error' => 'bad query'], JSON_UNESCAPED_UNICODE);

} catch (Throwable $e) {
  // Generische Fehlerbehandlung (keine sensiblen Details leaken)
  http_response_code(500);
  echo json_encode(['error' => $e->getMessage()], JSON_UNESCAPED_UNICODE);
}
