[CmdletBinding()]
param(
    [string]$InstallDirectory = "$env:LOCALAPPDATA\Programs\PureView",
    [switch]$SkipDefaultApps
)

$ErrorActionPreference = "Stop"
$SourceDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceExe = Join-Path $SourceDirectory "PureView.exe"
if (-not (Test-Path $SourceExe)) {
    throw "PureView.exe must be next to install.ps1."
}

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

New-Item -ItemType Directory -Path $InstallDirectory -Force | Out-Null
$InstalledExe = Join-Path $InstallDirectory "PureView.exe"
$FilesToInstall = @(
    "PureView.exe",
    "install.ps1",
    "install.cmd",
    "uninstall.ps1",
    "uninstall.cmd",
    "README.md",
    "LICENSE"
)
foreach ($File in $FilesToInstall) {
    $Source = Join-Path $SourceDirectory $File
    $Destination = Join-Path $InstallDirectory $File
    if ((Test-Path $Source) -and
        ([System.IO.Path]::GetFullPath($Source) -ne [System.IO.Path]::GetFullPath($Destination))) {
        Copy-Item $Source $Destination -Force
    }
}

$ClassesRoot = "HKCU:\Software\Classes"
$ProgId = "PureView.Image.1"
$ProgIdPath = Join-Path $ClassesRoot $ProgId
New-Item $ProgIdPath -Force | Out-Null
Set-Item $ProgIdPath -Value "PureView Image"
New-ItemProperty $ProgIdPath -Name "FriendlyTypeName" -Value "PureView Image" -PropertyType String -Force | Out-Null
New-ItemProperty $ProgIdPath -Name "AppUserModelID" -Value "PureView.Native" -PropertyType String -Force | Out-Null

$DefaultIconPath = Join-Path $ProgIdPath "DefaultIcon"
New-Item $DefaultIconPath -Force | Out-Null
Set-Item $DefaultIconPath -Value "`"$InstalledExe`",0"

$CommandPath = Join-Path $ProgIdPath "shell\open\command"
New-Item $CommandPath -Force | Out-Null
Set-Item $CommandPath -Value "`"$InstalledExe`" `"%1`""

$ApplicationPath = Join-Path $ClassesRoot "Applications\PureView.exe"
New-Item $ApplicationPath -Force | Out-Null
New-ItemProperty $ApplicationPath -Name "FriendlyAppName" -Value "PureView" -PropertyType String -Force | Out-Null
$ApplicationCommand = Join-Path $ApplicationPath "shell\open\command"
New-Item $ApplicationCommand -Force | Out-Null
Set-Item $ApplicationCommand -Value "`"$InstalledExe`" `"%1`""
$SupportedTypes = Join-Path $ApplicationPath "SupportedTypes"
New-Item $SupportedTypes -Force | Out-Null

$Capabilities = "HKCU:\Software\PureView\Capabilities"
New-Item $Capabilities -Force | Out-Null
New-ItemProperty $Capabilities -Name "ApplicationName" -Value "PureView" -PropertyType String -Force | Out-Null
New-ItemProperty $Capabilities -Name "ApplicationDescription" -Value "Fast, borderless native image viewer" -PropertyType String -Force | Out-Null
$FileAssociations = Join-Path $Capabilities "FileAssociations"
New-Item $FileAssociations -Force | Out-Null

foreach ($Extension in $Extensions) {
    New-ItemProperty $SupportedTypes -Name $Extension -Value "" -PropertyType String -Force | Out-Null
    New-ItemProperty $FileAssociations -Name $Extension -Value $ProgId -PropertyType String -Force | Out-Null
    $OpenWith = Join-Path $ClassesRoot "$Extension\OpenWithProgids"
    New-Item $OpenWith -Force | Out-Null
    New-ItemProperty $OpenWith -Name $ProgId -Value "" -PropertyType String -Force | Out-Null
}

$RegisteredApplications = "HKCU:\Software\RegisteredApplications"
New-Item $RegisteredApplications -Force | Out-Null
New-ItemProperty `
    $RegisteredApplications `
    -Name "PureView" `
    -Value "Software\PureView\Capabilities" `
    -PropertyType String `
    -Force | Out-Null

$AppPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\PureView.exe"
New-Item $AppPath -Force | Out-Null
Set-Item $AppPath -Value $InstalledExe
New-ItemProperty $AppPath -Name "Path" -Value $InstallDirectory -PropertyType String -Force | Out-Null

$StartMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$ShortcutPath = Join-Path $StartMenu "PureView.lnk"
$Shell = New-Object -ComObject WScript.Shell
$Shortcut = $Shell.CreateShortcut($ShortcutPath)
$Shortcut.TargetPath = $InstalledExe
$Shortcut.WorkingDirectory = $InstallDirectory
$Shortcut.IconLocation = "$InstalledExe,0"
$Shortcut.Save()

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

Write-Host "PureView installed to: $InstallDirectory" -ForegroundColor Green
Write-Host "Image formats are registered with Windows. Your existing defaults were not overwritten."

if (-not $SkipDefaultApps) {
    Start-Process "ms-settings:defaultapps?registeredAppUser=PureView"
    Write-Host "Choose PureView in the Windows Default apps page that just opened."
}
