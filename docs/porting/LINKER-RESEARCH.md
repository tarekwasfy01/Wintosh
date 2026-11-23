# Mach-O-Linker für den Windows-Build

Stand: 2026-11-23

## Ausgangslage

Clang erzeugt auf diesem Windows-Host i386-Mach-O-Objekte. Das vorhandene
`ld64.lld.exe` weist `-arch i386` zurück. `tools/link_i386_macho.py` ist ein
begrenzter i386-Linker-Prototyp: Er kombiniert `__TEXT,__text` und optionale
`__TEXT,__cstring` oder `__const`, `__DATA,__data` sowie nullinitialisierte
`__bss`/`__common`-Bereiche mehrerer `MH_OBJECT`-Dateien und löst begrenzte
`__IMPORT,__pointers`-Slots über die indirekte Symboltabelle auf. Es verarbeitet
32-Bit-`GENERIC_RELOC_VANILLA`-Relokationen. Absolute interne Adressen erhalten
`DYLD_INFO_ONLY`-Rebases für den Laufzeit-Slide.

\[
\text{Mach-O objects} \xrightarrow{\text{resolve + relocate + rebase}}
\text{segmented i386 MH_EXECUTE}
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
| Wintosh `link_i386_macho.py` | 1 | 1 | begrenzt (Text, Daten, Zerofill, C-String, Vanilla) | 0 | verifiziert für kleine statische C-Gäste |

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
`__TEXT,__text`-Section, höchstens einer `__TEXT,__cstring`- oder `__const`-
Section, einer `__DATA,__data`-Section und einer zerofill-Section `__bss`
oder `__common` sowie `LC_SYMTAB` sein. Die Sections
werden in Eingabereihenfolge mit ihrer Alignment-Anforderung platziert.
Externe `N_SECT`-Definitionen müssen in diesen Sections liegen; undefinierte
externe Symbole und indirekte Pointer-Ziele müssen durch ein Eingabeobjekt
definiert sein. Doppelte starke Definitionen werden abgewiesen.

| i386 Relokation | Behandlung |
|---|---|
| local, non-scattered, PC-relative, length 2, Vanilla, Ziel in unterstütztem Bereich | Placement- und VM-Adress-Deltas werden angewandt |
| external, non-scattered, PC-relative, length 2, Vanilla, Zieldefinition in Eingaben | aufgelöst und gepatcht |
| local/external, non-scattered, absolute, length 2, Vanilla, Zieldefinition in Eingaben | Linkzeit-Adresse geschrieben; dyld type-2 absolute32 rebase ausgegeben |
| local text reference to `__IMPORT,__pointers`, indirect target defined in inputs | pointer slot laid out, bound and rebased |
| andere Relokationstypen oder Ziele außerhalb der verknüpften Sections | explizit abgewiesen |

Für Clangs undefiniertes externes PC-relative Call-Ziel enthält das Feld den
lageabhängigen Anteil \(A=-P-4\). Mit Source-Placement \(B_i\), Target-
Placement \(B_j\), Target-Symbol-Offset \(S_j\) und den bevorzugten
Section-VM-Adressen \(V_s,V_t\) wird

\[
D = A + (V_t+B_j+S_j) - (V_s+B_i)
\]

geschrieben. `D` muss in einen vorzeichenbehafteten 32-Bit-Wert passen. Eine
absolute Referenz enthält zunächst die Linkzeit-Adresse \(V_t+B_j+S_j\); der
Rebase-Stream addiert anschließend den Image-Slide. Lokale PC-relative
Verweise erhalten die Placement-Deltas von Quelle und Ziel. Wenn beide
Sections identisch sind, kürzt sich ein gemeinsamer Slide heraus:

`D_local = D_input + (V_target,out - V_target,in) - (V_source,out - V_source,in)`.

Der Output hat ein 4096-Byte-`__TEXT`-Segment mit Code ab Datei-Offset `0x200`
und bei vorhandenen Daten ein 4096-Byte-`__DATA`-Segment ab Datei-Offset
`0x1000`. Andere Sections einschließlich Unwind-Metadaten werden weggelassen.
Das Image erhält `LC_SEGMENT` und `LC_MAIN`; `LC_DYLD_INFO_ONLY` kommt hinzu,
wenn absolute Rebases nötig sind. Eine Output-Symboltabelle, `LC_LOAD_DYLIB`,
CRT und Systembibliotheken werden nicht erzeugt.

## Laufzeitnachweis

Zwei Clang-Objekte wurden separat übersetzt: `main.o` definiert `_main` und
referenziert undefiniert `_helper`; `helper.o` definiert `_helper` und gibt 42
zurück. `llvm-objdump` zeigte in `main.o` die externe 32-Bit-PC-relative
`GENERIC_RELOC_VANILLA`-Relokation an `__text+0x17`. Mit `main.o helper.o`
schrieb der Linker den Call-Displacement `+0x15`; bei umgekehrter Eingabereihenfolge
schrieb er `-0x2b`. Beide i386-`MH_EXECUTE`-Ausgaben liefen im Win32-Wintosh-
Runner mit Exitcode 42.

Eine weitere Clang-Datei definierte `shared_value=40`, addierte 2, schrieb den
Wert zurück und las ihn erneut. Drei absolute `GENERIC_RELOC_VANILLA`-Felder in
`__text` wurden gegen `__DATA,__data` aufgelöst und als dyld type-2 32-Bit-
Rebases ausgegeben. `llvm-objdump` bestätigte die Text- und Datensections sowie
`LC_DYLD_INFO_ONLY`; Wintosh gab **42** zurück. Das belegt Lesen und Schreiben
des globalen Datenbereichs mit Laufzeit-Slide.

Das beweist begrenztes i386-Linken und Ausführen, keine beliebigen macOS-
Programme. Ein zweites Objekt referenzierte `_external_global` über einen
indirekten `__IMPORT,__pointers`-Slot. Der Linker las dessen Ziel über
`LC_DYSYMTAB`, band den Slot an die Definition in `__common`, und der Win32-
Runner gab nach Schreiben und Lesen **42** zurück. Das belegt begrenzte
objektübergreifende Datenauflösung, keine Darwin-Dylib-Bindings.

Die Zerofill-Bytes werden derzeit in den prototypischen
`__DATA,__data`-Dateibereich zusammengeführt und nicht als separates
`__bss`-Output-Section-Headerfeld abgebildet. Noch offen sind weitere
Relokationen wie `SECTDIFF`, Darwin-CRT,
Systembibliotheken, Host-Imports und Frameworks.

## Nächster Ausbau

Eine weitere Clang-Datei enthielt `__TEXT,__cstring` mit den Bytes `41 00`,
eine globale Datenvariable mit absolutem Verweis darauf sowie Code, der den
Zeiger dereferenzierte. Der Linker legte den Literalbereich im Output-
Datenbereich ab, rebased den Zeiger, und Wintosh gab den ersten Bytewert
**65** zurück. Der konstante Abschnitt bleibt dabei schreibbar; korrekte
Read-only-Seitenschutzabbildung ist noch offen.

Als Nächstes kommen echte getrennte Read-only-Sections und mehr Literaltypen.
Danach folgen importierte Darwin-Symbole und ausgewählte CRT/libc-Pfade. Der Windows-
Host besitzt weder `C:\msys64` noch `C:\cygwin64`; `ld64-msys` ist hier derzeit
keine baubare lokale Alternative. Die Datei-für-Datei-Lizenzprüfung bleibt
Pflicht, bevor Quellcode daraus übernommen wird.
