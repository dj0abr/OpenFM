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

# Service
sudo install -m 644 fmparser.service /etc/systemd/system/fmparser.service
sudo systemctl daemon-reload
sudo systemctl enable fmparser.service
sudo systemctl start fmparser.service
