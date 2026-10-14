<# Original Wintosh fixture tooling; GPL-3.0-only.
   Darwin objects are source-derived test artifacts, not Apple code. #>
param(
    [Parameter(Mandatory = $true)][string]$Clang,
    [string]$OutputDirectory,
    [string]$Linker
)
$ErrorActionPreference = 'Stop'
$clangPath = (Get-Item -LiteralPath $Clang).FullName
if ([System.IO.Path]::GetFileNameWithoutExtension($clangPath) -eq 'clang-cl') {
    $candidate = Join-Path (Split-Path $clangPath) 'clang.exe'
    if (Test-Path -LiteralPath $candidate) { $clangPath = $candidate }
}
$sources = Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.c' | Sort-Object Name
if ($OutputDirectory) { New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null }
foreach ($source in $sources) {
    $flags = @('-target', 'x86_64-apple-darwin', '-fsyntax-only', '-x', 'c')
    if ($source.BaseName -eq 'pthread-tlv') {
        # The installed Clang rejects Darwin TLS in syntax-only mode; retain
        # the __thread source and validate its C syntax with the host target.
        $flags = @('-fsyntax-only', '-x', 'c')
    }
    & $clangPath @flags $source.FullName
    if ($LASTEXITCODE -ne 0) { throw "fixture syntax failed: $($source.Name)" }
    Write-Output ("FIXTURE_SYNTAX={0}=PASS" -f $source.BaseName)
    if ($OutputDirectory -and $source.BaseName -ne 'pthread-tlv') {
        $object = Join-Path $OutputDirectory ($source.BaseName + '.o')
        $objectFlags = @('-target', 'x86_64-apple-darwin', '-c', '-x', 'c')
        if ($source.BaseName -eq 'memory') {
            $objectFlags += '-fstack-protector-strong'
        }
        & $clangPath @objectFlags $source.FullName -o $object
        if ($LASTEXITCODE -ne 0) { throw "fixture Darwin object build failed: $($source.Name)" }
        $bytes = [System.IO.File]::ReadAllBytes($object)
        if ($bytes.Length -lt 4 -or $bytes[0] -ne 0xCF -or $bytes[1] -ne 0xFA -or
            $bytes[2] -ne 0xED -or $bytes[3] -ne 0xFE) {
            throw "fixture is not a 64-bit Mach-O object: $($source.Name)"
        }
        Write-Output ("FIXTURE_MACHO_OBJECT={0}=PASS" -f $source.BaseName)
        if ($source.BaseName -eq 'memory') {
            $text = [System.Text.Encoding]::ASCII.GetString($bytes)
            if ($text -notmatch '___stack_chk_guard') {
                throw 'memory fixture does not reference Darwin ___stack_chk_guard'
            }
            Write-Output 'FIXTURE_STACK_GUARD=memory=PASS'
        }
        if ($Linker) {
            $executable = Join-Path $OutputDirectory ($source.BaseName + '.macho')
            $linkFlags = @('-arch', 'x86_64', '-platform_version', 'macos', '10.15', '14.0',
                '-e', '_main', '-undefined', 'dynamic_lookup', '-o', $executable, $object)
            & $Linker @linkFlags
            if ($LASTEXITCODE -ne 0) {
                throw "fixture Mach-O link failed: $($source.Name)"
            }
            $linkedBytes = [System.IO.File]::ReadAllBytes($executable)
            if ($linkedBytes.Length -lt 4 -or $linkedBytes[0] -ne 0xCF -or
                $linkedBytes[1] -ne 0xFA -or $linkedBytes[2] -ne 0xED -or
                $linkedBytes[3] -ne 0xFE) {
                throw "linked fixture is not a 64-bit Mach-O executable: $($source.Name)"
            }
            Write-Output ("FIXTURE_MACHO_EXECUTABLE={0}=PASS" -f $source.BaseName)
        }
    }
}
Write-Output ("FIXTURE_COUNT={0}" -f $sources.Count)
