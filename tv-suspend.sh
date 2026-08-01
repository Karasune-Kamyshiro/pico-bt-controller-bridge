#!/bin/bash
# --- configuration ---------------------------------------------------------
export HOME=/home/deck                    # adb-Key persistent im Home
export ANDROID_ADB_SERVER_PORT=5038       # eigener adb-Server, kollidiert nicht
# mit einem evtl. laufenden User-adb


ADB=/home/deck/Applications/platform-tools/adb         # adb binary from Googles platform-tools
TVIP=192.168.178.109                      # IP of your TV
TV=$TVIP:5555
TVMAC="2072A970FA5E"                      # MAC adress of the TVs, without the ':'
BOXIP=192.168.178.71                      # IP of Android-TV-Box
GATEWAY=192.168.178.1                     # Your Router IP, is used for a generall "network available" test








# --- Hilfsfunktionen -------------------------------------------------------

# Wartet bis zu 15s, bis die angegebene IP auf Ping antwortet
wait_for_net() {
for i in $(seq 1 15); do
ping -c1 -W1 "$1" >/dev/null 2>&1 && return 0
sleep 1
done
return 1
}

# Prueft, ob die Android-TV-Box laeuft.
# Ihr USB-Netzwerkadapter ist im Standby stromlos -> Ping ist zuverlaessig.
# 3 Versuche, damit ein einzelnes verlorenes Paket nicht faelschlich
# den TV abschaltet (im Zweifel lieber anlassen).
box_is_awake() {
for i in 1 2 3; do
ping -c1 -W1 $BOXIP >/dev/null 2>&1 && return 0
sleep 1
done
return 1
}

# Magic Packet an den TV schicken (Ersatz fuer ether-wake, ohne Zusatzpaket)
send_wol() {
python3 - "$TVMAC" <<'EOF'
import socket, sys
mac = sys.argv[1]
pkt = bytes.fromhex("FF"*6 + mac*16)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.sendto(pkt, ("255.255.255.255", 9))
EOF
}

# --- Hauptlogik ------------------------------------------------------------

case "$1" in
off)
# NetworkManager hat das Netz wegen PrepareForSleep bereits
# schlafen gelegt -> per D-Bus wieder aufwecken
busctl call org.freedesktop.NetworkManager /org/freedesktop/NetworkManager \
org.freedesktop.NetworkManager Sleep b false

wait_for_net $TVIP

if box_is_awake; then
echo "Android-Box laeuft - TV bleibt an"
else
echo "Box ist aus - schalte TV ab"
timeout 10 $ADB connect $TV
timeout 10 $ADB -s $TV shell input keyevent KEYCODE_SLEEP
fi

timeout 5 $ADB disconnect
$ADB kill-server
;;

on)
# Nach dem Resume braucht das Netzwerk ein paar Sekunden
wait_for_net $GATEWAY

# Erst das Netzwerk-Interface des TVs per Magic Packet wecken,
# dann warten, bis es ansprechbar ist
send_wol
sleep 2
wait_for_net $TVIP

timeout 10 $ADB connect $TV
timeout 10 $ADB -s $TV shell input keyevent KEYCODE_WAKEUP

timeout 5 $ADB disconnect
$ADB kill-server
;;

*)
echo "Aufruf: $0 {off|on}"
exit 1
;;
esac
