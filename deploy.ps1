param(
    [Parameter(Mandatory)][string]$Target,
    [switch]$NoRenderer,
    [switch]$SyncVideo,
    [switch]$Music
)
$ErrorActionPreference = 'Stop'

$Repo = $PSScriptRoot
$Game = (Resolve-Path (Join-Path $Repo '..\..')).Path
$Target = (Resolve-Path $Target).Path
if ($Target -eq $Game) { throw 'Target is the source game folder.' }

$BackupSuffix = '.bak-ra2enh'
function Backup($path) {
    if ((Test-Path $path) -and -not (Test-Path "$path$BackupSuffix")) {
        Copy-Item $path "$path$BackupSuffix"
    }
}

function Copy-IfChanged($from, $to) {
    if ((Test-Path $to) -and (Get-FileHash $from).Hash -eq (Get-FileHash $to).Hash) { return 'same' }
    Backup $to
    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
    Copy-Item $from $to -Force
    'copied'
}

# The list SetupCnCReloaded.exe copies from a Yuri's Revenge install.
$BaseFiles = 'gamemd.exe', 'BINKW32.dll', 'Blowfish.dll', 'ra2.mix', 'ra2md.mix', 'language.mix',
    'langmd.mix', 'expandmd01.mix', 'thememd.mix', 'movmd03.mix'
foreach ($f in $BaseFiles) {
    $to = Join-Path $Target $f
    if (Test-Path $to) { "base   $f exists, kept"; continue }
    Copy-Item (Join-Path $Game $f) $to
    "base   $f copied"
}

$Syringe = Test-Path (Join-Path $Target 'Syringe.exe')
$AddOns = @(
    @{ Dir = 'ra2enhancements\_dist'; Files = 'RA2Enhancements.dll', 'RA2Enhancements.dll.inj' },
    @{ Dir = 'hwcursor\_dist'; Files = 'HWCursor.dll', 'HWCursor.dll.inj', 'WasdScroll.dll', 'WasdScroll.dll.inj' }
)
$AddOns += @{ Dir = 'ra2video\_dist'; Files = @('RA2Video.exe') }
if (-not $Syringe) { $AddOns += @{ Dir = 'hwcursor\_dist'; Files = @('dsound.dll') } }
foreach ($a in $AddOns) {
    foreach ($f in $a.Files) {
        "addon  $f $(Copy-IfChanged (Join-Path $Repo "$($a.Dir)\$f") (Join-Path $Target $f))"
    }
}

$Bytes = [Text.Encoding]::GetEncoding(28591)
function Read-Lines($path) { [IO.File]::ReadAllText($path, $Bytes) -split "`r?`n" }
function Write-Lines($path, $lines) {
    $text = ($lines -join "`r`n").TrimEnd("`r", "`n") + "`r`n"
    [IO.File]::WriteAllText($path, $text, $Bytes)
}

# Syringe.exe logs "DLL load was prevented" for any .inj DLL not named by an -i= argument.
$ClientDefs = Join-Path $Target 'Resources\ClientDefinitions.ini'
if (Test-Path $ClientDefs) {
    $lines = Read-Lines $ClientDefs
    $changed = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^ExtraCommandLineParams=.*-i=') { continue }
        foreach ($dll in 'RA2Enhancements.dll', 'HWCursor.dll', 'WasdScroll.dll') {
            if ($lines[$i] -match "-i=$([regex]::Escape($dll))(\s|$)") { continue }
            $lines[$i] = $lines[$i] -replace '(-i=\S+)(?!.*-i=)', "`$1 -i=$dll"
            $changed = $true
        }
    }
    if ($changed) {
        Backup $ClientDefs
        Write-Lines $ClientDefs $lines
        'client ExtraCommandLineParams now loads the add-on DLLs'
    } else { 'client ExtraCommandLineParams already loads the add-on DLLs or uses no -i=' }
}

if ($Music) {
    $musicFiles = [ordered]@{ 'theme.mix' = 'theme.mix'; 'expand98.mix' = 'expandmd98.mix'; 'expand99.mix' = 'expandmd99.mix' }
    foreach ($from in $musicFiles.Keys) {
        "music  $($musicFiles[$from]) $(Copy-IfChanged (Join-Path $Game $from) (Join-Path $Target $musicFiles[$from]))"
    }
    "music  thememd.ini $(Copy-IfChanged (Join-Path $Game 'theme.ini') (Join-Path $Target 'thememd.ini'))"
}

$CursorPng = Join-Path $Target 'Resources\cursor.png'
$CursorCur = Join-Path $Target 'Resources\cursor.cur'
if ((Test-Path $CursorPng) -and -not (Test-Path $CursorCur)) {
    python -I (Join-Path $Repo 'png2cur.py') $CursorPng $CursorCur
    if ($LASTEXITCODE) { throw 'png2cur.py failed.' }
    'client cursor.cur built from cursor.png'
}

$KeyIni = 'KeyboardMD.ini'
$master = [ordered]@{}
foreach ($l in Read-Lines (Join-Path $Game $KeyIni)) {
    if ($l -match '^([^;\[=][^=]*)=(\d+)\s*$') { $master[$matches[1].Trim()] = $matches[2] }
}
$targetIni = Join-Path $Target $KeyIni
$lines = if (Test-Path $targetIni) { @(Read-Lines $targetIni) } else { @('[Hotkey]') }
$used = @{}
foreach ($v in $master.Values) { if ($v -ne '0') { $used[$v] = $true } }
$seen = @{}
$inHotkey = $false
$hotkeyEnd = -1
$out = New-Object System.Collections.Generic.List[string]
foreach ($l in $lines) {
    if ($l -match '^\s*\[(.+)\]') {
        if ($inHotkey) { $hotkeyEnd = $out.Count }
        $inHotkey = $matches[1] -eq 'Hotkey'
        $out.Add($l); continue
    }
    if ($inHotkey -and $l -match '^([^;=][^=]*)=(\d+)\s*$') {
        $name = $matches[1].Trim(); $val = $matches[2]
        if ($master.Contains($name)) {
            $seen[$name] = $true
            if ($val -ne $master[$name]) { "keys   $name $val -> $($master[$name])" }
            $out.Add("$name=$($master[$name])"); continue
        }
        if ($used.ContainsKey($val)) {
            "keys   $name $val -> 0 (key taken)"
            $out.Add("$name=0"); continue
        }
    }
    $out.Add($l)
}
if ($inHotkey) { $hotkeyEnd = $out.Count }
while ($hotkeyEnd -gt 0 -and $out[$hotkeyEnd - 1].Trim() -eq '') { $hotkeyEnd-- }
$missing = @($master.Keys | Where-Object { -not $seen[$_] } | ForEach-Object { "$_=$($master[$_])" })
foreach ($m in $missing) { "keys   $m added" }
$out.InsertRange($hotkeyEnd, [string[]]$missing)
Backup $targetIni
Write-Lines $targetIni $out

if (-not $NoRenderer) {
    $ddraw = Join-Path $Game 'ddraw.dll'
    $ddrawIni = Join-Path $Game 'ddraw.ini'
    "render ddraw.dll $(Copy-IfChanged $ddraw (Join-Path $Target 'ddraw.dll'))"
    foreach ($rel in 'Resources\Compatibility\DLL\cnc-ddraw.dll', 'Resources\cnc-ddraw.dll') {
        $to = Join-Path $Target $rel
        if (Test-Path $to) { "render $rel $(Copy-IfChanged $ddraw $to)" }
    }
    $shaders = Join-Path $Game 'Shaders'
    $copied = 0
    foreach ($f in Get-ChildItem $shaders -Recurse -File) {
        $rel = $f.FullName.Substring($Game.Length + 1)
        if ((Copy-IfChanged $f.FullName (Join-Path $Target $rel)) -eq 'copied') { $copied++ }
    }
    "render Shaders $copied files copied"

    $targetDdrawIni = Join-Path $Target 'ddraw.ini'
    $firstDeploy = -not (Test-Path "$targetDdrawIni$BackupSuffix")
    if ($firstDeploy) {
        "render ddraw.ini $(Copy-IfChanged $ddrawIni $targetDdrawIni)"
        if (-not (Test-Path "$targetDdrawIni$BackupSuffix")) { Copy-Item $ddrawIni "$targetDdrawIni$BackupSuffix" }
    }
    if ($firstDeploy -or $SyncVideo) {
        Add-Type -Namespace RA2Enh -Name Ini -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
public static extern int GetPrivateProfileString(string s, string k, string d, System.Text.StringBuilder r, int n, string f);
[DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
public static extern bool WritePrivateProfileString(string s, string k, string v, string f);
'@ -ErrorAction SilentlyContinue
        function Get-Ini($file, $section, $key) {
            $sb = New-Object System.Text.StringBuilder 4096
            $null = [RA2Enh.Ini]::GetPrivateProfileString($section, $key, "`u{1}", $sb, $sb.Capacity, $file)
            if ($sb.ToString() -eq "`u{1}") { $null } else { $sb.ToString() }
        }
        Backup $targetDdrawIni
        $videoKeys = 'inject_resolution', 'width', 'height', 'windowed', 'border', 'fullscreen', 'savesettings',
            'center_window', 'posX', 'posY', 'shader', 'ra2video_mode', 'ra2video_border'
        foreach ($k in $videoKeys) {
            $v = Get-Ini $ddrawIni 'ddraw' $k
            if ($null -ne $v) { $null = [RA2Enh.Ini]::WritePrivateProfileString('ddraw', $k, $v, $targetDdrawIni) }
            foreach ($s in 'game', 'gamemd', 'ra2', 'ra2md') {
                $null = [RA2Enh.Ini]::WritePrivateProfileString($s, $k, [NullString]::Value, $targetDdrawIni)
            }
        }
        $gameIni = Join-Path $Game 'RA2MD.INI'
        $targetGameIni = Join-Path $Target 'RA2MD.INI'
        Backup $targetGameIni
        foreach ($k in 'ScreenWidth', 'ScreenHeight') {
            $null = [RA2Enh.Ini]::WritePrivateProfileString('Video', $k, (Get-Ini $gameIni 'Video' $k), $targetGameIni)
        }
        $null = [RA2Enh.Ini]::WritePrivateProfileString([NullString]::Value, [NullString]::Value, [NullString]::Value, $targetDdrawIni)
        "render RA2Video settings synced: $(Get-Ini $targetDdrawIni 'ddraw' 'inject_resolution') in $(Get-Ini $targetDdrawIni 'ddraw' 'width')x$(Get-Ini $targetDdrawIni 'ddraw' 'height'), shader $(Get-Ini $targetDdrawIni 'ddraw' 'shader')"
    } else {
        'render ddraw.ini already deployed, kept (-SyncVideo recopies the RA2Video settings)'
    }
    $cfg = Join-Path $Target 'Resources\Compatibility\Configs\cnc-ddraw.ini'
    if (Test-Path $cfg) { "render $(Split-Path $cfg -Leaf) $(Copy-IfChanged $ddrawIni $cfg)" }
}
