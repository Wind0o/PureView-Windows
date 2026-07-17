[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory
)

$ErrorActionPreference = "Stop"
$PackageDirectory = [System.IO.Path]::GetFullPath($PackageDirectory)
$InstallDirectory = Join-Path $env:TEMP "PureView-Installer-Test-$PID"
$InstallScript = Join-Path $PackageDirectory "install.ps1"
$UninstallScript = Join-Path $PackageDirectory "uninstall.ps1"

if (-not (Test-Path $InstallScript) -or -not (Test-Path $UninstallScript)) {
    throw "The package does not contain both installer scripts."
}

try {
    & $InstallScript -InstallDirectory $InstallDirectory -SkipDefaultApps

    $InstalledExe = Join-Path $InstallDirectory "PureView.exe"
    if (-not (Test-Path $InstalledExe)) {
        throw "The installer did not copy PureView.exe."
    }

    $Registered = (Get-ItemProperty "HKCU:\Software\RegisteredApplications").PureView
    if ($Registered -ne "Software\PureView\Capabilities") {
        throw "RegisteredApplications does not point to PureView capabilities."
    }

    $Associations = Get-ItemProperty "HKCU:\Software\PureView\Capabilities\FileAssociations"
    $JpegAssociation = $Associations.PSObject.Properties[".jpg"].Value
    if ($JpegAssociation -ne "PureView.Image.1") {
        throw "The .jpg capability was not registered."
    }

    $OpenCommand = (Get-Item "HKCU:\Software\Classes\PureView.Image.1\shell\open\command").GetValue("")
    $ExpectedCommand = "`"$InstalledExe`" `"%1`""
    if ($OpenCommand -ne $ExpectedCommand) {
        throw "The ProgID open command is incorrect: $OpenCommand"
    }

    $OpenWith = Get-ItemProperty "HKCU:\Software\Classes\.jpg\OpenWithProgids"
    if ($null -eq $OpenWith.PSObject.Properties["PureView.Image.1"]) {
        throw "PureView is missing from .jpg OpenWithProgids."
    }

    $Process = Start-Process `
        $InstalledExe `
        -ArgumentList "--self-test" `
        -Wait `
        -PassThru
    if ($Process.ExitCode -ne 0) {
        throw "The installed executable self-test failed with exit code $($Process.ExitCode)."
    }
} finally {
    if (Test-Path $UninstallScript) {
        & $UninstallScript -InstallDirectory $InstallDirectory
    }
}

if (Test-Path "HKCU:\Software\PureView") {
    throw "The uninstaller left the PureView capabilities key behind."
}
if ($null -ne (Get-ItemProperty "HKCU:\Software\RegisteredApplications").PureView) {
    throw "The uninstaller left the RegisteredApplications value behind."
}

Write-Host "PureView per-user install, association, runtime, and uninstall checks passed."
