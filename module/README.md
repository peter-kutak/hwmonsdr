hwmonsw DKMS package

Inštalácia:
sudo cp -r module /usr/src/hwmonsw-1.0
sudo dkms add -m hwmonsw -v 1.0
sudo dkms build -m hwmonsw -v 1.0
sudo dkms install -m hwmonsw -v 1.0

Po inštalácii:
lsmod | grep hwmonsw
dmesg | tail -n 20
ls /sys/class/hwmon/

Aktualizácia RPM:
echo 1200 | sudo tee /sys/class/hwmon/hwmonX/device/update

Odstránenie:
sudo dkms remove -m hwmonsw -v 1.0 --all

Poznámky:
- Vyžaduje linux-headers a dkms.
- API sa môže líšiť podľa kernel verzie; uprav podľa potreby.


