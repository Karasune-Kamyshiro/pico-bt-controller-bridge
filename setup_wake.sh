#!/bin/sh
# setup-bluetooth-wake.sh

set -e

if [ "$(id -u)" -ne 0 ]; then!binsh
echo "please execute as root (sudo sh $0)"
exit 1
fi

echo "=== 1. Suche Bluetooth-USB-Adapter ==="
echo "Aktuell verbundene USB-Geräte:"
lsusb
echo ""
echo "Trag die idVendor:idProduct deines Bluetooth-Adapters ein (Format: xxxx:xxxx)."
echo "Meist zu erkennen an Namen wie 'IMC Networks', 'MediaTek', 'Realtek Bluetooth', 'Intel Wireless'."
read -p "Vendor:Product ID > " VIDPID

VID=$(echo "$VIDPID" | cut -d: -f1)
PID=$(echo "$VIDPID" | cut -d: -f2)

if [ -z "$VID" ] || [ -z "$PID" ]; then
echo "Ungueltige Eingabe."
exit 1
fi

echo ""
echo "=== 2. Suche sysfs-Pfad fuer $VID:$PID ==="
USB_PATH=""
for d in /sys/bus/usb/devices/*/; do
if [ -f "${d}idVendor" ] && [ -f "${d}idProduct" ]; then
v=$(cat "${d}idVendor" 2>/dev/null)
p=$(cat "${d}idProduct" 2>/dev/null)
if [ "$v" = "$VID" ] && [ "$p" = "$PID" ]; then
USB_PATH="${d%/}"
fi
fi
done

if [ -z "$USB_PATH" ]; then
echo "Kein passendes USB-Geraet gefunden. Ist der Adapter aktiv/verbunden?"
exit 1
fi

echo "Gefunden: $USB_PATH"

echo ""
echo "=== 3. Ermittle PCI-Kette ==="
FULL_PATH=$(readlink -f "$USB_PATH")
echo "Vollstaendiger Pfad: $FULL_PATH"

# Alle PCI-Adressen aus dem Pfad extrahieren (Format 0000:xx:xx.x)
PCI_ADDRS=$(echo "$FULL_PATH" | grep -oE '[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-9a-f]' | sort -u)

if [ -z "$PCI_ADDRS" ]; then
echo "Keine PCI-Adressen gefunden - unerwartet, bitte manuell pruefen."
exit 1
fi

echo "Gefundene PCI-Geraete in der Kette:"
echo "$PCI_ADDRS"

echo ""
echo "=== 4. Setze Wakeup + Power Control ==="

# USB-Geraet selbst
echo "USB-Geraet: $USB_PATH"
echo enabled > "$USB_PATH/power/wakeup" 2>/dev/null && echo "  wakeup=enabled gesetzt" || echo "  WARNUNG: power/wakeup nicht vorhanden"
echo on > "$USB_PATH/power/control" 2>/dev/null && echo "  control=on gesetzt" || echo "  WARNUNG: power/control nicht vorhanden"

# Alle PCI-Zwischenstationen
for addr in $PCI_ADDRS; do
CTRL_FILE="/sys/bus/pci/devices/$addr/power/control"
WAKE_FILE="/sys/bus/pci/devices/$addr/power/wakeup"
if [ -f "$CTRL_FILE" ]; then
echo on > "$CTRL_FILE"
echo "  PCI $addr: control=on gesetzt"
else
echo "  PCI $addr: power/control nicht gefunden (uebersprungen)"
fi
if [ -f "$WAKE_FILE" ]; then
echo enabled > "$WAKE_FILE"
echo "  PCI $addr: wakeup=enabled gesetzt"
else
echo "  PCI $addr: power/wakeup nicht gefunden (uebersprungen)"
fi
done

echo ""
echo "=== 5. mem_sleep pruefen ==="
CURRENT_SLEEP=$(cat /sys/power/mem_sleep)
echo "Aktuell: $CURRENT_SLEEP"
if ! echo "$CURRENT_SLEEP" | grep -q '\[deep\]'; then
echo deep > /sys/power/mem_sleep
echo "Auf 'deep' umgestellt."
else
echo "Bereits auf 'deep' - gut."
fi

echo ""
echo "=== 6. systemd-sleep-Hook einrichten (fuer Persistenz nach Resume) ==="
HOOK_DIR="/etc/systemd/system-sleep"
HOOK_FILE="$HOOK_DIR/bluetooth-wakeup-fix.sh"
mkdir -p "$HOOK_DIR"

{
echo '#!/bin/sh'
echo 'case "$1" in'
echo '    post)'
echo "        logger \"bluetooth-wakeup-fix: setting power control\""
echo "        echo enabled > $USB_PATH/power/wakeup 2>/dev/null"
echo "        echo on > $USB_PATH/power/control 2>/dev/null"
for addr in $PCI_ADDRS; do
echo "        echo on > /sys/bus/pci/devices/$addr/power/control 2>/dev/null"
echo "        echo enabled > /sys/bus/pci/devices/$addr/power/wakeup 2>/dev/null"
done
echo '        ;;'
echo 'esac'
} > "$HOOK_FILE"

chmod +x "$HOOK_FILE"
echo "Hook geschrieben nach: $HOOK_FILE"

echo ""
echo "=== 7. udev-Regel fuer das USB-Geraet (greift bei Neuverbindung/Reboot) ==="
UDEV_FILE="/etc/udev/rules.d/99-bluetooth-nosuspend.rules"
{
echo "ACTION==\"add\", SUBSYSTEM==\"usb\", ATTR{idVendor}==\"$VID\", ATTR{idProduct}==\"$PID\", ATTR{power/control}=\"on\""
echo "ACTION==\"add\", SUBSYSTEM==\"usb\", ATTR{idVendor}==\"$VID\", ATTR{idProduct}==\"$PID\", ATTR{power/wakeup}=\"enabled\""
} > "$UDEV_FILE"
udevadm control --reload-rules
udevadm trigger
echo "udev-Regel geschrieben nach: $UDEV_FILE"

echo ""
echo "=== Fertig ==="
echo "Gefundene PCI-Kette wurde fest in $HOOK_FILE eingetragen:"
echo "$PCI_ADDRS"
echo ""
echo "Jetzt testen mit:"
echo "  sudo systemctl suspend"
echo "Danach LANGE warten (mind. 30-60 Min, besser ueber Nacht) und per Controller-Taste wecken."
echo ""
echo "Kontrolle nach dem Aufwachen:"
echo "  journalctl -b | grep bluetooth-wakeup-fix"
echo "  cat $USB_PATH/power/control   (sollte 'on' zeigen)"
echo "  cat $USB_PATH/power/wakeup    (sollte 'enabled' zeigen)"
