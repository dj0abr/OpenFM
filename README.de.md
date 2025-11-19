[🇬🇧 English](README.md) | [🇩🇪 Deutsch](README.de.md)

# 📡 OpenFM Repeater System (Deutsch)

**Aktuelle Version 1.0**

Getestet auf: 
- Debian‑basierte Desktop‑Distributionen 
- Minimal‑Debian (nur Konsole) 
- Debian‑VM auf Proxmox

OpenFM ist ein Dashboard für **SVXLink von SM0SVX** und unterstützt das\
**FM‑Funknetz von DJ1JAY**.

Es bietet eine saubere, OpenDVM‑kompatible Oberfläche und wurde
ursprünglich für den Repeater DB0SL entwickelt, lässt sich aber für jede
Station anpassen.

## Funktionen

-   Dashboard‑Oberfläche für SVXLink
-   Unterstützung für FM‑Funknetze
-   OpenDVM‑kompatibles UI‑Design
-   Talkgroup‑Filter
-   Detaillierte Statistiken
-   Einstellungsmenü mit Passwortschutz für grundlegende Konfigurationen

------------------------------------------------------------------------

<a href="gui.png">
  <img src="gui.png" alt="Systemübersicht" width="250">
</a>

<a href="gui1.png">
  <img src="gui1.png" alt="Systemübersicht" width="250">
</a>

🔗 **Live Installation:** [fm.db0sl.de](https://fm.db0sl.de/)

## 📊 Status

Version 1.0 ist für den Betrieb an DB0SL freigegeben und sollte auch auf
anderen Repeatern zuverlässig laufen.
Für spezielle Hardwareanforderungen (z. B. Soundkarten) können kleine
Anpassungen in `svxlink.conf` erforderlich sein.

## 🛠️ Installation

Auf einem frischen, Debian‑basierten System (empfohlen: Minimal‑Debian):

``` bash
sudo apt update
sudo apt install git -y
git clone https://github.com/dj0abr/OpenFM.git
cd OpenFM
```

Installation starten:

``` bash
sudo ./install.sh
```

Neustarten:

``` bash
sudo reboot
```

Nach dem Neustart laufen die Dienste **svxlink** und **fmparser**
automatisch, die Datenbank wird angelegt und das System ist vollständig
betriebsbereit.

Nach dem Neustart (**nicht früher!**) kann das Setup-Passwort gesetzt werden.
Das Default Passwort ist: **setuppassword**

Mit diesem Script kann es geändert werden:
``` bash
sudo ./set-setup-password.sh
```
Dieses Passwort wird benötigt um die Eingaben der Setup-Seite speichern zu können.

## 🌐 Web‑Frontend

Das Webinterface zeigt alle Live‑Betriebsdaten an.
Um darauf zuzugreifen, öffne die IP‑Adresse dieser Maschine im Browser
innerhalb deines lokalen Netzwerks.

### 🔍 Funktionen

-   Live‑Status: Modus, Rufzeichen, Dauer, Talkgroup
-   Farbige Status‑Kacheln und Länderflaggen
-   „Last Heard"‑Liste mit Rufzeichen, Zeitstempel, Dauer, TG‑Namen
-   Aktivitätsdiagramm (48 h, RF / NET getrennt)
-   Balkenstatistiken und 30‑Tage‑Heatmap
-   Responsive Dark‑UI
-   Nutzt **Chart.js** als einzige externe Bibliothek

### 🧩 Technologie

-   Reines Vanilla‑JavaScript
-   CSS‑Grid‑Layout
-   1‑Sekunden‑Live‑Updates über AJAX
-   Läuft auf jedem Webserver (nginx, Apache, lighttpd)

------------------------------------------------------------------------

## 📄 Lizenz

Dieses Projekt steht unter denselben Lizenzbedingungen wie SVXLink.
Details finden sich in der LICENSE‑Datei.
