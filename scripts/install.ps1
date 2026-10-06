# Koshka shell installer.
#
# Copyright 2026 toiletbril
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# Run:
#     irm "https://fennec.support/kosh/install" | iex

& {
    param(
        [switch]$DryRun,
        [switch]$Force,
        [string]$InstallPath,
        [switch]$X,
        [switch]$Help,
        [Parameter(ValueFromRemainingArguments = $true)]$Rest
    )
    $ErrorActionPreference = "Stop"
    $ProgressPreference = "SilentlyContinue"
    [Net.ServicePointManager]::SecurityProtocol =
    [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $RELEASES = "https://github.com/fennec-support/kosh/releases"
    $WORK = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid())
    $HERE = (Get-Location).ProviderPath
    $IS_COLOR = -not $env:NO_COLOR -and -not [Console]::IsOutputRedirected
    $IS_ERROR_COLOR = -not $env:NO_COLOR -and -not [Console]::IsErrorRedirected

    function Paint($Code, $Text, $IsColor = $IS_COLOR) {
        if ($IsColor) { return "$([char]27)[${Code}m$Text$([char]27)[0m" }
        return "$Text"
    }

    function Mark($IsColor = $IS_COLOR) {
        return "$(Paint '1;95' ':3c' -IsColor $IsColor) "
    }

    function Label($Text, $Value) {
        $Tail = if ($Value) { " $Value" } else { "" }
        return "$(Mark)$(Paint 1 $Text)$Tail"
    }

    function Say($Text, $Value) {
        [Console]::Out.WriteLine((Label $Text $Value))
    }

    function Step($Text, $Value) {
        [Console]::Out.Write("$(Label $Text $Value) ")
    }

    function Line($Text) {
        [Console]::Out.WriteLine("$(Mark)$Text")
    }

    function Show-Progress($Percent) {
        if (-not [Console]::IsOutputRedirected) {
            [Console]::Out.Write("{0,3}%`b`b`b`b" -f [Math]::Min(99, $Percent))
        }
    }

    function Get-File($Address, $Path) {
        $Response = [Net.WebRequest]::Create($Address).GetResponse()
        $In = $Response.GetResponseStream()
        $Out = [IO.File]::Create($Path)
        try {
            $Total = $Response.ContentLength
            $Buffer = New-Object byte[] 65536
            $Done = 0
            Show-Progress 0
            while (($Read = $In.Read($Buffer, 0, $Buffer.Length)) -gt 0) {
                $Out.Write($Buffer, 0, $Read)
                $Done += $Read
                if ($Total -gt 0) {
                    Show-Progress ([int][Math]::Floor($Done * 100 / $Total))
                }
            }
        }
        finally {
            $Out.Dispose()
            $In.Dispose()
            $Response.Dispose()
        }
    }

    function Get-InstalledVersion($Path) {
        try {
            $FIRST = & $Path --version 2>$null | Select-Object -First 1
        }
        catch {
            return $null
        }
        if ("$FIRST" -match "^Koshka Shell (\S+)") { return $Matches[1] }
        return $null
    }

    function Test-AtLeast($Have, $Want) {
        $H = $Have -split "[^0-9]+"
        $W = $Want -split "[^0-9]+"
        for ($I = 0; $I -lt 3; $I++) {
            $A = [int]$H[$I]
            $B = [int]$W[$I]
            if ($A -ne $B) { return $A -gt $B }
        }
        return -not ($Have -match "-" -and $Want -notmatch "-")
    }

    function Ask($Question) {
        if ([Console]::IsInputRedirected) { return $true }
        [Console]::Error.Write("$(Mark -IsColor $IS_ERROR_COLOR)$Question [y/n] ")
        return [Console]::In.ReadLine() -notmatch "^[Nn]"
    }

    try {
        foreach ($ARGUMENT in $Rest) {
            if ($ARGUMENT -in "--help", "-h") { $Help = $true }
            else { throw "unknown option $ARGUMENT" }
        }
        if ($Help) {
            Line "Usage: install.ps1 [-DryRun] [-Force] [-InstallPath DIR] [-X]"
            Line "  -DryRun           list the downloads, install nothing"
            Line "  -Force            install even when kosh is up to date"
            Line "  -InstallPath DIR  install to DIR, extras to DIR\..\share"
            Line "  -X                trace every command"
            Line "  -Help             print this help"
            Line "Environment: KOSH_INSTALL_PATH, KOSH_INSTALL_VERSION,"
            Line "  KOSH_INSTALL_DRY_RUN, KOSH_INSTALL_FORCE, NO_COLOR"
            return
        }
        if ($PSBoundParameters.ContainsKey("InstallPath")) {
            if (-not $InstallPath) { throw "-InstallPath needs a path" }
            if ($InstallPath.StartsWith("-")) { throw "-InstallPath needs a path, not '$InstallPath'" }
        }
        if ($X) { Set-PSDebug -Trace 1 }
        if ($env:KOSH_INSTALL_DRY_RUN) { $DryRun = $true }
        if ($env:KOSH_INSTALL_FORCE) { $Force = $true }

        Say "Hi! This is Koshka Shell installer."
        Say "You can view the repository and this script at <github.com/fennec-support/kosh>"

        $ARCH, $ARCH_NAME = switch ([Runtime.InteropServices.RuntimeInformation, mscorlib]::OSArchitecture) {
            "X64" { "amd64", "AMD64" }
            "Arm64" { "aarch64", "ARM64" }
            default { throw "unsupported processor $_" }
        }
        Say "Detecting the system.." "Windows on $ARCH_NAME"

        $VERSION = $env:KOSH_INSTALL_VERSION
        if (-not $VERSION) {
            $VERSION = (Invoke-RestMethod "https://api.github.com/repos/fennec-support/kosh/releases/latest").tag_name
        }
        if ($VERSION -notmatch "^[A-Za-z0-9._-]+$" -or $VERSION -in "latest", "releases") {
            throw "invalid release version '$VERSION'"
        }
        Say "Looking up the latest release.." $VERSION

        $BIN_DIR = $InstallPath
        if (-not $BIN_DIR) { $BIN_DIR = $env:KOSH_INSTALL_PATH }
        if (-not $BIN_DIR) { $BIN_DIR = Join-Path $env:LOCALAPPDATA "kosh\bin" }
        $BIN_DIR = [IO.Path]::GetFullPath([IO.Path]::Combine($HERE, $BIN_DIR)).TrimEnd("\", "/")
        $SHARE_DIR = Join-Path (Split-Path -Parent $BIN_DIR) "share"
        $BINARY = "kosh-win32-$ARCH-$VERSION.exe"
        $FILES = @($BINARY, "kosh.bash")

        $INSTALLED = Join-Path $BIN_DIR "kosh.exe"
        if (-not (Test-Path $INSTALLED)) {
            $FOUND = Get-Command kosh -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            $INSTALLED = if ($FOUND) { $FOUND.Source } else { $null }
        }
        if ($INSTALLED -and -not $Force) {
            $INSTALLED_VERSION = Get-InstalledVersion $INSTALLED
            $IS_CURRENT = $false
            if ($INSTALLED_VERSION) {
                if ($env:KOSH_INSTALL_VERSION) { $IS_CURRENT = $INSTALLED_VERSION -eq $VERSION }
                else { $IS_CURRENT = Test-AtLeast $INSTALLED_VERSION $VERSION }
            }
            if ($IS_CURRENT) {
                Say "Kosh $INSTALLED_VERSION is already installed:" $INSTALLED
                Line "*pats you gently* You are up to date with $VERSION. Nothing to do."
                Line "Pass -Force to install it again."
                return
            }
        }

        if (-not $DryRun) {
            if (-not (Ask "Do you want to install Kosh $VERSION to $BIN_DIR?")) {
                Say "That's a shame. Specify another path via -InstallPath :c"
                return
            }
            if (-not (Ask "Do you want to install completions and manpages to $SHARE_DIR?")) {
                $FILES = @($BINARY)
            }
        }
        $IS_EXTRAS_WANTED = $FILES.Count -gt 1
        if ($IS_EXTRAS_WANTED -and (Get-Command zstd -ErrorAction SilentlyContinue)) {
            $FILES += "kosh.1.zst", "kosh.5.zst"
        }

        New-Item -ItemType Directory $WORK | Out-Null
        Set-Location $WORK
        $URL = "$RELEASES/download/$VERSION"
        try {
            Invoke-WebRequest "$URL/SHA256SUMS" -OutFile SHA256SUMS -UseBasicParsing
            $SUMS = Get-Content SHA256SUMS
        }
        catch {
            throw "release $VERSION has no SHA256SUMS file"
        }
        function Find-Sum($File) {
            return ($SUMS | Where-Object { ($_ -split "\s+\*?", 2)[1] -eq $File } | Select-Object -First 1) -split "\s+" | Select-Object -First 1
        }
        if (-not (Find-Sum $BINARY)) {
            throw "release $VERSION has no build for win32 on $ARCH"
        }
        foreach ($FILE in $FILES) {
            if (-not (Find-Sum $FILE)) {
                throw "release $VERSION has no checksum for $FILE"
            }
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
            Step "Downloading" "$(Paint '1;34' $FILE).."
            try {
                Get-File "$URL/$FILE" (Join-Path $WORK $FILE)
            }
            catch {
                throw "unable to download $FILE"
            }
            [Console]::Out.WriteLine("100%")
        }

        Step "Verifying the binaries.."
        foreach ($FILE in $FILES) {
            if ((Get-FileHash -Algorithm SHA256 $FILE).Hash -ne (Find-Sum $FILE)) {
                [Console]::Out.WriteLine("not ok")
                throw "checksum mismatch"
            }
        }
        [Console]::Out.WriteLine("ok")

        try {
            New-Item -ItemType Directory -Force $BIN_DIR | Out-Null
        }
        catch {
            throw "cannot write to $BIN_DIR"
        }
        $EXE = Join-Path $BIN_DIR "kosh.exe"
        Move-Item -Force $BINARY $EXE
        if ($IS_EXTRAS_WANTED) {
            $COMPLETIONS = Join-Path $SHARE_DIR "bash-completion\completions"
            try {
                New-Item -ItemType Directory -Force $COMPLETIONS | Out-Null
            }
            catch {
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

        Say "Success! Meow meow meow. Your binary is here:" $EXE
        $USER_PATH = (Get-Item "HKCU:\Environment").GetValue("Path", "", "DoNotExpandEnvironmentNames")
        $IS_ON_PATH = (($env:Path -split ";") + ($USER_PATH -split ";")) -contains $BIN_DIR
        if (-not $IS_ON_PATH -and (Ask "Do you want to add $BIN_DIR to the user PATH?")) {
            Set-ItemProperty -Path "HKCU:\Environment" -Name Path -Value "$USER_PATH;$BIN_DIR" -Type ExpandString
            [Environment]::SetEnvironmentVariable("KOSH_INSTALL", "1", "User")
            [Environment]::SetEnvironmentVariable("KOSH_INSTALL", $null, "User")
            $env:Path += ";$BIN_DIR"
            $IS_ON_PATH = $true
        }
        if ($IS_ON_PATH) {
            Line "Use $(Paint '1;34' 'kosh') to launch the shell."
        }
        else {
            Line "The install path is not in the PATH. Use $(Paint '1;34' $EXE) to launch the shell."
        }
    }
    catch {
        [Console]::Error.WriteLine("kosh installer: $(Paint '1;91' 'error:' -IsColor $IS_ERROR_COLOR) $($_.Exception.Message.TrimEnd('.')).")
        if ($PSCommandPath) { exit 1 }
    }
    finally {
        if ($X) { Set-PSDebug -Off }
        Set-Location $HERE
        Remove-Item -Recurse -Force $WORK -ErrorAction SilentlyContinue
    }
} @args
