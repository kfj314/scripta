# Installs the Scripta Sync Server tray app on Windows 10/11.
#   1. installs rclone with winget if it is missing
#   2. builds ScriptaSyncServer.exe from source into %LOCALAPPDATA%\Programs\ScriptaSyncServer
#   3. allows the server port through Windows Firewall for your local network only (one admin prompt)
#   4. starts the app, which adds Start menu and desktop shortcuts
# Uninstall: install.ps1 -Uninstall  (your shared folder and its files are never touched)
param(
  [string]$InstallDir = "$env:LOCALAPPDATA\Programs\ScriptaSyncServer",
  [int]$Port = 8080,
  [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$RuleName = "Scripta WebDAV ($Port)"
function Step($m) { Write-Host "==> $m" -ForegroundColor Green }

function Invoke-Elevated([string]$command) {
  # runs one PowerShell command as administrator; returns $true if it ran
  try {
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -Command $command"
    return $p.ExitCode -eq 0
  } catch { return $false }
}

function Stop-App {
  Get-Process ScriptaSyncServer -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
}

if ($Uninstall) {
  Step "Stopping the app and server"
  Stop-App
  Get-CimInstance Win32_Process -Filter "Name='rclone.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*serve webdav*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  Step "Removing shortcuts and sign-in entry"
  Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name ScriptaSyncServer -ErrorAction SilentlyContinue
  foreach ($d in [Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('DesktopDirectory')) {
    Remove-Item (Join-Path $d 'Scripta Sync Server.lnk') -ErrorAction SilentlyContinue
  }
  if (Get-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue) {
    Step "Removing firewall rule (admin prompt)"
    if (-not (Invoke-Elevated "Remove-NetFirewallRule -DisplayName '$RuleName'")) { Write-Warning "Firewall rule '$RuleName' was not removed." }
  }
  Step "Removing $InstallDir"
  Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
  Write-Host "Uninstalled. rclone and your shared folder were left in place."
  return
}

# 1. rclone
$rclone = Get-Command rclone -ErrorAction SilentlyContinue
if (-not $rclone) {
  $rclone = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter rclone.exe -ErrorAction SilentlyContinue | Select-Object -First 1
}
if (-not $rclone) {
  Step "rclone (the program that does the actual file serving) is not installed"
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw "winget not found. Install rclone from https://rclone.org/downloads/ and run this again." }
  $a = Read-Host "Install it now with winget? (Y/n)"
  if ($a -and $a -notmatch '^[Yy]') { throw "rclone is required." }
  winget install --id Rclone.Rclone -e --accept-source-agreements --accept-package-agreements
  if ($LASTEXITCODE -ne 0) { throw "winget could not install rclone." }
} else { Step "rclone found" }

# 2. build
Step "Building the app"
Stop-App
& "$PSScriptRoot\build.ps1" -OutDir $InstallDir

# 3. firewall
if (Get-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue) { Step "Firewall rule already present" }
else {
  Step "Allowing port $Port from your local network (approve the admin prompt)"
  $ok = Invoke-Elevated "New-NetFirewallRule -DisplayName '$RuleName' -Direction Inbound -Protocol TCP -LocalPort $Port -RemoteAddress LocalSubnet -Action Allow -Profile Any | Out-Null"
  if (-not $ok) { Write-Warning "Firewall rule not added. Other devices may not reach the server until port $Port is allowed." }
}

# 4. launch
Step "Starting Scripta Sync Server"
Start-Process "$InstallDir\ScriptaSyncServer.exe"
Write-Host ""
Write-Host "Done. The app window shows the URL, user and password to enter on the Scripta device." -ForegroundColor Cyan
Write-Host "Pick the folder to share in the app (default: Documents\Scripta), then press Start server."
