# Windows SmartScreen bypass

The `.msi` shipped under [GitHub Releases](https://github.com/matherose/VMAVIFICIENT/releases)
is **unsigned** — vmavificient does not have a code-signing certificate.
When you download it, Windows attaches a "Mark of the Web" (MOTW) flag
that triggers a SmartScreen warning on first run:

> Windows protected your PC. Microsoft Defender SmartScreen prevented
> an unrecognized app from starting.

This document walks through the bypass.

## Why the installer is unsigned

A code-signing certificate from a trusted CA costs $200–$500/year
and requires identity verification. v2.0 ships unsigned to keep
distribution friction (and cost) zero. A future release may sign
once the project has stable funding.

The contained binary is fully reproducible (`scripts/repro_check.sh`
in the source tree) — anyone can rebuild bit-for-bit from the source
at the tag and compare against the binary you downloaded.

## One-click bypass at install time

1. Double-click `vmavificient-2.0.0-rc1-windows-x86_64.msi`.
2. SmartScreen pops the blue "Windows protected your PC" dialog.
3. Click **More info** (the small link at top-left of the dialog).
4. Click the **Run anyway** button that appears.

Windows now runs the installer normally. Accept the UAC prompt
(installer needs admin to write under `%ProgramFiles%`).

## Verifying what you installed

The installer drops:

```
%ProgramFiles%\vmavificient\
    vmavificient.exe
    tessdata\eng.traineddata
    doc\LICENSE
    doc\LICENSE-BINARY
    doc\README.md
```

It also adds `%ProgramFiles%\vmavificient` to the **system** PATH
and sets `TESSDATA_PREFIX` as a **system** environment variable.

> **Open a fresh shell** (PowerShell or cmd) after install — existing
> shell windows don't pick up env-var changes from the MSI.

```cmd
where vmavificient
vmavificient version
echo %TESSDATA_PREFIX%
```

`where` should print the full install path, `version` should report
`2.0.0-rc1`, and `TESSDATA_PREFIX` should point at the tessdata subdir.

## Stripping the Mark of the Web before install

If you'd rather pre-strip the MOTW flag than click through the warning:

```powershell
Unblock-File -Path C:\Path\To\vmavificient-2.0.0-rc1-windows-x86_64.msi
```

Then double-click as usual; SmartScreen won't intercept.

## If you can't / don't want to install system-wide

Use the portable `.tar.zst` instead — no SmartScreen involvement:

```powershell
# Extract under PowerShell (requires zstd; install via scoop or chocolatey)
zstd -dc vmavificient-2.0.0-rc1-windows-x86_64-mingw.tar.zst | tar -xf -
.\bin\vmavificient.exe version
```

For OCR to find the bundled tessdata in this layout, set
`TESSDATA_PREFIX` manually:

```powershell
$env:TESSDATA_PREFIX = "$PWD\share\vmavificient\tessdata"
```

## Reporting if the bypass fails

If SmartScreen blocks even after "More info → Run anyway", or if
the installer reports an error, open an issue with:
- Windows version (`winver`)
- The `.msi` filename + sha256
- Screenshot of the dialog

[https://github.com/matherose/VMAVIFICIENT/issues](https://github.com/matherose/VMAVIFICIENT/issues)
