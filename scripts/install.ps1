#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

# Run:
#     irm "https://fennec.support/kosh/install" | iex

& {
  param([switch]$DryRun, [string]$InstallPath)
  $ErrorActionPreference = "Stop"
  $ProgressPreference = "SilentlyContinue"
  [Net.ServicePointManager]::SecurityProtocol =
    [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

  $RELEASES = "https://github.com/toiletbril/kosh/releases"
  $WORK = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid())
  $HERE = (Get-Location).ProviderPath
  $IS_COLOR = -not $env:NO_COLOR -and -not [Console]::IsOutputRedirected
  $IS_ERROR_COLOR = -not $env:NO_COLOR -and -not [Console]::IsErrorRedirected

  function Paint($IsColor, $Code, $Text) {
    if ($IsColor) {
      return "$([char]27)[${Code}m$Text$([char]27)[0m"
    }
    return "$Text"
  }

  function Mark($IsColor) {
    return "$(Paint $IsColor '1;95' ':3c') $(Paint $IsColor '1;97' '+') "
  }

  function Blue($Text) {
    return Paint $IS_COLOR "1;34" $Text
  }

  function Label($Text, $Value) {
    $Message = "$(Mark $IS_COLOR)$(Paint $IS_COLOR 1 $Text)"
    if ($null -ne $Value) {
      $Message += " $Value"
    }
    return $Message
  }

  function Step($Text, $Value) {
    [Console]::Out.Write("$(Label $Text $Value) ")
  }

  function Say($Text, $Value) {
    [Console]::Out.WriteLine((Label $Text $Value))
  }

  function Line($Text) {
    [Console]::Out.WriteLine("$(Mark $IS_COLOR)$Text")
  }

  function Result($Text) {
    [Console]::Out.WriteLine($Text)
  }

  function Ask($Question) {
    if ([Console]::IsInputRedirected -or -not [Environment]::UserInteractive) {
      return $true
    }
    [Console]::Error.Write("$(Mark $IS_ERROR_COLOR)$Question [y/n] ")
    $Answer = [Console]::In.ReadLine()
    return $Answer -notmatch "^[Nn]"
  }

  try {
    if ($env:KOSH_INSTALL_DRY_RUN) {
      $DryRun = $true
    }

    Say "Hi!"
    $ARCH = switch ([Runtime.InteropServices.RuntimeInformation, mscorlib]::OSArchitecture) {
      "X64" { "amd64" }
      "Arm64" { "aarch64" }
      default { throw "unsupported processor $_" }
    }
    $ARCH_NAME = if ($ARCH -eq "amd64") { "AMD64" } else { "ARM64" }
    Say "Detecting the system.." "Windows on $ARCH_NAME"

    $VERSION = $env:KOSH_INSTALL_VERSION
    if (-not $VERSION) {
      $VERSION = (Invoke-RestMethod "https://api.github.com/repos/toiletbril/kosh/releases/latest").tag_name
    }
    if ($VERSION -notmatch "^[A-Za-z0-9._-]+$" -or $VERSION -in "latest", "releases") {
      throw "invalid release version '$VERSION'"
    }
    Say "Looking up the latest release.." $VERSION

    $BIN_DIR = $InstallPath
    if (-not $BIN_DIR) {
      $BIN_DIR = $env:KOSH_INSTALL_PATH
    }
    if (-not $BIN_DIR) {
      $BIN_DIR = Join-Path $env:LOCALAPPDATA "kosh\bin"
    }
    $BIN_DIR = [IO.Path]::GetFullPath([IO.Path]::Combine($HERE, $BIN_DIR)).TrimEnd("\", "/")
    $SHARE_DIR = Join-Path (Split-Path -Parent $BIN_DIR) "share"

    $BINARY = "kosh-win32-$ARCH-$VERSION.exe"
    $FILES = @($BINARY)
    $IS_EXTRAS_WANTED = $true

    if (-not $DryRun) {
      if (-not (Ask "Do you want to install Kosh $VERSION to $BIN_DIR?")) {
        Say "That's a shame. Specify another path via -InstallPath :c"
        return
      }
      $IS_EXTRAS_WANTED = Ask "Do you want to install completions and manpages to $SHARE_DIR?"
    }

    if ($IS_EXTRAS_WANTED) {
      $FILES += "kosh.bash"
      if (Get-Command zstd -ErrorAction SilentlyContinue) {
        $FILES += "kosh.1.zst", "kosh.5.zst"
      }
    }

    New-Item -ItemType Directory $WORK | Out-Null
    Set-Location $WORK
    $URL = "$RELEASES/download/$VERSION"
    try {
      Invoke-WebRequest "$URL/SHA256SUMS" -OutFile SHA256SUMS -UseBasicParsing
    } catch {
      throw "release $VERSION has no SHA256SUMS file"
    }
    $SUMS = Get-Content SHA256SUMS

    function Find-Sum($File) {
      return $SUMS | Where-Object { ($_ -split "\s+\*?", 2)[1] -eq $File }
    }

    if (-not (Find-Sum $BINARY)) {
      throw "release $VERSION has no build for win32 on $ARCH"
    }

    if ($DryRun) {
      foreach ($FILE in $FILES) {
        Line "Would download $URL/$FILE and verify it against SHA256SUMS"
      }
      Line "Would install $(Join-Path $BIN_DIR 'kosh.exe')"
      Line "Would install completions and manpages to $SHARE_DIR"
      Line "Dry run: nothing was installed."
      return
    }

    foreach ($FILE in $FILES) {
      Step "Downloading" "$(Blue $FILE).."
      try {
        Invoke-WebRequest "$URL/$FILE" -OutFile $FILE -UseBasicParsing
      } catch {
        throw "unable to download $FILE"
      }
      Result "100%"
    }

    Step "Verifying the binaries.."
    foreach ($FILE in $FILES) {
      $EXPECTED = ((Find-Sum $FILE) -split "\s+")[0]
      if ((Get-FileHash -Algorithm SHA256 $FILE).Hash -ne $EXPECTED) {
        Result "not ok"
        throw "checksum mismatch"
      }
    }
    Result "ok"

    try {
      New-Item -ItemType Directory -Force $BIN_DIR | Out-Null
    } catch {
      throw "cannot write to $BIN_DIR"
    }
    $EXE = Join-Path $BIN_DIR "kosh.exe"
    Move-Item -Force $BINARY $EXE

    if ($IS_EXTRAS_WANTED) {
      $COMPLETIONS = Join-Path $SHARE_DIR "bash-completion\completions"
      try {
        New-Item -ItemType Directory -Force $COMPLETIONS | Out-Null
      } catch {
        throw "cannot write to $SHARE_DIR"
      }
      Move-Item -Force kosh.bash (Join-Path $COMPLETIONS "kosh")

      if ($FILES -contains "kosh.1.zst") {
        foreach ($PAGE in "kosh.1", "kosh.5") {
          $MAN = Join-Path $SHARE_DIR "man\man$($PAGE[-1])"
          New-Item -ItemType Directory -Force $MAN | Out-Null
          zstd -dqf "$PAGE.zst" -o (Join-Path $MAN $PAGE)
        }
      }
    }

    Say "Meow meow meow (success!)." $EXE
    $USER_PATH = (Get-Item "HKCU:\Environment").GetValue("Path", "", "DoNotExpandEnvironmentNames")
    $IS_ON_PATH = (($env:Path -split ";") + ($USER_PATH -split ";")) -contains $BIN_DIR
    if (-not $IS_ON_PATH -and (Ask "Do you want to add $BIN_DIR to the user PATH?")) {
      Set-ItemProperty "HKCU:\Environment" Path "$USER_PATH;$BIN_DIR" -Type ExpandString
      [Environment]::SetEnvironmentVariable("KOSH_INSTALL", "1", "User")
      [Environment]::SetEnvironmentVariable("KOSH_INSTALL", $null, "User")
      $env:Path += ";$BIN_DIR"
      $IS_ON_PATH = $true
    }
    if ($IS_ON_PATH) {
      Line "Use $(Blue 'kosh') to launch the shell."
    } else {
      Line "Use $EXE to launch the shell."
    }
  } catch {
    [Console]::Error.WriteLine("kosh installer: $(Paint $IS_ERROR_COLOR '1;91' 'error:') $($_.Exception.Message.TrimEnd('.')).")
    if ($PSCommandPath) {
      exit 1
    }
  } finally {
    Set-Location $HERE
    Remove-Item -Recurse -Force $WORK -ErrorAction SilentlyContinue
  }
} @args
