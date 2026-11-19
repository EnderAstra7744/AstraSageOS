# aspi-package-sage

Package repository of AstraSageOS. `aspi install <Name>` (and AstraStore) reads the file list of this repository and
downloads the archive `Name-Version.aspi` (also `.tar.gz` / `.tar.xz`).

| Package | What it is |
|---|---|
| `NetSurf-3.11.0.aspi` | NetSurf web browser (native program, about 2.8 MB unpacked) |
| `FroggyCode-2.0.0.aspi` | FroggyCode, the AstraScript code editor / IDE (native program) |

Rules: one archive per package version, name and version split at the last `-` (or `_`) before a digit,
`ASPIManifest.json` at the top of the archive, archive size at most 2 MiB (the unpacked package can be bigger).
