# Build Output Policy

The root `build` directory is reserved for the Windows GUI executable, its
portable INI file, and transient MFC/native-test objects under `build/obj`.
Running `scripts/clean_mfc.bat` removes only `build/obj` and `build/logs`.
With the expected build layout, this leaves:

- `build/Rl78ProgrammerTool.exe`
- `build/rl78_programmer.ini`

M032 and M484 Keil builds remain with their projects under `demo_code`. Their
`obj` and `lst` directories contain generated images, maps, listings and build
logs; the build scripts validate those files in place and no longer duplicate
them under root `build`. These Keil directories are excluded by `.gitignore`
and are not removed by `clean_mfc.bat`.

The cleaner does not delete any other file from the root `build` directory.

Diagnostic evidence that must be retained belongs under `docs/diagnostics`.
Hardware manuals and inspection extracts belong under `docs/reference`.
