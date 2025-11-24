#!/bin/bash
set -e

# Alle .wav Dateien im aktuellen Verzeichnis bearbeiten
for f in *.wav; do
  # Falls kein Match -> "*.wav" bleibt wörtlich -> überspringen
  [ -e "$f" ] || continue

  echo "Bearbeite: $f"

  tmp="$(mktemp --suffix=.wav)"

  # Stille vorne/hinten kürzen
  sox "$f" "$tmp" silence 1 0.05 1% 1 0.15 1%

  # Ergebnis zurück auf Originaldatei schreiben
  mv "$tmp" "$f"
done

echo "Fertig."
