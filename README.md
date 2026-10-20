# BCM53125 Switch-Mirroring (Asus RT-N66U / DD-WRT)

Kleines Linux-MIPS-Hilfsprogramm zum Prüfen und, nach Freigabe durch den Benutzer, Konfigurieren der Mirroring-Register eines Broadcom-Switches.

**Zielplattform:** Asus RT-N66U mit DD-WRT r44715, Kernel 4.4.241, MIPS32 **Little Endian**, Linux o32. Der Build ist freestanding, statisch und benötigt keine libc auf dem Router.

> **Status: experimentell.** Die Cross-Compilation und der CLI-Smoke-Test werden in GitHub Actions geprüft. Ob die vorhandenen Ethernet-Treiber-IOCTLs auf der konkreten Router-Firmware funktionieren, kann nur ein Probe-Lauf am Gerät zeigen. Ein erfolgreicher Build garantiert weder einen sicheren Registerzugriff noch einen vollständigen Paketmitschnitt.

## Downloads: GitHub Releases

Die aktuelle MIPS32-Little-Endian-Binary steht unter **[Releases](../../releases)** zur Verfügung. Jeder Push auf `main` veröffentlicht nach erfolgreichem Cross-Compile einen als **Pre-release** markierten Entwicklungsstand. Bei Push eines Versions-Tags wie `v1.0.0` entsteht eine reguläre Release.

Jede Release enthält:

- `switchmirror-mips32-le` – statisch gelinkte ausführbare Router-Binary
- `SHA256SUMS` – SHA-256-Prüfsumme

**Hinweis:** Der Upload in GitHub Releases erfolgt nach erfolgreichem Build und QEMU-Smoke-Test. Das ist kein Hardware-Nachweis. Frühere Releases mit dem Suffix `-be` waren für die falsche Byte-Reihenfolge gebaut und dürfen auf dem RT-N66U nicht verwendet werden. Die Little-Endian-Binary ist noch nicht auf dem tatsächlichen Router getestet.

## Automatischer Build

Unter [Actions](../../actions/workflows/cross-compile.yml) läuft der Build bei jedem Push auf `main`, jedem Pull Request, bei Versions-Tags und beim manuellen Start.

GitHub Actions installiert `gcc-mipsel-linux-gnu` und `qemu-user`, erzeugt ein statisches ELF32-MIPS-Little-Endian-Programm, prüft den ELF-Header und führt den Kommandozeilen-Smoke-Test unter QEMU aus.

Lokal unter Debian/Ubuntu:

```sh
sudo apt-get install gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu qemu-user
make
make smoke
```

## Übertragung und lesender Test

Kopiere die aus GitHub Releases heruntergeladene Datei `switchmirror-mips32-le` auf deinen eigenen Router. Unter Windows PowerShell:

```powershell
scp -O .\switchmirror-mips32-le root@192.168.1.1:/tmp/switchmirror
```

Auf dem Router:

```sh
chmod 700 /tmp/switchmirror
/tmp/switchmirror probe
```

`probe` liest die drei Mirroring-Register über die vom Broadcom-Treiber erwarteten IOCTLs. Es aktiviert keine Spiegelung. Bricht der Vorgang ab oder erscheinen unplausible Werte, **nicht mit `enable` fortfahren**.

## Schreibbefehle (nur nach erfolgreich verifiziertem Probe-Lauf)

```sh
/tmp/switchmirror enable
```

Das Werkzeug soll die eingehenden Pakete der Switch-Ports 0 (WAN) und 4 (physischer LAN 4) auf CPU-Port 8 spiegeln. Vor der Änderung gibt es einen `restore`-Befehl mit den tatsächlich gelesenen Originalwerten aus.

**Schreibzugriffe können die Verbindung unterbrechen.** Nicht über einen unsicheren Remote-Zugang testen. Lies den ausgegebenen Rücksetz-Befehl vollständig, bevor du weitermachst.

```sh
/tmp/switchmirror restore <original-capture> <original-ingress> <original-egress>
```

Die Platzhalter werden durch die beim Einschalten ausgegebenen Hexwerte ersetzt. Verwende **keine** geratenen Nullwerte. Das DD-WRT-NVRAM-Backup enthält keine temporären Switch-Registerwerte.

## Mitschnitt nach nachgewiesener Spiegelung

```sh
tcpdump -ni eth0 -s 0 -U -w /tmp/pppoe.pcap \
  'ether proto 0x8100 or ether proto 0x8863 or ether proto 0x8864'
```

Die PCAP kann PPP-Authentifizierungsdaten, einschließlich Zugangsdaten, enthalten. Prüfe und anonymisiere sie vor dem Teilen.

## Register und Referenzen

Die Register auf der Management-Seite `0x02` sind: Mirror Capture Control `0x10`, Ingress Mirror Control `0x12` und Egress Mirror Control `0x1c`. Die Masken und die Aktivierung entsprechen den Definitionen des Linux-B53-Treibers:

- [Linux b53_regs.h](https://github.com/torvalds/linux/blob/master/drivers/net/dsa/b53/b53_regs.h)
- [Broadcom robocfg als Beispiel für PHY-IOCTLs](https://github.com/RMerl/asuswrt-merlin/blob/master/release/src/router/utils/robocfg.c)

Das Programm ist kein Ersatz für einen Hardwaretest auf dem konkreten Gerät.
