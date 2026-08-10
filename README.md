# Mod J3's Hardcore

AzerothCore 3.3.5a J3's Hardcore module with an opt-out Hardcore mechanic. Fallen Hardcore players may return to non-Hardcore as "Fallen Undead" versions of their race. Players retain their level, class, skills, and progression.

This module requires no AzerothCore core patches and no client patches. Add the project to your `modules` folder, rebuild, configure the module, and run the included SQL.

The included SQL repurposes two unused NPC items as the default Hardcore Token and PvP Death Flag Token. The Hardcore Token determines whether a character is participating in Hardcore mode. The PvP Death Flag prevents Hardcore resurrection penalties for ignored PvP deaths when enabled.

If enabled, Fallen Hardcore players can have their equipped items destroyed on death and return as "Fallen" versions of their existing characters using Death Knight skin data to create corpse-like undead appearances for each race without client patching.

If a character has the configured Hardcore Token and is inside the configured Hardcore level range, Hardcore rules apply. Destroying the token manually opts the character out immediately.

Ignored PvP, battleground, and arena deaths can use `ModHardcore.PvPDeathFlagItemId` as a temporary persistent keyring marker so the exclusion survives logout and server restart.

## Core death / resurrection flow

A qualifying keyed Hardcore death keeps the Hardcore Key while the character is dead.

If no allowed resurrection occurs, the character remains dead with the Key and therefore remains Hardcore across relogs/restarts.

When `ModHardcore.MakeUndeadOnRez = true`, a **qualifying Hardcore death** immediately establishes and saves the fallen DK customization on the dead Player. If the player releases afterward, AzerothCore creates the corpse from those same appearance bytes. If classification finishes after release, the module repairs the existing corpse to match the Player.

When an allowed player, Spirit-Healer, or GM/direct resurrection occurs, the module runs an idempotent appearance ensure pass before/after resurrection: an already-valid DK customization is preserved exactly, invalid/missing pieces are repaired, and any existing corpse is made to match the Player. There is no separate GM appearance toggle.

When the resurrection succeeds:

1. The same validated fallen customization remains in place; it is never intentionally rerolled at resurrection.
2. Optional fallen racial rewards are learned when fallen mode is enabled.
3. If equipment destruction is enabled, optional level-gated resurrection clothing/inventory items are granted.
4. If fallen resurrection is enabled, the optional fallen title and achievement are granted.
5. If memorial teleport is enabled, the character is teleported once with the fallen appearance already persisted.
6. The module waits until the character is alive and fully in-world on the destination map, then gives the map/client a short non-blocking stabilization window.
7. The Hardcore Key is destroyed.
8. The character is permanently out of Hardcore.

When memorial teleport is disabled, the same appearance/reward work occurs immediately and the Key is destroyed without the destination-map finalization stage.

Deaths after the Key has been destroyed are ordinary deaths and are ignored by Hardcore penalties and announcements.

## Fallen Death Knight-style appearance

`ModHardcore.MakeUndeadOnRez` controls the fallen appearance system. The appearance is established on a qualifying Hardcore death so the dead Player, later corpse creation, resurrection, and memorial teleport all start from the same persisted customization.

`ModHardcore.UseDeathKnightHair = true` extends that transformation to DBC-valid Death Knight hair customization. The module preserves the player's current `hairStyle + hairColor` when that pair is already in the Death Knight section for the character's race/gender. Otherwise it chooses a valid DK hair pair. Existing facial hair is preserved when valid and only rerolled when the DBC provides DK facial-hair rows and the current value is invalid. Setting this option to `false` preserves the original hair style, hair color, and facial hair exactly. Tauren and native Forsaken have no DK hair rows in the supplied DBC and therefore keep their existing hair customization.

The automatic transformation constructs the full `PLAYER_BYTES` value once (skin, face, hair style, and hair color) rather than emitting individual byte writes. `PLAYER_BYTES_2` is likewise written once when DK facial-hair correction is needed.

The module writes validated Death Knight customization values derived from the supplied `CharSections.dbc` for the character's existing race and gender. Skin and face are always selected from the DK mapping; hair/facial-hair values are optionally made DK-valid through `ModHardcore.UseDeathKnightHair`. It does **not** change the real race, class, faction, equipment, weapons, animations or power type.

Automatic appearance changes are skipped for:

- Native Undead/Forsaken, because they are already undead.
- Death Knights, because they already use the Death Knight appearance system.

The automatic mapping is:

```text
Human Male       skins 12,13,14 faces 0,2,11
Human Female     skins 12,13,14 faces 0,1,10
Orc Male         skins 15,16,17 faces 0,1,5
Orc Female       skins 11,12,13 faces 0,7,8
Dwarf Male       skins 19,20,21 faces 0,1,9
Dwarf Female     skins 11,12,13 faces 0,5,6
Night Elf Male   skins 9,10,11 faces 0,1,4
Night Elf Female skins 9,10,11 faces 0,6,7
Tauren Male      skins 19,20,21 faces 0,2,4
Tauren Female    skins 11,12,13 faces 0,2,3
Gnome Male       skins 7,8,9 faces 0,2,3
Gnome Female     skins 7,8,9 faces 0,3,5
Troll Male       skins 15,16,17 faces 0,1,3
Troll Female     skins 15,16,17 faces 0,3,5
Blood Elf Male   skins 16,17,18 faces 2,4,7
Blood Elf Female skins 16,17,18 faces 0,3,8
Draenei Male     skins 14,15,16 faces 1,4,9
Draenei Female   skins 12,13,14 faces 0,4,8
```

Native Undead/Forsaken are intentionally not mapped for automatic replacement.

Skin, face, hair style, and hair color share the same `PLAYER_BYTES` field. The module builds the complete updated 32-bit value and writes it once. If DK hair is enabled, valid current DK hair is preserved and invalid hair is replaced with a valid DK combination. The canonical appearance is saved on the qualifying death, then reused rather than rerolled at resurrection.

The important part is validating `dkSkin` and `dkFace` against the character's race/gender combinations before writing them.


## Fallen DK skin synchronization

The display-repair layer is split into two independently configurable systems. `ModHardcore.EnableDkSkinSync = true` controls the older timer-based login/zone/map-entry fallback repairs. `ModHardcore.EnableDkSkinObjectCreateSync = true` controls the newer event-driven object-create repair. Both are intentionally separate from `ModHardcore.MakeUndeadOnRez`, so either sync system can continue repairing existing fallen appearances even if new transformations are disabled.

WoW 3.3.5a can correctly save and display these DK-only `CharSections.dbc` skin/face values on the owning character, but another client can rebuild a non-DK player with ordinary textures after that **observer** performs a full map/world load. Live changes to the subject's skin byte force the observer client to rebuild the correct DK skin/face composite.

The module therefore combines map/zone lifecycle repairs with an object-create repair:

1. **Observer map/zone stabilization:** real-player login and the later `OnPlayerUpdateZone` / `OnPlayerUpdateArea` lifecycle hooks schedule an early repair plus a fallback repair. Zone/area defaults are `250 ms` + `750 ms`; login defaults are `250 ms` + `1500 ms`. All four timings are configurable without rebuilding. If both zone and area update during the same transfer, the later event restarts the pair instead of stacking extra pulses.
2. **Fallen player arrives second:** if a real non-DK/non-Forsaken player whose current skin+face match the validated DK mapping enters a map after observers are already there, the module waits 500 ms, then repairs that fallen player only for real observers who actually have them at client. This reverse path remains one-shot.
3. **Fallen player becomes visible later:** AzerothCore's existing `UnitScript` values-update hooks are used while the player object is actually being built for that observer. The outgoing create packet is patched with a valid DK skin, then one pair-specific observer/subject repair is scheduled after `ObjectCreateSyncTime`. This closes the case where the observer's earlier zone scans finished before the fallen player was in visibility range. It is event-driven and requires no AzerothCore core patch.
4. A repair sends an **observer-only** temporary alternate DK skin update followed immediately by the subject's actual saved `PLAYER_BYTES` value.
5. The subject's real server values and database row are never changed or saved by this synchronization.

Sync settings:

```ini
# Timer-based fallback system
ModHardcore.EnableDkSkinSync = true
ModHardcore.ZoneSyncTime = 250
ModHardcore.ZoneSyncTimeRetry = 750
ModHardcore.LogonSyncTime = 250
ModHardcore.LogonSyncTimeRetry = 1500

# Event-driven object-create system
ModHardcore.EnableDkSkinObjectCreateSync = true
ModHardcore.ObjectCreateSyncTime = 100
```

This split allows direct A/B testing: set `EnableDkSkinSync = false` while leaving `EnableDkSkinObjectCreateSync = true` to test whether the newer visibility-time hook is sufficient by itself.

The synchronization is event-driven, not a continuous visibility scanner. The object-create path queues only the exact observer/subject pair whose player object AzerothCore is already constructing; there is no periodic proximity scan. Playerbots never act as observers and never schedule fallen-player sync work.

Because the feature identifies fallen appearances from the actual validated appearance bytes rather than the Hardcore Key, it continues to work after successful resurrection destroys the Key and can also support characters given fallen textures by another system later.

## Fallen racial rewards

Non-native Undead/Forsaken characters use a master enable plus two configurable spell slots:

```ini
ModHardcore.GiveFallenHCUndeadRacials = true
ModHardcore.FallenHCUndeadRacialSpell1 = 20577
ModHardcore.FallenHCUndeadRacialSpell2 = 5227
```

The defaults preserve Cannibalize (`20577`) + Underwater Breathing (`5227`).

Native Undead/Forsaken use their own enable and two bonus slots:

```ini
ModHardcore.GiveNativeUndeadFallenRacials = true
ModHardcore.FallenUndeadBonusSpell1 = 8359
ModHardcore.FallenUndeadBonusSpell2 = 0
```

For any configurable fallen spell slot, `0` or `-1` means no spell. Death Knights are excluded only from automatic skin/face replacement; a non-Undead Death Knight can still receive the configured non-native fallen racial rewards.

## Fallen title and achievement

When the successful resurrection is configured to create the fallen/undead state, these optional rewards can be granted:

```ini
ModHardcore.TitleOnDeath = true
ModHardcore.TitleOnDeathId = 119
ModHardcore.AchievementOnDeath = true
ModHardcore.AchievementOnDeathId = 3456
```

The default title is ID `119` (The Forsaken).

## Resurrection settings

- `ModHardcore.AllowPlayerRez` - allow another player to resurrect the Hardcore character.
- `ModHardcore.AllowSpiritRez` - allow Spirit Healer resurrection.
- `ModHardcore.AllowGMRez` - allow GM/direct resurrection.

Self resurrection and corpse reclaim remain blocked after a qualifying Hardcore death.

`ModHardcore.MakeUndeadOnRez` controls the fallen DK-style appearance for qualifying Hardcore deaths. Player, Spirit-Healer, and allowed GM/direct resurrection all use the same validation/finalization path; there is no separate GM appearance setting.

## Equipment loss and resurrection items

`ModHardcore.DestroyEquipmentOnDeath = true` destroys equipped items and money on each qualifying keyed Hardcore death.

When that setting is enabled, optional resurrection items can provide simple replacement clothing and supplies after an allowed resurrection:

```ini
ModHardcore.RespawnWithRezItems = true
ModHardcore.MinLvlGetRezItem = 20
ModHardcore.RespawnRandomEquip = "44694, 44693"
ModHardcore.RespawnItemInv = "9332, 44646"
```

One random item from `RespawnRandomEquip` is equipped in its normal valid equipment slot chosen by AzerothCore (shirt, chest, boots, and so on). If it cannot legally be equipped on that character, the module attempts inventory fallback. Every item listed in `RespawnItemInv` is added once to inventory. The whole reward subsection is ignored when equipment destruction is disabled or the player is below `MinLvlGetRezItem`.

There is no separate equipment-loss mode. In the normal second-life flow, the successful resurrection removes the Key, so later deaths are no longer Hardcore and cannot destroy equipment through this module.

## Hearthstone cooldown reset

With:

```ini
ModHardcore.ResetHearthstoneOnDeath = true
```

the standard Hearthstone cooldown is cleared on a **qualifying keyed Hardcore death**. The same PvP classification is used as every other Hardcore death effect, so a battleground/arena/world-PvP death that is configured not to count does not reset Hearthstone. The cooldown clear is sent again during successful resurrection finalization so the client shows it ready after a memorial transfer.

## PvP death filtering and Mak'gora

PvE and environmental deaths always count. These settings control PvP categories independently:

```ini
ModHardcore.CountBattlegroundDeaths = false
ModHardcore.CountArenaDeaths = false
ModHardcore.CountPvPDeaths = true
```

Ignored PvP-category deaths use a persistent item marker instead of a custom database table:

```ini
ModHardcore.PvPDeathFlagItemId = 34908
```

The flag is granted only when the character currently has the Hardcore Key **and** the matching category is configured not to count (`CountPvPDeaths = false`, `CountBattlegroundDeaths = false`, or `CountArenaDeaths = false`). No fallen appearance or Hardcore death penalties are applied to that ignored death. The item persists through logout/server restart, causes the eventual resurrection to bypass Hardcore resurrection/forfeit logic, and all copies are destroyed on that successful resurrection while the Hardcore Key remains. The configured flag item must be different from `TokenItemId`; a unique custom key item is recommended after testing.

`ModHardcore.EnableMakgora = true` makes a Hardcore duel loss/flee fatal. Interrupted duels do not trigger the Mak'gora death.

## Death announcements

Qualifying keyed Hardcore deaths can be broadcast with:

```ini
ModHardcore.AnnounceDeaths = true
ModHardcore.DefaultPlayerDeathMessageLevel = 10
```

The default of level 10 avoids low-level death spam. Each player can override the filter for the current login session:

```text
.hc death off
.hc death default
.hc death 50
```

`.hc` displays the current Hardcore rules and the player's status.

## Memorial teleport

The optional destination is configured with:

```ini
ModHardcore.TeleportOnHardcoreDeath = false
ModHardcore.DeathTeleportMap = 609
ModHardcore.DeathTeleportX = 2355
ModHardcore.DeathTeleportY = -5665
ModHardcore.DeathTeleportZ = 426
ModHardcore.DeathTeleportO = 0.0
```

The teleport is intentionally one-shot:

- If both player resurrection and Spirit Healer resurrection are disabled, the dead Hardcore character is teleported to the memorial on death and remains there as a dead/ghost character.
- If either player resurrection or Spirit Healer resurrection is allowed, the character is not teleported on death. The complete fallen customization was already established on the qualifying death; resurrection validates/reapplies the same values and repairs the corpse if necessary. After resurrection succeeds, fallen rewards are granted and the character is teleported once.
- The destination-map object is therefore constructed from the already-persisted DK appearance. The existing 500 ms post-arrival stabilization remains only for final Hardcore forfeit/key cleanup and Hearthstone state. No world-thread sleep is used.
- If the character was already memorialized at death, a later exceptional GM resurrection does not issue a redundant cross-map teleport; it still goes through the post-arrival finalization stage.

This ordering specifically targets the resurrection/teleport race where clients could briefly or persistently construct a mixed DK-body/normal-face appearance until relog.

## Level range and completion rewards

Hardcore rules apply through `ModHardcore.EndLevel`.

Examples:

- `EndLevel = 59`: crossing 59 -> 60 completes the challenge.
- `EndLevel = 60`: level 60 remains Hardcore; crossing 60 -> 61 completes it.
- `EndLevel = 80` on a level-80 realm: the challenge does not normally complete.

Milestone rewards are evaluated **before** completion logic and require the Hardcore Key. Therefore a player crossing 59 -> 60 can receive level-60 rewards when `EndLevel` is either 59 or 60.

Existing achievement/title maps remain supported:

```ini
ModHardcore.AchievementReward = "5 2090, 60 2186, 70 2187"
ModHardcore.TitleReward = "5 45, 60 141, 70 142"
```

A simple optional challenge item/spell milestone is also available:

```ini
ModHardcore.ChallengeRewardLevel = 60
ModHardcore.ChallengeRewardItemEnable = false
ModHardcore.ChallengeRewardItemId = 0
ModHardcore.ChallengeRewardSpellEnable = false
ModHardcore.ChallengeRewardSpellId = 0
```

Item and spell rewards can be enabled independently. Destroying the Hardcore Key before the milestone forfeits all Hardcore achievement/title/item/spell rewards.

## DK appearance debug commands

Enable only for development:

```ini
ModHardcore.DebugHCDK = true
```

Commands:

```text
.hcdk info
.hcdk apply <skin> <face> [hairColor] [hairStyle] [facialHair]
.hcdk refresh
.hcdk sync
.hcdk restore
.hcdk save
.hcdk auras
.hcdk show <race|all>
.hcdk showhair <race|all>
```

The raw `.hcdk apply` command remains a developer test tool. Automatic fallen appearance uses validated DK skin/face data and, when enabled, DBC-derived DK hair data.

For customization testing, `.hcdk apply <skin> <face> [hairColor] [hairStyle] [facialHair]` accepts the extra fields in that exact optional order. Any omitted field is preserved. The command constructs `PLAYER_BYTES` once and updates `PLAYER_BYTES_2` only when `facialHair` is supplied. The debug backup records all five customization values, so `.hcdk restore` restores skin, face, hair style, hair color, and facial hair together.

`.hcdk showhair <race|all>` prints the DK hair styles, hair colors, and facial-hair styles extracted from the supplied `CharSections.dbc` mapping used by the automatic transformation.

Example Blood Elf male diagnostic test:

```text
.hcdk apply 16 2 10
.hcdk save
```

After relogging both test clients, compare what the character sees locally with what another player sees. Then run:

```text
.hcdk refresh
```

`.hcdk refresh` does **not** alter or save appearance values. It forces the current `PLAYER_BYTES` and `PLAYER_BYTES_2` fields to be resent, which lets us test whether the discrepancy is caused by fresh object/login visibility rather than database persistence.

## Installation

Install/build the module normally as an AzerothCore module.

This streamlined rework requires **no custom `acore_characters` Hardcore tables** and contains no leaderboard/history SQL.

The original world-database token item SQL remains part of the base module and may still be required if item `11100` has not already been configured as the Hardcore Key.

## Credits

- peppernz - Mod fork HC token code, and inspiration - https://github.com/peppernz/mod-hardcore
- Zindokar - original `mod-hardcore` creator https://github.com/zindokar
- ZhengPeiRu21 - `mod-challenge-modes` https://github.com/ZhengPeiRu21
- J3 fork - token opt-out, death filtering/announcements, resurrection redesign, Mak'gora, memorial flow, and validated Death Knight-style fallen appearance

`.hcdk sync` is an observer-side diagnostic. Run it from the observing GM while targeting the fallen character. It prints whether the observer is real, both players are in-world/on the same map, `HaveAtClient()` is true, the target matches a valid fallen DK appearance, and which alternate DK skin/hair values would be used. If all gates pass it sends the exact same two-packet observer-only appearance pulse used by the automatic DK skin synchronizer.
