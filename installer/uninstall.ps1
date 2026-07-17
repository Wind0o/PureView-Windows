[CmdletBinding()]
param(
    [string]$InstallDirectory = "$env:LOCALAPPDATA\Programs\PureView"
)

$ErrorActionPreference = "Stop"
$ProgId = "PureView.Image.1"
$Extensions = @(
    ".jpg", ".jpeg", ".jpe", ".jfif",
    ".png", ".gif", ".tif", ".tiff", ".bmp", ".dib",
    ".ico", ".jxr", ".wdp", ".hdp", ".dds",
    ".webp", ".heic", ".heif", ".avif",
    ".jp2", ".j2k", ".jpf", ".jpx", ".jxl",
    ".dng", ".raw", ".arw", ".cr2", ".cr3", ".nef", ".nrw",
    ".orf", ".raf", ".rw2", ".pef", ".srw", ".x3f", ".3fr",
    ".erf", ".kdc", ".mos", ".mrw", ".psd", ".tga", ".hdr", ".exr"
)

Get-Process -Name "PureView" -ErrorAction SilentlyContinue | Stop-Process -Force

foreach ($Extension in $Extensions) {
    $OpenWith = "HKCU:\Software\Classes\$Extension\OpenWithProgids"
    if (Test-Path $OpenWith) {
        Remove-ItemProperty $OpenWith -Name $ProgId -ErrorAction SilentlyContinue
    }
}

Remove-Item "HKCU:\Software\Classes\$ProgId" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "HKCU:\Software\Classes\Applications\PureView.exe" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "HKCU:\Software\PureView" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\PureView.exe" -Recurse -Force -ErrorAction SilentlyContinue
Remove-ItemProperty "HKCU:\Software\RegisteredApplications" -Name "PureView" -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\PureView.lnk") -Force -ErrorAction SilentlyContinue

if (-not ("PureView.ShellNotify" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
namespace PureView {
    public static class ShellNotify {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(
            uint eventId, uint flags, IntPtr item1, IntPtr item2);
    }
}
"@
}
[PureView.ShellNotify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

$CleanupCommand = "timeout /t 2 /nobreak >nul & rmdir /s /q `"$InstallDirectory`""
Start-Process "cmd.exe" -ArgumentList "/d", "/c", $CleanupCommand -WindowStyle Hidden
Write-Host "PureView was uninstalled. Windows keeps your current default-app choices until you change them." -ForegroundColor Green
