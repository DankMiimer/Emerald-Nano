# Pokeemerald-Multiplatform ROM Hack Importer

The ROM Hack Importer is a Python-based utility located at `tools/romhack_importer/`.
Its purpose is to extract compatible modifications from a standard `pret/pokeemerald` decompilation project and convert them into a runtime mod package for `pokeemerald-multiplatform`.

## Features
- **Map Patching**: Detects changes to map tiles, dimensions, and object events.
- **Script Compilation**: Extracts `scripts.inc`, detecting modified scripts and compiling them into runtime bytecode.
- **Starters**: Detects changes to `starter_choose.c` and exports starter overrides.
- **Species**: Parses `species_info.h` and extracts changed Pokémon base stats.
- **Engine Change Detection**: Analyzes `src/` and `include/` to detect modifications to C code, warning you in the port report.
- **Constant Resolution**: Automatically resolves C preprocessor macros (e.g. `SPECIES_BULBASAUR`) into their numeric values for the runtime json.

## Usage

Run the importer by providing the path to a completely vanilla pokeemerald tree, the path to the modified hack, and the output directory for the mod package.

```bash
python3 tools/romhack_importer/main.py --vanilla <path/to/vanilla> --source <path/to/hack> --output mods/<mod_name>
```

### Options
- `--dry-run`: Runs the importer and outputs a report to stdout, but does not write any files to disk.
- `--force`: If the output directory already exists, overwrite it.

## Port Report

Every time the importer runs, it generates two files in the output directory:
- `PORT_REPORT.md`: A human-readable markdown summary of what was imported and what was unsupported.
- `port_report.json`: A machine-readable copy of the report.

If you modified C code, added new object event types, or used unsupported script macros, the report will list exactly which files and scripts were skipped so you can port them manually or redesign them.
