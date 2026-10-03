#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This file installs the Windows release of the Koshka shell. It prints the
# detected processor, downloads the matching binary, the zstd-compressed
# kosh(1) and kosh(5) manual pages, and the Bash completion, verifies each
# against the SHA256SUMS file of the same release, and installs them under a
# prefix with the layout `make install` uses. It asks before adding the bin
# directory to the user PATH. Messages use the colors of kosh diagnostics and
# help unless NO_COLOR is set, TERM is dumb, or the output is redirected.
#
# The script installs the latest release. KOSH_VERSION selects another release
# tag. KOSH_PREFIX selects the prefix and defaults to %LOCALAPPDATA%\kosh.
# KOSH_ARCH selects amd64 or aarch64 instead of the detected processor, so an
# arm64 machine can choose the x86-64 binary under emulation. KOSH_MODIFY_PATH
# set to yes or no answers the PATH question; without a console to ask, the
# PATH is left unchanged. The manual pages are skipped with a note when zstd is
# not on PATH.
#
#   irm https://fennec.support/kosh/install | iex

& {
  $ErrorActionPreference = "Stop"
  $ProgressPreference = "SilentlyContinue"

  $Repository = "toiletbril/kosh"
  $UseColor = -not $env:NO_COLOR -and $env:TERM -ne "dumb" -and
    -not [Console]::IsOutputRedirected

  function Write-Label([string] $Label, [string] $Color, [string] $Text) {
    if ($UseColor) {
      Write-Host $Label -ForegroundColor $Color -NoNewline
    } else {
      Write-Host $Label -NoNewline
    }
    Write-Host " $Text"
  }

  function Write-Report([string] $Heading, [string] $Text) {
    Write-Label $Heading "Blue" $Text
  }

  function Write-Note([string] $Text) {
    Write-Label "note:" "Cyan" "$Text."
  }

  function Write-Warn([string] $Text) {
    Write-Label "warning:" "Yellow" "$Text."
  }

  function Write-Failure([string] $Text) {
    if ($UseColor) {
      Write-Host "error:" -ForegroundColor Red -NoNewline
      Write-Host " install.ps1: $Text." -ForegroundColor White
    } else {
      Write-Host "error: install.ps1: $Text."
    }
  }

  function Get-DetectedProcessor {
    $Name = $null
    try {
      $Name = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    } catch {
      $Name = $env:PROCESSOR_ARCHITEW6432
      if (-not $Name) {
        $Name = $env:PROCESSOR_ARCHITECTURE
      }
    }

    switch -Regex ($Name) {
      "^(X64|AMD64)$" { return "amd64" }
      "^(Arm64|ARM64)$" { return "aarch64" }
      default { throw "unsupported processor '$Name'" }
    }
  }

  function Test-ShouldModifyPath([string] $Directory) {
    switch -Regex ($env:KOSH_MODIFY_PATH) {
      "^(y|yes|1|true)$" { return $true }
      "^(n|no|0|false)$" { return $false }
    }

    if (-not [Environment]::UserInteractive -or [Console]::IsInputRedirected) {
      return $false
    }

    $Answer = Read-Host "Add $Directory to the user PATH? [Y/n]"
    return $Answer -notmatch "^(n|no)$"
  }

  $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("kosh-install-" + [guid]::NewGuid())

  function Get-ListedSum([string] $Name) {
    foreach ($Line in Get-Content (Join-Path $WorkDir "SHA256SUMS")) {
      $Fields = $Line -split "\s+", 2
      if ($Fields.Count -eq 2 -and $Fields[1].TrimStart("*") -eq $Name) {
        return $Fields[0].ToLowerInvariant()
      }
    }

    return $null
  }

  function Get-VerifiedFile([string] $Name) {
    $ExpectedSum = Get-ListedSum $Name
    if (-not $ExpectedSum) {
      throw "SHA256SUMS does not list $Name"
    }

    $Path = Join-Path $WorkDir $Name
    Invoke-WebRequest "$BaseUrl/$Name" -OutFile $Path -UseBasicParsing

    $ActualSum = (Get-FileHash -Algorithm SHA256 $Path).Hash.ToLowerInvariant()
    if ($ActualSum -ne $ExpectedSum) {
      throw "checksum mismatch for $Name"
    }

    return $Path
  }

  function Install-PlacedFile([string] $Source, [string] $Target) {
    New-Item -ItemType Directory -Force (Split-Path $Target) | Out-Null
    Move-Item -Force $Source $Target
  }

  try {
    switch -Regex ($env:KOSH_ARCH) {
      "^$" { $Processor = Get-DetectedProcessor }
      "^(amd64|x86_64)$" { $Processor = "amd64" }
      "^(aarch64|arm64)$" { $Processor = "aarch64" }
      default { throw "unsupported processor '$env:KOSH_ARCH'" }
    }
    Write-Report "Detected" "Windows on $Processor"

    if ($env:KOSH_VERSION) {
      $Version = $env:KOSH_VERSION
    } else {
      $Latest = Invoke-RestMethod "https://api.github.com/repos/$Repository/releases/latest"
      $Version = $Latest.tag_name
    }
    if (-not $Version) {
      throw "unable to resolve the latest release"
    }

    if ($env:KOSH_PREFIX) {
      $Prefix = $env:KOSH_PREFIX
    } else {
      $Prefix = Join-Path $env:LOCALAPPDATA "kosh"
    }

    $Asset = "kosh-win32-$Processor-$Version.exe"
    $BaseUrl = "https://github.com/$Repository/releases/download/$Version"
    New-Item -ItemType Directory -Force $WorkDir | Out-Null

    Write-Report "Installing" "kosh $Version into $Prefix"
    try {
      Invoke-WebRequest "$BaseUrl/SHA256SUMS" -OutFile (Join-Path $WorkDir "SHA256SUMS") -UseBasicParsing
    } catch {
      throw "release $Version has no SHA256SUMS file"
    }

    if (-not (Get-ListedSum $Asset)) {
      if ($Processor -eq "aarch64") {
        Write-Note "set KOSH_ARCH=amd64 to install the x86-64 build under emulation"
      }
      throw "release $Version has no build for Windows on $Processor"
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
          throw "unable to decompress $Page.zst"
        }
        $Section = $Page.Substring($Page.Length - 1)
        Install-PlacedFile $Expanded (Join-Path $Prefix "share\man\man$Section\$Page")
      }
    } else {
      Write-Warn "zstd is not on PATH, so the manual pages were skipped"
    }

    Write-Report "Installed" $Target

    $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $Entries = @()
    if ($UserPath) {
      $Entries = $UserPath -split ";" | Where-Object { $_ }
    }
    if ($Entries -contains $BinDir) {
      return
    }

    if (Test-ShouldModifyPath $BinDir) {
      [Environment]::SetEnvironmentVariable("Path", (($Entries + $BinDir) -join ";"), "User")
      Write-Note "added $BinDir to the user PATH, open a new terminal to run kosh"
    } else {
      Write-Note "add $BinDir to PATH to run kosh by name"
    }
  } catch {
    Write-Failure $_.Exception.Message
  } finally {
    Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
  }
}
