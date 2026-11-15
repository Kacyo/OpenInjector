param(
    [string]$Output = 'src\resources\app.ico'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$outPath = [System.IO.Path]::GetFullPath((Join-Path $root $Output))
$outDir = [System.IO.Path]::GetDirectoryName($outPath)
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

function New-RoundedPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = @()

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $scale = $size / 256.0
    $graphics.ScaleTransform($scale, $scale)

    $outer = New-RoundedPath 8 8 240 240 56
    $rect = New-Object System.Drawing.RectangleF(8, 8, 240, 240)
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        $rect,
        [System.Drawing.Color]::FromArgb(255, 0x8B, 0x7B, 0xFF),
        [System.Drawing.Color]::FromArgb(255, 0x4F, 0x9C, 0xFF),
        45.0)
    $graphics.FillPath($gradient, $outer)

    # Top highlight.
    $glowPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $glowPath.AddEllipse(16, -70, 224, 170)
    $glowBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.RectangleF(16, -70, 224, 170)),
        [System.Drawing.Color]::FromArgb(70, 255, 255, 255),
        [System.Drawing.Color]::FromArgb(0, 255, 255, 255),
        90.0)
    $clip = $graphics.Clip
    $graphics.SetClip($outer)
    $graphics.FillPath($glowBrush, $glowPath)
    $graphics.Clip = $clip

    # Inner outline.
    $inner = New-RoundedPath 14 14 228 228 50
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(46, 255, 255, 255), 3)
    $graphics.DrawPath($pen, $inner)

    # Lightning bolt.
    $bolt = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF(154, 34)),
        (New-Object System.Drawing.PointF(82, 142)),
        (New-Object System.Drawing.PointF(126, 142)),
        (New-Object System.Drawing.PointF(100, 226)),
        (New-Object System.Drawing.PointF(176, 108)),
        (New-Object System.Drawing.PointF(132, 108))
    )
    $shadowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(52, 22, 20, 66))
    $shadow = [System.Drawing.PointF[]]($bolt | ForEach-Object {
        New-Object System.Drawing.PointF(($_.X + 4), ($_.Y + 5))
    })
    $graphics.FillPolygon($shadowBrush, $shadow)

    $boltBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $graphics.FillPolygon($boltBrush, $bolt)

    $graphics.Dispose()
    $gradient.Dispose()
    $glowBrush.Dispose()
    $pen.Dispose()
    $shadowBrush.Dispose()
    $boltBrush.Dispose()
    $glowPath.Dispose()
    $outer.Dispose()
    $inner.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += [PSCustomObject]@{ Size = $size; Bytes = $stream.ToArray() }
    $stream.Dispose()
    $bitmap.Dispose()
}

$ico = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($ico)
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$images.Count)

$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dimension = if ($image.Size -ge 256) { 0 } else { $image.Size }
    $writer.Write([byte]$dimension)
    $writer.Write([byte]$dimension)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$image.Bytes.Length)
    $writer.Write([UInt32]$offset)
    $offset += $image.Bytes.Length
}
foreach ($image in $images) {
    $writer.Write($image.Bytes)
}
$writer.Flush()
[System.IO.File]::WriteAllBytes($outPath, $ico.ToArray())
$writer.Dispose()
$ico.Dispose()

Write-Host "Icon generated: $outPath ($($images.Count) sizes)"
