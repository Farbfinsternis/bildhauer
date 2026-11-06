# Measures what sculpting costs on the PC and on an Android device, with the
# same model files on both. Results: build/perf/results.csv (see
# tests/perf_bench.cpp) and build/perf/render.csv. No input is simulated.
#
#   .\tools\perf-bench.ps1 -Device pc
#   .\tools\perf-bench.ps1 -Device android -Serial <adb-serial>
#   .\tools\perf-bench.ps1 -Device pc -Triangles 10000 -Model my.sc1
param(
  [Parameter(Mandatory)][ValidateSet('pc', 'android')][string]$Device,
  [string]$Serial,
  [int[]]$Triangles = @(10000, 50000, 100000, 250000, 500000, 1000000),
  [string[]]$Model = @(),
  [int]$PauseSeconds = 60,  # lets a phone or tablet cool down between models
  [switch]$RenderOnly       # Android: only the frame times in the app
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$perf = Join-Path $root 'build\perf'
$models = Join-Path $perf 'models'
New-Item -ItemType Directory -Force $models | Out-Null
$vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools'
$vcvars = "$vsRoot\VC\Auxiliary\Build\vcvars64.bat"
$vsCmake = "$vsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

function Invoke-Vs([string]$command) {
  cmd /c "`"$vcvars`" >nul 2>nul & $command"
  if ($LASTEXITCODE -ne 0) { throw "Failed: $command" }
}

# The model files come from the PC, so both devices measure the same meshes.
Invoke-Vs "`"$vsCmake`" --build `"$root\build-release`" --target perf_bench bildhauer"
$bench = Join-Path $root 'build-release\perf_bench.exe'
$files = @()
foreach ($count in $Triangles) {
  $file = Join-Path $models "sphere-$count.sc1"
  if (-not (Test-Path $file)) { & $bench --generate $count $file; if ($LASTEXITCODE -ne 0) { throw 'Generating a model failed' } }
  $files += $file
}
$files += $Model | ForEach-Object { (Resolve-Path $_).Path }
$results = Join-Path $perf 'results.csv'
$render = Join-Path $perf 'render.csv'
if (-not (Test-Path $results)) {
  'device,model,triangles,section,name,size,count,median_ms,p95_ms,worst_ms,tessellate_ms,tool_ms,geometry_ms,beautify_ms,pick_ms,dabs,triangles_after,extra' |
    Set-Content $results -Encoding ascii
}
if (-not (Test-Path $render)) { 'device,model,triangles,upload,median_ms,worst_ms' | Set-Content $render -Encoding ascii }

if ($Device -eq 'pc') {
  foreach ($file in $files) {
    $name = Split-Path $file -Leaf
    Write-Host "PC: $name"
    & $bench $file --device pc --out $results
    if ($LASTEXITCODE -ne 0) { throw "perf_bench failed on $name" }
    # upload in render.csv: 0 = idle frame, 1 = idle frame with a full upload,
    # 2 = whole stroke frame, 3 = the same with a full upload (Android only).
    $shot = Join-Path $perf "pc-$name.png"
    Start-Process (Join-Path $root 'build-release\bildhauer.exe') -ArgumentList "`"$file`" --snapshot `"$shot`" --frames 60 --no-ui" -Wait
    $lines = @(Get-Content "$shot.txt")
    if ($lines[0] -notmatch '^(\d+) triangles, median ([\d.]+), worst ([\d.]+)') { throw "Unexpected frame times: $lines" }
    $count = $Matches[1]
    "pc,$name,$count,0,$($Matches[2]),$($Matches[3])" | Add-Content $render -Encoding ascii
    if ($lines[1] -match '^stroke median ([\d.]+), worst ([\d.]+)') {
      "pc,$name,$count,2,$($Matches[1]),$($Matches[2])" | Add-Content $render -Encoding ascii
    }
    Remove-Item $shot, "$shot.txt" -ErrorAction SilentlyContinue
  }
  return
}

. 'F:\dev\Android\env.ps1'
$adb = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe'
$target = if ($Serial) { @('-s', $Serial) } else { @('-d') }
$cmakeDir = Join-Path $env:ANDROID_HOME 'cmake\3.31.6\bin'
$ndk = Join-Path $env:ANDROID_HOME 'ndk\30.0.16248370'
$buildDir = Join-Path $root 'build-android-bench'
# The app's own compiler settings: Debug with -O2 (android/app/build.gradle).
& "$cmakeDir\cmake.exe" -S $root -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$cmakeDir\ninja.exe" `
  "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 `
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Debug -DBILDHAUER_ANDROID_OPTIMIZE_DEBUG=ON -DBILDHAUER_BUILD_BENCHMARKS=ON | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Configuring the Android benchmark failed' }
& "$cmakeDir\cmake.exe" --build $buildDir --target perf_bench
if ($LASTEXITCODE -ne 0) { throw 'Building the Android benchmark failed' }

$remote = '/data/local/tmp/bildhauer-perf'
$label = (& $adb @target shell getprop ro.product.model).Trim() -replace '\s', '_'
& $adb @target shell "mkdir -p $remote"
& $adb @target push (Join-Path $buildDir 'perf_bench') "$remote/perf_bench" | Out-Null
& $adb @target shell "chmod 755 $remote/perf_bench"
$first = $true
foreach ($file in $files) {
  $name = Split-Path $file -Leaf
  if (-not $first) { Write-Host "Cooling down for $PauseSeconds s"; Start-Sleep $PauseSeconds }
  $first = $false
  Write-Host "${label}: $name"
  & $adb @target push $file "$remote/$name" | Out-Null
  if (-not $RenderOnly) {
    & $adb @target shell "rm -f $remote/out.csv; $remote/perf_bench $remote/$name --device $label --out $remote/out.csv"
    if ($LASTEXITCODE -ne 0) { throw "perf_bench failed on $name" }
    (& $adb @target shell "cat $remote/out.csv") | Where-Object { $_ } | Add-Content $results -Encoding ascii
  }

  # Frame times in the real app, on its real surface.
  & $adb @target shell "am force-stop org.bildhauer.app; run-as org.bildhauer.app cp $remote/$name files/bench.sc1"
  & $adb @target logcat -c
  & $adb @target shell 'am start -W -n org.bildhauer.app/.MainActivity --ez bench true' | Out-Null
  $log = @()
  for ($i = 0; $i -lt 120 -and -not ($log -match 'Bench done'); ++$i) {
    Start-Sleep 1
    $log = & $adb @target logcat -d -s bildhauer:I
  }
  foreach ($line in $log) {
    if ($line -match 'Bench render upload=(\d) triangles=(\d+) median=([\d.]+) worst=([\d.]+)') {
      "$label,$name,$($Matches[2]),$($Matches[1]),$($Matches[3]),$($Matches[4])" | Add-Content $render -Encoding ascii
    }
    if ($line -match 'Bench stroke (.*)') { Write-Host "  $($Matches[1])" }
  }
  if (-not ($log -match 'Bench done')) { Write-Warning "No frame times for $name; is the screen on and unlocked?" }
  & $adb @target shell "am force-stop org.bildhauer.app; run-as org.bildhauer.app rm -f files/bench.sc1; rm -f $remote/$name"
}
& $adb @target shell "rm -rf $remote"
