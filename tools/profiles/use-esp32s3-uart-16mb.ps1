$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$idfProfile = "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1"
$defaults = "sdkconfig.defaults;sdkconfig.defaults.esp32s3-uart-16mb"

Push-Location $repoRoot
try {
    if (Test-Path $idfProfile) {
        . $idfProfile
    }

    if (Test-Path "sdkconfig") {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        Copy-Item "sdkconfig" "sdkconfig.backup-$stamp"
    }

    Remove-Item "sdkconfig" -ErrorAction SilentlyContinue
    idf.py -D "SDKCONFIG_DEFAULTS=$defaults" set-target esp32s3
    idf.py -D "SDKCONFIG_DEFAULTS=$defaults" reconfigure
}
finally {
    Pop-Location
}
