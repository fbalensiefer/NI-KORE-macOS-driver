# Projekt: Native macOS-Unterstützung für den Native Instruments KORE 1

## 1. Projektziel

Ich möchte eine native macOS-Unterstützung für den **Native Instruments KORE 1 Controller** entwickeln.

Das Ziel ist, den KORE 1 unter aktuellen macOS-Versionen wieder vollständig nutzbar zu machen, obwohl Native Instruments keinen aktuellen offiziellen Treiber mehr bereitstellt.

Langfristig soll eine native Lösung entstehen, die:

- den KORE 1 zuverlässig über USB erkennt und initialisiert,
- sämtliche Bedienelemente ausliest,
- Tasten, Encoder, Drehregler und Joystick korrekt interpretiert,
- die Eingaben als MIDI-Events über CoreMIDI bereitstellt,
- möglichst ohne zusätzliche Drittanbieter-Treiber funktioniert,
- unter aktuellen macOS-Versionen stabil läuft.

Die Entwicklung soll sich möglichst eng am vorhandenen Linux-Treiber `snd-usb-caiaq` orientieren.

**Wichtig:** Zunächst muss die USB-Kommunikation vollständig verstanden und stabil implementiert werden. Erst danach sollte die CoreMIDI-Integration erfolgen.

---

## 2. Hardware und USB-Identifikation

Gerät: Native Instruments KORE 1

USB-Kennung:

- Vendor ID: `0x17CC`
- Product ID: `0x4711`

Die Kommunikation über libusb funktioniert grundsätzlich.

Folgende USB-Konfiguration wurde bereits erfolgreich identifiziert:

| Eigenschaft | Wert |
|---|---|
| Interface | `0` |
| Alternate Setting | `1` |
| Bulk OUT Endpoint | `0x01` |
| Bulk IN Endpoint | `0x81` |

Die relevante Kommunikation läuft über Interface 0, Alternate Setting 1.

Für den KORE 1 ist Endpoint `0x81` der relevante Bulk-IN-Endpunkt. Andere Endpoints, die im Linux-Treiber für andere Geräte der CAIAQ-Familie verwendet werden, dürfen nicht ohne Prüfung auf den KORE 1 übertragen werden.

---

## 3. Bereits erfolgreich getestete USB-Kommunikation

Ein kleines Testprogramm namens `kore-init-test` konnte das Gerät öffnen und erfolgreich den Geräteinformationsbefehl senden.

Gesendeter Befehl:

```c
unsigned char cmd[] = {0x01};
```

Empfangene Antwort:

```text
01 29 00 02 08 03 20 00 04 02 00 00 01 01 02
```

Die Antwort hat eine Länge von 15 Bytes.

Der Befehl `0x01` entspricht `GET_DEVICE_INFO`.

Damit ist nachgewiesen:

- Das Gerät wird unter macOS erkannt.
- libusb kann auf das Gerät zugreifen.
- Interface 0 lässt sich verwenden.
- Alternate Setting 1 lässt sich aktivieren.
- Bulk-OUT-Kommunikation funktioniert.
- Bulk-IN-Kommunikation funktioniert.
- Der KORE 1 antwortet auf mindestens einen Steuerbefehl.

---

## 4. AUTO_MSG – Aktivierung der automatischen Nachrichtenübertragung

Ein weiterer wichtiger Schritt war die Aktivierung der automatischen Nachrichtenübertragung.

Bisher verwendeter Befehl:

```c
unsigned char auto_msg[] = {0x0B, 0x01, 0x0A, 0x05};
```

Dieser wird über Bulk OUT Endpoint `0x01` gesendet.

Die Linux-Implementierung bestätigt, dass `AUTO_MSG` die Befehls-ID `0x0B` verwendet und drei Parameter übermittelt:

```c
char tmp[3] = { digital, analog, erp };
```

Der Linux-Treiber sendet somit:

```text
0B [digital] [analog] [erp]
```

Das ist eine gesicherte Erkenntnis aus dem Linux-Quellcode.

**Noch nicht abschließend geklärt ist, welche konkreten Parameterwerte beim KORE 1 welche Datenströme aktivieren.** Insbesondere darf nicht ohne weitere Prüfung angenommen werden, dass die Parameter einfach als Bitmasken funktionieren.

Die zuvor vorgeschlagene alternative Konfiguration

```c
unsigned char auto_msg[] = {0x0B, 0x01, 0x01, 0x01};
```

war lediglich ein diagnostischer Testvorschlag. Sie ist noch nicht als korrekte oder endgültige Konfiguration bestätigt.

---

## 5. Linux-Referenztreiber

Die wichtigste Referenz für das Projekt ist der Linux-Kernel-Treiber:

` sound/usb/caiaq/ `

Insbesondere relevant sind:

- `device.c`
- `input.c`
- `device.h`

Der Treiber unterstützt verschiedene Native-Instruments-Geräte aus der CAIAQ-Familie.

### Relevante Befehls-IDs

| ID | Bezeichnung | Funktion |
|---|---|---|
| `0x01` | GET_DEVICE_INFO | Geräteinformationen abrufen |
| `0x02` | READ_ERP | ERP-Eingabedaten |
| `0x03` | READ_ANALOG | Analoge Eingabedaten |
| `0x04` | READ_IO | Digitale Eingabedaten |
| `0x05` | WRITE_IO | Digitale Ausgänge steuern |
| `0x06` | MIDI_READ | MIDI-Daten lesen |
| `0x07` | MIDI_WRITE | MIDI-Daten schreiben |
| `0x08` | LCD | LCD-Funktion |
| `0x09` | AUDIO_PARAMS | Audio-Parameter |
| `0x0B` | AUTO_MSG | Automatische Nachrichten aktivieren |
| `0x0C` | DIMM_LEDS | LEDs dimmen |

Die Befehle werden über das CAIAQ-Protokoll übertragen. Bei der hier untersuchten KORE-1-Konfiguration läuft die relevante Eingabekommunikation über Bulk IN `0x81`.

### Wichtige Linux-Implementierung

Die Funktion `snd_usb_caiaq_set_auto_msg()` in `device.c` baut die drei Parameter wie folgt auf:

```c
char tmp[3] = { digital, analog, erp };
return snd_usb_caiaq_send_command(cdev, EP1_CMD_AUTO_MSG, tmp, sizeof(tmp));
```

Der Linux-Treiber konfiguriert außerdem Interface 0 mit Alternate Setting 1.

**Wichtiger nächster Schritt:** Die Initialisierungsroutine in `input.c` vollständig untersuchen. Insbesondere muss festgestellt werden, welche AUTO_MSG-Werte der Linux-Treiber für den KORE 1 tatsächlich verwendet. Die gerätespezifische Initialisierung ist entscheidend, um die korrekte Konfiguration unter macOS nachzubilden.

---

## 6. KORE-1-spezifische Datenformate aus dem Linux-Treiber

Die bisherige Analyse des Linux-Treibers hat bereits wichtige Informationen zur Interpretation der Eingabedaten geliefert.

### 6.1 READ_ERP – Befehl `0x02`

Der KORE-1-Treiber erwartet mindestens 16 Payload-Bytes.

Der Linux-Treiber verwendet folgende Reihenfolge für die ERP-Dekodierung:

```c
decode_erp(buf[7],  buf[5]);
decode_erp(buf[12], buf[14]);
decode_erp(buf[15], buf[13]);
decode_erp(buf[0],  buf[2]);
decode_erp(buf[3],  buf[1]);
decode_erp(buf[8],  buf[10]);
decode_erp(buf[11], buf[9]);
decode_erp(buf[4],  buf[6]);
```

Dabei ist zu beachten:

- `buf` bezeichnet im Linux-Treiber den Payload ohne das erste Command-Byte.
- Im rohen USB-Paket liegt der Payload ab `raw[1]`.
- Die Werte müssen deshalb bei einer Implementierung mit dem vollständigen USB-Paket entsprechend um eins versetzt adressiert werden.

Das bedeutet: Der KORE 1 liefert acht ERP-Werte, die aus jeweils zwei Bytes dekodiert werden.

Der Linux-Treiber wandelt diese Werte in einen Wertebereich von ungefähr 0 bis 999 um.

Die tatsächliche Bedeutung und Zuordnung der acht Werte zu den physischen Bedienelementen muss durch kontrollierte Tests ermittelt werden.

### 6.2 READ_ANALOG – Befehl `0x03`

Der Linux-Treiber erwartet beim KORE 1 drei 16-Bit-Werte.

Diese werden Big-Endian dekodiert:

```c
value1 = (buf[0] << 8) | buf[1];
value2 = (buf[2] << 8) | buf[3];
value3 = (buf[4] << 8) | buf[5];
```

Auch hier ist `buf` der Payload ohne das Command-Byte.

### 6.3 READ_IO – Befehl `0x04`

Der KORE-1-Treiber interpretiert die ersten vier Payload-Bytes als 32-Bit-Bitfeld.

Damit lassen sich 32 digitale Zustände abbilden.

Die fünfte Payload-Position wird für einen weiteren Wert verwendet:

```c
ABS_MISC = 255 - buf[4];
```

Der Linux-Treiber ordnet die digitalen Zustände den Tasten und den Touch-Sensoren der Bedienelemente zu.

Für den KORE 1 gibt es laut Linux-Treiber eine Zuordnung für:

- 32 Tasten
- 8 touch-sensitive Regler

Die genaue Zuordnung sollte aus `input.c` übernommen werden und nicht durch Vermutungen ersetzt werden.

---

## 7. Aktueller macOS-Monitor

Es existiert bereits eine Datei:

```text
~/kore-monitor.c
```

Dieses C-Programm verwendet libusb und arbeitet asynchron.

Die bisher implementierten Funktionen:

1. Gerät anhand VID/PID öffnen.
2. Interface 0 beanspruchen.
3. Alternate Setting 1 aktivieren.
4. Asynchronen Bulk-IN-Transfer auf Endpoint `0x81` starten.
5. GET_DEVICE_INFO (`0x01`) senden.
6. AUTO_MSG (`0x0B`) senden.
7. Empfangene USB-Pakete mit Zeitstempel und Hex-Daten ausgeben.
8. Nach jedem Empfang den nächsten asynchronen Transfer einreichen.
9. Bei Ctrl+C laufende Transfers abbrechen und das Interface freigeben.

Das Programm ist damit bereits ein funktionierender Ausgangspunkt für die Protokollanalyse.

---

## 8. Letzter umfangreicher USB-Test

Ich habe einen umfangreichen Monitor-Log hochgeladen.

Der vollständige Log wurde programmatisch ausgewertet.

Datei:

```text
Eingefügtes Markdown(1).md
```

Umfang:

- 122.679 Zeichen
- 1.853 Zeilen

### Ergebnis der Paketanalyse

| Pakettyp | Anzahl | Länge |
|---|---:|---:|
| `0x01` | 1 | 15 Bytes |
| `0x0B` | 1 | 1 Byte |
| `0x02` | 873 | 33 Bytes |
| `0x03` | 0 | – |
| `0x04` | 0 | – |

Die Pakete mit `0x02` waren kontinuierlich vorhanden.

Beispiel aus dem Log:

```text
[14:56:45.807] IN 0x81 | 33 bytes |
02 36 10 58 A7 FF B2 6B FF 3F 29 3C 64 FF 0B 6B 7E 00 00 ...
```

### Gesicherte Schlussfolgerungen

1. Die Kommunikation über Bulk IN `0x81` funktioniert.
2. Der KORE 1 sendet nach der Initialisierung kontinuierlich `READ_ERP`-Pakete.
3. Es wurden 873 Pakete des Typs `0x02` empfangen.
4. Die Pakete haben eine Länge von 33 Bytes.
5. In den Daten gibt es erkennbare Variationen einzelner Bytes.
6. Es wurden in diesem Mitschnitt keine Pakete vom Typ `0x03` oder `0x04` beobachtet.

### Was daraus noch nicht geschlossen werden darf

Der Log beweist nicht, dass alle Eingabeströme aktiviert sind.

Insbesondere ist noch nicht geklärt:

- Warum ausschließlich `READ_ERP` übertragen wird.
- Welche AUTO_MSG-Konfiguration die anderen Eingabeströme aktiviert.
- Ob die beobachteten ERP-Änderungen durch konkrete physische Bedienhandlungen verursacht wurden.
- Welche ERP-Kanäle welchen Bedienelementen entsprechen.

Die im Log beobachteten Variationen sind ein guter Ausgangspunkt für die weitere Analyse, reichen aber nicht aus, um eine vollständige Zuordnung zu erstellen.

---

## 9. Bisheriger technischer Stand

| Bereich | Status |
|---|---|
| USB-Geräteerkennung | Erfolgreich |
| libusb-Zugriff unter macOS | Erfolgreich |
| Interface 0 | Erfolgreich |
| Alternate Setting 1 | Erfolgreich |
| Bulk OUT `0x01` | Erfolgreich |
| Bulk IN `0x81` | Erfolgreich |
| GET_DEVICE_INFO | Erfolgreich |
| AUTO_MSG | Befehl erfolgreich gesendet |
| READ_ERP | 873 Pakete empfangen |
| READ_ANALOG | Noch nicht nachgewiesen |
| READ_IO | Noch nicht nachgewiesen |
| ERP-Dekodierung | Linux-Algorithmus bekannt, macOS-Implementierung offen |
| Zuordnung der Eingabekanäle | Offen |
| MIDI-Ausgabe über CoreMIDI | Noch nicht implementiert |
| Native macOS-App | Noch nicht implementiert |
| Stabilität / Langzeittest | Noch offen |

### Phase 1: Linux-Initialisierung vollständig verstehen

Untersuche den Linux-Quellcode, insbesondere:

- `sound/usb/caiaq/input.c`
- `sound/usb/caiaq/device.c`
- `sound/usb/caiaq/device.h`

Ziele:

1. Die KORE-1-spezifische Initialisierung finden.
2. Die tatsächlichen AUTO_MSG-Parameter des Linux-Treibers für KORE 1 bestimmen.
3. Verstehen, wann `READ_ERP`, `READ_ANALOG` und `READ_IO` aktiviert werden.
4. Die Linux-Eingabeverarbeitung vollständig nachvollziehen.
5. Die KORE-1-spezifischen Zuordnungen von Tasten, Touch-Sensoren und analogen Eingängen dokumentieren.

Bitte keine Protokollwerte erfinden. Wenn die Linux-Implementierung keine eindeutige Antwort liefert, muss dies als offene Frage gekennzeichnet werden.

### Phase 2: USB-Monitor verbessern

Entwickle eine verbesserte Version von `kore-monitor.c`.

Anforderungen:

- Asynchrone USB-Kommunikation beibehalten.
- Alle empfangenen Pakettypen erkennen.
- Pakete nach Command-ID unterscheiden.
- Paketlängen protokollieren.
- READ_ERP dekodieren.
- READ_ANALOG dekodieren.
- READ_IO dekodieren.
- Änderungen der einzelnen Kanäle übersichtlich darstellen.
- Unveränderte Werte nicht permanent wiederholen.
- Rohdaten weiterhin optional ausgeben können.
- Fehler und USB-Timeouts sauber behandeln.
- Ctrl+C soll weiterhin einen sauberen Abbruch ermöglichen.

Ideal wäre ein Ausgabeformat wie:

```text
[14:30:01.123] READ_ERP
ERP 1: 512
ERP 2: 498
ERP 3: 999
ERP 4: 120
ERP 5: 340
ERP 6: 700
ERP 7: 210
ERP 8: 800

[14:30:01.456] READ_IO
KEY 01: PRESSED
KEY 02: RELEASED
```

Die konkrete Zuordnung der Kanäle muss zunächst neutral bleiben, bis sie experimentell bestätigt wurde.

### Phase 3: Kontrollierte Hardwaretests

Anschließend sollen die Bedienelemente einzeln getestet werden.

Vorgeschlagene Reihenfolge:

1. Alle Regler in Ruheposition lassen.
2. Einen einzelnen Regler bewegen.
3. ERP-Werte vergleichen.
4. Regler wieder zurückstellen.
5. Nächsten Regler bewegen.
6. Dasselbe mit Joystick, Tasten und Touch-Funktionen durchführen.
7. Die Ergebnisse dokumentieren.

Ziel ist eine eindeutige Tabelle:

| Physisches Bedienelement | USB-Pakettyp | Kanal / Bit | Wertebereich |
|---|---|---|---|
| Regler 1 | Noch zu bestimmen | Offen | Offen |
| Regler 2 | Noch zu bestimmen | Offen | Offen |
| Joystick X | Noch zu bestimmen | Offen | Offen |
| Joystick Y | Noch zu bestimmen | Offen | Offen |
| Taste 1 | Noch zu bestimmen | Offen | Offen |

### Phase 4: CoreMIDI-Integration

Erst wenn die Eingabedaten zuverlässig dekodiert sind:

- CoreMIDI-Client erstellen.
- Virtuellen MIDI-Ausgang bereitstellen.
- Reglerbewegungen in MIDI-CC-Nachrichten übersetzen.
- Tasten als Note-On/Note-Off oder konfigurierbare MIDI-Events ausgeben.
- Joystick-Achsen als MIDI-CC ausgeben.
- Touch-Sensoren und weitere Funktionen berücksichtigen.
- MIDI-Kanal und CC-Zuordnung konfigurierbar machen.

### Phase 5: Native macOS-Anwendung

Langfristig soll eine eigenständige macOS-Anwendung entstehen.

Denkbare Architektur:

- USB-Kommunikationsmodul auf Basis von libusb oder einer geeigneten nativen macOS-USB-Schnittstelle.
- Protokollparser für die CAIAQ-Kommandos.
- KORE-1-spezifische Input-Abstraktion.
- CoreMIDI-Ausgabe.
- Konfigurationsoberfläche.
- Statusanzeige für Verbindung und Eingabekanäle.
- Optional automatischer Start bzw. automatische Geräteerkennung.

Ein Kernel Extension Driver sollte nach Möglichkeit vermieden werden. Eine User-Space-Lösung ist vorzuziehen, sofern sie die erforderliche Funktionalität zuverlässig abbilden kann.

### Besonders wichtig

1. **Keine unbelegten Annahmen:** Unterscheide klar zwischen gesicherten Erkenntnissen, Hypothesen und offenen Fragen.
2. **Linux als Referenz:** Nutze den Linux-Treiber als primäre technische Referenz.
3. **KORE-1-spezifisch arbeiten:** Nicht unbesehen Annahmen anderer CAIAQ-Geräte übernehmen.
4. **Vollständige Dateien liefern:** Wenn du C-Code änderst, liefere grundsätzlich den vollständigen Inhalt der Datei.
5. **Terminalbefehle direkt nutzbar machen:** Gib konkrete Copy-and-paste-Befehle für macOS.
6. **Kleine Testschritte:** Nach jeder Änderung soll ein klarer Test erfolgen.
7. **Keine vorschnelle CoreMIDI-Implementierung:** Erst USB-Daten vollständig verstehen und dekodieren.
8. **Bestehende funktionierende Kommunikation nicht zerstören:** Die aktuelle libusb-Kommunikation ist bereits ein wichtiger Fortschritt.
9. **Keine erfundenen Dateien oder Downloadlinks:** Wenn du eine Datei erstellst, muss sie tatsächlich vorhanden und überprüfbar sein.
10. **Fehleranalyse anhand realer Logs:** Bei neuen Logs die tatsächlichen Pakettypen, Längen und Datenänderungen systematisch analysieren.
