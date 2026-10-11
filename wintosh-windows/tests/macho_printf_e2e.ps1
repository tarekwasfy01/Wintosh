<#
This file is part of the Darling Windows port.

Copyright (C) 2026 Darling Windows port contributors

Licensed under the GNU General Public License, version 3.
#>

param(
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Fixture
)

$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Fixture) {
    Remove-Item -LiteralPath $Fixture -Force
}

& $Generator --emit-printf $Fixture
if ($LASTEXITCODE -ne 0) { throw "Mach-O printf fixture generation failed: $LASTEXITCODE" }

$output = & $Runner --startup-abi main $Fixture 2>&1
$exitCode = $LASTEXITCODE
$text = ($output -join "`n")
Write-Output $text
if ($exitCode -ne 0) { throw "Mach-O printf runner failed: $exitCode" }
if ($text -notmatch 'mach-o printf 7') {
    throw 'Mach-O printf output was not observed'
}

$bundle = Join-Path ([System.IO.Path]::GetDirectoryName($Fixture)) 'Printf.app'
$bundleContents = Join-Path $bundle 'Contents'
$bundleMacOS = Join-Path $bundleContents 'MacOS'
if (Test-Path -LiteralPath $bundle) {
    Remove-Item -LiteralPath $bundle -Recurse -Force
}
New-Item -ItemType Directory -Path $bundleMacOS -Force | Out-Null
Copy-Item -LiteralPath $Fixture -Destination (Join-Path $bundleMacOS 'Printf')
@'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>Printf</string></dict></plist>
'@ | Set-Content -LiteralPath (Join-Path $bundleContents 'Info.plist') -Encoding utf8

$bundleOutput = & $Runner --startup-abi main $bundle 2>&1
$bundleExitCode = $LASTEXITCODE
$bundleText = ($bundleOutput -join "`n")
Write-Output $bundleText
if ($bundleExitCode -ne 0 -or $bundleText -notmatch 'mach-o printf 7') {
    throw "Mach-O .app printf execution failed: $bundleExitCode"
}
Remove-Item -LiteralPath $bundle -Recurse -Force

Write-Output 'MACHO_PRINTF_E2E=PASS'
