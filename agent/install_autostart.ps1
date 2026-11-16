# Installs (or removes with -Uninstall) a Startup-folder shortcut that runs the
# DeskBuddy agent silently (pythonw = no console window) when you log in.
param([switch]$Uninstall)

$lnkPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'DeskBuddy Agent.lnk'
$script  = Join-Path $PSScriptRoot 'deskbuddy_agent.py'

if ($Uninstall) {
    Remove-Item $lnkPath -ErrorAction SilentlyContinue
    Write-Host "DeskBuddy autostart removed"
    return
}

$pythonw = (Get-Command pythonw -ErrorAction Stop).Source
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($lnkPath)
$lnk.TargetPath = $pythonw
$lnk.Arguments = "`"$script`""
$lnk.WorkingDirectory = $PSScriptRoot
$lnk.Description = 'DeskBuddy PC agent'
$lnk.Save()
Write-Host "DeskBuddy autostart installed -> $lnkPath"

# start it now (kill any previous instance first)
Get-CimInstance Win32_Process -Filter "Name='pythonw.exe' OR Name='python.exe'" |
    Where-Object { $_.CommandLine -like '*deskbuddy_agent.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-Process $pythonw -ArgumentList "`"$script`"" -WorkingDirectory $PSScriptRoot
Write-Host "Agent running in background"
