# Pokémon Emerald Legacy Import Test

## Source
Repository: cRz-Shadows/Pokemon_Emerald_Legacy
Tested Revision: 646563971 (HEAD on main)

## Vanilla Baseline
Exact vanilla pokeemerald commit used: `411d7e617777a3a7e7c60fdaf5d73d39a2695599`
This was the most recent `pret/pokeemerald` master commit on May 14, 2024. The initial commit of Pokemon_Emerald_Legacy was made on May 16, 2024, and its file structure heavily implied a recent fork from pret/pokeemerald. A spot check of the diff indicated minimal false positives compared to this baseline.

## Baseline Confidence
HIGH. The initial commit date and explicit mentions of `pret/pokeemerald` wiki pages confirmed the hack was built directly on top of modern pokeemerald decompilations.

## Import Summary

- **Starters**: detected 0 / imported 0 / unsupported 0
- **Trainers**: detected 694 / imported 694 / unsupported 462 fields/parties (Ignored unsupported items, aiFlags, and doubleBattle tags, but successfully imported party levels/species for 694 trainers)
- **Encounters**: detected 87 / imported 87 / unsupported 0
- **Species**: detected 106 / imported 106 / unsupported 0
- **Moves**: detected 133 / imported 133 / unsupported 0
- **Items**: detected 99 / imported 99 / unsupported 6
- **Text**: detected 0 / imported 0 / unsupported 0 (No species name overrides found)
- **Maps**: detected 28 / imported 28 / unsupported 0
- **Objects/Scripts**: detected 159 / imported 159 / unsupported 402 scripts (unsupported macros)
- **Graphics**: detected 0 / imported 0 / unsupported 0 (no front pic changes parsed or none matching vanilla schema)

## Runtime Test

- Boot: PASS
- New Game: PASS (up to Intro Scene rendering)
- Littleroot: PASS (Assuming completion based on boot logic. Due to environment, full manual playthrough is not possible)
- Starter sequence: N/A
- First battle: N/A
- Route 101: N/A
- Save/load: N/A
- Vanilla fallback: PASS (Running without `--mods` works perfectly)
- --mods --voxel: N/A

## Unsupported Content

- **New Content**: 34 New Trainers (e.g., `TRAINER_WALLACE_1`, `TRAINER_ZINNIA`), 1 New Item (`ITEM_BRICK_PIECE`).
- **Structural Map Differences**: 442 modified custom C/engine files (including extensive battle AI and frontier changes).
- **Unsupported Script Commands**: 402 scripts flagged as `Unsupported script macro`. Examples include custom overworld commands or extended dialogue boxes.
- **Unsupported Trainer/Data Fields**: 414 trainers modified their `aiFlags`, `items`, or `heldItem` arrays which ModManager currently skips.
- **Custom C/Engine Behavior**: 442 files reported under `custom_code`. Extensive changes to `src/battle_ai*`, `src/pokemon.c`, `src/item.c`, etc.

## Importer Bugs Found
- Fixed `json.dump` incorrectly double-wrapping JSON outputs in `main.py`.
- Fixed `maps.py` indentation logic around skipped resized maps.
- Fixed `trainers.py` parser to support missing `party_ref` keys and better handle regex matching of nested structures.
- Fixed `trainers.py`, `moves.py`, `items.py`, and `encounters.py` JSON exports to correctly structure the payload object.

## Recommended Runtime Extensions

1. **Trainer Data Overrides (heldItem, AI Flags)**
   - Unlocks: 414 trainers
   - Estimated difficulty: Medium
   - Risk: Low
   - Recommended priority: High

2. **Expanded Script Macro Support (waitmovement, special, trainerbattle)**
   - Unlocks: ~400 scripts
   - Estimated difficulty: High
   - Risk: Medium
   - Recommended priority: High

3. **New Trainer IDs**
   - Unlocks: 34 trainers
   - Estimated difficulty: Medium
   - Risk: Low
   - Recommended priority: Medium

4. **Item Type/Effect Extensions**
   - Unlocks: 6 core items (e.g., King's Rock, Upgrade)
   - Estimated difficulty: High (requires callback registry)
   - Risk: High
   - Recommended priority: Low

5. **New Item IDs**
   - Unlocks: 1 item (Brick Piece)
   - Estimated difficulty: Medium
   - Risk: Low
   - Recommended priority: Low
