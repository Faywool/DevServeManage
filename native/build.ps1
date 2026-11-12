$ErrorActionPreference = 'Stop'
$native = $PSScriptRoot
$root = Split-Path -Parent $native
$kit = if ($env:W64DEVKIT) { $env:W64DEVKIT } else { Join-Path (Split-Path -Parent $root) '.tools\w64kit\w64devkit' }
if (-not (Test-Path (Join-Path $kit 'bin'))) {
    throw ('w64devkit not found: ' + $kit)
}
$gxx = Join-Path $kit 'bin\g++.exe'
$useGccDriver = $false
if (-not (Test-Path $gxx)) {
    # Some w64devkit archives omit the g++ alias but still ship the C++ frontend.
    $gxx = Join-Path $kit 'bin\gcc.exe'
    $useGccDriver = $true
}
if (-not (Test-Path $gxx)) { throw ('C++ compiler not found: ' + $gxx) }
$windres = Join-Path $kit 'bin\windres.exe'
$out = Join-Path $native 'dist'
$build = Join-Path $native 'build'
New-Item -ItemType Directory -Force -Path $out, $build | Out-Null

Push-Location $native
try {
    $res = Start-Process -FilePath $windres -ArgumentList @('app.rc', '-O', 'coff', '-o', 'build\app.res.o') -NoNewWindow -Wait -PassThru
    if ($res.ExitCode -ne 0) { throw 'windres failed' }

    $gxxArgs = @(
        '-std=c++20', '-O2', '-municode', '-mwindows', '-static-libgcc', '-static-libstdc++',
        '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00', '-I', 'src',
        'src\main.cpp', 'src\core.cpp', 'build\app.res.o',
        '-o', (Join-Path $out 'DevServeManage.exe'),
        '-ld2d1', '-ldwrite', '-ldwmapi', '-lole32', '-loleaut32', '-luuid',
        '-lshell32', '-lshlwapi', '-luser32', '-lgdi32', '-ladvapi32',
        '-lcomctl32', '-liphlpapi', '-lws2_32', '-lcomdlg32', '-static'
    )
    if ($useGccDriver) { $gxxArgs += '-lstdc++' }
    $compiler = Start-Process -FilePath $gxx -ArgumentList $gxxArgs -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $build 'compiler.out') `
        -RedirectStandardError (Join-Path $build 'compiler.err')
    if ($compiler.ExitCode -ne 0) {
        Get-Content (Join-Path $build 'compiler.err') -Tail 30
        throw 'link failed'
    }
    Write-Host ('Built: ' + (Join-Path $out 'DevServeManage.exe'))
    Get-Item (Join-Path $out 'DevServeManage.exe') | Select-Object FullName, Length, LastWriteTime
}
finally {
    Pop-Location
}
