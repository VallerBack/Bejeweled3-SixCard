$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$out = Join-Path $root 'out'
New-Item -ItemType Directory -Force -Path $build,$out | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio C++ x86/x64 tools and Windows SDK.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
function Compile([string]$line) {
    $batch = Join-Path $build 'compile.cmd'
    @('@echo off', ('call "' + $vcvars + '" x86'), 'if errorlevel 1 exit /b %errorlevel%', $line) | Set-Content -LiteralPath $batch -Encoding ascii
    Push-Location $build
    try { & $env:ComSpec /d /c $batch; if ($LASTEXITCODE -ne 0) { throw "Compile/test failed: $LASTEXITCODE" } }
    finally { Pop-Location }
}
$names = 'deferred_save.cpp','mode_scope.cpp','danger_hooks.cpp','six_card_core.cpp','game_stats_persistence.cpp','poker_wait_six.cpp','poker_sidecar.cpp','pokerboard_hook.cpp','pokerboard_face_draw.cpp','pokerendlevel_stats.cpp','pokerboard_capture_hook.cpp','pokerboard_endlevel_probe.cpp'
$sources = ($names | ForEach-Object { '"' + (Join-Path $root ('src/game/' + $_)) + '"' }) -join ' '
Compile ('cl.exe /nologo /utf-8 /MT /LD /std:c++14 /EHsc /O2 /W4 ' + $sources + ' /link /OUT:"' + (Join-Path $out 'BJ3SixCard.dll') + '"')
Compile ('cl.exe /nologo /utf-8 /MT /std:c++14 /EHsc /O2 /W4 /DUNICODE /D_UNICODE "' + (Join-Path $root 'src/helper/BJ3DllLoader.cpp') + '" /link /OUT:"' + (Join-Path $out 'BJ3DllLoader.exe') + '"')
# Generate a build-local launcher pinned to this build, preserving source pins.
$launcher = Get-Content -Raw -Encoding UTF8 (Join-Path $root 'src/launcher/bj3_ranker_draw_launch.cpp')
$dllHash = (Get-FileHash (Join-Path $out 'BJ3SixCard.dll')).Hash.ToLowerInvariant()
$loaderHash = (Get-FileHash (Join-Path $out 'BJ3DllLoader.exe')).Hash.ToLowerInvariant()
$launcher = $launcher -replace '(?s)(kDllSha256\[\]\s*=\s*L")[a-f0-9]+', ('${1}' + $dllHash)
$launcher = $launcher -replace '(?s)(kLoaderSha256\[\]\s*=\s*L")[a-f0-9]+', ('${1}' + $loaderHash)
$pair = (Join-Path $root 'src/game/stats_pair_io.h').Replace('\','/')
$profile = (Join-Path $root 'src/launcher/profile_isolation.h').Replace('\','/')
$launcher = $launcher.Replace('"../game/stats_pair_io.h"', ('"' + $pair + '"')).Replace('"profile_isolation.h"', ('"' + $profile + '"'))
$generated = Join-Path $build 'launcher.cpp'
[IO.File]::WriteAllText($generated,$launcher,(New-Object Text.UTF8Encoding($false)))
Compile ('cl.exe /nologo /utf-8 /MT /std:c++17 /EHsc /O2 /W4 "' + $generated + '" /link /OUT:"' + (Join-Path $out 'BJ3SixCard.exe') + '"')
foreach ($test in 'test_mode_scope','test_danger_steps') {
    Compile ('cl.exe /nologo /utf-8 /MT /std:c++14 /EHsc /O2 /W4 "' + (Join-Path $root ('tests/' + $test + '.cpp')) + '" "' + (Join-Path $root 'src/game/six_card_core.cpp') + '" /link /OUT:"' + (Join-Path $build ($test + '.exe')) + '"')
    & (Join-Path $build ($test + '.exe'))
    if ($LASTEXITCODE -ne 0) { throw "Test failed: $test" }
}
Get-ChildItem $out -File | Get-FileHash -Algorithm SHA256 | Select-Object Hash,Path
