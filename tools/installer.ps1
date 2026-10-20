param(
    [ValidateSet('install', 'uninstall')][string]$Mode = 'install',
    [string]$Game = '',       # optional: game folder, skips the folder picker
    [switch]$Quiet,           # optional: no message boxes (for testing)
    [switch]$SkipProcessCheck # testing only
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

$Repo = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'files') -PathType Container) { $Repo = $PSScriptRoot }
$Files = Join-Path $Repo 'files'
$PatchedBink = '6a58f884a8ff7355143c828d7c2badd3a1ec29fd38eccf4de3a3f5ea6055291e'
$Backup = '.fa-backup'
$RuntimeFiles = @('mw4splash.dll', 'rtlcompanion.dll', 'bink2w64.dll', 'trhook.dll', 'trdata2.bin')

function Say([string]$fa, [string]$en, [string]$icon = 'Information') {
    if ($Quiet) { Write-Host "[$icon] $en"; return }
    [System.Windows.Forms.MessageBox]::Show("$fa`n`n$en", 'MW4 Farsi', 'OK', $icon,
        'Button1', 'RightAlign, RtlReading') | Out-Null
}
function Fail([string]$fa, [string]$en) { Say $fa $en 'Error'; exit 1 }
function Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower() }

# --- find the game's sp26 folder (accepts the game folder or sp26 itself)
function Resolve-Sp26([string]$dir) {
    if (-not $dir) { return $null }
    if (Test-Path -LiteralPath (Join-Path $dir 'sp26-cod.exe')) { return $dir }
    $s = Join-Path $dir 'sp26'
    if (Test-Path -LiteralPath (Join-Path $s 'sp26-cod.exe')) { return $s }
    return $null
}
if ($Game) { $Sp26 = Resolve-Sp26 $Game }
else {
    $Sp26 = Resolve-Sp26 (Split-Path -Parent $Repo)   # repo placed inside the game folder
    if (-not $Sp26) {
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = 'پوشه بازی را انتخاب کنید (پوشه‌ای که sp26 داخلش است) - Select the Modern Warfare 4 game folder'
        $dlg.ShowNewFolderButton = $false
        if ($dlg.ShowDialog() -ne 'OK') { exit 1 }
        $Sp26 = Resolve-Sp26 $dlg.SelectedPath
    }
}
if (-not $Sp26) {
    Fail 'پوشه بازی پیدا نشد. پوشه‌ای را انتخاب کنید که داخلش پوشه sp26 و فایل sp26-cod.exe باشد.' 'Game folder not found: pick the folder that contains sp26\sp26-cod.exe.'
}
$GameRoot = Split-Path -Parent $Sp26
if (-not $SkipProcessCheck) {
    # block if the game runs from this folder, or if Windows hides its path from us
    $running = Get-Process -Name 'sp26-cod', 'cod' -ErrorAction SilentlyContinue |
        Where-Object { -not $_.Path -or $_.Path.StartsWith($GameRoot, [StringComparison]::OrdinalIgnoreCase) }
    if ($running) { Fail 'اول بازی را کامل ببندید.' 'Close the game first.' }
}
Write-Host "Game folder: $Sp26"

$bink = Join-Path $Sp26 'bink2w64.dll'
$hook = Join-Path $Sp26 'trhook.dll'
$data = Join-Path $Sp26 'trdata2.bin'
$OurData = Hash (Join-Path $Files 'trdata2.bin')

try {
    if ($Mode -eq 'install') {
        # Check the entire package before touching installed files. Install dependencies first.
        foreach ($n in $RuntimeFiles) {
            if (-not (Test-Path -LiteralPath (Join-Path $Files $n) -PathType Leaf)) { throw "Missing package file: $n" }
        }
        # back up original files once; never back up our own files
        if ((Test-Path -LiteralPath $bink) -and (Hash $bink) -ne $PatchedBink -and -not (Test-Path -LiteralPath "$bink$Backup")) {
            Copy-Item -LiteralPath $bink -Destination "$bink$Backup"
            Write-Host 'Backed up original bink2w64.dll'
        }
        if ((Test-Path -LiteralPath $data) -and (Hash $data) -ne $OurData -and -not (Test-Path -LiteralPath "$data$Backup")) {
            Copy-Item -LiteralPath $data -Destination "$data$Backup"
            Write-Host 'Backed up previous trdata2.bin'
        }
        foreach ($n in $RuntimeFiles) {
            $src = Join-Path $Files $n
            $dst = Join-Path $Sp26 $n
            Copy-Item -LiteralPath $src -Destination $dst -Force
            if ((Hash $src) -ne (Hash $dst)) { throw "verify failed: $n" }
            Write-Host "Installed $n"
        }
        # Remove obsolete artwork from local preview builds after successful installation.
        Remove-Item -LiteralPath (Join-Path $Sp26 'mw4-splash.bmp') -Force -ErrorAction SilentlyContinue
        Say "ترجمه فارسی نصب شد.`nبازی را با زبان English اجرا کنید و Subtitles را روشن کنید.`nفقط برای بخش داستانی؛ آنلاین بازی نکنید." 'Persian translation installed. Run the campaign with the game language set to English and Subtitles on. Campaign/offline only.'
    }
    else {
        $keepHook = $false
        if (Test-Path -LiteralPath "$bink$Backup") {
            Move-Item -LiteralPath "$bink$Backup" -Destination $bink -Force
            Write-Host 'Restored original bink2w64.dll'
        }
        elseif ((Test-Path -LiteralPath $bink) -and (Hash $bink) -eq $PatchedBink) {
            $keepHook = $true   # patched bink needs trhook.dll, which depends on rtlcompanion.dll
        }
        if (Test-Path -LiteralPath "$data$Backup") {
            Move-Item -LiteralPath "$data$Backup" -Destination $data -Force
            Write-Host 'Restored previous trdata2.bin'
        }
        elseif ((Test-Path -LiteralPath $data) -and (Hash $data) -eq $OurData) {
            Remove-Item -LiteralPath $data -Force
            Write-Host 'Removed trdata2.bin'
        }
        if (-not $keepHook) {
            Remove-Item -LiteralPath $hook -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath (Join-Path $Sp26 'trhook.log') -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath (Join-Path $Sp26 'rtlcompanion.dll') -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath (Join-Path $Sp26 'rtlcompanion.log') -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath (Join-Path $Sp26 'mw4splash.dll') -Force -ErrorAction SilentlyContinue
            Say 'ترجمه فارسی حذف شد و بازی به حالت اصلی برگشت.' 'Persian translation removed; original files restored.'
        }
        else {
            Say "متن‌های فارسی حذف شد، ولی نسخه اصلی bink2w64.dll پیدا نشد؛ پس trhook.dll و وابستگی‌های آن نگه داشته شدند تا بازی سالم بماند.`nبرای حذف کامل، فایل اصلی bink2w64.dll را از نصب بازی برگردانید (مثلاً Verify files در لانچر)." 'Translation removed, but no backup of the original bink2w64.dll was found, so trhook.dll, rtlcompanion.dll and mw4splash.dll were kept. Restore the original bink2w64.dll from your game install to remove the hook completely.' 'Warning'
        }
        # Clean up artwork left by preview builds.
        Remove-Item -LiteralPath (Join-Path $Sp26 'mw4-splash.bmp') -Force -ErrorAction SilentlyContinue
    }
}
catch {
    Fail "خطا: $($_.Exception.Message)`nبازی را ببندید و دوباره امتحان کنید." "Error: $($_.Exception.Message). Close the game and try again."
}
