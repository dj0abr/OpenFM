echo "*9120#" > /home/svxlink/repeater_dtmf_ctrl
sleep 5
aplay --device=plughw:1,1 /usr/share/svxlink/sounds/de_DE/Rundspruch/Notfunkübung2.wav
#AUDIODEV=hw:0,1 play /usr/share/svxlink/sounds/de_DE/Rundspruch/dlrs.mp3
#AUDIODEV=hw:0,1 play /usr/share/svxlink/sounds/de_DE/Rundspruch/4-NDSRS22.mp3
#aplay --device=plughw:0,1 /usr/share/svxlink/sounds/de_DE/Rundspruch/rundspruch-ende.wav
exit

