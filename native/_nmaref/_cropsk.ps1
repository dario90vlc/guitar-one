Add-Type -AssemblyName System.Drawing
$src = "C:\Users\Usuario\.omp\agent\sessions\-Desktop-Nueva carpeta (7)\2026-10-09T17-21-05-944Z_01a121ae-cb98-70f3-aa46-fd6f9aa9d9bb\local\pasted-image-1797189940c21e93.png"
$out = "C:\Windows\Temp"
$img = [System.Drawing.Image]::FromFile($src)
Write-Output ("imagen: {0}x{1}" -f $img.Width, $img.Height)
# fila de la clave SECRETA y fila de la LEGACY, ampliadas x4
$rows = @( @{n='sec';  y=262}, @{n='leg'; y=614} )
foreach ($r in $rows) {
    $rect = New-Object System.Drawing.Rectangle(85, $r.y, 430, 34)
    $crop = New-Object System.Drawing.Bitmap(1720, 136)
    $g = [System.Drawing.Graphics]::FromImage($crop)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle(0,0,1720,136)), $rect, 'Pixel')
    $g.Dispose()
    $p = Join-Path $out ("sk_" + $r.n + ".png")
    $crop.Save($p); $crop.Dispose()
    Write-Output ("guardado " + $p)
}
$img.Dispose()
