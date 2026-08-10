# Modern Emerald Test Report

## Objective
To successfully launch the Modern Emerald mod (version 3.4) on `pokeemerald-multiplatform` and reach the Modern Emerald beginning-of-game configuration/challenge menus, demonstrating that the engine behaves distinctively from vanilla.

## Approach
1. **Mod Loader Hardening**: Enabled the `--mod <mod_id>` CLI parameter, filtering all mod loading so that only the specified mod gets evaluated. This ensures isolated behavior from overlapping mods (like Emerald Legacy).
2. **Config UI Segregation**: `tx_rac_menu.c` from Modern Emerald relies heavily on custom engine structs (e.g. `SaveBlock1` mutations). Instead of replacing the multiplatform engine's struct, we constructed a decoupled `ModernEmeraldConfig` data model.
3. **Runtime Hook**: Injected an isolated callback into `src/main_menu.c`. If `gActiveModSelector == "modern_emerald"`, the Birch Intro seamlessly transitions into `CB2_InitTxRandomizerChallengesMenu` immediately after the gender selection.
4. **Resumption**: After configuring challenges, `CB2_NewGameBirchSpeech_ReturnFromTxRandomizerChallengesOptions` resets the renderer parameters and routes the player back to the name selection screen to continue vanilla flow.

## Results
- The project builds cleanly with `tx_rac_menu.c` acting as a host-side runtime extension.
- The command `./pokeemerald --mods --mod modern_emerald` successfully filters mod payloads.
- The Birch sequence seamlessly hooks into the custom Challenge/Randomizer UI.
- Identified and resolved a SIGSEGV crash during the Challenge/Randomizer menu caused by an uninitialized scroll offset pointer in `AddScrollIndicatorArrowPairParameterized` inside `tx_rac_menu.c`.
- **(NEW)** Fixed a task leak where `arrowTaskId` wasn't being destroyed when leaving the options menu, preventing menu corruption.
- **(NEW)** Fixed a critical text rendering SIGSEGV during the Route 101 Starter Selection sequence. The `mod_text.c` loader was injecting standard ASCII strings without the required `0xFF` (`EOS`) byte, causing `GetStringWidth` to scan unmapped memory. The loader now correctly converts text boundaries, and the game successfully reaches gameplay!
- No vanilla functionality or alternative mod support (like Emerald Legacy) is degraded.

## Next Milestones
- Persist `gModernEmeraldConfig` to a `save.moddata` extension file to preserve challenges across play sessions.
- Address outstanding script opcode incompatibility (for when Modern Emerald specific scripts trigger in the overworld).
