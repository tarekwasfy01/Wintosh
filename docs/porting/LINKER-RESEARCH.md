# Mach-O-Linker für den Windows-Build

Stand: 2026-11-23

## Ausgangslage

Clang erzeugt auf diesem Windows-Host i386-Mach-O-Objekte. Das vorhandene
`ld64.lld.exe` weist `-arch i386` zurück. `tools/link_i386_macho.py` ist nun
ein begrenzter i386-Linker-Prototyp: Er kombiniert die `__TEXT,__text`-
Sections mehrerer `MH_OBJECT`-Dateien, löst externe Definitionen aus diesen
Sections auf und unterstützt 32-Bit-PC-relative
`GENERIC_RELOC_VANILLA`-Relokationen. Das Ergebnis ist ein minimales
`MH_EXECUTE` ohne CRT, Imports, Datenbereiche oder Frameworks.

\[
\text{Mach-O objects} \xrightarrow{\text{resolve + PC-relative relocate}}
\text{single-segment i386 MH_EXECUTE}
\]

## Kandidatenmatrix

Bewertung: `1` = im genannten Umfang belegt, `0` = nicht verfügbar/nicht
implementiert, `?` = offen. Ein positiver Wert ist kein Vollständigkeitsbeleg.

| Kandidat | i386 Mach-O-Ziel | Windows-Host belegt | Objektlinken | CRT/Dylibs/Frameworks | Entscheidung |
|---|---:|---:|---:|---:|---|
| Vorhandenes LLVM `ld64.lld` | 0 (lokal `-arch i386` abgelehnt) | 1 | 0 | 0 | kein i386-Pfad |
| Apple `ld64` / `ld-classic` | 1 (Apple-Handbuch dokumentiert i386) | 0 (Upstream Darwin-Host) | 1 (voller Linker) | ? | Semantik-Referenz |
| `tpoechtrager/cctools-port` | 1 (i386 als Ziel) | 0 (README listet Linux/BSD/macOS/iOS Hosts) | 1 (portiert `ld64`) | ? | Portierungsreferenz |
| `LongDirtyAnimAlf/ld64-msys` | ? (geerbter ld64-Code) | 1 (README nennt MSYS2/Windows) | ? | ? | Alternative für MSYS2-Prototyp |
| Wintosh `link_i386_macho.py` | 1 | 1 | begrenzt (Textsections + PC-relative Vanilla) | 0 | verifiziert für kleine statische C-Gäste |

Quellen:

- [Apple ld64-Quellcode](https://github.com/apple-oss-distributions/ld64)
- [Apple ld-classic-Handbuch](https://github.com/apple-oss-distributions/ld64/blob/main/doc/man/man1/ld-classic.1)
- [`cctools-port` README](https://github.com/tpoechtrager/cctools-port)
- [`ld64-msys` README](https://github.com/LongDirtyAnimAlf/ld64-msys)

`ld64-msys` basiert auf Apple-/cctools-Code. Vor einer Übernahme von Dateien
müssen deren Copyright- und Lizenzhinweise einzeln geprüft und erhalten
werden. Es wurde kein fremder Linker-Code in Wintosh kopiert.

## Unterstützter Link-Vertrag

Jedes Objekt muss little-endian i386 `MH_OBJECT` mit genau einer
`__TEXT,__text`-Section und `LC_SYMTAB` sein. Die Section wird in Eingabereihenfolge
mit ihrer Alignment-Anforderung angeordnet. Externe `N_SECT`-Definitionen
müssen in dieser Text-Section liegen; alle externen `N_UNDF`-Symbole müssen von
einem der Eingabeobjekte definiert werden. Doppelte Definitionen werden
abgewiesen.

Die Relokationsmatrix des Prototyps ist:

| i386 Relokation | Behandlung |
|---|---|
| local, non-scattered, PC-relative, length 2, Vanilla, Ziel eigene Text-Section | unverändert übernommen; gemeinsamer Slide kürzt sich heraus |
| external, non-scattered, PC-relative, length 2, Vanilla, Zieldefinition in einer Eingabe-Text-Section | aufgelöst und gepatcht |
| andere Relokationstypen oder Ziele außerhalb der Text-Sections | explizit abgewiesen |

Für Clangs undefiniertes externes Call-Ziel enthält das gespeicherte Feld den
lageabhängigen Anteil \(A=-P-4\). Bei Eingabe-Section-Placement \(B_i\) und
aufgelöstem Symbol-Offset \(S_j\) wird

\[
D = A + S_j - B_i = S_j - (B_i + P + 4)
\]

geschrieben. `D` muss in einen vorzeichenbehafteten 32-Bit-Wert passen. Bei
local same-section PC-relative references bleibt der ursprüngliche Abstand
gültig, da \((S+\delta)-(P+4+\delta)=S-(P+4)\).

Der Output ist auf ein 4096-Byte-Image mit Code ab Datei-Offset `0x200`
begrenzt. Nicht-Text-Sections einschließlich Unwind-Metadaten werden
weggelassen. Das Image erhält `LC_SEGMENT` und `LC_MAIN`; es enthält keine
Symtab, `LC_LOAD_DYLIB`, `LC_DYLD_INFO`, CRT oder Systembibliotheken.

## Laufzeitnachweis

Zwei Clang-Objekte wurden separat übersetzt: `main.o` definiert `_main` und
referenziert undefiniert `_helper`; `helper.o` definiert `_helper` und gibt 42
zurück. `llvm-objdump` zeigte in `main.o` die externe 32-Bit-PC-relative
`GENERIC_RELOC_VANILLA`-Relokation an `__text+0x17`.

Mit `main.o helper.o` schrieb der Linker den Call-Displacement `+0x15`; bei
umgekehrter Eingabereihenfolge schrieb er `-0x2b`. Beide Ausgaben wurden als
i386 `MH_EXECUTE` erkannt und liefen im Win32-Wintosh-Runner mit Exitcode 42.
Die zweite Anordnung prüft, dass das Ziel vor dem Aufrufer liegen darf.

Das beweist begrenztes objektübergreifendes Linken und Ausführen, keine
beliebigen macOS-Programme. Noch offen sind weitere i386-Relokationen,
`__data`/`__const`, echte Darwin-CRT-Initialisierung, externe Host-Imports,
dyld, libc und Framework-Abhängigkeiten.

## Nächster Ausbau

Der nächste Link-Schritt ist Daten-Section-Platzierung mit lokalen und
externen absoluten i386-Relokationen; danach kommen stubs und importierte
Darwin-Symbole. Der Windows-Host besitzt weder `C:\msys64` noch `C:\cygwin64`,
und die Shell kann GitHub nicht auflösen. `ld64-msys` ist deshalb keine aktuell
baubare lokale Alternative. Es bleibt möglich, den Prototyp anhand der
gemessenen Mach-O-Relokationen gezielt auszubauen.
