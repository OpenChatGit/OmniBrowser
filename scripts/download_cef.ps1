# Downloads and extracts the pinned CEF binary distribution into third_party/cef.
param(
  [string]$Version = "144.0.32+g5ce7d26+chromium-144.0.7559.258",
  [string]$Platform = "windows64"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ThirdParty = Join-Path $Root "third_party"
$CefDir = Join-Path $ThirdParty "cef"
$FileName = "cef_binary_${Version}_${Platform}.tar.bz2"
$Url = "https://cef-builds.spotifycdn.com/" + ([uri]::EscapeDataString($FileName) -replace '\+', '%2B')
$Archive = Join-Path $ThirdParty $FileName

New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null

if (-not (Test-Path $Archive)) {
  Write-Host "Downloading $Url"
  curl.exe -L --retry 3 --retry-delay 2 -o $Archive $Url
} else {
  Write-Host "Using existing archive $Archive"
}

$Extract = Join-Path $ThirdParty "cef_extract"
if (Test-Path $Extract) { Remove-Item -Recurse -Force $Extract }
New-Item -ItemType Directory -Force -Path $Extract | Out-Null

Write-Host "Extracting..."
tar -xjf $Archive -C $Extract
$Inner = Get-ChildItem $Extract -Directory | Select-Object -First 1
if (-not $Inner) { throw "Extraction failed: no directory found" }

if (Test-Path $CefDir) { Remove-Item -Recurse -Force $CefDir }
Move-Item $Inner.FullName $CefDir
Remove-Item -Recurse -Force $Extract

Write-Host "CEF ready at $CefDir"
