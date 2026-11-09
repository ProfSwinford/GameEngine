# ============================================================================
#  Syntax-check every code example in the scripting reference against the real
#  engine headers, at the standard a project's scripts are compiled with.
#
#  A precompiled header of <engine/Engine.h> is what makes this affordable:
#  the umbrella header plus nlohmann/json is about six seconds per translation
#  unit cold, and there are nearly 800 of them. With the pch the whole sweep is
#  roughly a minute.
#
#      perl tools/apidoc/extract.pl
#      pwsh tools/apidoc/compile-examples.ps1
#      perl tools/apidoc/analyse.pl
#
#  Needs a configured debug build tree, for nlohmann/json's headers.
# ============================================================================
param(
    [string]$ExampleDir = "build\apidoc-examples",
    [string]$VcVars     = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    [string]$Json       = "build\debug\_deps\nlohmann_json-src\include"
)

$ErrorActionPreference = 'Continue'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$ex   = Join-Path $repo $ExampleDir
$inc1 = Join-Path $repo 'engine\include'
$inc2 = Join-Path $repo $Json
$work = Join-Path $ex '_pch'
$log  = Join-Path $ex 'compile.log'

if (-not (Test-Path $ex))   { Write-Output "no examples at $ex - run tools/apidoc/extract.pl first"; exit 1 }
if (-not (Test-Path $VcVars)) { Write-Output "vcvars64.bat not found at $VcVars - pass -VcVars"; exit 1 }
if (-not (Test-Path $inc2)) { Write-Output "nlohmann/json headers not found at $inc2 - configure a debug build first"; exit 1 }

if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path $work | Out-Null
Set-Content -Encoding utf8 -Path (Join-Path $work 'pch.cpp') -Value '#include <engine/Engine.h>'

# /std:c++20 and not c++latest: a project's SCRIPTS are compiled as C++20 even
# though the engine itself is C++23, so this is the standard that matters here.
$flags = "/nologo /std:c++20 /EHsc /permissive- /W3 /I `"$inc1`" /I `"$inc2`""

Remove-Item -Force $log -ErrorAction SilentlyContinue
$sw = [Diagnostics.Stopwatch]::StartNew()

$pchCmd = "cl $flags /Yc`"engine/Engine.h`" /Fp`"$work\engine.pch`" /Fo`"$work\pch.obj`" /c `"$work\pch.cpp`""
$r = cmd /c "`"$VcVars`" >NUL 2>&1 && $pchCmd 2>&1"
if ($LASTEXITCODE -ne 0) {
    "PCH BUILD FAILED" | Add-Content $log
    $r | Add-Content $log
    Write-Output "pch failed - see $log"
    exit 1
}
Write-Output "pch built in $([math]::Round($sw.Elapsed.TotalSeconds,1))s"

$files = Get-ChildItem (Join-Path $ex '*.cpp') | Sort-Object Name
$batch = 40
$done  = 0

for ($i = 0; $i -lt $files.Count; $i += $batch) {
    $slice = $files[$i..([math]::Min($i + $batch - 1, $files.Count - 1))]
    $list  = ($slice | ForEach-Object { "`"$($_.FullName)`"" }) -join ' '
    $cmd = "cl $flags /MP8 /Zs /Yu`"engine/Engine.h`" /Fp`"$work\engine.pch`" $list 2>&1"
    (cmd /c "`"$VcVars`" >NUL 2>&1 && $cmd") | Add-Content $log
    $done += $slice.Count
    Write-Output "$done / $($files.Count)  ($([math]::Round($sw.Elapsed.TotalSeconds,0))s)"
}

$sw.Stop()
Write-Output ""
Write-Output "compiled $($files.Count) translation units in $([math]::Round($sw.Elapsed.TotalMinutes,1)) min"
Write-Output "now run: perl tools/apidoc/analyse.pl"
