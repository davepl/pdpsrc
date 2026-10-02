# Run on Windows with the normal SMB client policy. This script changes no policy.
# The temporary UNC connection and downloaded files are removed on completion.
param(
    [Parameter(Mandatory=$true)][string]$Server,
    [string]$Share = 'pdp',
    [string]$User = 'pdp',
    [Parameter(Mandatory=$true)][string]$ExpectedSha256,
    [string]$BinaryPath = 'nested\binary file.bin',
    [switch]$Writable
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$unc = "\\$Server\$Share"
Get-CimInstance Win32_OperatingSystem |
    Select-Object Caption,Version,BuildNumber | Format-List
Get-SmbClientConfiguration |
    Select-Object EnableSecuritySignature,RequireSecuritySignature,
        EnableInsecureGuestLogons,Smb2DialectMin,Smb2DialectMax | Format-List
if (Get-SmbConnection -ErrorAction SilentlyContinue |
        Where-Object { $_.ServerName -eq $Server -and $_.ShareName -eq $Share }) {
    throw 'An existing connection uses this share; preserve it and test a fresh session.'
}
# net use prompts without printing the password. Do not replace * with a secret.
& net.exe use $unc '*' "/user:$User" '/persistent:no'
if ($LASTEXITCODE -ne 0) { throw "net use failed: $LASTEXITCODE" }
try {
    & net.exe view "\\$Server"
    if ($LASTEXITCODE -ne 0) { throw "share enumeration failed: $LASTEXITCODE" }
    Write-Output 'ROOT:'
    Get-ChildItem -LiteralPath $unc | Select-Object Name,Length,Mode | Format-Table
    Write-Output 'NESTED:'
    Get-ChildItem -LiteralPath "$unc\nested" |
        Select-Object Name,Length,Mode | Format-Table
    if (@(Get-ChildItem -LiteralPath "$unc\empty").Count -ne 0) {
        throw 'The empty-directory fixture was not empty'
    }
    if ((Get-Item -LiteralPath "$unc\zero").Length -ne 0) {
        throw 'The zero-byte fixture had a nonzero length'
    }
    $download = Join-Path $env:TEMP ('smbd-copy-' + [Guid]::NewGuid() + '.bin')
    try {
        Copy-Item -LiteralPath "$unc\$BinaryPath" -Destination $download
        $hash = (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash
        Write-Output "SHA256: $hash"
        if ($hash -ne $ExpectedSha256) { throw 'Copied bytes differ' }
        if ($Writable) {
            $testRoot = "$unc\windows-write-$([Guid]::NewGuid().ToString('N').Substring(0,12))"
            New-Item -ItemType Directory -Path $testRoot | Out-Null
            Copy-Item -LiteralPath $download -Destination "$testRoot\upload.bin"
            $uploaded = (Get-FileHash -LiteralPath "$testRoot\upload.bin" -Algorithm SHA256).Hash
            if ($uploaded -ne $ExpectedSha256) { throw 'Uploaded bytes differ' }
            Rename-Item -LiteralPath "$testRoot\upload.bin" -NewName 'renamed.bin'
            New-Item -ItemType Directory -Path "$testRoot\folder" | Out-Null
            Rename-Item -LiteralPath "$testRoot\folder" -NewName 'renamed folder'
            Remove-Item -LiteralPath "$testRoot\renamed folder"
            Remove-Item -Force -LiteralPath "$testRoot\renamed.bin"
            Remove-Item -LiteralPath $testRoot
            Write-Output 'WINDOWS NATIVE WRITE/RENAME/DELETE TEST PASS'
        }
    } finally {
        if (Test-Path -LiteralPath $download) { Remove-Item -Force -LiteralPath $download }
    }
    Get-SmbConnection | Where-Object ServerName -EQ $Server |
        Select-Object ServerName,ShareName,UserName,Dialect,Signed,Encrypted,NumOpens |
        Format-List
    Write-Output 'WINDOWS NATIVE CLIENT TEST PASS'
} finally {
    & net.exe use $unc '/delete' '/y'
}
