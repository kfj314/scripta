# Draws the Scripta Sync Server icon (a simple typewriter on a sage tile) into a multi-size .ico.
param([string]$Out = "$PSScriptRoot\scripta-server.ico")
Add-Type -AssemblyName System.Drawing

# palette (matches Pal in ScriptaSyncServer.cs)
function C($r, $g, $b) { [System.Drawing.Color]::FromArgb($r, $g, $b) }
$SageLight = C 163 177 138; $SageDeep = C 107 122 90
$Charcoal  = C 47 51 55;    $CharSoft = C 74 79 84
$Brown     = C 123 99 82;   $Orange   = C 192 97 43
$Icy       = C 236 244 247; $IcyMid   = C 168 200 214

function New-RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = [Math]::Max(0.5, $r * 2)
  $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
  $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
  $p.CloseFigure(); $p
}
function Brush($c) { New-Object System.Drawing.SolidBrush $c }
function Pt($x, $y) { [System.Drawing.PointF]::new($x, $y) }

function Draw-Icon([int]$S) {
  $bmp = New-Object System.Drawing.Bitmap $S, $S
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'
  $k = $S / 256.0
  $small = $S -lt 32

  # tile
  $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush (Pt 0 0), (Pt $S $S), $SageLight, $SageDeep
  $g.FillPath($grad, (New-RoundRect (8*$k) (8*$k) (240*$k) (240*$k) (52*$k)))

  # paper rising out of the roller
  $g.FillPath((Brush $Icy), (New-RoundRect (80*$k) (34*$k) (96*$k) (90*$k) (4*$k)))
  if (-not $small) {
    $lh = [Math]::Max(1.0, 9*$k)
    $g.FillRectangle((Brush $Brown),    96*$k, 52*$k, 46*$k, $lh)   # heading
    $g.FillRectangle((Brush $CharSoft), 96*$k, 70*$k, 64*$k, $lh)
    $g.FillRectangle((Brush $CharSoft), 96*$k, 86*$k, 54*$k, $lh)
  }

  # carriage return lever (behind the roller end)
  $lever = New-Object System.Drawing.Pen $Orange, ([Math]::Max(1.5, 12*$k))
  $lever.StartCap = 'Round'; $lever.EndCap = 'Round'
  $g.DrawLine($lever, 60*$k, 110*$k, 30*$k, 84*$k)

  # platen roller + knobs
  $g.FillPath((Brush $Charcoal), (New-RoundRect (52*$k) (104*$k) (152*$k) (26*$k) (13*$k)))
  $g.FillEllipse((Brush $Brown), 34*$k, 103*$k, 28*$k, 28*$k)
  $g.FillEllipse((Brush $Brown), 194*$k, 103*$k, 28*$k, 28*$k)

  # body: upper block + sloped keyboard deck
  $body = New-Object System.Drawing.Drawing2D.GraphicsPath
  $body.AddPolygon([System.Drawing.PointF[]]@((Pt (62*$k) (128*$k)), (Pt (194*$k) (128*$k)), (Pt (222*$k) (214*$k)), (Pt (34*$k) (214*$k))))
  $g.FillPath((Brush $Charcoal), $body)
  $g.FillPath((Brush $Charcoal), (New-RoundRect (30*$k) (196*$k) (196*$k) (24*$k) (10*$k)))
  # type window
  $g.FillPath((Brush $Orange), (New-RoundRect (116*$k) (134*$k) (24*$k) (8*$k) (3*$k)))

  # keys
  if (-not $small) {
    $kr = 13*$k
    foreach ($row in @(@(156, 6, 70), @(176, 7, 58))) {
      $y = $row[0]*$k; $n = $row[1]; $x0 = $row[2]*$k
      $step = (256*$k - 2*$x0) / ($n - 1)
      for ($i = 0; $i -lt $n; $i++) { $g.FillEllipse((Brush $Icy), $x0 + $i*$step - $kr/2, $y - $kr/2, $kr, $kr) }
    }
  }
  $g.FillPath((Brush $IcyMid), (New-RoundRect (84*$k) (198*$k) (88*$k) ([Math]::Max(2, 11*$k)) (5*$k)))   # space bar

  $g.Dispose(); $bmp
}

# ICO container with PNG-compressed entries
$sizes = 256, 48, 32, 24, 16
$pngs = foreach ($s in $sizes) { $b = Draw-Icon $s; $ms = New-Object IO.MemoryStream; $b.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose(); ,$ms.ToArray() }
$fs = [IO.File]::Create($Out); $w = New-Object IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]; $dim = if ($s -ge 256) { 0 } else { $s }
  $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
  $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
  $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Close()

# preview: 256 plus the small sizes, each shown at 1x and 4x (nearest-neighbour) for checking legibility
$prev = New-Object System.Drawing.Bitmap 560, 268
$pg = [System.Drawing.Graphics]::FromImage($prev); $pg.Clear((C 245 246 241))
$big = Draw-Icon 256; $pg.DrawImage($big, 6, 6, 256, 256)
$pg.InterpolationMode = 'NearestNeighbor'; $pg.PixelOffsetMode = 'Half'
$x = 276
foreach ($s in 48, 32, 16) {
  $b = Draw-Icon $s
  $pg.DrawImage($b, $x, 10, $s, $s)
  $z = [Math]::Min(4, [int](190 / $s)); $pg.DrawImage($b, $x, 70, $s * $z, $s * $z)
  $x += [Math]::Max($s * $z, 48) + 12
}
$prev.Save("$PSScriptRoot\icon-preview.png", [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host "Wrote $Out"
