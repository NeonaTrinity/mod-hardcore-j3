# Simple Hardcore Mod
AzerothCore module to replicate hardcore mode. 
Intended to be bizzlike but with some level configurations.

# This Fork

Hardcore mode can now be opted out of, every new player receives a token item which keeps them in hardcore mode.
Hardcore deaths are announced to the world!

## Configurable options

- Players can be resurrected by other players or a graveyard spirit healer but will lose their hardcore status. No running back to your corpse and reviving

- A recurrected player is never the same, you will acquire undead racials and lose most of your given ones (configurable).

- Hardcore Playerbots

- Achievements (Requires MPQ update on client, and DBC update on server)

## Notes 

This fork depends on Playerbots fork of Azerothcore. 

Player deaths and major level up milestones are announced to the world.

# Configuration

## Core Settings

**ModHardcore.Enable**
- Description: Enable Hardcore Mod.
- Default: 0 - Disabled, 1 - Enabled

**ModHardcore.TokenItemId**
- Description: Item ID of the hardcore token item. This is an unused NPC item used to track hardcore players. You may need to change this if it conflicts with other customizations.
- Default: 11100

## Level Range Settings

**ModHardcore.StartLevel** and **ModHardcore.EndLevel**
- Description: Set Hardcore Level at min-max range. This allows you to make certain levels not hardcore. Note that players can also opt out themselves at any time by removing the hardcore token item.
- Default: ModHardcore.StartLevel = 1, ModHardcore.EndLevel = 80

Examples:
- Set a range of level 10 to 79 to enable hardcore, so at level 1-9 and 80 hardcore is disabled.
- Set a range of level 1 to 80 to enable hardcore for all levels.

## Resurrection Settings

**ModHardcore.AllowPlayerRez**
- Description: Allow another player to resurrect dead hardcore player.
- Default: 0 - Disabled, 1 - Enabled

**ModHardcore.AllowSpiritRez**
- Description: Allow Spirit Healer at Graveyard to resurrect.
- Default: 0 - Disabled, 1 - Enabled

**ModHardcore.AllowGMRez**
- Description: Enable GM resurrection.
- Default: 0 - Disabled, 1 - Enabled

## Undead Transformation Settings

**ModHardcore.MakeUndeadOnRez**
- Description: Make player undead on resurrection. Change player racials to mostly undead racials. (Experimental)
- Default: 0 - Disabled, 1 - Enabled

**ModHardcore.MakeUndeadOnGMRez**
- Description: Make player undead on GM resurrection.
- Default: 0 - Disabled, 1 - Enabled

## On Death

**ModHardcore.DestroyEquipmentOnDeath**
- Description: Destroy equiped items and gold on death
- Default: 0 - Disabled, 1 - Enabled

## Chat Settings

**ModHardcore.DisableChatWhenDead**
- Description: Make player undead on GM resurrection.
- Default: 0 - Allow Chat, 1 - No Chat

## Playerbot Settings

**ModHardcore.AllowHardcorePlayerBots**
- Description: Allow Hardcore Player Bots. (Experimental)
- Default: 0 - Disabled, 1 - Enabled

## Achievements & Titles

**ModHardcore.AchievementReward**
- Description: Comma separate pairs of (level, achievement Id)
- Default: "60 15050, 70 15051, 80 15052"

**ModHardcore.TitleReward**
- Description: Comma separate pairs of (level, title Id)
ModHardcore.TitleReward = "60 178, 70 179, 80 180"


# Installation
Official module install guide:https://www.azerothcore.org/wiki/installing-a-module

** Clone and build

- clone into you azerothcore modules folder
git clone https://github.com/peppernz/mod-hardcore.git

- cd to you build folder and start the build for the worldserver

** Database update

This script modifies an unused NPC item into the Hardcore token trinket.

- check the follow file, you may want to update the name or description. if by some chance you've already allocated 11100 to another custom
  item you'll need to edit the script and the conf file to reflect this.
- copy the modules/mod-hardcore/data/sql/db-world/base/create_hc_item_11100.sql to your worldserver data/sql/ or apply to database directly.
  
** Dbc updates on server

For achievements there are 2 patches in mod-hardcore/data/dbc/patches with new achievements. These are optional but you'll need to update the conf to reflect any changes.
The new achievements start at id 15000 to try not to conflict with other modules.

** Titles

It is best to create you own title/titles by using a DBC editor. (I use MyDbcEditor.exe on linux)
The procedure is to juts copy the last entry and edit the new one with your new title name. In the conf file the default title setup is for
title at level 60, 70 then 80 which will be id 178, 179 and 180 if you haven't added titles before. Otherwise your ids will be different
and you'll need to make sure the conf file reflects that (or if you just want a single title)


# To-Do
- Fix buggy undead mode (requires dbc updates - for now don't enable it in your production instance)


# Credits

- [Zindokar](https://github.com/Zindokar) - mod-hardcore creator
- [ZhengPeiRu21](https://github.com/ZhengPeiRu21) - mod-challenge-modes 