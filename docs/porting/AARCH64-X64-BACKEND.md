# AArch64 Mach-O auf x64 Windows: Backend-Integrationsvertrag

Stand: 2026-11-23. Dieses Dokument beschreibt den nächsten echten
Ausführungspfad für ARM64-Gäste auf x64-Windows. Es behauptet keine bereits
integrierte Emulation.

## Architekturgrenze im aktuellen Quellstand

Der Loader kann ARM64-Mach-O parsen, mappen und einen Teil der ARM64-
Relokationen anwenden. `MachOImage::ExecuteEntry` lehnt ARM64-Gäste auf x64
Windows jedoch explizit ab. Auf nativem Windows ARM64 nimmt der Code nur
passende ARM64-Gäste an. Die beiden Pfade sind getrennt:

| Übergang | x64 Windows | ARM64 Windows |
|---|---:|---:|
| ARM64-Mach-O lesen und CPU-Slice auswählen | vorhanden | vorhanden |
| Segmente mappen und unterstützte Relokationen anwenden | vorhanden | vorhanden |
| ARM64-Instruktionen ausführen | fehlt | native CPU, noch nicht end-to-end belegt |
| Darwin-Imports aus ARM64-Gastcode aufrufen | fehlt | teilweise vorhandene ABI-Brücke |
| Darwin-Systemdienste und GUI aus dem ARM64-Gast bedienen | fehlt | bestehende Windows-Implementierungen, ARM64-Gastbelege fehlen |

Ein CPU-Backend allein schließt somit nicht den Ausführungspfad.

## Kompositionsmodell

Sei `M` der geladene Mach-O-Zustand, `R` der AArch64-Registerzustand, `S`
der Darwin-Dienstzustand und `H` der Windows-Host. Ein ausführbarer Schritt
ist die Komposition

```text
T = E ∘ C ∘ D ∘ L
```

mit:

| Abbildung | Eingabe → Ausgabe | Verantwortung |
|---|---|---|
| `L` | Mach-O-Datei → `M` | Slice, Segmente, Relokationen, dyld-Bindings |
| `D` | `(M, R)` → neuer `R` oder Trap | AArch64-Instruktionen ausführen |
| `C` | Trap/Register → Host-Aufruf oder Darwin-Dienst | ABI- und Symbolübersetzung |
| `E` | Dienstresultat → Gastzustand | Rückgaberegister, Fehlerstatus, Fortsetzung/Exit |

Für Boolesche Fähigkeitsmatrizen entspricht eine erfolgreiche Ausführung
einer Verkettung ausschließlich vorhandener Kanten:

```text
(A ⊙ B)[i,j] = OR_k (A[i,k] AND B[k,j])
P(ARM64-Mach-O, Windows-x64) = L ⊙ D ⊙ C ⊙ E
```

Ein Nullfaktor in einem erforderlichen Übergang bedeutet weiterhin
`P = 0`; WSL1 ändert keine dieser Mach-O-, Darwin- oder CPU-Kanten.

## Dynarmic-Anbindung

Dynarmics A64-API verlangt einen `UserCallbacks`-Kontext für Instruktions-
und Datenspeicher, SVC, Ausnahmen und Zeitzähler. Der JIT stellt PC, SP,
allgemeine Register, Vektorregister und Statusregister separat bereit.
Damit ist eine x64-Host-Integration grundsätzlich möglich, aber Wintosh muss
den Speicherzugriff und jede Trap-Semantik selbst definieren.

Referenzen:

- [Dynarmic A64 callbacks and configuration](https://github.com/yuzu-mirror/dynarmic/blob/master/src/dynarmic/interface/A64/config.h)
- [Dynarmic A64 JIT register and run API](https://github.com/yuzu-mirror/dynarmic/blob/master/src/dynarmic/interface/A64/a64.h)
- [Dynarmic build and dependency contract](https://github.com/yuzu-mirror/dynarmic/blob/master/CMakeLists.txt)
- [Dynarmic 0BSD license and limitations](https://github.com/yuzu-mirror/dynarmic/blob/master/README.md)

### Erforderliche Übergangs-Matrix

| Welle | Implementierung | Eintrittsbedingung für nächste Welle |
|---|---|---|
| 0. Abhängigkeit | festen Upstream-Commit wählen; `0BSD`-Kern und Lizenzen aller tatsächlich gebauten Abhängigkeiten inventarisieren; x64-Windows-Build reproduzieren | keine implizite, bewegliche Branch-Abhängigkeit; reproduzierbarer Build |
| 1. CPU-Adapter | `A64::Jit` hinter Wintosh-Backend kapseln; `Mapping` als kontrollierten Gastadressraum anbinden; Lese-/Schreibzugriffe mit Segmentgrenzen und Darwin-Schutzrechten prüfen | künstliches A64-Programm läuft bis zu einer definierten Haltbedingung und liefert Registerzustand zurück |
| 2. Trap-Pfad | SVC, Gast-Exit und undefinierte Instruktion in explizite Wintosh-Trap-Ereignisse übersetzen | PC, x0-x7, SP und Fehler-/Exitstatus sind deterministisch und im Dispatcher beobachtbar |
| 3. Import-Brücke | ARM64-Import-Stubs in Gastcode erzeugen; symbolgebundene Stub-ID trappt; Signatur-Metadaten marshallen Gastregister zum Host-ABI | skalare und Pointer-Importe laufen mit begrenzter, dokumentierter Signaturmenge |
| 4. Darwin-Eintritt | `MachOImage::ExecuteEntry` auf Backend-Auswahl erweitern; `DarwinStart`-Stack, argc/argv/envp/apple, TLS und Initializer an den A64-Kontext binden | ARM64-Mach-O ohne Frameworks startet, ruft Darwin-Imports auf und beendet sich korrekt |
| 5. Frameworks | bestehende Foundation/CoreFoundation/AppKit-Symbole über dieselbe Import-Brücke erreichbar machen; ARM64-Objective-C-Argumente und Rückgaben pro Typ abdecken | echte ARM64-Gast-App-Fixtures belegen jeden genutzten Frameworkpfad |

Die Abhängigkeitswelle darf nicht als Integration gelten, solange Welle 1 bis
4 nicht den kontrollierten End-to-End-Aufruf bilden. Arbiträre AppKit-
Anwendungen erfordern zusätzlich Welle 5 und echte App-Fixtures.

## Speicher- und Trap-Invarianten

Für jeden Callback-Zugriff `(a,n)` gilt:

```text
valid(a,n) := exists segment s:
  s.start <= a AND n <= s.end - a AND s.permissions allow operation
```

Überlauf muss vor `s.end - a` abgefangen werden. Callback-Zugriffe dürfen
keine Windows-Adresse dereferenzieren, bevor `valid` erfüllt ist.

Für einen importierten A64-Aufruf gilt zunächst die eingeschränkte
Skalarabbildung:

```text
guest x0..x7 → signature decoder → host ABI arguments
host scalar/pointer result → guest x0
guest PC after SVC → resume
```

Aggregate, FP/SIMD-Argumente, variadische Funktionen, Block-Closures,
Objective-C-Messenger und Stackoverflow-Argumente benötigen eigene
Signaturklassen; sie dürfen nicht stillschweigend als `uint64_t`-Aufruf
behandelt werden. Mach-Traps und Darwin-Syscalls sind getrennte
Service-Namespaces und dürfen nicht durch bloße Nummernidentität vermischt
werden.

## Status

| Fähigkeit | Stand |
|---|---|
| ARM64 parse/map/Relokations-Subset | implementiert, bestehende Grenzen gelten |
| Dynarmic-Abhängigkeit gepinnt und lokal gebaut | fehlt |
| A64-Backend in Wintosh | fehlt |
| x64-Host-Gast-Speicherprüfung | fehlt |
| A64-Import-Trampoline und ABI-Dispatcher | fehlt |
| ARM64 Mach-O end-to-end auf x64 Windows | fehlt |

Die lokal erfolgreiche x64-Clang-Kompilierung belegt nur den vorhandenen
Wintosh-Runner. Sie ist kein Beleg für ARM64-Ausführung auf x64.
