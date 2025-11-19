#!/bin/bash
# set-setup-password.sh
# Setzt das Setup-Passwort in mmdvmdb.config.setup_password (Klartext)

set -euo pipefail

DB_NAME="mmdvmdb"
DB_USER="root"   # ggf. anpassen, z.B. "root" o.ä.

# Muss als root laufen (direkt oder via sudo)
if [[ "$EUID" -ne 0 ]]; then
  echo "Dieses Skript muss als root ausgeführt werden (sudo ./set-setup-password.sh)."
  exit 1
fi

echo "Setup-Passwort für Datenbank '$DB_NAME'."

# Passwort verdeckt einlesen
read -s -p "Neues Setup-Passwort: " PW1
echo
read -s -p "Neues Setup-Passwort (Wiederholung): " PW2
echo

# Prüfen ob identisch
if [[ "$PW1" != "$PW2" ]]; then
  echo "Fehler: Passwörter stimmen nicht überein."
  exit 1
fi

if [[ -z "$PW1" ]]; then
  echo "Fehler: Passwort darf nicht leer sein."
  exit 1
fi

# Für SQL escapen: Backslash und einfache Anführungszeichen
pw_sql=${PW1//\\/\\\\}
pw_sql=${pw_sql//\'/\'\\\'\'}

SQL="UPDATE config SET setup_password = '$pw_sql' WHERE id = 1;"

echo "Schreibe Setup-Passwort in die Datenbank..."

# Wenn der DB-User ein Passwort hat, wird mysql dich ggf. interaktiv fragen (-p)
if mysql -u"$DB_USER" "$DB_NAME" -e "$SQL"; then
  echo "Setup-Passwort wurde erfolgreich aktualisiert."
else
  echo "Fehler: MySQL-Update fehlgeschlagen."
  exit 1
fi
