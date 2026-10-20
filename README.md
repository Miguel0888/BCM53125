# BCM53125 Switch-Mirroring – Asus RT-N66U / DD-WRT

Kleines Linux-MIPS-Hilfsprogramm zum Lesen und Konfigurieren der Port-Mirroring-Register eines Broadcom-BCM53xx-Switches. Ziel ist, den Ethernet-Datenverkehr zwischen ONT und Icotera auf der CPU-Schnittstelle eines Asus RT-N66U für einen PPPoE-Mitschnitt sichtbar zu machen.

**Testgerät:** Asus RT-N66U, DD-WRT r44715, Linux 4.4.241, `/proc/switch/eth0/driver`: `bcm53xx`, MIPS Little Endian (mipsel, ohne FPU). Die Binary wird mit einer OpenWrt-`bcm47xx/mips74k`-Toolchain (musl, Soft-Float) statisch kompiliert.

> **Hardwaretest vom 20.10.2026:** Die Binary aus [v0.1.0-alpha.10](../../releases/tag/v0.1.0-alpha.10) startet auf dem RT-N66U. Sowohl `probe` als auch `enable` wurden auf dem Gerät erfolgreich ausgeführt; alle drei Mirroring-Register ließen sich lesen bzw. nach dem Schreiben zurücklesen. Die tatsächliche Vollständigkeit der gespiegelten Pakete und die Funktion von `restore` wurden zu diesem Zeitpunkt **noch nicht anhand eines Mitschnitts bzw. Restore-Tests bestätigt**.

## Download

Die ausführbare Datei `switchmirror-mips32-le` sowie `SHA256SUMS` stehen unter **[GitHub Releases](../../releases)** zum Herunterladen bereit. Der letzte ausdrücklich auf dem Router bestätigte Build ist **[v0.1.0-alpha.10](../../releases/tag/v0.1.0-alpha.10)**.

Ältere **Big-Endian**-Builds (`*-be`) sind auf dem RT-N66U falsch. Frühere Little-Endian-Builds vor v0.1.0-alpha.10 verursachten auf dem Testgerät einen Segmentation Fault oder `Illegal instruction`; sie dürfen nicht als erfolgreich getestet betrachtet werden.

Bei jedem Push auf `main` veröffentlicht die GitHub Action nach erfolgreichem Build eine neue Entwicklungs-Release. Diese zusätzlichen Releases sind nicht automatisch auf dem echten Router getestet.

## Anschlüsse und Voraussetzungen

Der Asus ist für diesen Test ein **temporärer Layer-2-Switch**, nicht selbst der PPPoE-Client:

```text
Genexis FiberTwist ONT -- Asus WAN (Switch-Port 0)
                             |
                             +-- Asus LAN 4 (Switch-Port 4) -- Icotera WAN
                             |
                             +-- Asus LAN 2 -- PC für SSH/Administration
```

Im beschriebenen Testaufbau wurde Hardware-VLAN 1002 bereits **separat** auf `0t 4t 8t` eingerichtet: Switch-Port 0 = WAN, Port 4 = physischer LAN 4, Port 8 = CPU. Das Mirror-Hilfsprogramm **ändert diese VLAN-Zuordnung nicht**. Die VLAN-Einrichtung gehört nicht zum normalen `switchmirror`-Aufruf.

Die Icotera-WAN-MAC war in diesem Test `00:0f:15:d5:f2:bf`. Bei der Aufzeichnung kann die Verbindung des Icotera durch einen erneuten PPPoE-Verbindungsaufbau oder seinen Neustart neu ausgehandelt werden.

## Datei auf den Asus übertragen

Unter Windows PowerShell (Router-IP ggf. anpassen):

```powershell
scp -O .\switchmirror-mips32-le root@192.168.1.1:/tmp/switchmirror
```

Auf dem Router:

```sh
chmod 700 /tmp/switchmirror
/tmp/switchmirror probe
```

**Bestätigte Ausgabe auf dem Testgerät vor Aktivierung:**

```text
switchmirror: normal libc startup; command=probe
MDIO interface: Broadcom private ethernet ioctl
capture: 0x0000
ingress: 0x0000
egress:  0x0000
Probe completed. Mirroring settings unchanged.
```

`probe` liest die Konfiguration nur. Es schreibt **keine Mirroring-Konfigurationsregister**; für die Registeradressierung werden jedoch interne MDIO-Seiten- und Adressregister gesetzt. Wenn `probe` fehlschlägt oder offensichtlich falsche Werte liefert, **nicht `enable` ausführen**.

## Mirroring aktivieren

**Achtung:** Änderungen an Hardware-Switch-Registern können die Verbindung unterbrechen. Nur mit lokalem Zugriff und bekannter Wiederherstellung testen; keine unbekannten Originalregisterwerte überschreiben.

```sh
/tmp/switchmirror enable
```

Das Werkzeug spiegelt **eingehende** Pakete von Switch-Port 0 (ONT/WAN) und Port 4 (Icotera) auf CPU-Port 8. Es konfiguriert dabei:

| Register (Management-Seite `0x02`) | Funktion | Wert im Test |
| --- | --- | --- |
| `0x10` | Mirror Capture Control – aktiv, Ziel CPU-Port 8 | `0x8008` |
| `0x12` | Ingress Mirror Control – Quellports 0 und 4 | `0x0011` |
| `0x1c` | Egress Mirror Control – keine ausgehenden Quellen | `0x0000` |

**Bestätigte Ausgabe auf dem Testgerät:**

```text
switchmirror: normal libc startup; command=enable
MDIO interface: Broadcom private ethernet ioctl
capture: 0x0000
ingress: 0x0000
egress:  0x0000
RESTORE COMMAND: /tmp/switchmirror restore 0x0000 0x0000 0x0000
Mirroring registers written and read back successfully.
```

Damit sind die Registeränderungen bestätigt; ob die CPU tatsächlich alle gespiegelten Pakete empfängt, ist eine **eigene Prüfung**.

## Vollständigen Ethernet-Mitschnitt versuchen

Im Test wurde die Aufzeichnung auf der **physischen CPU-Schnittstelle `eth0`** gestartet, nicht auf `vlan1002` oder `vlan2`:

```sh
tcpdump -ni eth0 -s 0 -U -w /tmp/pppoe.pcap
```

Das absichtlich **ungefilterte** Capture verhindert, dass getaggte PPPoE-Pakete wegen eines ungeeigneten Filters verloren gehen. Es kann allerdings auch anderen lokalen Ethernetverkehr enthalten.

1. `tcpdump` starten und laufen lassen. Die Meldung `listening on eth0` ist **noch kein Nachweis**, dass alle Pakete ankommen.
2. Während der Aufzeichnung den Icotera neu einwählen lassen oder ihn neu starten und bis zur erfolgreichen Internetverbindung warten. **Den Asus dabei nicht neu starten.**
3. Aufzeichnung im `tcpdump`-Terminal mit **Strg+C** beenden; Statistik beachten.
4. Danach das Mirroring zurücksetzen (nächster Abschnitt) und PCAP prüfen.

Eine funktionierende Aufzeichnung sollte die gesamte PPPoE-Discovery (`PADI`, `PADO`, `PADR`, `PADS`) und ggf. nachfolgende PPPoE-Session-Frames zeigen. Sind erneut nur PADI sichtbar, muss die tatsächliche Spiegelung zum CPU-Port geprüft werden; das erfolgreiche Zurücklesen der Register allein beweist die Paketzustellung nicht.

**Wichtig:** `/tmp` ist beim DD-WRT-Testgerät RAM-basiert. Die PCAP verbraucht Arbeitsspeicher und geht bei einem **Asus-Neustart** verloren. Den Mitschnitt nicht länger als nötig laufen lassen.

## Mirroring wiederherstellen

Der Rücksetzbefehl verwendet die **tatsächlich vor `enable` angezeigten Werte**, nicht pauschal Nullen:

```sh
/tmp/switchmirror restore 0x0000 0x0000 0x0000
/tmp/switchmirror probe
```

Die Nullwerte gelten ausschließlich für die protokollierte Ausgangskonfiguration dieses Testgeräts. Bei anderen Ausgangswerten den dort ausgegebenen `RESTORE COMMAND` verwenden. Das Zurücksetzen wurde bislang **noch nicht praktisch bestätigt**.

## PCAP nach Windows kopieren und prüfen

```powershell
scp -O root@192.168.1.1:/tmp/pppoe.pcap .\pppoe.pcap
```

Eine lokale Sichtprüfung ist unter anderem mit Wireshark oder `tcpdump` möglich:

```sh
tcpdump -n -e -vv -r /tmp/pppoe.pcap 'pppoed or pppoes or (vlan and (pppoed or pppoes))'
```

**Datenschutz:** Eine komplette PPPoE-PCAP kann PAP/CHAP-Aushandlung, Zugangsdaten, Nutzdaten und zusätzliche LAN-Frames enthalten. Vor einer Weitergabe vertrauliche Inhalte entfernen. Die gesamte PCAP nicht ungeprüft öffentlich hochladen.

## Automatischer Build

Der Workflow [Cross-compile and release MIPS32 LE](../../actions/workflows/cross-compile.yml) verwendet die OpenWrt-Toolchain `bcm47xx/mips74k` mit überprüfter SHA-256 und führt QEMU-Smoke-Tests für MIPS `24Kc` ohne Hardware-FPU aus.

Das Programm lässt sich auch mit einer lokal installierten entsprechenden OpenWrt-Toolchain erzeugen:

```sh
make CC=/pfad/zur/toolchain/bin/mipsel-openwrt-linux-musl-gcc
make smoke
```

Für die Smoke-Tests muss `qemu-mipsel` vorhanden sein. Build- und Emulationstests ersetzen **keine** Messung auf dem Router.

## Register- und Treiberquellen

- [Linux B53-Registerdefinitionen](https://github.com/torvalds/linux/blob/master/drivers/net/dsa/b53/b53_regs.h)
- [Broadcom `robocfg` (Referenz für private PHY-IOCTLs)](https://github.com/RMerl/asuswrt-merlin/blob/master/release/src/router/utils/robocfg.c)

Dieses Werkzeug ist experimentell. Es nimmt keine persistente DD-WRT-Konfiguration vor, und es ersetzt kein Backup des Routers.
