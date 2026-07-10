[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPngPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputRcPath,

    [Parameter(Mandatory = $true)]
    [string]$ResourceHeaderPath,

    [ValidateRange(3, 16384)]
    [int]$OutputWidth = 768,

    [ValidateRange(2, 16384)]
    [int]$OutputHeight = 512,

    [ValidateRange(1, 10485760)]
    [int]$MaximumPngBytes = 409600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-AtomicBytes {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes
    )

    $temporaryPath = "$Path.tmp.$PID"
    $backupPath = "$Path.backup.$PID"
    try {
        [System.IO.File]::WriteAllBytes($temporaryPath, $Bytes)
        if ([System.IO.File]::Exists($Path)) {
            [System.IO.File]::Replace($temporaryPath, $Path, $backupPath, $true)
            [System.IO.File]::Delete($backupPath)
        }
        else {
            [System.IO.File]::Move($temporaryPath, $Path)
        }
    }
    finally {
        if ([System.IO.File]::Exists($temporaryPath)) {
            [System.IO.File]::Delete($temporaryPath)
        }
        if ([System.IO.File]::Exists($backupPath)) {
            [System.IO.File]::Delete($backupPath)
        }
    }
}

function Write-AtomicUtf8Text {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $bytes = $utf8NoBom.GetBytes($Text)
    Write-AtomicBytes -Path $Path -Bytes $bytes
}

$fullInputPath = [System.IO.Path]::GetFullPath($InputPath)
$fullOutputPngPath = [System.IO.Path]::GetFullPath($OutputPngPath)
$fullOutputRcPath = [System.IO.Path]::GetFullPath($OutputRcPath)
$fullResourceHeaderPath = [System.IO.Path]::GetFullPath($ResourceHeaderPath)

if (-not [System.IO.File]::Exists($fullInputPath)) {
    throw "Logo source is missing: $fullInputPath"
}
if (-not [System.IO.File]::Exists($fullResourceHeaderPath)) {
    throw "Resource header is missing: $fullResourceHeaderPath"
}
if (($OutputWidth % 3) -ne 0 -or ($OutputHeight % 2) -ne 0) {
    throw "Output atlas must be divisible by 3x2: ${OutputWidth}x${OutputHeight}"
}

$outputCellWidth = [int]($OutputWidth / 3)
$outputCellHeight = [int]($OutputHeight / 2)
if ($outputCellWidth -ne $outputCellHeight) {
    throw "Output atlas cells must be square: ${outputCellWidth}x${outputCellHeight}"
}

$outputDirectory = [System.IO.Path]::GetDirectoryName($fullOutputPngPath)
$rcDirectory = [System.IO.Path]::GetDirectoryName($fullOutputRcPath)
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
[System.IO.Directory]::CreateDirectory($rcDirectory) | Out-Null

Add-Type -AssemblyName System.Drawing

$source = $null
$atlas = $null
$graphics = $null
$attributes = $null
$pngStream = $null
$verificationStream = $null
$verificationImage = $null

try {
    $source = [System.Drawing.Bitmap]::FromFile($fullInputPath)
    if (($source.Width % 3) -ne 0 -or ($source.Height % 2) -ne 0) {
        throw "Source atlas must be divisible by 3x2: $($source.Width)x$($source.Height)"
    }

    $sourceCellWidth = [int]($source.Width / 3)
    $sourceCellHeight = [int]($source.Height / 2)
    if ($sourceCellWidth -ne $sourceCellHeight) {
        throw "Source atlas cells must be square: ${sourceCellWidth}x${sourceCellHeight}"
    }
    if (-not [System.Drawing.Image]::IsAlphaPixelFormat($source.PixelFormat)) {
        throw "Source atlas must contain an alpha channel: $($source.PixelFormat)"
    }

    $atlas = New-Object System.Drawing.Bitmap(
        $OutputWidth,
        $OutputHeight,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $atlas.SetResolution($source.HorizontalResolution, $source.VerticalResolution)

    $graphics = [System.Drawing.Graphics]::FromImage($atlas)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

    $attributes = New-Object System.Drawing.Imaging.ImageAttributes
    $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)

    for ($row = 0; $row -lt 2; ++$row) {
        for ($column = 0; $column -lt 3; ++$column) {
            $destination = New-Object System.Drawing.Rectangle(
                ($column * $outputCellWidth),
                ($row * $outputCellHeight),
                $outputCellWidth,
                $outputCellHeight)
            $graphics.DrawImage(
                $source,
                $destination,
                ($column * $sourceCellWidth),
                ($row * $sourceCellHeight),
                $sourceCellWidth,
                $sourceCellHeight,
                [System.Drawing.GraphicsUnit]::Pixel,
                $attributes)
        }
    }

    $graphics.Flush([System.Drawing.Drawing2D.FlushIntention]::Flush)

    $pngStream = New-Object System.IO.MemoryStream
    $atlas.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngBytes = $pngStream.ToArray()
    if ($pngBytes.Length -ge $MaximumPngBytes) {
        throw "Generated PNG is too large: actual=$($pngBytes.Length) limit=$MaximumPngBytes"
    }

    $verificationStream = New-Object System.IO.MemoryStream(, $pngBytes)
    $verificationImage = [System.Drawing.Bitmap]::FromStream($verificationStream)
    if ($verificationImage.Width -ne $OutputWidth -or $verificationImage.Height -ne $OutputHeight) {
        throw "Generated PNG geometry mismatch: actual=$($verificationImage.Width)x$($verificationImage.Height) expected=${OutputWidth}x${OutputHeight}"
    }
    if (-not [System.Drawing.Image]::IsAlphaPixelFormat($verificationImage.PixelFormat)) {
        throw "Generated PNG lost its alpha channel: $($verificationImage.PixelFormat)"
    }

    Write-AtomicBytes -Path $fullOutputPngPath -Bytes $pngBytes

    $resourceHeaderForRc = $fullResourceHeaderPath.Replace('\', '/')
    $pngPathForRc = $fullOutputPngPath.Replace('\', '/')
    $rcText = "#include `"$resourceHeaderForRc`"`r`n`r`nIDR_MAIN_LOGO_PNG RCDATA `"$pngPathForRc`"`r`n"
    Write-AtomicUtf8Text -Path $fullOutputRcPath -Text $rcText

    Write-Host "Generated logo PNG atlas ${OutputWidth}x${OutputHeight}: $fullOutputPngPath ($($pngBytes.Length) bytes)"
}
finally {
    if ($verificationImage -ne $null) {
        $verificationImage.Dispose()
    }
    if ($verificationStream -ne $null) {
        $verificationStream.Dispose()
    }
    if ($pngStream -ne $null) {
        $pngStream.Dispose()
    }
    if ($attributes -ne $null) {
        $attributes.Dispose()
    }
    if ($graphics -ne $null) {
        $graphics.Dispose()
    }
    if ($atlas -ne $null) {
        $atlas.Dispose()
    }
    if ($source -ne $null) {
        $source.Dispose()
    }
}
