# Builds the app icons from the logo (docs/bildhauer.png, transparent):
# - src/app/bildhauer.ico for Windows, PNG frames from 16 to 256 px
# - Android adaptive launcher icon foregrounds, the logo 58 dp wide in the
#   108 dp layer, in mipmap-mdpi .. mipmap-xxxhdpi
# Run from anywhere: powershell -File tools/make-icons.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$logo = [Drawing.Image]::FromFile((Join-Path $root 'docs/bildhauer.png'))

# The logo scaled into a transparent square of `canvas` pixels, `size` wide.
function Render([int]$canvas, [int]$size) {
  $bmp = New-Object Drawing.Bitmap $canvas, $canvas, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::HighQuality
  $g.Clear([Drawing.Color]::Transparent)
  $at = [int](($canvas - $size) / 2)
  $g.DrawImage($logo, (New-Object Drawing.Rectangle $at, $at, $size, $size))
  $g.Dispose()
  return $bmp
}

function PngBytes($bmp) {
  $ms = New-Object IO.MemoryStream
  $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png)
  return ,$ms.ToArray()
}

# Windows: an ICO whose frames are PNGs (supported since Windows Vista).
$sizes = 16, 24, 32, 48, 64, 128, 256
$frames = foreach ($s in $sizes) { ,(PngBytes (Render $s $s)) }
$ico = New-Object IO.MemoryStream
$w = New-Object IO.BinaryWriter $ico
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i] % 256  # 0 means 256
  $w.Write([byte]$s); $w.Write([byte]$s); $w.Write([byte]0); $w.Write([byte]0)
  $w.Write([uint16]1); $w.Write([uint16]32)
  $w.Write([uint32]$frames[$i].Length); $w.Write([uint32]$offset)
  $offset += $frames[$i].Length
}
foreach ($f in $frames) { $w.Write($f) }
[IO.File]::WriteAllBytes((Join-Path $root 'src/app/bildhauer.ico'), $ico.ToArray())

# Android: 108 dp foreground layers. Launchers show the middle 72 dp, round
# ones as a circle: at 58 dp the lettering at the bottom stays inside it.
$res = Join-Path $root 'android/app/src/main/res'
$densities = @{ mdpi = 1.0; hdpi = 1.5; xhdpi = 2.0; xxhdpi = 3.0; xxxhdpi = 4.0 }
foreach ($d in $densities.Keys) {
  $dir = Join-Path $res "mipmap-$d"
  New-Item -ItemType Directory -Force $dir | Out-Null
  $scale = $densities[$d]
  (Render ([int](108 * $scale)) ([int](58 * $scale))).Save((Join-Path $dir 'ic_launcher_foreground.png'), [Drawing.Imaging.ImageFormat]::Png)
}
$logo.Dispose()
Write-Output 'Icons written.'
