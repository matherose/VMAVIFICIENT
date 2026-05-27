# macOS Gatekeeper bypass

The `.pkg` shipped under [GitHub Releases](https://github.com/matherose/VMAVIFICIENT/releases)
is **unsigned** — vmavificient is not a paid Apple Developer Program member.
When you download it, macOS attaches a `com.apple.quarantine` extended
attribute that triggers a Gatekeeper block on first run:

> "vmavificient.pkg" cannot be opened because the developer cannot be verified.

This document walks through the two-step bypass.

## Why the package is unsigned

Apple charges $99/year for a Developer ID certificate. v2.0 ships
unsigned to keep distribution friction (and cost) zero. A future
release may sign + notarize once the project has stable funding.

The contained binary is fully reproducible (see `scripts/repro_check.sh`
in the source tree) — anyone can rebuild bit-for-bit from the source
at the tag and compare against the binary you downloaded.

## One-time bypass for the installer

Before double-clicking the `.pkg`, remove the quarantine attribute
that triggers the Gatekeeper warning:

```sh
xattr -d com.apple.quarantine ~/Downloads/vmavificient-2.0.0-rc1-macos-arm64.pkg
```

Then either:

```sh
# (a) GUI: double-click the .pkg, Installer.app runs as usual.
open ~/Downloads/vmavificient-2.0.0-rc1-macos-arm64.pkg

# (b) CLI: sudo installer drops the files directly.
sudo installer -pkg ~/Downloads/vmavificient-2.0.0-rc1-macos-arm64.pkg -target /
```

After install, `which vmavificient` should print `/usr/local/bin/vmavificient`.
Run `vmavificient version` to confirm.

## If you can't / don't want to install system-wide

Use the portable `.tar.zst` instead — no Gatekeeper involvement at
all because nothing is "opened" by the OS:

```sh
zstd -dc vmavificient-2.0.0-rc1-macos-arm64.tar.zst | tar -xf -
./bin/vmavificient version
```

The binary inside the tarball isn't quarantine-flagged (xattr lives
on the `.tar.zst` itself; extracting strips it).

## What "Gatekeeper" actually does here

When Safari / Mail / `curl` downloads a file, macOS LaunchServices
sets the `com.apple.quarantine` xattr on it. Gatekeeper checks this
xattr on first open. If the file is unsigned by a known developer
and the xattr is set, the open is blocked.

The `xattr -d` command strips just that one attribute. It does NOT
disable Gatekeeper system-wide (that would be `sudo spctl
--master-disable`, which we explicitly do NOT recommend).

## Verifying what you installed

To audit the package contents before installing:

```sh
pkgutil --payload-files vmavificient-2.0.0-rc1-macos-arm64.pkg
```

To compute the artifact's sha256 and compare against the value
the release page lists in the build log:

```sh
shasum -a 256 vmavificient-2.0.0-rc1-macos-arm64.pkg
```

(The `.pkg` outer is not reproducible — Apple's pkgbuild embeds
timestamps. The contained binary IS reproducible. See the rc1
release notes for the nuance.)
