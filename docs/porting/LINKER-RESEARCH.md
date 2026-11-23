# Mach-O-Linker für den Windows-Build

Stand: 2026-11-23

## Ausgangslage

Wintosh kann auf Windows bereits einen kleinen, manuell gepackten i386
`MH_EXECUTE`-Mach-O ausführen. Clang kann hier auch echte i386-Mach-O-Objekte
erzeugen. Der installierte `ld64.lld.exe` weist `-arch i386` jedoch zurück.
Damit fehlt der Schritt

\[
\text{Mach-O object files} \xrightarrow{\text{link}} \text{Mach-O executable}
\]

zwischen Compiler und Runner. Der bisherige Laufzeitnachweis ersetzt diesen
Linkschritt ausdrücklich nicht.

## Kandidatenmatrix

Bewertung je Kriterium: `1` = belegt/erfüllt, `0` = belegt/nicht erfüllt,
`?` = noch offen. `1` bedeutet keine Aussage zur Gesamtvollständigkeit.

| Kandidat | i386 Mach-O-Ziel | Windows-Host belegt | C-Objekte/Relokationen | GUI-/Framework-Linking | Entscheidung |
|---|---:|---:|---:|---:|---|
| Vorhandenes LLVM `ld64.lld` | 0 (lokaler Aufruf lehnt `-arch i386` ab) | 1 | ? | ? | derzeit kein i386-Pfad |
| Apple `ld64` / `ld-classic` | 1 (Apple-Handbuch dokumentiert i386) | 0 (Upstream ist Apples Darwin-Linker) | 1 (voller Linker) | 1 (Linken ist nicht Ausführen) | Referenz für Format und Semantik |
| `tpoechtrager/cctools-port` | 1 (i386 in Zielarchitekturen) | 0 (README listet Linux, BSD, macOS und iOS als Hosts) | 1 (portiert `ld64`) | ? | als Quell-/Portierungsreferenz nutzen |
| `LongDirtyAnimAlf/ld64-msys` | ? (geerbter ld64-Code; lokal noch nicht gebaut) | 1 (README nennt MSYS2/Windows) | ? | ? | bester Kandidat für einen Build-Prototyp, falls MSYS2 bereitsteht |
| Eigenständiger minimaler i386-Linker | begrenzt planbar | 1 | ? (noch nicht implementiert) | 0 (außerhalb des ersten Umfangs) | Rückfalloption nur für klar definierte Relokationen |

Quellen:

- [Apple ld64-Quellcode](https://github.com/apple-oss-distributions/ld64)
- [Apple ld-classic-Handbuch](https://github.com/apple-oss-distributions/ld64/blob/main/doc/man/man1/ld-classic.1)
- [`cctools-port` README](https://github.com/tpoechtrager/cctools-port)
- [`ld64-msys` README](https://github.com/LongDirtyAnimAlf/ld64-msys)

`ld64-msys` basiert auf Apple-/cctools-Code. Vor Übernahme von Quelldateien
müssen die mitgelieferten Copyright- und Lizenzhinweise jeder Datei geprüft und
erhalten werden. Dieser Arbeitsstand kopiert daraus keinen Code.

## Formales Abnahmemodell

Für eine Objektmenge \(O\), ein Ziel \(a\) und einen Linker \(L\) gilt:

\[
\operatorname{LinkOK}(L,O,a) =
\operatorname{Parse}(O) \land
\operatorname{Resolve}(O) \land
\operatorname{Relocate}(O,a) \land
\operatorname{EmitMachO}(L,a)
\]

Der erste lokale Meilenstein ist bewusst enger als eine macOS-App:

\[
O = \{\text{Clang-i386-Objekt mit `_main` und ohne externe Symbole}\},
\quad a = \text{i386 Darwin}
\]

Abnahme erfolgt nur, wenn der erzeugte `MH_EXECUTE` von einem unabhängigen
Mach-O-Inspektor als i386 erkannt wird und Wintosh genau diesen Linker-Output
startet. Danach folgen in getrennten Stufen: lokale i386-Relokationen,
definierte externe Symbole, Darwin-Startup plus `_exit`, dann ausgewählte
Systembibliotheken. Jede Stufe muss ihre Eingabemenge und offenen Relokationen
benennen; ein erfolgreicher Link allein beweist keine ABI- oder Framework-
Kompatibilität.

## Nächste technische Aktion

Der Windows-Host besitzt derzeit Visual-Studio-Clang/LLD, aber weder
`C:\\msys64` noch `C:\\cygwin64`. Deshalb kann `ld64-msys` in dieser Umgebung
nicht unmittelbar konfiguriert werden. Zuerst wird ein Windows-buildbarer
Prototyp bzw. die minimal erforderliche Build-Abhängigkeit ermittelt. Es wird
kein fremder Linker-Quellcode ohne Lizenzinventar in den Runner kopiert.
