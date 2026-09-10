# Rebuild the original ProcessLens icon. Requires Windows PowerShell / System.Drawing.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$iconRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\assets'))
[IO.Directory]::CreateDirectory($iconRoot) | Out-Null
$iconImages = @()
foreach ($iconSize in @(16, 24, 32, 48, 64, 128, 256)) {
    $bitmap = [Drawing.Bitmap]::new($iconSize, $iconSize)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform($iconSize / 256.0, $iconSize / 256.0)
    $background = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 17, 24, 33))
    $shape = [Drawing.Drawing2D.GraphicsPath]::new()
    $shape.AddArc(4, 4, 64, 64, 180, 90)
    $shape.AddArc(188, 4, 64, 64, 270, 90)
    $shape.AddArc(188, 188, 64, 64, 0, 90)
    $shape.AddArc(4, 188, 64, 64, 90, 90)
    $shape.CloseFigure()
    $graphics.FillPath($background, $shape)
    $pen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 107, 229, 187), 13)
    $pen.StartCap = $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
    $graphics.DrawEllipse($pen, 49, 43, 139, 139)
    $graphics.DrawLine($pen, 173, 166, 212, 205)
    $points = [Drawing.PointF[]]@([Drawing.PointF]::new(69, 120), [Drawing.PointF]::new(96, 120), [Drawing.PointF]::new(110, 86), [Drawing.PointF]::new(132, 147), [Drawing.PointF]::new(145, 113), [Drawing.PointF]::new(167, 113))
    $pen.Width = 9
    $graphics.DrawLines($pen, $points)
    $stream = [IO.MemoryStream]::new()
    $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    $iconImages += ,$stream.ToArray()
    $stream.Dispose(); $pen.Dispose(); $shape.Dispose(); $background.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
$output = [IO.File]::Create((Join-Path $iconRoot 'ProcessLens.ico'))
$writer = [IO.BinaryWriter]::new($output)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$iconImages.Count)
    $offset = 6 + 16 * $iconImages.Count
    $sizes = @(16, 24, 32, 48, 64, 128, 256)
    for ($index = 0; $index -lt $iconImages.Count; $index++) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$iconImages[$index].Length); $writer.Write([uint32]$offset)
        $offset += $iconImages[$index].Length
    }
    foreach ($imageBytes in $iconImages) { $writer.Write([byte[]]$imageBytes) }
} finally { $writer.Dispose(); $output.Dispose() }
Write-Output 'Generated assets/ProcessLens.ico (16–256 px)'
