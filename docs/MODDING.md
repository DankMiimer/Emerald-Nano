# Pokémon Emerald Multiplatform Modding

The mod manager currently enables dynamic overrides for specific data types through JSON files and raw data files, all placed within the `mods/` directory.

## Getting Started

Enable mods by launching the game with the `--mods` flag:

```bash
./pokeemerald --mods
```

The manager scans the `mods/` directory for mod packages containing a `mod.json` manifest. Mods are loaded in deterministic order based on priority and ID.

## Supported Resource Types

### `data/starters.json`

Overrides the three starter Pokémon presented in Professor Birch's bag.

*Note: In Milestone 1, generic constant-name lookup is not yet supported. You must use numeric species IDs (e.g., 406 for Rayquaza) instead of string macros.*

**Schema:**
```json
{
  "starters": [
    {
      "slot": 0,
      "species": 406,
      "level": 5
    },
    {
      "slot": 1,
      "species": 151,
      "level": 5
    },
    {
      "slot": 2,
      "species": 349,
      "level": 5
    }
  ]
}
```

* `slot`: 0 for left (Treecko), 1 for center (Torchic), 2 for right (Mudkip).
* `species`: The numeric ID of the Pokémon species.
* `level`: The starting level (1-100). Default is 5.

### `data/trainers.json`

Overrides the opponent's Trainer Party configuration and details.

**Schema:**
```json
{
  "trainers": [
    {
      "id": 318,
      "party": [
        {
          "species": 406,
          "level": 99
        }
      ]
    }
  ]
}
```

### `graphics/trainer_front_pics/<ID>.4bpp`

Provides an uncompressed 2048-byte `.4bpp` graphical override for a specific Trainer front sprite. Replace `<ID>` with the `TRAINER_PIC_*` numeric ID.
