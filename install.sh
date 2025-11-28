#!/usr/bin/env bash

# (c) DJ0ABR
# Unified installer for OpenFM on Debian x86_64
# Usage: sudo ./install.sh

set -euo pipefail
set -o errtrace
umask 022

# Muss als root laufen (direkt oder via sudo)
if [[ "$EUID" -ne 0 ]]; then
  echo "Dieses Skript muss als root ausgeführt werden (sudo ./set-setup-password.sh)."
  exit 1
fi

# install packages
apt update
apt install \
git build-essential cmake libusb-1.0-0-dev \
libasound2-dev libfftw3-dev libgps-dev \
libwxgtk3.2-dev logrotate curl ca-certificates \
libmariadb-dev libmariadb-dev-compat mariadb-server \
apache2 php libapache2-mod-php php-mysql wget php-curl libmosquitto-dev nlohmann-json3-dev
apt-get autoremove -y || true
apt-get clean || true

# Ensure service is running (Debian: mariadb)
systemctl enable --now mariadb || true
# Feed SQL via heredoc to avoid set -e pitfalls with read -d ''
mysql -u root --protocol=socket <<'EOSQL'
CREATE DATABASE IF NOT EXISTS mmdvmdb CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
CREATE USER IF NOT EXISTS 'svxlink'@'localhost' IDENTIFIED VIA unix_socket;
GRANT ALL PRIVILEGES ON mmdvmdb.* TO 'svxlink'@'localhost';
CREATE USER IF NOT EXISTS 'www-data'@'localhost' IDENTIFIED VIA unix_socket;
GRANT SELECT ON mmdvmdb.* TO 'www-data'@'localhost';
GRANT INSERT, UPDATE ON mmdvmdb.* TO 'www-data'@'localhost';
FLUSH PRIVILEGES;
EOSQL
echo "MariaDB initialized."

# copy GUI
cp -R gui/html/* /var/www/html

#make parser
cd gui/parser
make -j 4
# set file permissions
chown svxlink:svxlink /etc/svxlink/node_info.json
chown svxlink:svxlink /etc/svxlink/svxlink.conf

# allow user svxlink to restart the service AND reboot the system
SYSTEMCTL_BIN="$(command -v systemctl || echo /usr/bin/systemctl)"
SHUTDOWN_BIN="$(command -v shutdown || echo /usr/sbin/shutdown)"
TMP_FILE="$(mktemp)"
cat >"$TMP_FILE" <<EOF
svxlink ALL=(root) NOPASSWD: $SYSTEMCTL_BIN restart svxlink.service, $SHUTDOWN_BIN -r now
EOF
visudo -cf "$TMP_FILE"
install -o root -g root -m 440 "$TMP_FILE" /etc/sudoers.d/svxlink-service
rm -f "$TMP_FILE"

# Service
sudo install -m 644 fmparser.service /etc/systemd/system/svxlink.service
sudo systemctl daemon-reload
sudo systemctl enable fmparser.service
sudo systemctl start fmparser.service
