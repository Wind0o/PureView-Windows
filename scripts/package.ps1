[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [string]$OutputDirectory = "dist",
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [System.IO.Path]::GetFullPath((Join-Path $RepositoryRoot $OutputDirectory))
$Executable = Join-Path $BuildDirectory "PureView.exe"
if (-not (Test-Path $Executable)) {
    throw "PureView.exe was not found at $Executable"
}

$Stage = Join-Path $OutputDirectory "PureView-$Version-Windows-x64"
$Zip = "$Stage.zip"
$HashFile = "$Zip.sha256"
Remove-Item $Stage, $Zip, $HashFile -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

Copy-Item $Executable (Join-Path $Stage "PureView.exe")
Copy-Item (Join-Path $RepositoryRoot "installer\install.ps1") $Stage
Copy-Item (Join-Path $RepositoryRoot "installer\install.cmd") $Stage
Copy-Item (Join-Path $RepositoryRoot "installer\uninstall.ps1") $Stage
Copy-Item (Join-Path $RepositoryRoot "installer\uninstall.cmd") $Stage
Copy-Item (Join-Path $RepositoryRoot "README.md") $Stage
Copy-Item (Join-Path $RepositoryRoot "LICENSE") $Stage

$StagedProcess = Start-Process `
    (Join-Path $Stage "PureView.exe") `
    -ArgumentList "--self-test" `
    -Wait `
    -PassThru
if ($StagedProcess.ExitCode -ne 0) {
    throw "The staged PureView.exe self-test failed with exit code $($StagedProcess.ExitCode)."
}

Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Zip -CompressionLevel Optimal

$VerificationDirectory = Join-Path $OutputDirectory "_verify-$Version"
Remove-Item $VerificationDirectory -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive $Zip $VerificationDirectory
$ExtractedProcess = Start-Process `
    (Join-Path $VerificationDirectory "PureView.exe") `
    -ArgumentList "--self-test" `
    -Wait `
    -PassThru
if ($ExtractedProcess.ExitCode -ne 0) {
    throw "The independently extracted PureView.exe self-test failed with exit code $($ExtractedProcess.ExitCode)."
}
foreach ($Required in @("PureView.exe", "install.cmd", "install.ps1", "uninstall.cmd", "uninstall.ps1", "README.md", "LICENSE")) {
    if (-not (Test-Path (Join-Path $VerificationDirectory $Required))) {
        throw "Release package is missing $Required."
    }
}
Remove-Item $VerificationDirectory -Recurse -Force

$Hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  $([System.IO.Path]::GetFileName($Zip))" |
    Set-Content $HashFile -Encoding utf8NoBOM

Write-Host "Created $Zip"
Write-Host "Created $HashFile"
