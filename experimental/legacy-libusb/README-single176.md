# KORE 1 — isolierter 176-Byte-Einkanaltest (experimentell)

Basierend auf der vom Nutzer bereitgestellten Sweep-Version. Vierkanal-Sweep bleibt erhalten.

Build (macOS):
```sh
cc -std=c11 -Wall -Wextra -O2 -o kore1-bridge-single176 kore1-bridge-single176.c $(pkg-config --cflags --libs libusb-1.0) -framework CoreMIDI -framework CoreFoundation -lm -lpthread
```

Aufnahme mit MAYA44 wie bisher starten, dann (bei sehr niedrigem Pegel):
```sh
./kore1-bridge-single176 --audio-single-176 1
```

Die 1 durch 2, 3 oder 4 ersetzen; jeden Kanal getrennt aufnehmen. 1 s Stille, 1,2 s 440-Hz-Testsignal, dann Stille. Tonamplitude 2048/8388607 (~-72 dBFS); 176 Byte je OUT-Mikroframe.

WICHTIG: Die Framing-Hypothese ist noch NICHT als Linux-paritätisch nachgewiesen. 176 Bytes entsprechen 22 Acht-Byte-Gruppen mit insgesamt 44 PCM-Bytes pro Stream; bei 8000 Mikroframes/s sind das rechnerisch ~29,33 k Stereo-Frames/s pro Stream. Der 440-Hz-Phaseninkrement wurde für diesen Test entsprechend angepasst, dies ist aber keine Lösung des grundlegenden Rate-Mismatch. Die Linux-konforme RX-Längensteuerung und tatsächliche Stream-Anordnung müssen noch unabhängig geprüft werden.

Keine Kopfhörer tragen; keine Lautsprecher aktivieren. Bei Übersteuerung sofort stoppen.
