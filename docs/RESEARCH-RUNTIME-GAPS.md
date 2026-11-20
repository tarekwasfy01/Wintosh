# Runtime-Recherche und offene Lücken

Stand: 20. November 2026

## Ergebnis in Kürze

Wintosh kann bereits synthetische Mach-O-Fixtures laden und einen begrenzten
Darwin-Startup-Pfad demonstrieren. Das ist noch keine vollständige macOS-
Runtime. Darling selbst besteht aus mehreren Schichten: Mach-O-Loader,
`dyld`-Integration, Darwin-/POSIX-Syscalls, Mach-IPC und nachgebildeten
Frameworks. Eine Windows-Portierung muss diese Schichten deshalb getrennt
und mit klaren ABI-Grenzen erweitern.

Seit der vorherigen Bestandsaufnahme existiert ein begrenzter `NSRunLoop`-
Bridge mit mode-bewusster Callback-Warteschlange und AppKit-Main-Loop-Pump.
Apples Dokumentation macht klar, dass ein vollständiges Modell zusätzlich
quell-, timer- und observerbasierte Eingaben pro Modus sowie rekursive
Aktivierungen verlangt; die aktuelle Bridge deckt diese Verträge noch nicht ab.
Referenzen: [Foundation `run(mode:before:)`](https://developer.apple.com/documentation/foundation/runloop/run%28mode%3Abefore%3A%29),
[CoreFoundation `CFRunLoop`](https://developer.apple.com/documentation/corefoundation/cfrunloop),
[`CFRunLoopPerformBlock`](https://developer.apple.com/documentation/corefoundation/cfrunloopperformblock%28_%3A_%3A_%3A%29).

Der Mach-O-Hostresolver exportiert jetzt zusätzlich ausgewählte
`CFString`-Erzeugungs- und Leseoperationen sowie `CFArray`-Typ-, Anzahl- und
Indexabfragen. CFArray unterstützt nun Create/Copy, mutable Create/Copy,
grundlegende Mutation und synchrones ApplyFunction über
`kCFTypeArrayCallBacks` und kopierte Callback-Strukturen; null-Callbacktabellen
werden als leere Version-0-Strukturen behandelt. Retain/Release/Gleichheit
werden gemäß der jeweiligen Array-Tabelle ausgeführt.
String-ABI-Aufrufe akzeptieren nur die implementierte UTF-8-Darstellung.
`CFRunLoopPerformBlock` ist nun angebunden: ein einzelner Modus
oder ein Array von Modi wird kopiert, der Gast-Block bleibt bis zur Ausführung
retained, und die AppKit-Hauptschleife wird aufgeweckt. Ein Längenlimit von
4095 UTF-8-Bytes pro Modus gilt vorläufig. `CFRunLoopSource`/Timer/Observer
und die vollständigen RunLoop-Rückgabestatus bleiben offen. Die ABI-Ausführung
ist noch nicht durch einen Gast-Fixture-Lauf bestätigt.

Die Dictionary-Schicht wurde danach erweitert: `CFDictionaryCreate`, mutable
Erzeugung/Kopie, Set/Add/Replace/Remove sowie synchrone Enumeration sind jetzt
im Resolver. Als Callback-Verträge gelten nur die exportierten
`kCFTypeDictionaryKeyCallBacks` und `kCFTypeDictionaryValueCallBacks` oder
inhaltlich identische Kopien. Apples Dokumentation bestätigt deren Retain/
Release-/Equality-Semantik; benutzerdefinierte und vollständig leere Callback-
Tabellen werden abgewiesen, weil Wintosh deren Ownership- und Hashregeln noch
nicht abbildet. Referenzen: [Apple kCFType key callbacks](https://developer.apple.com/documentation/corefoundation/kcftypedictionarykeycallbacks),
[`CFDictionaryCreateMutable`](https://developer.apple.com/documentation/corefoundation/cfdictionarycreatemutable%28_%3A_%3A_%3A_%3A%29),
und [`CFDictionarySetValue`](https://developer.apple.com/documentation/corefoundation/cfdictionarysetvalue%28_%3A_%3A_%3A%29).

## Externe Referenzen

- [Darling auf GitHub](https://github.com/darlinghq/darling) beschreibt Darling
  als Mach-O-Loader plus Userspace-Kernelserver und Framework-Reimplementierungen.
- [Darling Loader-Dokumentation](https://docs.darlinghq.org/internals/basics/loader.html)
  beschreibt die Übergabe vom Loader an `dyld`.
- [Darling Host-API-Dokumentation](https://docs.darlinghq.org/internals/calling-host-system-apis.html)
  beschreibt die Auflösung von Gast-Symbolen auf Host-Implementierungen.
- [Apple dyld4-Dokumentation](https://github.com/apple-oss-distributions/dyld/blob/main/doc/dyld4.md)
  dokumentiert den Startup-Pfad und die Mach-O-/dyld-Schichten.
- [Apple Open Source](https://opensource.apple.com/) ist die maßgebliche Quelle
  für lizenzkonform nutzbare Apple-Open-Source-Bestandteile.

### TinyOS als Kandidat

[tinyos/tinyos-main](https://github.com/tinyos/tinyos-main) ist ein
eingebettetes Betriebssystem für stromsparende Funk- und Sensorgeräte. Dass
Entwicklungswerkzeuge auf mehreren Host-Betriebssystemen eingesetzt werden
können, bedeutet nicht, dass TinyOS macOS-Binärdateien oder Darwin-APIs
ausführt. Das Repository wurde am 12. April 2026 archiviert; die README nennt
`tinyprod/prod` beziehungsweise `tp-freeforall/prod` als Entwicklungszweig.

| Benötigte Wintosh-Schicht | Beitrag von TinyOS | Bewertung |
|---|---|---|
| Mach-O-Laden und `dyld` | keiner | anderer Binär- und Startpfad |
| Darwin-Syscalls und Mach-IPC | keiner | TinyOS abstrahiert Embedded-Hardware |
| Foundation, AppKit und Cocoa | keiner | keine macOS-Frameworks |
| Windows-GUI und App-Bundles | keiner | Firmware-/Mote-Ziel statt Desktop-App |
| Build- und Portabilitätsmuster | eventuell | nur als allgemeine Toolchain-Referenz |

Damit gilt für die Laufzeit-Portierung: `TinyOS → Wintosh` liefert keine
fehlende Kompatibilitätsschicht. Es wird kein TinyOS-Code in den macOS-
Laufzeitpfad übernommen.

## Befund im aktuellen Wintosh-Code

### Bereits vorhanden

- Mach-O-Header- und Segmentprüfung sowie Mapping ausführbarer Segmente.
- begrenzte Symbol-/Bind-Auflösung über Host-Exports.
- x86_64-Darwin-Startup-Trampoline für den kontrollierten Fixture-Pfad.
- fail-closed Verhalten für echte Mach-O-Ausführung im MSVC-Build.
- synthetische Smoke- und Darwin-Startup-Fixtures.
- erste Foundation-/CoreFoundation-nahe Host-Implementierungen.

### Nach Recherche klar offen

1. **Startup-ABI:** Der Host baut den Initialstack korrekt auf, aber der
   direkte Zugriff eines handkodierten Gast-Fixtures auf spätere Startup-Werte
   ist noch nicht zuverlässig. Das ist ein ABI-/Trampoline-Thema, kein Beweis
   für fehlende `argv`-/`envp`-Daten.
2. **dyld-Semantik:** Lazy Binding, Weak Bindings, Re-Exports, Initializer-
   Reihenfolge und Objective-C-Registrierung fehlen für reale Bundles.
3. **Host-Export-Tabelle:** Die Exporte müssen als nach Familien getrennte,
   ABI-geprüfte Tabelle aus libc, Speicher, Dateien und Threads geführt werden.
4. **Darwin-Datenlayout:** `stat`, `dirent`, `FILE`, TLS/TLV und weitere
   Strukturen müssen exakt vom Darwin-Layout getrennt vom Windows-Layout
   modelliert werden.
5. **Aufrufkonventionen:** Die ARM64-Objective-C-Brücke deckt ausgewählte
   skalare und Aggregate-Signaturen ab; x86_64 hat ausgewählte SysV-Variadic-
   Bridges. Allgemeine Aggregate, Rückrufe Gast→Host und Host→Gast sowie
   Objective-C-Ausnahmen brauchen weiterhin eigene, ABI-geprüfte Bridges.
6. **Fehlerisolierung:** Ein Gastzugriffsfehler muss in einen reproduzierbaren
   Runner-Fehler mit Exit-Code übersetzt werden, ohne Windows-Stack-Unwinding
   durch Gast-Frames zu erzwingen.
7. **Framework- und Bundle-Auflösung:** Für `.app`-Bundles fehlen noch eine
   kontrollierte Pfadauflösung, Framework-Versionen und Ressourcenregeln.
8. **Architekturen:** ARM64-Mach-O erfordert eine eigene Ausführungsschicht;
   x86_64-Code kann nicht automatisch auf einem x86_64-Windows-Host durch
   Windows-ABI-Aufrufe ersetzt werden.

## Konsequenz für die Reihenfolge

Der schnellste belastbare Weg ist nicht das blinde Kopieren von WSL-Code,
sondern eine Fixture-Leiter, die jede ABI-Familie isoliert beweist:

1. Darwin-Startup mit `argv`/`envp`/Apple-Vektoren stabilisieren.
2. libc-/Speicher-Exporte inklusive Guard-Symbolen absichern.
3. Datei-I/O und Host→Gast-Rückruf (`qsort`) hinzufügen.
4. TLS/TLV und `pthread_create` beweisen.
5. Chained Bindings, danach Classic/Lazy Binding implementieren.
6. kontrollierte `.app`-/Framework-Auflösung ergänzen.
7. erst danach Foundation-/Objective-C- und ARM64-Schichten erweitern.

Diese Reihenfolge entspricht der Architektur der Referenzprojekte und hält
den jetzigen synthetischen Fixture-Pfad funktionsfähig, während die reale
Darwin-Kompatibilität schrittweise zunimmt.

## CFSet ABI-Portierung

Die Gast-ABI ergänzt CFSet-Erzeugung, mutable Kopien, Abfragen, Traversierung,
Mutation und Mengenoperationen. `CFEqual`-gleiche Einträge gelten als ein
Element; der aktuelle Vektorpfad ist linear statt hashbasiert. Unterstützt
wird `kCFTypeSetCallBacks`; benutzerdefinierte und leere Callbacktabellen
werden abgewiesen. Das folgt Apples [CFSet-Vertrag](https://developer.apple.com/documentation/corefoundation/cfset),
dem [CFSetCallBacks-Layout](https://developer.apple.com/documentation/corefoundation/cfsetcallbacks)
und [CFSetCreateMutable](https://developer.apple.com/documentation/corefoundation/cfsetcreatemutable%28_%3A_%3A_%3A%29).
