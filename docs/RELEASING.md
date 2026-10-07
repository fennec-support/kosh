# Release checklist

This checklist takes a green staging branch through every channel from which
users install. Complete the steps in order because each step depends on the
preceding steps. Repositories live under `github.com/fennec-support` unless
noted.

## 1. Before tagging

- [ ] CI is green on `staging` for Linux, macOS, and Windows, including the
      coverage and Windows test jobs.
- [ ] `make MODE=rel test`, `make toiletline_test`, `NO_TOILETLINE=1 make`, and
      `make -C src MODE=rel TARGET=Windows_NT` pass locally.
- [ ] `src/toiletline` points at a commit that is pushed to a public branch of
      `toiletbril/toiletline`. A release tarball does not carry submodule
      contents, and the package recipes fetch toiletline by that commit.
- [ ] `docs/kosh.1`, `docs/kosh.5`, and `completions/kosh.bash` describe every
      new flag, option, variable, and builtin.

## 2. kosh

- [ ] Set the version in `src/base/Common.hpp`: `KOSH_VER_MAJOR`, `MINOR`,
      `PATCH`, and an empty `KOSH_VER_SUFFIX`. Commit on `staging`.
- [ ] Merge `staging` into `master` and push.
- [ ] Run the **Release** workflow with `ref` = `master` and `tag` = `X.Y.Z`. It
      cross-compiles, pushes the `ghcr.io` image, and creates the release as a
      **prerelease** with `<TBD>` notes.
- [ ] Check the assets: `kosh-darwin-aarch64-X.Y.Z`, `kosh-linux-aarch64-X.Y.Z`,
      `kosh-linux-amd64-X.Y.Z`, `kosh-win32-amd64-X.Y.Z.exe`, `kosh.1.zst`,
      `kosh.5.zst`, `kosh.bash`, and `SHA256SUMS`.
- [ ] Write the release notes, then turn off **prerelease** and mark it
      **latest**. The installers, the Homebrew formula, and every editor client
      skip prereleases, so nothing below sees the release until this is done.
- [ ] Check the installers against it:
      `KOSH_INSTALL_DRY_RUN=1 sh scripts/install.sh` and `scripts/install.ps1`.
- [ ] Set the next development version on `staging`: bump `KOSH_VER_PATCH` and
      set `KOSH_VER_SUFFIX` to `"/dev"`.

## 3. Homebrew (`homebrew-kosh`)

- [ ] Nothing to edit: the formula reads the newest release tag and its
      `SHA256SUMS` each time Homebrew loads it.
- [ ] Re-run its CI, or `brew update && brew upgrade kosh` on macOS, and
      confirm `kosh --version` prints the new version.

## 4. Distribution recipes (`deploy/`)

- [ ] Run `scripts/update-package-recipes.sh` with the tag as its operand when
      needed. The script pins the release commit and time, the toiletline
      commit, and both SHA-512 sums in `deploy/archlinux/PKGBUILD` and
      `deploy/alpine/APKBUILD`. It resets `pkgrel` when the version changes,
      stores the tag in `_tag`, removes a leading `v` from `pkgver`, and spells
      a pre-release such as `0.2.0-rc1` as `0.2.0_rc1`. Any other hyphenated tag
      is refused. Commit the result.
- [ ] Once the release carries the `FORTIFY_FLAGS` change in `src/Makefile`,
      delete the `_FORTIFY_SOURCE` workaround in the PKGBUILD's `build()`.
- [ ] Once the release runs as `koshkit` from a link of that name, add
      `ln -s kosh "$pkgdir/usr/bin/koshkit"` to both recipes' `package()`,
      matching `make install`.
- [ ] **AUR** (`kosh-shell`): the package receives no review. Copy `PKGBUILD`
      and `kosh-shell.install` into the AUR clone, run
      `makepkg --printsrcinfo > .SRCINFO`, build once with `makepkg`, and push.
- [ ] **Alpine aports**: the maintainer named in the APKBUILD opens or updates
      the merge request against aports.
- [ ] **nixpkgs**: `flake.nix` reads the version from `Common.hpp`, so the flake
      needs no edit. A nixpkgs package, once it exists, needs its version and
      hash bumped in a pull request.

## 5. Editor clients

The clients download the newest kosh release automatically, so release kosh
clients only when their own code changes.

- [ ] **VS Code** (`kosh-vscode`): bump `version` in `package.json`, push, then
      push a tag `vX.Y.Z` (the workflow only runs on tags starting with `v`). It
      publishes to the Marketplace with `VSCE_PAT` and to Open VSX with
      `OVSX_PAT`.
- [ ] **Zed** (`kosh-zed`): bump `version` in `extension.toml` and
      `Cargo.toml`, push, then open one pull request against
      `zed-industries/extensions` that moves the `extensions/kosh-lsp` submodule
      to that commit and sets the same `version` in `extensions.toml`. Run
      `pnpm sort-extensions` and `pnpm test` in that checkout first.
- [ ] **Neovim** (`kosh.nvim`): push; plugin managers track the branch. Tag
      `X.Y.Z` when the plugin itself changed.

## 6. After

- [ ] Install from each channel on a clean machine or container and run
      `kosh --version`.
- [ ] Open sessions in VS Code and Zed with no kosh on `PATH` and confirm the
      client downloads the new release.
