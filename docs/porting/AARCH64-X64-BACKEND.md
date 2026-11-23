# AArch64 Mach-O auf x64 Windows: Backend-Integrationsvertrag

Stand: 2026-11-23. Dieses Dokument beschreibt den ARM64-Gastpfad auf
x64-Windows und kennzeichnet getrennt, was implementiert und was zur Laufzeit
noch nicht belegt ist.

## Architekturgrenze im aktuellen Quellstand

Der Loader kann ARM64-Mach-O parsen, mappen und einen Teil der ARM64-
Relokationen anwenden. Auf x64-Windows führt Dynarmic den Gastcode in einem
registrierten Speicherraum aus; Darwin-SVCs und ein begrenzter Host-Importpfad
werden dispatcht. Auf nativem Windows ARM64 läuft ARM64-Code auf der CPU.
Beide Pfade sind noch nicht end-to-end mit einer realen macOS-Anwendung belegt:

| Übergang | x64 Windows | ARM64 Windows |
|---|---:|---:|
| ARM64-Mach-O lesen und CPU-Slice auswählen | vorhanden | vorhanden |
| Segmente mappen und unterstützte Relokationen anwenden | vorhanden | vorhanden |
| ARM64-Instruktionen ausführen | Dynarmic-Backend mit ARM64-`main`-Fixture belegt | native CPU, noch nicht end-to-end belegt |
| Darwin-Imports aus ARM64-Gastcode aufrufen | `_strlen` und Objective-C-Gastcallbacks mit Integer-/Double-Argumenten und -Rückgaben über SVC end-to-end belegt; weitere Imports nur teilweise | teilweise vorhandene ABI-Brücke |
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
- [Apple XNU syscall number and argument table](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/syscalls.master)

### Erforderliche Übergangs-Matrix

| Welle | Implementierung | Eintrittsbedingung für nächste Welle |
|---|---|---|
| 0. Abhängigkeit | festen Upstream-Commit wählen; `0BSD`-Kern und Lizenzen aller tatsächlich gebauten Abhängigkeiten inventarisieren; x64-Windows-Build reproduzieren | keine implizite, bewegliche Branch-Abhängigkeit; reproduzierbarer Build |
| 1. CPU-Adapter | `A64::Jit` hinter Wintosh-Backend kapseln; `Mapping` als kontrollierten Gastadressraum anbinden; Lese-/Schreibzugriffe mit Segmentgrenzen und Darwin-Schutzrechten prüfen | künstliches A64-Programm läuft bis zu einer definierten Haltbedingung und liefert Registerzustand zurück |
| 2. Trap-Pfad | SVC, Gast-Exit und undefinierte Instruktion in explizite Wintosh-Trap-Ereignisse übersetzen | CPU-Zustand samt PC, x0-x30, SIMD/PSTATE und Fehlerstatus ist rückführbar; der UNIX-SVC-Dispatcher unterstützt exit/wait4/read/write/readv/writev/pread/pwrite/open/close/link/unlink/chdir/chmod/fchmod/symlink/readlink/umask/getpid/getppid/getuid/geteuid/getgid/getegid/getpgrp/dup/dup2/pipe/socket/socketpair/connect/bind/listen/accept/recvfrom/sendto/getpeername/getsockname/shutdown/gettimeofday/getrusage/getpriority/setpriority/access/rename/mkdir/rmdir/fsync/fdatasync/stat/lstat/fstat/lseek/truncate/ftruncate/mmap/munmap/mprotect/msync/madvise/sysctl/kill/sigaction/sigprocmask |
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
Gast-BL/BLR → Gast-Trampolin (movz/movk x16, SVC #0x82)
→ Import-ID + Symboladapter → typisierter Windows-Host-Aufruf
→ ARM64-Rückgaberegister und Fortsetzung nach SVC
```

Aggregate, FP/SIMD-Argumente, variadische Funktionen, Block-Closures,
Nicht katalogisierte Host-Imports brechen mit dem Symbolnamen ab. Der
Objective-C-Messenger nutzt den vorhandenen Encoding-gesteuerten Dispatcher;
Struktur- und FP-Rückgaben hängen von dessen unterstützten Encodings ab.
Sonstige Aggregate, Block-Closures und native Callback-Übergänge sind weiter
offen. Mach-Traps und Darwin-Syscalls sind getrennte
Service-Namespaces und dürfen nicht durch bloße Nummernidentität vermischt
werden.

## Status

### Windows-Buildprobe und erste Wintosh-Anbindung (2026-11-23)

Der lokale, nur zur Evaluation angelegte Upstream-Checkout steht auf
`yuzu-mirror/dynarmic` Commit `9d4582339990d4eae53f1dc7160686920fc2075c`.
vcpkg installierte Boost 1.92 samt `boost-icl` und `boost-variant` (48 Pakete
einschließlich der schon vorhandenen Module). Dynarmics x64-Windows-A64-
Frontend baute anschließend mit MSVC 19.51 und Ninja zu `dynarmic.lib`.
Dynarmics CMake-Dateien im ignorierten Probeklon mussten `/Zi` auf `/Z7`
umstellen, weil der installierte PDB-Manager `C1902` lieferte. Diese lokale
Toolchain-Anpassung ist kein Patch am Wintosh-Repository.

Für den lokalen Wintosh-Build sind `WINTOSH_DYNARMIC_SOURCE_DIR` und
`WINTOSH_DYNARMIC_BUILD_DIR` optionale CMake-Pfade. Mit beiden gesetzt wurde
der A64-Adapter in `wintosh_windows_host.lib` kompiliert und das Release-
Programm `wintosh.exe` gegen Dynarmic, fmt, mcl, Zydis und Zycore gelinkt.
Ein erster Debug-Link wurde erwartungsgemäß wegen `_ITERATOR_DEBUG_LEVEL=2`
gegenüber `0` in Dynarmics Release-Bibliothek abgelehnt; der Release-Build
behebt diese ABI-Konfigurationsabweichung.

Die erste Wintosh-Anbindung führt AArch64-`ExecuteEntry` über den A64-JIT.
Dynarmic-Speicher-Callbacks prüfen jede Instruktions- und Datenadresse gegen
die registrierten Segmente und deren Darwin-Rechte. Argument-, Umgebungs- und
Startup-Puffer werden für den Gastzugriff registriert. Ein kompatibler
`main`-Return wird über einen privaten Nicht-Ausführbar-Sentinel erkannt.
Lokale Release-Fixtures belegen `_strlen`-Import und begrenzte
`objc_msgSend`-Gastcallbacks mit Integer-/Double-Werten, Objektidentität,
Zeiger-Dereferenzierung und sieben Integerargumenten, von denen eines auf dem
Gaststack übergeben wird. Zwei- und viergliedrige Double-HFAs sowie eine
24-Byte-Struktur mit indirektem ARM64-Rückgabepointer werden ebenfalls
verifiziert. Andere Aggregatlayouts und ein funktionierender macOS-App- oder
Frameworkstart sind damit nicht belegt.

Der Adapterzustand ist als `A64CpuState` mit `x0..x30`, `v0..v31`, `SP`,
`PC`, `PSTATE`, `FPCR` und `FPSR` modelliert. Jeder Lauf gibt diesen Zustand
zusammen mit dem Trapgrund zurück; ein Folgelauf kann ihn wieder übernehmen.
Dynarmic setzt die Gast-PC vor dem SVC-Callback auf die nächste Instruktion.
Der Loader startet den Gast danach mit dem geänderten Rückgaberegister und
PSTATE fort. Ein erneuter Lauf erzeugt derzeit einen neuen JIT-Kontext;
dynamische Zustände wie der ARM-Exclusive-Monitor werden dabei nicht erhalten
und bleiben deshalb fail-closed.

Der Loader dispatcht bei `svc #0x80` die Darwin-UNIX-Aufrufe `exit`, `read`,
`write`, `readv`, `writev`, `pread`, `pwrite`, `open`, `close`, `link`,
`unlink`, `chdir`, `chmod`, `symlink`,
`readlink`, `umask`, `getpid`, `getppid`, `getuid`, `geteuid`, `getgid`,
`getegid`, `getpgrp`, `dup`, `dup2`, `pipe`, `access`, `rename`, `mkdir`,
`rmdir`, `fsync`, `fdatasync`, `stat`, `lstat`, `fstat`, `lseek`, `truncate`
und `ftruncate` an vorhandene Wintosh-Adapter. BSD-Socketaufrufe `socket`,
`socketpair`, `connect`, `bind`, `listen`, `accept`, `recvfrom`, `sendto`,
`getpeername`, `getsockname` und `shutdown` werden an WinSock-Adapter
weitergereicht. Adressdaten laufen über begrenzte lokale Hostpuffer. Die
Längen- und Ausgabepointer werden geprüft und die Ergebnisse erst danach in
den Gastadressraum kopiert. Der BSD-Syscall `gettimeofday` schreibt timeval
und optional den Mach-Absolutzeitwert; die veraltete Zeitzonen-Ausgabe wird
ignoriert. `clock_gettime` und `nanosleep` sind keine direkten XNU-Syscalls
in der geprüften Tabelle und bleiben Framework-/LibSystem-Importe.
`readv` und `writev` kopieren
das iovec-Array nach einer Bereichsprüfung in Hostspeicher und validieren jeden
einzelnen Gastpuffer in Leserichtung bzw. Schreibrichtung. `pread` und `pwrite`
prüfen zusätzlich den 64-Bit-Dateioffset und begrenzen einzelne Transfers auf
die von den Windows-I/O-Adaptern unterstützte DWORD-Größe.
Mehrfach-Rückgaben wie bei
`getpid`, `getuid`, `getgid` und `pipe` werden in `x0` und `x1` abgebildet.
Für `read` und `write` wird der Gastpuffer vor dem Hostaufruf auf Lesbarkeit
bzw. Schreibbarkeit und Bereichsüberlauf geprüft. Pfadargumente für `open`,
`link`, `unlink`, `chdir`, `chmod`, `symlink`, `readlink`, `access`, `rename`,
`mkdir`, `rmdir`, `stat` und `lstat` müssen innerhalb eines lesbaren
Gastbereichs bis zum NUL-Byte reichen. Ausgabeparameter von `readlink`,
`stat`, `lstat` und `fstat` werden vor dem Aufruf gegen Gast-Schreibrechte
und die erwartete Strukturgröße geprüft. Rückgabewert und PSTATE-Carry folgen der Darwin-
Syscall-Konvention; unbekannte UNIX-Nummern liefern `ENOSYS`. Nicht-UNIX-
Dispatchklassen und andere SVC-Immediatewerte werden explizit abgelehnt. Die
Nummern stammen aus Apples XNU `syscalls.master`. Das ist ein erster schmaler
Kernel-Aufrufpfad, keine allgemeine Darwin-Syscall- oder Mach-Trap-
Implementierung.

Exklusive ARM64-Stores stoppen fail-closed, weil ein gemeinsamer
Exclusive-Monitor fehlt. ARM64-Initialisierer und -Terminierer mit Code
werden ebenfalls abgelehnt. Der Speicherraum umfasst das Hauptexecutable,
alle beim Bootstrap gemappten ARM64-dylib-Segmente, Startpuffer und zur
Laufzeit registrierte `mmap`-Bereiche. Fat-Binaries können über `--arch arm64`
auch auf x64-Windows explizit den ARM64-Slice wählen; der CPU-Typ wird für
Graphauflösung, Bindings, Initialisierer und Provider-Mappings weitergereicht.
Dadurch können ARM64-Images untereinander aufgelöste Gastfunktionen aufrufen.
Host-Imports werden über pro Mapping erzeugte Trampoline in einen begrenzten
Symboladapter geleitet; nicht unterstützte Symbole schlagen kontrolliert fehl.
`mmap` reicht die sechs
Darwin-Argumente an den vorhandenen Windows-Adapter weiter und registriert
erfolgreiche Bereiche samt Gastrechten. `mprotect` akzeptiert nur vollständig
abgedeckte, explizit schützbare Bereiche (Executable-Segmente und registrierte
Mappings) und aktualisiert Host- sowie Gastschutzrechte. `munmap` gibt nur eine
vollständige, exakt registrierte Mapping-Länge frei; Teilbereiche, feste
Ersetzungen bestehender Gast-Mappings und nicht registrierte Bereiche schlagen
geschlossen fehl. Dadurch bleibt die Backend-Tabelle mit dem tatsächlich
besessenen Hostspeicher synchron. `sysctl` ist auf die im vorhandenen Adapter
implementierten zweistufigen `KERN`- und `HW`-MIBs beschränkt. MIB und
Eingabewerte werden vorab kopiert beziehungsweise geprüft; Ausgaben laufen
über einen begrenzten Hostpuffer und werden danach in den Gast geschrieben.
Weitere Unix-Syscalls, Mach-Traps, Host-Imports und Cocoa bleiben offen.

Der Upstream-Commit ist noch nicht in der Projektkonfiguration festgeschrieben;
die optionalen Pfade setzen einen separat gebauten Checkout voraus. Vor einer
standardmäßigen Integration müssen Upstream-Pin, Reproduzierbarkeit und
Lizenzen der tatsächlich gebauten Abhängigkeiten in den Projektmetadaten
festgelegt werden.

| Fähigkeit | Stand |
|---|---|
| ARM64 parse/map/Relokations-Subset | implementiert, bestehende Grenzen gelten |
| Dynarmic-Commit lokal untersucht | `9d458233`; Windows-x64-A64 statisch gebaut |
| Dynarmic-Abhängigkeit reproduzierbar gebaut | lokal mit Probe-CMake-Dateianpassung; noch nicht im Projekt fixiert |
| A64-Backend in Wintosh | optionaler Adapter lokal gebaut; ARM64-Import-Fixture läuft auf x64 Windows |
| A64-Trap-Zustand | x0–x30, v0–v31, SP, PC, PSTATE, FPCR und FPSR rückführbar und fortsetzbar |
| Darwin-UNIX-SVC | `exit`, `wait4` (mit lokaler Status-/Rusage-Kopie), `read`, `write`, `readv`, `writev`, `pread`, `pwrite`, `open`, `close`, `link`, `unlink`, `chdir`, `chmod`, `fchmod`, `symlink`, `readlink`, `umask`, Prozess-/Gruppen-ID-Aufrufe, `dup`, `dup2`, `pipe`, `socket`, `socketpair`, `connect`, `bind`, `listen`, `accept`, `recvfrom`, `sendto`, `getpeername`, `getsockname`, `shutdown`, `gettimeofday`, `getrusage`, `getpriority`, `setpriority`, `access`, `rename`, `mkdir`, `rmdir`, `fsync`, `fdatasync`, `stat`, `lstat`, `fstat`, `lseek`, `truncate`, `ftruncate`, `mmap`, `munmap` (nur ganzes Mapping), `mprotect`, `msync`, `madvise`, `sysctl` (implementierte zweistufige `KERN`-/`HW`-MIBs), `kill`, `sigaction`, `sigprocmask`; andere UNIX-Nummern liefern `ENOSYS`; kein Mach-Trap-Dispatcher |
| x64-Host-Gast-Speicherprüfung | Segmentrechte und Bereichsgrenzen im Callback umgesetzt; Hauptexecutable, Bootstrap-dylibs, Startpuffer und dynamische Mappings werden registriert; keine Laufzeit-Fixture |
| A64-Import-Trampoline und ABI-Dispatcher | teilweise: `_strlen`, Objekt-/Zeigerwerte, Integer-/Double-Callbacks, Stackargument, 2-/4-Double-HFAs und indirektes 24-Byte-Struct-Return bestehen; übrige Aggregate/Blocks/Variadics bleiben offen |
| Schmale ARM64 Mach-O Runtime-Fixtures auf x64 Windows | `_strlen` und ein Objective-C-Gastcallback bestehen; App-Bundles/Frameworkstart fehlen |

Die lokalen Fixtures belegen nur die jeweils beschriebenen AArch64-Instruktions-
und Importpfade im Wintosh-Runner. Sie sind kein Beleg für eine ausführbare
allgemeine macOS-App oder vollständige Darwin-Kompatibilität.
