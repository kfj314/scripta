# Builds ScriptaSyncServer.exe with the .NET Framework compiler that ships with Windows 10/11.
param([string]$OutDir = "$env:LOCALAPPDATA\Programs\ScriptaSyncServer")
$ErrorActionPreference = 'Stop'
$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { throw "C# compiler not found at $csc (.NET Framework 4.x is part of Windows 10/11)." }
if (-not (Test-Path "$PSScriptRoot\scripta-server.ico")) { & "$PSScriptRoot\make-icon.ps1" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
& $csc /nologo /target:winexe /optimize+ /out:"$OutDir\ScriptaSyncServer.exe" /win32icon:"$PSScriptRoot\scripta-server.ico" `
  /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll /r:Microsoft.CSharp.dll /r:System.Core.dll `
  "$PSScriptRoot\ScriptaSyncServer.cs"
if ($LASTEXITCODE -ne 0) { throw "Build failed." }
Write-Host "Built $OutDir\ScriptaSyncServer.exe"
