param(
  [switch]$Install,
  [switch]$SmokeTest,
  [string]$Serial
)
$ErrorActionPreference = 'Stop'
$toolEnvironment = 'F:\dev\Android\env.ps1'
if (-not (Test-Path -LiteralPath $toolEnvironment)) {
  throw "Android tools are expected under F:\dev\Android. See android/README.md."
}
. $toolEnvironment
$tasks = @('assembleDebug')
if ($SmokeTest) { $tasks += 'assembleDebugAndroidTest' }
& "$PSScriptRoot\gradlew.bat" -p $PSScriptRoot @tasks --console plain
if ($LASTEXITCODE -ne 0) { throw 'Android build failed' }
if ($Install -or $SmokeTest) {
  $adb = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe'
  $device = if ($Serial) { @('-s', $Serial) } else { @('-d') }
  & $adb @device install -r "$PSScriptRoot\app\build\outputs\apk\debug\app-debug.apk"
  if ($LASTEXITCODE -ne 0) { throw 'APK installation failed' }
  if ($SmokeTest) {
    & $adb @device install -r "$PSScriptRoot\app\build\outputs\apk\androidTest\debug\app-debug-androidTest.apk"
    if ($LASTEXITCODE -ne 0) { throw 'Test APK installation failed' }
    $result = & $adb @device shell am instrument -w org.bildhauer.app.test/org.bildhauer.app.SmokeInstrumentation
    $result | Write-Output
    if ($LASTEXITCODE -ne 0 -or ($result -join "`n") -notmatch 'PASS:') { throw 'Device smoke test failed' }
  }
  & $adb @device shell am start -W -n org.bildhauer.app/.MainActivity
  if ($LASTEXITCODE -ne 0) { throw 'Could not start the app' }
}
