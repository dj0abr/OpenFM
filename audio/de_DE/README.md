these voice samples were generated with the free tool:

echo "eins" | ./piper -m ./de_DE-thorsten-high.onnx -f output.wav


then converted to a 16k bitrate with:

sox output.wav -c 1 -r 16000 -b 16 output_16k.wav


and then the overly long ending was trimmed with:

sox output_16k.wav output_16k_trimmed.wav silence 1 0.05 1% 1 0.15 1%


the files were essentially created automatically (with only a few manual adjustments) and are therefore not all perfect.
Add or correct them yourself if needed.

the entire de_DE directory must then be copied to:
/usr/share/svxlink/soun
