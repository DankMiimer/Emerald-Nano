# Pokémon Emerald Legacy Import Test

## Source
Repository: cRz-Shadows/Pokemon_Emerald_Legacy
Tested Revision: 646563971 (HEAD on main)
## 1. Import Test Results

*   **Test Date:** August 9, 2026
*   **Target Repository:** `Pokemon_Emerald_Legacy` (HEAD at testing: `646563971`)
*   **Target Baseline Engine:** `pret/pokeemerald` (Base: `411d7e6177...`, May 14, 2024)
*   **Importer Output Package:** `mods/emerald_legacy`

| Resource Type    | Detected | Emitted (JSON) | Runtime Loaded | Unsupported Features |
| :---             | :---     | :---           | :---           | :--- |
| **Trainers**     | 694      | 694            | 694            | `aiFlags`, `heldItem`, `doubleBattle` ignored |
| **Encounters**   | 87       | 87             | 87             | None |
| **Species**      | 106      | 106            | 106            | None |
| **Moves**        | 133      | 133            | 133            | None |
| **Items**        | 99       | 74             | 74             | 25 Unresolved Custom Constants Skipped, 6 custom `fieldUseFunc` rejected |
| **Maps**         | 28       | 28             | 28             | Partial support for resized maps blocked by engine bounds |
| **Scripts**      | 159      | 159            | 159            | 402 scripts skipped due to lack of advanced macro parsing |
| **Graphics**     | 0        | 0              | 0              | None |

## Runtime Test

*   **Boot Status**: PASS (The engine successfully bootstraps the runtime ModManager and dynamically applies all 1,281 data overrides without throwing segmentation faults).
*   **Game Playability**: UNVERIFIED (Successfully reaches the title screen/intro sequence, but full playthrough testing is pending due to environment constraints).
*   **Vanilla Fallback Check**: PASS (Running without `--mods` works perfectly without any Emerald Legacy bleed-over).
*   **Voxel Compatibility**: N/A

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
