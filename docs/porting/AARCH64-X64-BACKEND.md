# AArch64 Mach-O auf x64 Windows: Backend-Integrationsvertrag

Stand: 2026-11-24. Dieses Dokument beschreibt den ARM64-Gastpfad auf
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
| ARM64-Instruktionen ausführen | optionales Dynarmic-Backend; ARM64-`main`-, syscall-, heap-, mmap-, file-, callback- und dylib-Fixtures belegt | native CPU, noch nicht end-to-end belegt |
| Darwin-Imports aus ARM64-Gastcode aufrufen | ausgewählte UNIX-SVCs, dyld-, Objective-C- und CoreFoundation-Imports mit Fixtures belegt; Signatur- und Symbolumfang bleibt begrenzt | teilweise vorhandene ABI-Brücke |
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
| 0. Abhängigkeit | Dynarmic `9d458233`, vcpkg-Baseline und Abhängigkeitsnotizen sind fixiert; Offline-Scriptlauf aus sauberem Quellcheckout baut `dynarmic.lib` | Standardmäßiger vcpkg-Manifestmodus mit Netz sowie erneuter Lizenzabgleich der tatsächlich gelinkten Teilmengen |
| 1. CPU-Adapter | `A64::Jit` hinter Wintosh-Backend; registrierter Gastadressraum, Rechteprüfungen und Registerzustand sind durch ARM64-Fixtures belegt | bleibt offen für nicht getestete Instruktionsgruppen und native Windows ARM64 End-to-End-Ausführung |
| 2. Trap-Pfad | SVC, Gast-Exit und Fehlerzustand sind angebunden; ein begrenzter Darwin-UNIX-SVC-Satz ist in Loader-Fixtures abgedeckt | zusätzliche Syscalls, Mach-Traps und Kernelzustandsparität |
| 3. Import-Brücke | symbolgebundene ARM64-Trampoline, ausgewählte skalare/Pointer- und Callback-Signaturen, verschachtelte SVCs, Gast-Datenbereiche und Objective-C-Dispatch sind belegt | übrige Aggregate, Variadics, Block-Closures und nicht katalogisierte Imports |
| 4. Darwin-Eintritt | ARM64-`main`, Argument-/Umgebungsstart, ausgewählte Initialisierer, Objective-C `+load`, `dlopen`/`dlsym`/`dlclose` und DSO-Abbau haben Fixtures | vollständige dyld- und Prozesssemantik; echte signierte/gebündelte macOS-Anwendung |
| 5. Frameworks | ausgewählte Foundation/CoreFoundation/Mach-/AppKit-Slices werden über dieselbe ABI-Brücke aufgerufen | echte ARM64-App-Fixtures und vollständige framework-/ressourcenbezogene Startpfade |

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

### Aktualisierung des ARM64-Gastpfads (2026-11-24)

Die optionale Dynarmic-Anbindung führt inzwischen mehr als die anfängliche
`main`-Fixture aus: lokale ARM64-Gäste decken ausgewählte UNIX-SVCs, Gast-Heap,
`mmap`/`mprotect`/`munmap`, Datei-I/O, Objective-C-Callbacks und -Blöcke,
Initialisierer, `+load`, sowie `dlopen`/`dlsym`/`dlclose` mit einem ARM64-dylib
ab. CoreFoundation-Fixtures belegen Run-Loop-, Timer-, Mach-Port- und
Allocator-Pfade. Der Importpfad unterscheidet ausführbare Hostziele von
lesbaren Windows-Datenbereichen anhand `VirtualQuery`; Datenbereiche werden
mit ihren Windows-Lese-/Schreibrechten für den Gast registriert. Bei
`MEM_PRIVATE` begrenzt `HeapSize` den Gastbereich auf die einzelne
Prozess-Heap-Allokation statt auf die von `VirtualQuery` zusammengefasste
Heapregion; dadurch kollidieren spätere Gast-`malloc`-Aufrufe nicht mit
unbeteiligten Host-Heapblöcken. Private Datenimporte, die nicht am Anfang einer
bekannten Heap-Allokation liegen, schlagen geschlossen fehl. Das behebt nicht
die Darwin-ABI beliebiger Host-Globals und ersetzt keine App-Bundle-Fixture.

Der Backend-Build bleibt optional. `tools/Build-Dynarmic.ps1` pinnt den
Upstream-Commit und `tools/dynarmic/vcpkg.json` die Boost-Baseline; der Build
wendet außerdem die reproduzierte MSVC-PDB-Anpassung aus der lokalen Probe an.
Der Offline-Scriptpfad wurde aus einem sauberen, gepinnten Quellcheckout gegen
die vorhandenen x64-windows-Boost-Header ausgeführt; CMake-Konfiguration und
alle 133 Compile-/Linkschritte waren erfolgreich. Der vcpkg-Manifestmodus
konnte wegen fehlender DNS-/Netzwerkverbindung nicht geprüft werden. Die
Lizenztexte liegen unter `licenses/dynarmic/`; die Provenienzprüfung erkennt
jetzt neun zusätzliche Notices. Ein komponentengenauer Abgleich der
tatsächlich gelinkten Teilmengen bleibt offen.

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
verifiziert. Ein Gastmethoden-Callback führt außerdem während des Callbacks
`sel_registerName`, rekursives `objc_msgSend` und `_strlen` über weitere SVCs
aus. Heap-Speicher wird über verschachteltes `_malloc`/`_realloc`/`_free` in
den Gast-Speicherbereichen registriert; eine Fixture schreibt, liest, vergrößert
und gibt die Allokation frei. Andere Aggregatlayouts und ein funktionierender
macOS-App- oder Frameworkstart sind damit nicht belegt.

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
und `ftruncate` an vorhandene Wintosh-Adapter. `fcntl` deckt Descriptor-
Duplikation und ausgewählte Flags ab; `getrlimit`/`setrlimit` kopieren geprüfte
Gaststrukturen. `flock` leitet Datei-Lock-/Unlock-Operationen weiter. Ein
begrenztes `ioctl`-Subset leitet `TIOCGWINSZ`, `TIOCSWINSZ` und `FIONREAD`
weiter. BSD-Socketaufrufe `socket`,
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
Mappings) und aktualisiert Host- sowie Gastschutzrechte. `munmap` kann
page-ausgerichtete Teilbereiche anonymer und placeholder-basierter Datei-
Mappings abtrennen. Host- und
Gasttabellen werden dabei gemeinsam in linke und rechte Restintervalle zerlegt;
die Windows-Reservierung bleibt erhalten, bis kein aktiver Teil mehr existiert.
Schreibbare `MAP_PRIVATE`-Datei-Views sichern die Bytes der überlebenden
Intervalle vor dem Split und stellen sie in den privaten Kopien wieder her;
der temporäre Speicherbedarf wächst mit der View-Größe. Nicht feste Datei-Views
verwenden auf Windows-Versionen mit `VirtualAlloc2`,
`MapViewOfFile3` und `UnmapViewOfFile2` eine Placeholder-Reservierung. Damit
können Darwin-seitige, seitenorientierte Datei-Offsets auch dann abgebildet
werden, wenn sie nicht an Windows' 64-KiB-Allokationsgranularität ausgerichtet
sind. Auf älteren Versionen bleibt der Granularitäts-Offset-und-Delta-Fallback
aktiv; dort ist `munmap` für Datei-Views weiterhin nur als Ganzes möglich.
Auf placeholder-fähigem Windows kann `MAP_FIXED` ein Intervall innerhalb einer
einzelnen besessenen Datei-View durch eine andere Datei-View oder durch
zero-initialisierten anonymen Speicher ersetzen. Beim Datei-zu-Datei-Pfad
bleiben die alten linken/rechten Views erhalten; beim Datei-zu-anonym-Pfad
wechselt das Zielintervall in den separaten anonymen Ownership-Index.
Umgekehrt kann eine Datei-View eine vollständige, zusammenhängende anonyme
Reservation am identischen Basisbereich ersetzen; Teilbereichs-Ersetzungen
anonymer Reservierungen durch Datei-Views sind noch offen. Feste Ersetzungen
anonymer Views sind weiterhin auf `MAP_FIXED`-Teilbereiche
beschränkt, die vollständig von derselben besessenen anonymen Reservierung
abgedeckt sind; dazu zählt auch eine decommittete Lücke, solange die
Windows-Reservierung noch durch aktive Nachbarintervalle gehalten wird. Der
Host recommittet die Windows-Seiten. Bei einer aktiven Teilersetzung gilt
`M′ = (M \\ U) ∪ U′`; beim Belegen einer Reservierungslücke gilt
`M′ = M ∪ U′`. Überlappungen mit Images, nicht besessenen Datei-Views, fremden
Reservierungen oder Adressen außerhalb der jeweiligen Reservierung schlagen
geschlossen fehl. Dadurch bleibt die Backend-Tabelle mit dem tatsächlich
besessenen Hostspeicher synchron. Formal ist eine Reservierung
`R = [b, b + n)` und ihre aktive Gastabbildung die Intervallmenge
`M = ⋃ᵢ [lᵢ, rᵢ)`. Ein Unmap-Intervall `U = [u, u + k)` ist nur gültig, wenn
`U ⊆ M` innerhalb derselben Reservierung gilt; danach wird `M′ = M \ U`
sowohl im Host-Ownership-Index als auch im Gast-Backend gespeichert.
`sysctl` ist auf die im vorhandenen Adapter
implementierten zweistufigen `KERN`- und `HW`-MIBs beschränkt. MIB und
Eingabewerte werden vorab kopiert beziehungsweise geprüft; Ausgaben laufen
über einen begrenzten Hostpuffer und werden danach in den Gast geschrieben.
Weitere Unix-Syscalls, Mach-Traps, Host-Imports und Cocoa bleiben offen.

Die optionale CMake-Integration erwartet weiterhin separat gebaute Bibliotheken.
Der Upstream-Pin, vcpkg-Baseline und Buildschritte sind im Repo dokumentiert;
der netzwerkgestützte vcpkg-Manifestmodus und der komponentengenaue
Lizenzabgleich bleiben Gates vor einer standardmäßigen Integration.

| Fähigkeit | Stand |
|---|---|
| ARM64 parse/map/Relokations-Subset | implementiert, bestehende Grenzen gelten |
| Dynarmic-Commit lokal untersucht | `9d458233`; Windows-x64-A64 statisch gebaut |
| Dynarmic-Abhängigkeit reproduzierbar gebaut | Offline-Buildscript aus sauberem Quellcheckout erfolgreich; vcpkg-Manifestmodus wegen DNS nicht validiert |
| A64-Backend in Wintosh | optionaler Adapter lokal gebaut; mehrere ARM64-Runtime-Fixtures laufen auf x64 Windows |
| A64-Trap-Zustand | x0–x30, v0–v31, SP, PC, PSTATE, FPCR und FPSR rückführbar und fortsetzbar |
| Darwin-UNIX-SVC | `exit`, `wait4` (mit lokaler Status-/Rusage-Kopie), `read`, `write`, `readv`, `writev`, `pread`, `pwrite`, `open`, `close`, `fcntl` (Descriptor-Duplikation und ausgewählte Flags), `getdirentries` (Darwin-64-bit-`dirent`), `poll` (geprüfte `pollfd`-Kopie), `select` (geprüfte `fd_set`-/`timeval`-Kopie), `ioctl` (`TIOCGWINSZ`, `TIOCSWINSZ`, `FIONREAD`), `setsockopt`, `getsockopt` (begrenzte Gastpufferkopie), `pathconf`, `fpathconf`, `getrlimit`, `setrlimit`, `link`, `unlink`, `chdir`, `chmod`, `fchmod`, `symlink`, `readlink`, `umask`, Prozess-/Gruppen-ID-Aufrufe, `dup`, `dup2`, `pipe`, `socket`, `socketpair`, `connect`, `bind`, `listen`, `accept`, `recvfrom`, `sendto`, `getpeername`, `getsockname`, `shutdown`, `gettimeofday`, `getrusage`, `getpriority`, `setpriority`, `access`, `rename`, `mkdir`, `rmdir`, `fsync`, `fdatasync`, `stat`, `lstat`, `fstat`, `statfs`, `fstatfs`, `lseek`, `truncate`, `ftruncate`, `mmap`, `munmap` (nur ganzes Mapping), `mprotect`, `msync`, `madvise`, `sysctl` (implementierte zweistufige `KERN`-/`HW`-MIBs), `kill`, `sigaction`, `sigprocmask`; andere UNIX-Nummern liefern `ENOSYS`; kein Mach-Trap-Dispatcher |
| Neue SVC-Adapter in diesem Stand | ARM64-Gastfixture belegt `pathconf`-/`fpathconf`-Limits und `EFAULT`, `getdirentries`-Datensatzlayout, Namen und Base-Offset sowie `EFAULT`, `poll`-/`select`-Schreibbereitschaft, `select`-Timeout-Kopie und `EFAULT`, `statfs`-/`fstatfs`-Metadatenkopien und `EFAULT`, Socket-Option-Set/Get-Rundlauf und `EFAULT`, `getlogin`/`setlogin`, `getgroups`/`setgroups`, gleichbleibende `setuid`/`setgid`/`setegid`/`seteuid`-Werte und geprüfte Pufferkopien, `getdtablesize`, `setpgid`, `getpgid`, `setsid`, `flock`, `fcntl` (Lesen/Setzen/Duplizieren), `getrlimit`/`setrlimit`, `FIONREAD`-Pipe-Ausgabe, `EPERM`/`ENOTTY` und Carry-Bit; Terminal-Winsize-Ausgaben bleiben ungeprüft |
| Zeitstempel-SVCs | `utimes` und `futimes` kopieren Gast-`timeval`-Paare in geprüfte temporäre Zeitwerte; die Fixture prüft erfolgreiche Pfad-/Deskriptoränderungen sowie `EINVAL` und `EFAULT` |
| Verzeichnis-SVCs | `fchdir` leitet den Arbeitsverzeichniswechsel aus dem gespeicherten Deskriptorpfad ab; die ARM64-Fixture prüft Erfolg und `EBADF` für einen ungültigen Deskriptor |
| x64-Host-Gast-Speicherprüfung | Segmentrechte und Bereichsgrenzen umgesetzt; Image-, Startup-, Heap-, mmap- und Host-Datenbereiche werden geprüft bzw. registriert und in Fixtures ausgeführt |
| A64-Import-Trampoline und ABI-Dispatcher | ausgewählte C-/Darwin-/Framework-Imports, Gastcallbacks, Heap-/Block- und Datenimporte, Stackargumente, HFA- und indirekte Struct-Rückgaben bestehen; übrige Aggregate und Signaturen bleiben offen |
| Schmale ARM64 Mach-O Runtime-Fixtures auf x64 Windows | mehrere Source-Fixtures für Syscalls, dyld, Objective-C, CoreFoundation und Speicher; keine echte macOS-App oder vollständige Bundle-Ausführung belegt |

Die lokalen Fixtures belegen nur die jeweils beschriebenen AArch64-Instruktions-
und Importpfade im Wintosh-Runner. Sie sind kein Beleg für eine ausführbare
allgemeine macOS-App oder vollständige Darwin-Kompatibilität.
