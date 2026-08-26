param(
    [Parameter(Mandatory = $true)]
    [string[]] $Paths
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($env:WINDOWS_SIGNING_CERTIFICATE_BASE64)) {
    throw "WINDOWS_SIGNING_CERTIFICATE_BASE64 is not configured"
}
if ([string]::IsNullOrWhiteSpace($env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD)) {
    throw "WINDOWS_SIGNING_CERTIFICATE_PASSWORD is not configured"
}

$certificate_path = Join-Path $env:RUNNER_TEMP "uil-signing-certificate.pfx"
$timestamp_url = if ([string]::IsNullOrWhiteSpace($env:WINDOWS_SIGNING_TIMESTAMP_URL)) {
    "http://timestamp.digicert.com"
} else {
    $env:WINDOWS_SIGNING_TIMESTAMP_URL
}

$sign_tool_command = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
$sign_tool = if ($null -ne $sign_tool_command) {
    $sign_tool_command.Source
} else {
    Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" `
        -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if ([string]::IsNullOrWhiteSpace($sign_tool)) {
    throw "signtool.exe was not found"
}

try {
    [IO.File]::WriteAllBytes(
        $certificate_path,
        [Convert]::FromBase64String($env:WINDOWS_SIGNING_CERTIFICATE_BASE64))

    foreach ($path in $Paths) {
        $resolved_path = (Resolve-Path $path).Path
        & $sign_tool sign `
            /fd SHA256 `
            /f $certificate_path `
            /p $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD `
            /td SHA256 `
            /tr $timestamp_url `
            $resolved_path
        if ($LASTEXITCODE -ne 0) {
            throw "Signing failed for $resolved_path"
        }

        & $sign_tool verify /pa /v $resolved_path
        if ($LASTEXITCODE -ne 0) {
            throw "Signature verification failed for $resolved_path"
        }
    }
} finally {
    Remove-Item $certificate_path -Force -ErrorAction SilentlyContinue
}
