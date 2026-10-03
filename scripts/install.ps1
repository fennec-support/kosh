#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This file installs the Windows release of the Koshka shell. It downloads the
# binary, the zstd-compressed kosh(1) and kosh(5) manual pages, and the Bash
# completion, verifies each against the SHA256SUMS file of the same release,
# installs them under a prefix with the layout `make install` uses, and adds
# the bin directory to the user PATH. Windows on arm64 runs the x86-64 binary
# through emulation.
#
# The script installs the latest release. KOSH_VERSION selects another release
# tag. KOSH_PREFIX selects the prefix and
# defaults to %LOCALAPPDATA%\kosh. The manual pages are skipped with a note
# when zstd is not on PATH.
#
#   irm https://fennec.support/install-kosh | iex

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Repository = "toiletbril/kosh"

if ($env:KOSH_VERSION) {
  $Version = $env:KOSH_VERSION
} else {
  $Latest = Invoke-RestMethod "https://api.github.com/repos/$Repository/releases/latest"
  $Version = $Latest.tag_name
}
if (-not $Version) {
  throw "install.ps1: unable to resolve the latest release"
}

if ($env:KOSH_PREFIX) {
  $Prefix = $env:KOSH_PREFIX
} else {
  $Prefix = Join-Path $env:LOCALAPPDATA "kosh"
}

$Asset = "kosh-win32-amd64-$Version.exe"
$BaseUrl = "https://github.com/$Repository/releases/download/$Version"
$WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("kosh-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force $WorkDir | Out-Null

function Get-VerifiedFile([string] $Name) {
  $Path = Join-Path $WorkDir $Name
  Invoke-WebRequest "$BaseUrl/$Name" -OutFile $Path -UseBasicParsing

  $ExpectedSum = $null
  foreach ($Line in Get-Content (Join-Path $WorkDir "SHA256SUMS")) {
    $Fields = $Line -split "\s+", 2
    if ($Fields.Count -eq 2 -and $Fields[1].TrimStart("*") -eq $Name) {
      $ExpectedSum = $Fields[0].ToLowerInvariant()
    }
  }
  if (-not $ExpectedSum) {
    throw "install.ps1: SHA256SUMS does not list $Name"
  }

  $ActualSum = (Get-FileHash -Algorithm SHA256 $Path).Hash.ToLowerInvariant()
  if ($ActualSum -ne $ExpectedSum) {
    throw "install.ps1: checksum mismatch for ${Name}: expected $ExpectedSum, got $ActualSum"
  }

  return $Path
}

function Install-PlacedFile([string] $Source, [string] $Target) {
  New-Item -ItemType Directory -Force (Split-Path $Target) | Out-Null
  Move-Item -Force $Source $Target
}

try {
  Write-Host "Installing kosh $Version into $Prefix"
  try {
    Invoke-WebRequest "$BaseUrl/SHA256SUMS" -OutFile (Join-Path $WorkDir "SHA256SUMS") -UseBasicParsing
  } catch {
    throw "install.ps1: release $Version has no SHA256SUMS file"
  }

  $BinDir = Join-Path $Prefix "bin"
  $Target = Join-Path $BinDir "kosh.exe"
  Install-PlacedFile (Get-VerifiedFile $Asset) $Target
  Unblock-File $Target

  $Completion = Join-Path $Prefix "share\bash-completion\completions\kosh"
  Install-PlacedFile (Get-VerifiedFile "kosh.bash") $Completion

  if (Get-Command zstd -ErrorAction SilentlyContinue) {
    foreach ($Page in @("kosh.1", "kosh.5")) {
      $Compressed = Get-VerifiedFile "$Page.zst"
      $Expanded = Join-Path $WorkDir $Page
      & zstd -dqf $Compressed -o $Expanded
      if ($LASTEXITCODE -ne 0) {
        throw "install.ps1: unable to decompress $Page.zst"
      }
      $Section = $Page.Substring($Page.Length - 1)
      Install-PlacedFile $Expanded (Join-Path $Prefix "share\man\man$Section\$Page")
    }
  } else {
    Write-Warning "install.ps1: zstd is not on PATH, so the manual pages were skipped"
  }

  Write-Host "Installed $Target"

  $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $Entries = @()
  if ($UserPath) {
    $Entries = $UserPath -split ";" | Where-Object { $_ }
  }
  if ($Entries -notcontains $BinDir) {
    [Environment]::SetEnvironmentVariable("Path", (($Entries + $BinDir) -join ";"), "User")
    Write-Host "Added $BinDir to the user PATH. Open a new terminal to run kosh."
  }
} finally {
  Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
}
