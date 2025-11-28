<?php
declare(strict_types=1);
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store, must-revalidate');

function response(bool $ok, array $payload = []): void {
  echo json_encode(
    $ok ? array_merge(['ok' => true],  $payload)
        : array_merge(['ok' => false], $payload),
    JSON_UNESCAPED_UNICODE
  );
  exit;
}

// Nur POST akzeptieren
if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
  http_response_code(405);
  response(false, ['error' => 'method not allowed']);
}

/* ======= DB-Verbindung (auch für Passwort) ======= */
try {
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
} catch (Throwable $e) {
  http_response_code(500);
  response(false, ['error' => 'DB connection failed']);
}

/**
 * Setup-Passwort aus DB lesen.
 * Fallback: 'setuppassword', falls nichts in der DB steht.
 */
$configPassword = 'setuppassword'; // Fallback, wenn in DB nichts gesetzt ist

try {
  $stmt = $pdo->prepare('SELECT setup_password FROM config WHERE id = 1');
  $stmt->execute();
  $row = $stmt->fetch();
  if ($row && $row['setup_password'] !== null && $row['setup_password'] !== '') {
    $configPassword = (string)$row['setup_password'];
  }
} catch (Throwable $e) {
  // Wenn das Lesen fehlschlägt, lieber hart abbrechen
  http_response_code(500);
  response(false, ['error' => 'could not read setup password']);
}

/* ======= Passwort prüfen ======= */
$givenPw = $_POST['ConfigPassword'] ?? '';
if (!hash_equals($configPassword, (string)$givenPw)) {
  http_response_code(401);
  response(false, ['error' => 'auth required']);
}
unset($_POST['ConfigPassword']);

/**
 * Erwartete Felder aus setup.html
 *
 * Station:
 *   Callsign, Region, Location, Locator, Latitude, Longitude, URL, CTCSS, SYSOP
 * Repeater/Hotspot:
 *   RXFrequency, TXFrequency, Network, CTCSSRepeater
 * Talkgroups:
 *   TGDefault, TGMonitored
 */
$expected = [
  'Callsign',
  'Region',
  'Location',
  'Locator',
  'Latitude',
  'Longitude',
  'URL',
  'CTCSS',
  'SYSOP',
  'RXFrequency',
  'TXFrequency',
  'Network',
  'CTCSSRepeater',
  'TGDefault',
  'TGMonitored',
  'RebootFlag',
];

// einsammeln
$data = [];
foreach ($expected as $k) {
  $data[$k] = isset($_POST[$k]) ? trim((string)$_POST[$k]) : '';
}

// Dezimaltrennzeichen vereinheitlichen (Komma → Punkt)
foreach (['Latitude','Longitude'] as $k) {
  if ($data[$k] !== '') {
    $data[$k] = str_replace(',', '.', $data[$k]);
  }
}

// ggf. URL automatisch mit https:// versehen
if ($data['URL'] !== '' && !preg_match('#^https?://#i', $data['URL'])) {
  $data['URL'] = 'https://' . $data['URL'];
}

/* ======= Validierung ======= */
$errors = [];

// Callsign
if ($data['Callsign'] === '' || !preg_match('/^[A-Za-z0-9\/]{3,}$/', $data['Callsign'])) {
  $errors[] = 'Invalid callsign';
}

// Frequenzen: integer Hz
foreach (['RXFrequency','TXFrequency'] as $hz) {
  if ($data[$hz] === '' || !preg_match('/^\d+$/', $data[$hz])) {
    $errors[] = "$hz must be an integer number (Hz)";
  }
}

// Koordinaten: müssen numerisch sein
if ($data['Latitude'] === '' || $data['Longitude'] === '' ||
    !is_numeric($data['Latitude']) || !is_numeric($data['Longitude'])) {
  $errors[] = 'Latitude/Longitude invalid';
}

// Default Talkgroup: optional, wenn gesetzt dann numerisch
if ($data['TGDefault'] !== '' && !preg_match('/^\d+$/', $data['TGDefault'])) {
  $errors[] = 'TGDefault must be numeric if set';
}

if ($errors) {
  http_response_code(400);
  response(false, ['error' => implode('; ', $errors)]);
}

/* ======= Typkonvertierung / Aufbereitung für DB ======= */

// Callsign immer Uppercase
$callsign = strtoupper($data['Callsign']);

// TGDefault als int (NULL wenn leer)
$defaultTg = ($data['TGDefault'] === '') ? null : (int)$data['TGDefault'];

// Monitor-TGs bleiben als freier String (z.B. "262, 910, 263")
$monitorTgs = $data['TGMonitored'] !== '' ? $data['TGMonitored'] : null;

// Koordinaten als String, aber validierte numerische Werte
$lat = $data['Latitude'];
$lon = $data['Longitude'];

// RX/TX Freq als String (Spalten sind varchar), aber numerisch validiert
$rxfreq = $data['RXFrequency'];
$txfreq = $data['TXFrequency'];

// Region → nodeLocation
$nodeLocation = $data['Region'] !== '' ? $data['Region'] : null;

// Network → dns_domain (z.B. FQDN, Verbund-Name, …)
$dnsDomain = $data['Network'] !== '' ? $data['Network'] : null;

// CTCSS-Logik: falls Repeater-spezifischer Wert gesetzt ist, den bevorzugen
$ctcssRepeater = $data['CTCSSRepeater'];

// weitere Felder
$location   = $data['Location'] !== '' ? $data['Location'] : null;
$locator    = $data['Locator']  !== '' ? strtoupper($data['Locator']) : null;
$sysop      = $data['SYSOP']    !== '' ? $data['SYSOP'] : null;
$website    = $data['URL']      !== '' ? $data['URL'] : null;
// RebootFlag: '1' = Reboot angefordert, sonst 0
$rebootRequested = ($data['RebootFlag'] === '1') ? 1 : 0;

/* ======= DB-Speichern ======= */
try {
  /**
   * Wir gehen von EINER Konfig-Zeile in `config` aus (id=1).
   * Single-row Upsert auf die bestehende Tabelle `config`.
   * Spalten müssen zu deinem SELECT in api.php (config_inbox) passen.
   */
  $sql = "
    INSERT INTO config (
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
      CTCSS,
      reboot_requested
    ) VALUES (
      1,
      :callsign,
      :dns_domain,
      :default_tg,
      :monitor_tgs,
      :Location,
      :Locator,
      :SysOp,
      :LAT,
      :LON,
      :TXFREQ,
      :RXFREQ,
      :Website,
      :nodeLocation,
      :CTCSS,
      :reboot_requested
    )
    ON DUPLICATE KEY UPDATE
      callsign         = VALUES(callsign),
      dns_domain       = VALUES(dns_domain),
      default_tg       = VALUES(default_tg),
      monitor_tgs      = VALUES(monitor_tgs),
      Location         = VALUES(Location),
      Locator          = VALUES(Locator),
      SysOp            = VALUES(SysOp),
      LAT              = VALUES(LAT),
      LON              = VALUES(LON),
      TXFREQ           = VALUES(TXFREQ),
      RXFREQ           = VALUES(RXFREQ),
      Website          = VALUES(Website),
      nodeLocation     = VALUES(nodeLocation),
      CTCSS            = VALUES(CTCSS),
      reboot_requested = VALUES(reboot_requested),
      updated_at       = CURRENT_TIMESTAMP
  ";

  $stmt = $pdo->prepare($sql);
  $stmt->execute([
    ':callsign'     => $callsign,
    ':dns_domain'   => $dnsDomain,
    ':default_tg'   => $defaultTg,
    ':monitor_tgs'  => $monitorTgs,
    ':Location'     => $location,
    ':Locator'      => $locator,
    ':SysOp'        => $sysop,
    ':LAT'          => $lat,
    ':LON'          => $lon,
    ':TXFREQ'       => $txfreq,
    ':RXFREQ'       => $rxfreq,
    ':Website'      => $website,
    ':nodeLocation' => $nodeLocation,
    ':CTCSS'        => $ctcssRepeater,
    ':reboot_requested' => $rebootRequested,
  ]);

  // kleine Statistik: wie viele Felder waren nicht leer
  $count = 0;
  foreach ($data as $k => $v) {
    if ($v !== '') {
      $count++;
    }
  }

  response(true, [
    'stored_table' => 'config',
    'id'           => 1,
    'count'        => $count,
  ]);

} catch (Throwable $e) {
  http_response_code(500);
  response(false, ['error' => $e->getMessage()]);
}
