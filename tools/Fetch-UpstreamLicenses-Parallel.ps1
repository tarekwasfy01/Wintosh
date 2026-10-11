param(
    [string]$Inventory = (Join-Path $PSScriptRoot '..\licenses\EXTERNAL-COMPONENT-PROVENANCE.csv'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\licenses\upstream-candidates')
)
$ErrorActionPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$names = @('LICENSE','LICENSE.txt','LICENSE.md','COPYING','COPYING.txt','NOTICE','NOTICE.txt','COPYRIGHT','COPYRIGHT.txt','LICENCE','LICENCE.txt','LICENCE.md')
$rows = @(Import-Csv $Inventory | Where-Object { $_.license_status -eq 'review-required' -and $_.upstream_origin -match '^https://github\.com/([^/]+)/([^/]+?)(?:\.git)?$' })
$results = $rows | ForEach-Object -ThrottleLimit 12 -Parallel {
    $row = $_; $m = [regex]::Match($row.upstream_origin, '^https://github\.com/([^/]+)/([^/]+?)(?:\.git)?$')
    $owner=$m.Groups[1].Value; $repo=$m.Groups[2].Value; $safe=($row.component -replace '[^A-Za-z0-9_.-]','_'); $commit=if($row.head_commit){$row.head_commit}else{'HEAD'}
    $found=$false
    foreach($name in $using:names) {
        if($found){break}
        $uri="https://raw.githubusercontent.com/$owner/$repo/$commit/$name"
        try {
            $r=Invoke-WebRequest -Uri $uri -Headers @{ 'User-Agent'='Wintosh-license-inventory/1.0' } -UseBasicParsing -TimeoutSec 6
            if($r.StatusCode -eq 200 -and -not [string]::IsNullOrWhiteSpace($r.Content)) {
                $dest=Join-Path $using:OutputDirectory "$safe-$name"
                [IO.File]::WriteAllText($dest,$r.Content,[Text.Encoding]::UTF8)
                [pscustomobject]@{component=$row.component;status='candidate-found';license_file=$dest;source_url=$uri;sha256=(Get-FileHash $dest -Algorithm SHA256).Hash}; $found=$true
            }
        } catch {}
    }
    if(-not $found){[pscustomobject]@{component=$row.component;status='not-found';license_file='';source_url='';sha256=''}}
}
$path=Join-Path $OutputDirectory 'CANDIDATE-INVENTORY.csv'; $old=@(); if(Test-Path $path){$old=@(Import-Csv $path)}
$merged=@($old+$results)|Group-Object component|ForEach-Object { $f=$_.Group|Where-Object status -eq 'candidate-found'|Select-Object -Last 1; if($f){$f}else{$_.Group|Select-Object -Last 1} }
$merged|Sort-Object component|Export-Csv $path -NoTypeInformation -Encoding UTF8
"COMPONENTS=$(@($rows).Count)"; "RUN_FOUND=$(@($results|Where-Object status -eq 'candidate-found').Count)"; "RUN_NOT_FOUND=$(@($results|Where-Object status -eq 'not-found').Count)"; "INVENTORY=$(@($merged).Count)"
