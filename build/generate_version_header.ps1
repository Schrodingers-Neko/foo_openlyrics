param (
        [Parameter(Mandatory)]
        [string]$output_path
      )
$version_string = git describe --tags --match 'v[0-9]*' --first-parent --always HEAD
Write-Output "Computing version header from version string '$version_string'..."
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrEmpty($version_string)) {
    throw 'Failed to compute the component version from Git'
}

if ($version_string[0] -ne 'v') {
    # Forks need not copy upstream's component tags. Data tags must not become component versions.
    $version_string = "v0.0.0-g$version_string"
}
$version_string = $version_string.Substring(1) # Remove the leading 'v'

$output_dir = Split-Path -Parent $output_path
New-Item -ItemType "directory" -Force $output_dir | Out-Null
Write-Output "#pragma once`n`n#define OPENLYRICS_VERSION ""$version_string""" > $output_path
Write-Output "Version header saved to $output_path"
