# Mach-O-Linker für den Windows-Build

Stand: 2026-11-23

## Ausgangslage

Clang erzeugt auf diesem Windows-Host echte i386-Mach-O-Objekte. Das
installierte `ld64.lld.exe` weist `-arch i386` zurück. Der neue
`tools/pack_i386_macho.py`-Prototyp übernimmt den `__TEXT,__text`-Abschnitt
eines engen, relocationsfreien Ein-Objekt-Falls in ein `MH_EXECUTE`-Image.
Damit ist die Kette

\[
\text{Clang Mach-O object} \xrightarrow{\text{section packaging}} \text{MH_EXECUTE}
\]

konkret ausführbar. Das ist **kein Linken**: Der Packer löst weder Symbole noch
Relokationen auf. Ein echter Mach-O-Linker bleibt erforderlich.

## Kandidatenmatrix

Bewertung je Kriterium: `1` = belegt/erfüllt, `0` = belegt/nicht erfüllt,
`?` = noch offen. Ein positiver Wert gilt nur für den beschriebenen Umfang.

| Kandidat | i386 Mach-O-Ziel | Windows-Host belegt | C-Objekte/Relokationen | GUI-/Framework-Linking | Entscheidung |
|---|---:|---:|---:|---:|---|
| Vorhandenes LLVM `ld64.lld` | 0 (lokaler Aufruf lehnt `-arch i386` ab) | 1 | ? | ? | derzeit kein i386-Pfad |
| Apple `ld64` / `ld-classic` | 1 (Apple-Handbuch dokumentiert i386) | 0 (Upstream ist Apples Darwin-Linker) | 1 (voller Linker) | ? | Referenz für Format und Semantik |
| `tpoechtrager/cctools-port` | 1 (i386 in Zielarchitekturen) | 0 (README listet Linux, BSD, macOS und iOS als Hosts) | 1 (portiert `ld64`) | ? | Quell-/Portierungsreferenz |
| `LongDirtyAnimAlf/ld64-msys` | ? (geerbter ld64-Code; lokal noch nicht gebaut) | 1 (README nennt MSYS2/Windows) | ? | ? | bester Build-Prototyp, falls MSYS2 bereitsteht |
| Wintosh `pack_i386_macho.py` | 1 (emittiert i386 `MH_EXECUTE`) | 1 | 0 (packt nur relocationsfreies `__text`) | 0 | Verifizierter Format-/Runner-Prototyp, kein Linker |
| Eigenständiger minimaler i386-Linker | begrenzt planbar | 1 | ? (noch nicht implementiert) | 0 (außerhalb des ersten Umfangs) | spätere Option mit expliziter Relokationsmenge |

Quellen:

- [Apple ld64-Quellcode](https://github.com/apple-oss-distributions/ld64)
- [Apple ld-classic-Handbuch](https://github.com/apple-oss-distributions/ld64/blob/main/doc/man/man1/ld-classic.1)
- [`cctools-port` README](https://github.com/tpoechtrager/cctools-port)
- [`ld64-msys` README](https://github.com/LongDirtyAnimAlf/ld64-msys)

`ld64-msys` basiert auf Apple-/cctools-Code. Vor Übernahme von Quelldateien
müssen die Copyright- und Lizenzhinweise jeder Datei geprüft und erhalten
werden. Dieser Arbeitsstand kopiert daraus keinen Code.

## Formales Abnahmemodell

Für Objektmenge \(O\), Ziel \(a\) und Linker \(L\) gilt:

\[
\operatorname{LinkOK}(L,O,a) =
\operatorname{Parse}(O) \land
\operatorname{Resolve}(O) \land
\operatorname{Relocate}(O,a) \land
\operatorname{EmitMachO}(L,a)
\]

Der verifizierte Packerschritt ist enger als ein minimaler Link:

\[
O = \{\text{Clang-i386-Objekt mit `_main`, ohne undefinierte externe Symbole}\},
\quad a = \text{i386 Darwin}
\]

Gemessen wurde ein Clang-Objekt mit drei Sections, keiner `__text`-Relokation,
einem externen `_main` und ohne undefinierte externe Symbole. Der Packer schrieb
ein 4096-Byte-Image mit `LC_MAIN.entryoff=0x200`; `llvm-objdump` erkannte
`MH_EXECUTE/I386`, und der Win32-Runner gab Exitcode 42 zurück. Nicht-Text-
Sections einschließlich Unwind-Metadaten werden weggelassen. Diese Messung
beweist Format-Packaging und Ausführung dieses Eingabefalls, nicht Objektlinken.

Der nächste Linker-Abnahmeschritt ist eine echte i386-Relokation zwischen zwei
Objekt-Sections. Danach folgen undefinierte Symbole, Darwin-Startup plus
`_exit`, dann ausgewählte Systembibliotheken. Jede Stufe muss ihre Eingabemenge
und offenen Relokationen benennen; ein erfolgreicher Link allein beweist keine
ABI- oder Framework-Kompatibilität.

## Nächste technische Aktion

Der Windows-Host besitzt Visual-Studio-Clang/LLD, aber weder `C:\msys64` noch
`C:\cygwin64`; außerdem kann die Shell hier GitHub nicht auflösen. Der
`ld64-msys`-Build ist daher noch nicht lokal ausführbar. Dessen README nennt
MSYS2 und einen Bash-`configure`/`make`-Build als Pfad. Vor einer Quell-
Übernahme bleibt das Datei-für-Datei-Lizenzinventar Pflicht.
