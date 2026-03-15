[CmdletBinding()]
param(
    [string]$Root,
    [switch]$DryRun,
    [switch]$NoBackup
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = $MyInvocation.MyCommand.Path
$scriptDir = Split-Path -Parent $scriptPath
if ([string]::IsNullOrEmpty($scriptDir)) {
    $scriptDir = (Get-Location).Path
}
if ([string]::IsNullOrEmpty($Root)) {
    $Root = Join-Path $scriptDir "src"
}

$cp1251 = [System.Text.Encoding]::GetEncoding(1251)
$utf8Strict = [System.Text.UTF8Encoding]::new($false, $true)
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$mojibakeHintPattern = "[\u0420\u0421]"
$cyrillicPattern = "[\u0400-\u04FF]"
$textExtensions = @(".lua", ".txt", ".md", ".json", ".ini", ".cfg", ".toml", ".yml", ".yaml", ".xml")

function Try-FixMojibakeRun {
    param([string]$Run)

    if ([string]::IsNullOrEmpty($Run)) {
        return $Run
    }
    if ($Run -notmatch $mojibakeHintPattern) {
        return $Run
    }

    try {
        $bytes = $cp1251.GetBytes($Run)
        $decoded = $utf8Strict.GetString($bytes)
    } catch {
        return $Run
    }

    if ($decoded.Length -ge $Run.Length) {
        return $Run
    }
    if ($decoded -notmatch $cyrillicPattern) {
        return $Run
    }

    return $decoded
}

if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Error "Root path not found: $Root"
    exit 2
}

$rootFullPath = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Root).Path)
$backupRoot = $null
$backupRequested = (-not $DryRun -and -not $NoBackup)

$files = Get-ChildItem -LiteralPath $rootFullPath -Recurse -File | Where-Object {
    $textExtensions -contains $_.Extension.ToLowerInvariant()
}

$script:totalReplacements = 0
$changedFiles = New-Object System.Collections.Generic.List[object]
$skippedFiles = New-Object System.Collections.Generic.List[string]

foreach ($file in $files) {
    try {
        $originalText = [System.IO.File]::ReadAllText($file.FullName, $utf8Strict)
    } catch {
        $skippedFiles.Add($file.FullName)
        continue
    }

    $script:fileReplacementCount = 0
    $newText = [regex]::Replace($originalText, "[^\x00-\x7F]+", {
        param($match)
        $originalRun = $match.Value
        $fixedRun = Try-FixMojibakeRun -Run $originalRun
        if ($fixedRun -ne $originalRun) {
            $script:fileReplacementCount++
            $script:totalReplacements++
            return $fixedRun
        }
        return $originalRun
    })

    if ($script:fileReplacementCount -le 0 -or $newText -eq $originalText) {
        continue
    }

    if (-not $DryRun) {
        if ($backupRequested) {
            if (-not $backupRoot) {
                $backupRoot = Join-Path $scriptDir ("mojibake_backup_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
                [void](New-Item -ItemType Directory -Path $backupRoot -Force)
            }
            $relativePath = $file.FullName.Substring($rootFullPath.Length).TrimStart('\', '/')
            $backupPath = Join-Path $backupRoot $relativePath
            $backupDir = [System.IO.Path]::GetDirectoryName($backupPath)
            if ($backupDir -and -not (Test-Path -LiteralPath $backupDir -PathType Container)) {
                [void](New-Item -ItemType Directory -Path $backupDir -Force)
            }
            Copy-Item -LiteralPath $file.FullName -Destination $backupPath -Force
        }

        [System.IO.File]::WriteAllText($file.FullName, $newText, $utf8NoBom)
    }

    $changedFiles.Add([pscustomobject]@{
        File = $file.FullName
        Replacements = $script:fileReplacementCount
    })
}

if ($changedFiles.Count -gt 0) {
    $changedFiles |
        Sort-Object Replacements -Descending |
        Format-Table -AutoSize
} else {
    Write-Host "No mojibake fragments found."
}

Write-Host ("TOTAL_REPLACEMENTS={0}" -f $script:totalReplacements)
Write-Host ("FILES_CHANGED={0}" -f $changedFiles.Count)
Write-Host ("FILES_SKIPPED_NON_UTF8={0}" -f $skippedFiles.Count)

if ($DryRun) {
    Write-Host "Mode: dry run (no files were changed)."
} elseif ($backupRoot) {
    Write-Host ("Backup folder: {0}" -f $backupRoot)
} elseif ($backupRequested) {
    Write-Host "Backup folder: not created (no file changes)."
}

exit 0
