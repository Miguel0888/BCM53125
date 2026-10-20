# BCM53125 Switch-Mirroring – Asus RT-N66U / DD-WRT

Kleines Linux-MIPS-Hilfsprogramm zum Lesen und Konfigurieren der Port-Mirroring-Register eines Broadcom-BCM53xx-Switches. Ziel ist, den Ethernet-Datenverkehr zwischen ONT und Icotera auf der CPU-Schnittstelle eines Asus RT-N66U für einen PPPoE-Mitschnitt sichtbar zu machen.

**Testgerät:** Asus RT-N66U, DD-WRT r44715, Linux 4.4.241, `/proc/switch/eth0/driver`: `bcm53xx`, MIPS Little Endian (mipsel, ohne FPU). Die Binary wird mit einer OpenWrt-`bcm47xx/mips74k`-Toolchain (musl, Soft-Float) statisch kompiliert.

> **Hardwaretest vom 20.10.2026:** Die Binary aus [v0.1.0-alpha.10](../../releases/tag/v0.1.0-alpha.10) wurde auf einem Asus RT-N66U mit DD-WRT erfolgreich mit `probe` und `enable` ausgeführt. Die drei Mirroring-Register ließen sich lesen und die neuen Werte erfolgreich zurücklesen. Ein anschließender Mitschnitt auf `eth0` hat **PPPoE-Frames in beiden Richtungen einschließlich PADI, PADO, PADR, PADS, LCP und IPCP** erfasst. Das ist ein praktischer Nachweis für die nutzbare Spiegelung in diesem Aufbau. Die `restore`-Funktion ist anhand der vorliegenden Rückmeldungen noch nicht praktisch bestätigt; außerdem ist die Vollständigkeit *sämtlicher* Ethernet-Frames nicht nachgewiesen. Siehe [Icotera-i4883-00-Praxisbeispiel](#praxisbeispiel-icotera-i4883-00--vollständige-pppoe-analyse).

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

Damit sind die Registeränderungen bestätigt. Im dokumentierten Icotera-Test wurde anschließend ein **beidseitiger** PPPoE-Mitschnitt aufgezeichnet. Das beweist den Empfang der gezeigten Protokollnachrichten, aber nicht die lückenlose Erfassung jedes Ethernet-Frames.

## Ethernet-Mitschnitt aufzeichnen

Im Test wurde die Aufzeichnung auf der **physischen CPU-Schnittstelle `eth0`** gestartet, nicht auf `vlan1002` oder `vlan2`:

```sh
tcpdump -ni eth0 -s 0 -U -w /tmp/pppoe.pcap
```

Das absichtlich **ungefilterte** Capture verhindert, dass getaggte PPPoE-Pakete wegen eines ungeeigneten Filters verloren gehen. Es kann allerdings auch anderen lokalen Ethernetverkehr enthalten.

1. `tcpdump` starten und laufen lassen. Die Meldung `listening on eth0` ist **noch kein Nachweis**, dass alle Pakete ankommen.
2. Während der Aufzeichnung den Icotera neu einwählen lassen oder ihn neu starten und bis zur erfolgreichen Internetverbindung warten. **Den Asus dabei nicht neu starten.**
3. Aufzeichnung im `tcpdump`-Terminal mit **Strg+C** beenden; Statistik beachten.
4. Danach das Mirroring zurücksetzen (nächster Abschnitt) und PCAP prüfen.

Im dokumentierten Test enthält die bereinigte PCAP **PADI, zwei PADO-Angebote, PADR, PADS, LCP und IPCP in beide Richtungen**. Sind in einem anderen Aufbau erneut nur PADI sichtbar, muss die tatsächliche Spiegelung zum CPU-Port geprüft werden; das erfolgreiche Zurücklesen der Register allein beweist die Paketzustellung nicht.

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

Eine lokale Sichtprüfung ist unter anderem mit Wireshark (Anzeigefilter `pppoed || pppoes`, `lcp`, `ipcp` bzw. `chap`) oder `tcpdump` möglich:

```sh
tcpdump -n -e -vv -r /tmp/pppoe.pcap 'pppoed or pppoes or (vlan and (pppoed or pppoes))'
```

**Datenschutz:** Eine komplette PPPoE-PCAP kann PAP/CHAP-Aushandlung, Zugangsdaten, Nutzdaten und zusätzliche LAN-Frames enthalten. Auch CHAP-Challenge/Response-Paare sind sicherheitsrelevant, obwohl das Passwort nicht im Klartext vorliegt. In Wireshark einen geeigneten Anzeigefilter setzen, dann unter **Datei → Export Specified Packets → Displayed** nur die angezeigten Pakete exportieren. **Speichern unter** allein entfernt ausgefilterte Pakete nicht zuverlässig. Eine vollständige PCAP niemals ungeprüft öffentlich hochladen. Im folgenden Praxisbeispiel wurde ausdrücklich nur die gefilterte Datei untersucht.

## Praxisbeispiel: Icotera i4883-00 – vollständige PPPoE-Analyse

Dieses Beispiel dokumentiert einen **tatsächlich erfolgreichen Verbindungsaufbau eines Icotera i4883-00** über einen als transparenten Layer-2-Zwischenschalter verwendeten **Asus RT-N66U** und ein **Genexis FiberTwist-P3411B ONT**. Ausgangspunkt war die Frage, warum die PPPoE-Einwahl mit DD-WRT auf dem Asus selbst scheiterte, obwohl dieselbe Glasfaserleitung mit dem Icotera funktionierte.

Die Analyse beruht auf drei voneinander zu unterscheidenden Informationsquellen:

1. **Bereinigte Aufzeichnung `[clean] pppoe.pcap`:** 86 Ethernet-Pakete, gefiltert auf PPPoE-Discovery, LCP und IPCP; ohne CHAP-/PAP-Pakete und ohne IP-Nutzdaten. Die unten genannten Frame-Nummern beziehen sich auf **diese exportierte, neu nummerierte Datei**, nicht auf die ursprüngliche vollständige PCAP.
2. **Wireshark-Screenshot aus der ursprünglichen Aufzeichnung:** drei CHAP-Nachrichten (Challenge, Response, Success); Benutzerkennung und Challenge-/Response-Werte werden hier bewusst nicht wiedergegeben.
3. **Icotera-WAN-Statusseite und beobachteter Betrieb:** aktive PPPoE-Verbindung mit öffentlicher IPv4-Adresse, funktionierendes WLAN und Internetzugang sowie abweichende LED-Anzeigen.

### A. Testaufbau und Ausgangslage

```text
FTTH/Provider
     |
Genexis FiberTwist-P3411B (ONT)
     |
     | Ethernet – PPPoE auf IEEE 802.1Q VLAN 1002
     |
Asus RT-N66U – DD-WRT v3.0-r44715
     |  physischer WAN-Port = BCM53xx-Port 0
     |  physischer LAN-4-Port = BCM53xx-Port 4
     |  CPU-Port = BCM53xx-Port 8
     |  Hardware-VLAN 1002 = 0t 4t 8t
     |  switchmirror: Ingress-Port 0 und 4 → CPU-Port 8
     |
Icotera i4883-00 (WAN; PPPoE-Client)
     |
WLAN / LAN-Clients
```

Der Administrations-PC war separat über Asus LAN 2 verbunden. Der Asus baute in diesem Test **keine eigene PPPoE-Verbindung** auf. Seine Aufgabe bestand darin, VLAN 1002 auf Hardware-Ebene zwischen ONT und Icotera zu übertragen und mit `switchmirror` Kopien der Pakete zur CPU zu leiten.

Anfänglich ließen sich mit `tcpdump -ni vlan1002` nur einzelne **PADI-Broadcasts** beobachten. Das war **kein Beweis für fehlende Providerantworten**: Unicast-Pakete konnten im Broadcom-Switch direkt zwischen den physischen Ports weitergeleitet werden, ohne den normalen CPU-Paketpfad zu erreichen. Erst mit aktiviertem Hardware-Port-Mirroring auf `eth0` wurde der bidirektionale Verbindungsaufbau sichtbar.

**Identität des PPPoE-Clients:** Die tatsächliche WAN-MAC des Icotera lautet `00:0f:15:d5:f2:bf`. Eine zunächst am Asus geklonte andere Icotera-MAC `00:0f:15:d5:f2:bd` war **nicht** die für PPPoE verwendete WAN-MAC. Diese Korrektur ist für spätere Vergleichstests wesentlich; sie beweist noch nicht, dass allein die falsche MAC den ursprünglichen Asus-Fehler verursacht hatte.

### B. Eckdaten des bereinigten Mitschnitts

| Merkmal | Beobachteter Wert |
| --- | --- |
| Aufnahmezeitraum | 20.10.2026, **03:20:43.564 bis 03:24:04.743** (Anzeigezeit des Mitschnitts) |
| Umfang der **bereinigten** Datei | **86 Frames**, 6.582 Bytes im PCAP-Format |
| Ethernet-Kapselung | **IEEE 802.1Q, VLAN-ID 1002**, in allen 86 exportierten Frames |
| PPPoE-Discovery-EtherType | `0x8863` |
| PPPoE-Session-EtherType | `0x8864` |
| Teilnehmer/WAN-MAC | `00:0f:15:d5:f2:bf` |
| Bevorzugter PPPoE-Server (AC) | `NMS1-ASR1`, MAC `00:a7:42:16:36:ec` |
| Zweiter antwortender AC | `NMS1-ASR2`, MAC `00:a7:42:16:04:9a` |
| Host-Uniq während Discovery | `0x0000028d` |
| Ausgehandelte neue PPPoE-Session-ID | `0xf170` |
| Vorherige, beim Start noch sichtbare Session-ID | `0xf0aa` |
| LCP-MRU | **1492 Byte** auf beiden Seiten |
| Authentifizierung | **CHAP mit MD5**; in LCP ausgehandelt, Ablauf im separaten Screenshot bestätigt |
| WAN-IPv4 | `89.57.85.200` (dynamisch über IPCP) |
| PPP-Gegenstelle (Peer) | `185.12.88.132` |
| Primärer DNS | `185.12.89.40` |
| Sekundärer DNS | `185.12.88.177` |

**Paketverteilung der bereinigten Datei:** 1 × PADI, **2 × PADO**, 1 × PADR, 1 × PADS, 1 × PADT, **73 × LCP** und **7 × IPCP** = **86 Frames**. Es gibt darin **keine PAP-, CHAP- oder eigentlichen IP-Nutzdatenframes**, weil diese beim Export ausgeschlossen wurden. Die Zahlen beschreiben daher **nicht** die Paketverteilung des unbereinigten Netzverkehrs.

### C. Alte Sitzung wird beendet – Frames 1–11

Die Aufzeichnung beginnt **nicht** mit einer neuen PPPoE-Einwahl, sondern mit dem Ende einer bereits bestehenden PPPoE-Sitzung `0xf0aa`:

| Frame | Uhrzeit | Ereignis |
| --- | --- | --- |
| 1–4 | 03:20:43–03:20:44 | LCP Echo-Request und Echo-Reply in beiden Richtungen |
| 5–8 | 03:20:53–03:21:23 | Weitere Echo-Requests des Servers; in der gefilterten Datei keine zugehörigen Antworten sichtbar |
| 9 | 03:21:32.945 | LCP Terminate-Request vom Server |
| 10 | 03:21:33.048 | **PPPoE PADT** vom Server, Session `0xf0aa` |
| 11 | 03:21:33.093 | IPCP Terminate-Request vom Server (noch der alten Session zugeordnet) |

Die Reihenfolge zeigt einen Session-Abbau **vor** der neuen Einwahl. Ob der Icotera-Neustart, eine Leitungstrennung oder ein weiterer Anlass diesen Abbau ausgelöst hat, geht aus diesen Frames allein nicht hervor. Ebenso lässt sich aus dem gefilterten Mitschnitt **kein Providerproblem** ableiten.

### D. PPPoE Discovery – Frames 12–16

| Frame | Zeit | Richtung | Nachricht und Inhalt |
| --- | --- | --- | --- |
| **12** | 03:21:41.255411 | Icotera → Broadcast | **PADI**, Service-Name leer, Host-Uniq `0x0000028d` |
| **13** | 03:21:41.259918 | AC1 → Icotera | **PADO**, AC-Name **`NMS1-ASR1`**, Service-Name leer |
| **14** | 03:21:41.260220 | Icotera → AC1 | **PADR** an `00:a7:42:16:36:ec`; AC1 wird gewählt |
| **15** | 03:21:41.260522 | AC2 → Icotera | Zweites **PADO** von **`NMS1-ASR2`**, nach dem PADR für AC1 |
| **16** | 03:21:41.603669 | AC1 → Icotera | **PADS**, Session-ID **`0xf170`** zugewiesen |

**Zeitliche Einordnung:**

- PADI → erstes PADO: ca. **4,5 ms**.
- PADI → zweites PADO: ca. **5,1 ms**.
- Erstes PADO → PADR: ca. **0,3 ms**.
- PADR → PADS: ca. **343 ms**.

Die geringe Antwortzeit des ersten AC belegt, dass der Provider den **korrekt übertragenen und getaggten** PADI des Icotera unmittelbar erreicht und darauf antwortet. Ein spezieller PPPoE-Service-Name ist in diesem erfolgreichen Verbindungsaufbau **nicht erforderlich**; das Service-Name-Tag ist leer. Die beiden PADO-Angebote zeigen zugleich, dass mehrere Access-Concentrators erreichbar sind. Die Reihenfolge der Angebote erklärt, weshalb der Icotera bereits AC1 auswählt, bevor AC2 antwortet.

Die Session-ID `0xf170` gilt nur für **diese** Einwahl und darf nicht als statischer Konfigurationswert übernommen werden.

### E. LCP-Aushandlung – Frames 17–23

Mit dem neuen Session-Identifier `0xf170` folgt die PPP-Link-Control-Aushandlung:

| Frame | Ereignis | Wesentliche Daten |
| --- | --- | --- |
| **17** | Configure-Request von AC1 | MRU **1492**; **Authentication Protocol CHAP (`0xc223`), Algorithmus MD5 (`0x05`)**; Magic-Number |
| **18** | Configure-Request vom Icotera | MRU **1492**, Magic-Number |
| **19** | Configure-Ack von AC1 | Bestätigt die Icotera-Optionen aus Frame 18 |
| **20** | Weiterer Configure-Request von AC1 | Erneut MRU 1492, CHAP-MD5 und Magic-Number |
| **21** | Configure-Ack vom Icotera | Bestätigt den AC-Request aus Frame 20 |
| **22–23** | Echo-Request und Echo-Reply | LCP-Erreichbarkeit nach der Aushandlung |

Die LCP-MRU von **1492** ist typisch für PPPoE: Von den üblichen **1500 Byte Ethernet-MTU** werden **8 Byte für PPPoE- und PPP-Header** benötigt, sodass maximal **1492 Byte für PPP-Nutzdaten** bleiben. Hier ist **MRU** im Mitschnitt belegt; die IP-MTU muss dazu passend konfiguriert werden.

Frame **25** enthält außerdem einen **LCP Protocol-Reject** (`Code 8`) von AC1. Er wird getrennt von der erfolgreichen LCP-Konfiguration beobachtet. Der zurückgewiesene PPP-Protokollwert beginnt mit `0x80fd`; ohne weitere Pakete lässt sich daraus **kein Verbindungsfehler** ableiten, denn IPCP wird anschließend erfolgreich abgeschlossen.

### F. CHAP-Authentifizierung – im Original sichtbar, aus der bereinigten PCAP entfernt

In der **ursprünglichen** Wireshark-Aufzeichnung waren drei CHAP-Nachrichten sichtbar:

| Ursprüngliche Frame-Nummer | Richtung | Nachricht |
| --- | --- | --- |
| **169** | Provider → Icotera | **CHAP Challenge** |
| **170** | Icotera → Provider | **CHAP Response** (enthält Benutzerkennung und einen berechneten Antwortwert) |
| **172** | Provider → Icotera | **CHAP Success** |

Damit ist die **erfolgreiche CHAP-Authentifizierung** im Originalmitschnitt bzw. Screenshot belegt. Die exportierte Datei mit 86 Frames enthält diese Nachrichten absichtlich **nicht**; dort lässt sich nur aus den LCP-Optionen erkennen, dass CHAP-MD5 verlangt wurde. Die Authentifizierungsantwort ist **nicht das Klartextpasswort**, kann aber in Kombination mit der Challenge für Offline-Passwortversuche relevant sein.

**Keine Benutzernamen, Kennwörter, Challenge-Werte oder CHAP-Response-Bytes in das öffentliche Repository oder in Debug-Ausgaben aufnehmen.** Ein Anzeigefilter auf `pppoed || lcp || ipcp` mit **Export Specified Packets → Displayed** ermöglicht für diese Analyse einen Export ohne CHAP-Nachrichten.

### G. IPCP: Dynamische IPv4-Adresse, Gegenstelle und DNS – Frames 24–30

| Frame | Richtung | IPCP-Nachricht | Inhalt |
| --- | --- | --- | --- |
| **24** | Icotera → AC1 | Configure-Request | Gewünschte IP **`0.0.0.0`**, primärer und sekundärer DNS jeweils **`0.0.0.0`** |
| **26** | AC1 → Icotera | Configure-Request | Adresse der PPP-Gegenstelle **`185.12.88.132`** |
| **27** | AC1 → Icotera | Configure-Nak | Angebotene Client-IP **`89.57.85.200`**, DNS **`185.12.89.40`** und **`185.12.88.177`** |
| **28** | Icotera → AC1 | Configure-Ack | Bestätigt Peer-Adresse `185.12.88.132` |
| **29** | Icotera → AC1 | Neuer Configure-Request | Übernimmt die vom Provider angebotene Client-IP und DNS-Adressen |
| **30** | AC1 → Icotera | Configure-Ack | Bestätigt Client-IP und DNS; **IPCP abgeschlossen** |

Das wichtige Detail: **`0.0.0.0` ist hier kein Fehler.** Der Icotera fragt eine dynamische Adresszuweisung an; der Provider schlägt per Configure-Nak konkrete Werte vor, die der Icotera anschließend akzeptiert. Die WAN-IP ist damit **nicht** als statische Adresse am Router einzutragen.

Die Icotera-WAN-Statusseite zeigte passend dazu:

```text
IP-Art:             PPPoE
IP-Adresse:         89.57.85.200
Subnetzmaske:       255.255.255.255
Standard-Gateway:   185.12.88.132
DNS:                185.12.89.40 / 185.12.88.177
MAC-Adresse (WAN):  00:0f:15:d5:f2:bf
WAN-Port:           verbunden / 1000 Mbit/s Full Duplex
Paketfehler:        0
Kollisionen:        0
```

Die Maske **/32 (`255.255.255.255`)** ist bei einem PPP-Punkt-zu-Punkt-Link plausibel. Die PPP-Gegenstelle wird als Gateway angezeigt, auch wenn sie nicht in einem klassischen gemeinsamen Ethernet-/IPv4-Subnetz liegt.

**Frame 25** ist ein LCP Protocol-Reject zwischen den ersten IPCP-Paketen; er hat den erfolgreichen Abschluss in Frames 26–30 nicht verhindert.

### H. Laufender Betrieb und Echo-Pakete – Frames 31–86

Nach dem erfolgreichen IPCP-Abschluss enthält die bereinigte Aufzeichnung zahlreiche **LCP Echo-Requests (Code 9)** und **Echo-Replies (Code 10)** zwischen Icotera und AC1, abwechselnd in beide Richtungen. Das dient der Kontrolle, ob der bestehende PPP-Link noch erreichbar ist.

Die wiederkehrenden Echo-Paare belegen in dieser Aufnahme, dass die PPP-Verbindung **nach der Einrichtung weiter aktiv** war und dass das Mirroring **auch Unicast-Session-Frames in beide Richtungen** an die CPU liefert. Aus der gefilterten Datei lässt sich dagegen weder der eigentliche Internetdurchsatz noch die Vollständigkeit des gesamten Ethernetverkehrs bestimmen.

### I. Abweichende System- und WLAN-LEDs des Icotera

Eine separate, reproduzierte Beobachtung während des Tests: **System-LED (oben) und Wi-Fi-LED (unten) leuchteten dauerhaft rot**, die mittlere **Voice-LED** blieb aus. Laut der am Gerät verwendeten Anleitung bedeuten die roten System- und Wi-Fi-Anzeigen eine nicht verfügbare Internetverbindung. **Tatsächlich funktionierten jedoch sowohl die PPPoE-Verbindung als auch das Internet über WLAN.**

Die LED-Farben stimmen somit **nicht** mit dem funktionierenden Datenpfad überein. Mögliche, aber **nicht bewiesene** Erklärungen sind ein separater Provider-/Connectivity-Check oder die fehlende Weiterleitung anderer Ethernet-Protokolle/VLANs durch die nur für VLAN 1002 konfigurierte Zwischenschaltung. Die PPPoE-PCAP **belegt keine Ursache für die roten LEDs**. Die ausgeschaltete Voice-LED ist ohne aktivierten Telefondienst kein eigenständiger Befund.

### J. Schlussfolgerungen für den Asus als eigenständigen PPPoE-Router

**Belegt durch Mitschnitt und Hardwaretest:**

- Der funktionierende Icotera nutzt **PPPoE auf getaggtem VLAN 1002**, mit leerem Service-Name-Tag und **MRU 1492**.
- Der Provider bietet **mindestens zwei PPPoE-Access-Concentrators** an; AC1 antwortete in diesem Test innerhalb von ca. 4,5 ms.
- Der Icotera nutzt tatsächlich die **WAN-MAC `00:0f:15:d5:f2:bf`**, nicht die zunächst irrtümlich geklonte `…:bd`.
- Der Provider fordert **CHAP-MD5** und die Anmeldung war laut Original-Screenshot erfolgreich.
- Die IPv4-Adresse, PPP-Gegenstelle und DNS-Werte werden **dynamisch über IPCP** verhandelt.
- Hardware-VLAN `0t 4t 8t` und das aktivierte Port-Mirroring ermöglichen **einen belegbar beidseitigen Mitschnitt** auf `eth0`.

**Noch nicht durch diesen Vergleich bewiesen:**

- Ob der ursprüngliche Asus-PPPoE-Fehler **allein** durch die fehlende Hardware-VLAN-1002-Freigabe, durch die falsche geklonte MAC oder durch eine Kombination verursacht wurde.
- Ob der Asus nach der Umstellung vom transparenten Switch zum PPPoE-Router mit identischen Einstellungen bereits selbst erfolgreich einwählen kann.
- Warum die Icotera-LEDs trotz funktionierender Internetverbindung rot bleiben.
- Ob außer VLAN 1002 weitere Providerdienste oder Managementverbindungen notwendig sind.

**Praktische Folgerung:** Bei einem erneuten Einwahltest des Asus zuerst sicherstellen, dass VLAN 1002 **im Hardware-Switch bis zum physikalischen WAN-Port** korrekt übertragen wird, die tatsächliche WAN-MAC bzw. Provider-MAC-Policy beachtet wird und der PPPoE-Client auf dem richtigen VLAN-Interface arbeitet. Der Mitschnitt eines lokal ausgehenden PADI alleine genügt nicht als Nachweis, dass das Paket den WAN-Port tatsächlich korrekt verlassen hat.

### K. Wireshark-Filter zur Nachprüfung

| Zweck | Wireshark-Anzeigefilter |
| --- | --- |
| Alle PPPoE-Pakete | `pppoed || pppoes` |
| Discovery (PADI/PADO/PADR/PADS/PADT) | `pppoed` |
| LCP-Aushandlung und Echo-Pakete | `lcp` |
| IPv4-/DNS-Verhandlung | `ipcp` |
| CHAP-Aushandlung **nur in der Original-PCAP** | `chap` |
| Als bereinigte Arbeitskopie exportieren | `pppoed || lcp || ipcp` |

**Hinweis zur Nachvollziehbarkeit:** Die hier referenzierte gefilterte PCAP wurde **nicht** im öffentlichen Repository abgelegt, da auch Netzwerk-Metadaten personenbezogen oder sicherheitsrelevant sein können. Frame-Nummern und konkrete IP-Adressen beschreiben einen Einzeltest und sind keine dauerhaft gültigen Provider-Konfigurationswerte.

---

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
