# Release downloads

Koshka releases are available for Linux, macOS, and Windows. Choose the
binary for your operating system and processor. Replace `X.Y.Z` below with
the release version.

| System | Release file |
| ------ | ------------ |
| Linux on x86-64 | `kosh-linux-amd64-X.Y.Z` |
| Linux on ARM64 | `kosh-linux-aarch64-X.Y.Z` |
| macOS on Apple silicon | `kosh-darwin-aarch64-X.Y.Z` |
| Windows on x86-64 | `kosh-win32-amd64-X.Y.Z.exe` |

The release also includes `SHA256SUMS`, the compressed manuals `kosh.1.zst`
and `kosh.5.zst`, and the Bash completion file `kosh.bash`. Check your
download against its entry in `SHA256SUMS`. Run `kosh --version` to see the
installed version.

The [shell manual](kosh.1) covers invocation, commands, the editor, and
bundled utilities. The [settings manual](kosh.5) covers `kosh.conf` and
startup scripts. Their HTML versions are [the shell manual](html/kosh.1.html)
and [the settings manual](html/kosh.5.html).

On macOS, an existing Homebrew installation can be updated with
`brew update && brew upgrade kosh`.

The VS Code and Zed extensions download Koshka when it is missing from
`PATH`. The Neovim plugin is available as `kosh.nvim`.
