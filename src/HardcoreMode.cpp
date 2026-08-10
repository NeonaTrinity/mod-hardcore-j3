/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license:
 * https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * DML streamlined Hardcore rework:
 * - Hardcore Key remains the sole authority for Hardcore participation.
 * - No custom character-database tables are required.
 * - Successful allowed resurrection forfeits Hardcore and destroys the Key.
 * - Optional Death Knight-style fallen appearance is applied without changing race/class.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Corpse.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Random.h"
#include "RBAC.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "UpdateData.h"
#include "UpdateMask.h"
#include "WorldSession.h"
#include "WorldPacket.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace Acore::ChatCommands;

namespace HardcoreConfig
{
    static constexpr uint32 DEFAULT_TOKEN_ID = 11100;
    static constexpr uint32 DEFAULT_PVP_DEATH_FLAG_ITEM_ID = 34908;

    bool Enabled() { return sConfigMgr->GetOption<bool>("ModHardcore.Enable", false); }
    uint32 TokenItemId() { return sConfigMgr->GetOption<uint32>("ModHardcore.TokenItemId", DEFAULT_TOKEN_ID); }
    uint32 PvPDeathFlagItemId() { return sConfigMgr->GetOption<uint32>("ModHardcore.PvPDeathFlagItemId", DEFAULT_PVP_DEATH_FLAG_ITEM_ID); }
    bool AllowBots() { return sConfigMgr->GetOption<bool>("ModHardcore.AllowHardcorePlayerBots", false); }

    bool AllowPlayerRez() { return sConfigMgr->GetOption<bool>("ModHardcore.AllowPlayerRez", false); }
    bool AllowSpiritRez() { return sConfigMgr->GetOption<bool>("ModHardcore.AllowSpiritRez", false); }
    bool AllowGMRez() { return sConfigMgr->GetOption<bool>("ModHardcore.AllowGMRez", false); }

    // These are the NEW meanings of the original options. They no longer change
    // race/racials. They only control the validated DK skin/face application.
    bool MakeUndeadOnRez() { return sConfigMgr->GetOption<bool>("ModHardcore.MakeUndeadOnRez", false); }
    bool UseDeathKnightHair() { return sConfigMgr->GetOption<bool>("ModHardcore.UseDeathKnightHair", true); }

    bool DestroyEquipmentOnDeath() { return sConfigMgr->GetOption<bool>("ModHardcore.DestroyEquipmentOnDeath", true); }
    bool ResetHearthstoneOnDeath() { return sConfigMgr->GetOption<bool>("ModHardcore.ResetHearthstoneOnDeath", true); }
    bool DisableChatWhenDead() { return sConfigMgr->GetOption<bool>("ModHardcore.DisableChatWhenDead", false); }

    bool CountBattlegroundDeaths() { return sConfigMgr->GetOption<bool>("ModHardcore.CountBattlegroundDeaths", false); }
    bool CountArenaDeaths() { return sConfigMgr->GetOption<bool>("ModHardcore.CountArenaDeaths", false); }
    bool CountPvPDeaths() { return sConfigMgr->GetOption<bool>("ModHardcore.CountPvPDeaths", true); }
    bool EnableMakgora() { return sConfigMgr->GetOption<bool>("ModHardcore.EnableMakgora", false); }

    bool AnnounceDeaths() { return sConfigMgr->GetOption<bool>("ModHardcore.AnnounceDeaths", true); }
    uint32 DefaultPlayerDeathMessageLevel() { return std::min<uint32>(254, sConfigMgr->GetOption<uint32>("ModHardcore.DefaultPlayerDeathMessageLevel", 10)); }

    bool GiveFallenHCUndeadRacials() { return sConfigMgr->GetOption<bool>("ModHardcore.GiveFallenHCUndeadRacials", true); }
    int32 FallenHCUndeadRacialSpell1() { return sConfigMgr->GetOption<int32>("ModHardcore.FallenHCUndeadRacialSpell1", 20577); }
    int32 FallenHCUndeadRacialSpell2() { return sConfigMgr->GetOption<int32>("ModHardcore.FallenHCUndeadRacialSpell2", 5227); }
    bool GiveNativeUndeadFallenRacials() { return sConfigMgr->GetOption<bool>("ModHardcore.GiveNativeUndeadFallenRacials", true); }
    int32 FallenUndeadBonusSpell1() { return sConfigMgr->GetOption<int32>("ModHardcore.FallenUndeadBonusSpell1", 8359); }
    int32 FallenUndeadBonusSpell2() { return sConfigMgr->GetOption<int32>("ModHardcore.FallenUndeadBonusSpell2", 0); }

    bool RespawnWithRezItems() { return sConfigMgr->GetOption<bool>("ModHardcore.RespawnWithRezItems", true); }
    uint32 MinLvlGetRezItem() { return sConfigMgr->GetOption<uint32>("ModHardcore.MinLvlGetRezItem", 20); }
    std::string RespawnRandomEquip() { return sConfigMgr->GetOption<std::string>("ModHardcore.RespawnRandomEquip", "44694, 44693"); }
    std::string RespawnItemInv() { return sConfigMgr->GetOption<std::string>("ModHardcore.RespawnItemInv", "9332, 44646"); }

    bool TitleOnDeath() { return sConfigMgr->GetOption<bool>("ModHardcore.TitleOnDeath", true); }
    uint32 TitleOnDeathId() { return sConfigMgr->GetOption<uint32>("ModHardcore.TitleOnDeathId", 119); }
    bool AchievementOnDeath() { return sConfigMgr->GetOption<bool>("ModHardcore.AchievementOnDeath", true); }
    uint32 AchievementOnDeathId() { return sConfigMgr->GetOption<uint32>("ModHardcore.AchievementOnDeathId", 3456); }

    bool TeleportOnHardcoreDeath() { return sConfigMgr->GetOption<bool>("ModHardcore.TeleportOnHardcoreDeath", false); }
    uint32 DeathTeleportMap() { return sConfigMgr->GetOption<uint32>("ModHardcore.DeathTeleportMap", 609); }
    float DeathTeleportX() { return sConfigMgr->GetOption<float>("ModHardcore.DeathTeleportX", 2355.0f); }
    float DeathTeleportY() { return sConfigMgr->GetOption<float>("ModHardcore.DeathTeleportY", -5665.0f); }
    float DeathTeleportZ() { return sConfigMgr->GetOption<float>("ModHardcore.DeathTeleportZ", 426.0f); }
    float DeathTeleportO() { return sConfigMgr->GetOption<float>("ModHardcore.DeathTeleportO", 0.0f); }

    bool DebugHCDK() { return sConfigMgr->GetOption<bool>("ModHardcore.DebugHCDK", false); }
    bool EnableDkSkinSync() { return sConfigMgr->GetOption<bool>("ModHardcore.EnableDkSkinSync", true); }
    bool EnableDkSkinObjectCreateSync() { return sConfigMgr->GetOption<bool>("ModHardcore.EnableDkSkinObjectCreateSync", true); }
    uint32 ZoneSyncTime() { return sConfigMgr->GetOption<uint32>("ModHardcore.ZoneSyncTime", 250); }
    uint32 ZoneSyncTimeRetry() { return sConfigMgr->GetOption<uint32>("ModHardcore.ZoneSyncTimeRetry", 750); }
    uint32 LogonSyncTime() { return sConfigMgr->GetOption<uint32>("ModHardcore.LogonSyncTime", 250); }
    uint32 LogonSyncTimeRetry() { return sConfigMgr->GetOption<uint32>("ModHardcore.LogonSyncTimeRetry", 1500); }
    uint32 ObjectCreateSyncTime() { return sConfigMgr->GetOption<uint32>("ModHardcore.ObjectCreateSyncTime", 100); }

    int32 StartLevel() { return sConfigMgr->GetOption<int32>("ModHardcore.StartLevel", 1); }
    int32 EndLevel() { return sConfigMgr->GetOption<int32>("ModHardcore.EndLevel", 59); }
    bool AnnounceLevelUp() { return sConfigMgr->GetOption<bool>("ModHardcore.AnnounceLevelUp", true); }

    bool ChallengeRewardItemEnable() { return sConfigMgr->GetOption<bool>("ModHardcore.ChallengeRewardItemEnable", false); }
    uint32 ChallengeRewardItemId() { return sConfigMgr->GetOption<uint32>("ModHardcore.ChallengeRewardItemId", 0); }
    bool ChallengeRewardSpellEnable() { return sConfigMgr->GetOption<bool>("ModHardcore.ChallengeRewardSpellEnable", false); }
    uint32 ChallengeRewardSpellId() { return sConfigMgr->GetOption<uint32>("ModHardcore.ChallengeRewardSpellId", 0); }
    uint32 ChallengeRewardLevel() { return sConfigMgr->GetOption<uint32>("ModHardcore.ChallengeRewardLevel", 60); }
}

namespace HardcoreDkAppearance
{
    struct AppearanceBackup
    {
        uint8 Skin = 0;
        uint8 Face = 0;
        uint8 HairStyle = 0;
        uint8 HairColor = 0;
        uint8 FacialHair = 0;
    };

    struct AppearanceOptions
    {
        std::string RaceName;
        std::vector<uint8> Skins;
        std::vector<uint8> Faces;
        bool PreserveCurrentSkin = false;
    };

    struct HairOptions
    {
        std::vector<uint8> HairStyles;
        std::vector<uint8> HairColors;
        std::vector<uint8> FacialHairStyles;
    };

    static std::unordered_map<uint32, AppearanceBackup> Backups;

    static uint16 MakeKey(uint8 race, uint8 gender)
    {
        return (uint16(race) << 8) | uint16(gender);
    }

    static std::unordered_map<uint16, AppearanceOptions> const& GetAppearanceMap()
    {
        static const std::unordered_map<uint16, AppearanceOptions> map =
        {
            { MakeKey(RACE_HUMAN,    GENDER_MALE),   { "Human",     {12,13,14}, {0,2,11}, false } },
            { MakeKey(RACE_HUMAN,    GENDER_FEMALE), { "Human",     {12,13,14}, {0,1,10}, false } },
            { MakeKey(RACE_ORC,      GENDER_MALE),   { "Orc",       {15,16,17}, {0,1,5},  false } },
            { MakeKey(RACE_ORC,      GENDER_FEMALE), { "Orc",       {11,12,13}, {0,7,8},  false } },
            { MakeKey(RACE_DWARF,    GENDER_MALE),   { "Dwarf",     {19,20,21}, {0,1,9},  false } },
            { MakeKey(RACE_DWARF,    GENDER_FEMALE), { "Dwarf",     {11,12,13}, {0,5,6},  false } },
            { MakeKey(RACE_NIGHTELF, GENDER_MALE),   { "Night Elf", {9,10,11},  {0,1,4},  false } },
            { MakeKey(RACE_NIGHTELF, GENDER_FEMALE), { "Night Elf", {9,10,11},  {0,6,7},  false } },
            { MakeKey(RACE_UNDEAD_PLAYER,   GENDER_MALE),   { "Undead",    {}, {}, true } },
            { MakeKey(RACE_UNDEAD_PLAYER,   GENDER_FEMALE), { "Undead",    {}, {}, true } },
            { MakeKey(RACE_TAUREN,   GENDER_MALE),   { "Tauren",    {19,20,21}, {0,2,4},  false } },
            { MakeKey(RACE_TAUREN,   GENDER_FEMALE), { "Tauren",    {11,12,13}, {0,2,3},  false } },
            { MakeKey(RACE_GNOME,    GENDER_MALE),   { "Gnome",     {7,8,9},    {0,2,3},  false } },
            { MakeKey(RACE_GNOME,    GENDER_FEMALE), { "Gnome",     {7,8,9},    {0,3,5},  false } },
            { MakeKey(RACE_TROLL,    GENDER_MALE),   { "Troll",     {15,16,17}, {0,1,3},  false } },
            { MakeKey(RACE_TROLL,    GENDER_FEMALE), { "Troll",     {15,16,17}, {0,3,5},  false } },
            { MakeKey(RACE_BLOODELF, GENDER_MALE),   { "Blood Elf", {16,17,18}, {2,4,7},  false } },
            { MakeKey(RACE_BLOODELF, GENDER_FEMALE), { "Blood Elf", {16,17,18}, {0,3,8},  false } },
            { MakeKey(RACE_DRAENEI,  GENDER_MALE),   { "Draenei",   {14,15,16}, {1,4,9},  false } },
            { MakeKey(RACE_DRAENEI,  GENDER_FEMALE), { "Draenei",   {12,13,14}, {0,4,8},  false } }
        };
        return map;
    }

    AppearanceOptions const* GetOptions(uint8 race, uint8 gender)
    {
        auto const& map = GetAppearanceMap();
        auto itr = map.find(MakeKey(race, gender));
        return itr != map.end() ? &itr->second : nullptr;
    }

    // DK hair rows extracted from the server's WotLK 3.3.5a CharSections.dbc.
    // These are Flags=5, GenType=3 hair rows and GenType=2 facial-hair rows.
    // Tauren and native Forsaken do not have DK hair rows in this DBC and are
    // intentionally left unchanged by UseDeathKnightHair.
    static std::unordered_map<uint16, HairOptions> const& GetHairMap()
    {
        static const std::unordered_map<uint16, HairOptions> map =
        {
            { MakeKey(RACE_HUMAN,    GENDER_MALE),   { {0,1,2,3,4,5,6,7,8,9,10,11}, {10,11,12}, {0,1,2,3,4,5,6,7,8} } },
            { MakeKey(RACE_HUMAN,    GENDER_FEMALE), { {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18}, {10,11,12}, {} } },
            { MakeKey(RACE_ORC,      GENDER_MALE),   { {0,1,2,3,4,5,6}, {8,9,10}, {0,1,2,3,4,5,6,7,8,9,10} } },
            { MakeKey(RACE_ORC,      GENDER_FEMALE), { {0,1,2,3,4,5,6,7}, {8,9,10}, {} } },
            { MakeKey(RACE_DWARF,    GENDER_MALE),   { {0,1,2,3,4,5,6,7,8,9,10}, {10,11,12}, {0,1,2,3,4,5,6,7,8,9,10} } },
            { MakeKey(RACE_DWARF,    GENDER_FEMALE), { {0,1,2,3,4,5,6,7,8,9,10,11,12,13}, {10,11,12}, {} } },
            { MakeKey(RACE_NIGHTELF, GENDER_MALE),   { {0,1,2,3,4,5,6}, {10,11,12}, {0,1,2,3,4,5} } },
            { MakeKey(RACE_NIGHTELF, GENDER_FEMALE), { {0,1,2,3,4,5,6}, {10,11,12}, {0,1,2,3,4,5,6,7,8,9} } },
            { MakeKey(RACE_GNOME,    GENDER_MALE),   { {0,1,2,3,4,5,6}, {9,10,11}, {0,1,2,3,4,5,6,7} } },
            { MakeKey(RACE_GNOME,    GENDER_FEMALE), { {0,1,2,3,4,5,6}, {9,10,11}, {} } },
            { MakeKey(RACE_TROLL,    GENDER_MALE),   { {0,1,2,3,4,5}, {10,11,12}, {0,1,2,3,4,5,6,7,8,9,10} } },
            { MakeKey(RACE_TROLL,    GENDER_FEMALE), { {0,1,2,3,4}, {10,11,12}, {} } },
            { MakeKey(RACE_BLOODELF, GENDER_MALE),   { {0,1,2,3,4,5,6,7,8,9,10}, {10,11,12}, {0,1,2,3,4,5,6,7,8,9} } },
            { MakeKey(RACE_BLOODELF, GENDER_FEMALE), { {0,1,2,3,4,5,6,7,8,9,10,11,12,13}, {10,11,12}, {} } },
            { MakeKey(RACE_DRAENEI,  GENDER_MALE),   { {0,1,2,3,4,5,6,7,8,9}, {7,8,9}, {} } },
            { MakeKey(RACE_DRAENEI,  GENDER_FEMALE), { {0,1,2,3,4,5,6,7,8,9,10}, {7,8,9}, {} } }
        };
        return map;
    }

    HairOptions const* GetHairOptions(uint8 race, uint8 gender)
    {
        auto const& map = GetHairMap();
        auto itr = map.find(MakeKey(race, gender));
        return itr != map.end() ? &itr->second : nullptr;
    }

    bool Contains(std::vector<uint8> const& values, uint8 value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    uint32 ReplaceAppearanceByte(uint32 bytes, uint8 offset, uint8 value)
    {
        uint32 shift = uint32(offset) * 8u;
        bytes &= ~(0xFFu << shift);
        bytes |= uint32(value) << shift;
        return bytes;
    }

    uint8 RandomValue(std::vector<uint8> const& values)
    {
        if (values.empty())
            return 0;
        return values[urand(0, uint32(values.size() - 1))];
    }

    bool IsValidPair(uint8 race, uint8 gender, uint8 skin, uint8 face)
    {
        // Native Forsaken are intentionally excluded from automatic appearance
        // changes. They already use an undead body and are left untouched.
        if (race == RACE_UNDEAD_PLAYER)
            return false;

        AppearanceOptions const* options = GetOptions(race, gender);
        if (!options || options->Skins.empty() || options->Faces.empty())
            return false;

        return std::find(options->Skins.begin(), options->Skins.end(), skin) != options->Skins.end() &&
               std::find(options->Faces.begin(), options->Faces.end(), face) != options->Faces.end();
    }

    void SyncCorpseAppearanceToPlayer(Player* player)
    {
        if (!player)
            return;

        Corpse* corpse = player->GetCorpse();
        if (!corpse)
            return;

        uint8 skin = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID);
        uint8 face = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID);
        uint8 hairStyle = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID);
        uint8 hairColor = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID);
        uint8 facialHair = player->GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE);

        // Match AzerothCore Player::CreateCorpse() packing exactly so the dead
        // player and resurrectable corpse never advertise different customization.
        uint32 corpseBytes1 = uint32(player->getRace()) << 8;
        corpseBytes1 |= uint32(player->GetByteValue(PLAYER_BYTES_3, PLAYER_BYTES_3_OFFSET_GENDER)) << 16;
        corpseBytes1 |= uint32(skin) << 24;

        uint32 corpseBytes2 = uint32(face);
        corpseBytes2 |= uint32(hairStyle) << 8;
        corpseBytes2 |= uint32(hairColor) << 16;
        corpseBytes2 |= uint32(facialHair) << 24;

        bool changed = corpse->GetUInt32Value(CORPSE_FIELD_BYTES_1) != corpseBytes1 ||
                       corpse->GetUInt32Value(CORPSE_FIELD_BYTES_2) != corpseBytes2;
        if (!changed)
            return;

        corpse->SetUInt32Value(CORPSE_FIELD_BYTES_1, corpseBytes1);
        corpse->SetUInt32Value(CORPSE_FIELD_BYTES_2, corpseBytes2);

        // BG/arena corpses are intentionally not persisted by AzerothCore.
        if (player->GetMap() && !player->GetMap()->IsBattlegroundOrArena())
            corpse->SaveToDB();
    }

    bool EnsureDeathKnightAppearance(Player* player, bool syncCorpse = true)
    {
        if (!player || !player->GetSession() || player->GetSession()->IsBot() || player->getRace() == RACE_UNDEAD_PLAYER || player->getClass() == CLASS_DEATH_KNIGHT)
            return false;

        AppearanceOptions const* options = GetOptions(player->getRace(), player->getGender());
        if (!options || options->Skins.empty() || options->Faces.empty())
            return false;

        uint8 skin = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID);
        uint8 face = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID);
        uint8 hairStyle = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID);
        uint8 hairColor = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID);
        uint8 facialHair = player->GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE);

        // Once a valid DK skin+face has been selected, keep it stable forever.
        // Resurrection is an ensure/repair pass, not a second appearance reroll.
        if (!IsValidPair(player->getRace(), player->getGender(), skin, face))
        {
            skin = RandomValue(options->Skins);
            face = RandomValue(options->Faces);
        }

        if (HardcoreConfig::UseDeathKnightHair())
        {
            if (HairOptions const* hair = GetHairOptions(player->getRace(), player->getGender()))
            {
                bool currentHairIsDk = Contains(hair->HairStyles, hairStyle) && Contains(hair->HairColors, hairColor);
                if (!currentHairIsDk && !hair->HairStyles.empty() && !hair->HairColors.empty())
                {
                    hairStyle = RandomValue(hair->HairStyles);
                    hairColor = RandomValue(hair->HairColors);
                }

                if (!hair->FacialHairStyles.empty() && !Contains(hair->FacialHairStyles, facialHair))
                    facialHair = RandomValue(hair->FacialHairStyles);
            }
        }

        uint32 oldBytes = player->GetUInt32Value(PLAYER_BYTES);
        uint32 newBytes = oldBytes;
        newBytes = ReplaceAppearanceByte(newBytes, PLAYER_BYTES_OFFSET_SKIN_ID, skin);
        newBytes = ReplaceAppearanceByte(newBytes, PLAYER_BYTES_OFFSET_FACE_ID, face);
        newBytes = ReplaceAppearanceByte(newBytes, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID, hairStyle);
        newBytes = ReplaceAppearanceByte(newBytes, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID, hairColor);

        uint32 oldBytes2 = player->GetUInt32Value(PLAYER_BYTES_2);
        uint32 newBytes2 = oldBytes2;
        if (HardcoreConfig::UseDeathKnightHair())
            newBytes2 = ReplaceAppearanceByte(newBytes2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE, facialHair);

        bool changed = oldBytes != newBytes || oldBytes2 != newBytes2;
        if (oldBytes != newBytes)
            player->SetUInt32Value(PLAYER_BYTES, newBytes);
        if (oldBytes2 != newBytes2)
            player->SetUInt32Value(PLAYER_BYTES_2, newBytes2);

        // Persist the canonical fallen customization as soon as it is first
        // created on death, and again only if a later repair actually changes it.
        if (changed)
            player->SaveToDB(false, false);

        if (syncCorpse)
            SyncCorpseAppearanceToPlayer(player);

        return IsValidPair(player->getRace(), player->getGender(),
            player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID),
            player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID));
    }

    void LearnConfiguredSpell(Player* player, int32 spellId)
    {
        // 0 and -1 are both documented as disabled slots. Any other negative
        // value is also ignored rather than being converted to a huge uint32.
        if (player && spellId > 0)
            player->learnSpell(uint32(spellId), false);
    }

    void GrantFallenRacials(Player* player)
    {
        if (!player)
            return;

        if (player->getRace() == RACE_UNDEAD_PLAYER)
        {
            if (!HardcoreConfig::GiveNativeUndeadFallenRacials())
                return;

            LearnConfiguredSpell(player, HardcoreConfig::FallenUndeadBonusSpell1());
            LearnConfiguredSpell(player, HardcoreConfig::FallenUndeadBonusSpell2());
            return;
        }

        if (!HardcoreConfig::GiveFallenHCUndeadRacials())
            return;

        LearnConfiguredSpell(player, HardcoreConfig::FallenHCUndeadRacialSpell1());
        LearnConfiguredSpell(player, HardcoreConfig::FallenHCUndeadRacialSpell2());
    }

    Player* GetTarget(ChatHandler* handler)
    {
        if (!handler || !handler->GetSession())
            return nullptr;

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendErrorMessage("Select a player or use the command on yourself.");
            return nullptr;
        }

        if (handler->HasLowerSecurity(target))
            return nullptr;

        return target;
    }

    bool DebugEnabled(ChatHandler* handler)
    {
        if (HardcoreConfig::DebugHCDK())
            return true;

        handler->SendErrorMessage("Hardcore DK debug commands are disabled. Set ModHardcore.DebugHCDK = 1 to enable them.");
        return false;
    }

    void PrintAppearance(ChatHandler* handler, Player* target)
    {
        handler->PSendSysMessage(
            "HC DK appearance: {} | race={} class={} gender={} | skin={} face={} hairStyle={} hairColor={} facialHair={} | nativeDisplay={}",
            target->GetName(),
            uint32(target->getRace()),
            uint32(target->getClass()),
            uint32(target->getGender()),
            uint32(target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID)),
            uint32(target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID)),
            uint32(target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID)),
            uint32(target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID)),
            uint32(target->GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE)),
            target->GetNativeDisplayId());
    }

    void DumpAurasToConsole(ChatHandler* handler, Player* target)
    {
        std::set<uint32> auraSpellIds;
        for (auto const& auraPair : target->GetAppliedAuras())
            auraSpellIds.insert(auraPair.first);

        LOG_INFO("server.loading", "===== HC DK AURA DUMP: {} =====", target->GetName());
        LOG_INFO("server.loading", "race={} class={} gender={} display={} nativeDisplay={} uniqueAppliedAuras={}",
            uint32(target->getRace()), uint32(target->getClass()), uint32(target->getGender()),
            target->GetDisplayId(), target->GetNativeDisplayId(), auraSpellIds.size());

        for (uint32 spellId : auraSpellIds)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo)
            {
                LOG_INFO("server.loading", "[HC-DK-AURA] id={} | SpellInfo not found", spellId);
                continue;
            }

            char const* spellName = spellInfo->SpellName[0] ? spellInfo->SpellName[0] : "<unnamed>";
            LOG_INFO("server.loading",
                "[HC-DK-AURA] id={} name='{}' passive={} deathPersistent={} visual0={} visual1={} attrs={} attrsEx3={} | "
                "e0:aura={} misc={} miscB={} trigger={} | e1:aura={} misc={} miscB={} trigger={} | e2:aura={} misc={} miscB={} trigger={}",
                spellId, spellName, spellInfo->IsPassive() ? 1 : 0, spellInfo->IsDeathPersistent() ? 1 : 0,
                spellInfo->SpellVisual[0], spellInfo->SpellVisual[1], spellInfo->Attributes, spellInfo->AttributesEx3,
                uint32(spellInfo->Effects[EFFECT_0].ApplyAuraName), spellInfo->Effects[EFFECT_0].MiscValue,
                spellInfo->Effects[EFFECT_0].MiscValueB, spellInfo->Effects[EFFECT_0].TriggerSpell,
                uint32(spellInfo->Effects[EFFECT_1].ApplyAuraName), spellInfo->Effects[EFFECT_1].MiscValue,
                spellInfo->Effects[EFFECT_1].MiscValueB, spellInfo->Effects[EFFECT_1].TriggerSpell,
                uint32(spellInfo->Effects[EFFECT_2].ApplyAuraName), spellInfo->Effects[EFFECT_2].MiscValue,
                spellInfo->Effects[EFFECT_2].MiscValueB, spellInfo->Effects[EFFECT_2].TriggerSpell);
        }

        LOG_INFO("server.loading", "===== END HC DK AURA DUMP: {} =====", target->GetName());
        handler->PSendSysMessage("Dumped {} unique applied auras for {} to the worldserver console.", auraSpellIds.size(), target->GetName());
    }

    std::string NormalizeRaceName(std::string value)
    {
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c)
        {
            return std::isspace(c) || c == '_' || c == '-';
        }), value.end());
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    }

    bool MatchesRace(std::string const& normalized, std::string const& raceName)
    {
        std::string candidate = NormalizeRaceName(raceName);
        if (normalized == candidate)
            return true;
        return candidate == "undead" && normalized == "forsaken";
    }

    std::string ValuesToString(std::vector<uint8> const& values)
    {
        std::ostringstream out;
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i)
                out << ',';
            out << uint32(values[i]);
        }
        return out.str();
    }

    void ShowRace(ChatHandler* handler, std::string const& raceName)
    {
        for (uint8 gender : { uint8(GENDER_MALE), uint8(GENDER_FEMALE) })
        {
            AppearanceOptions const* found = nullptr;
            for (auto const& entry : GetAppearanceMap())
            {
                uint8 entryGender = uint8(entry.first & 0xFF);
                if (entryGender == gender && MatchesRace(NormalizeRaceName(raceName), entry.second.RaceName))
                {
                    found = &entry.second;
                    break;
                }
            }

            if (!found)
                continue;

            std::string genderName = gender == GENDER_MALE ? "Male" : "Female";
            if (found->RaceName == "Undead")
                handler->PSendSysMessage("{} {}   automatic fallen appearance: unchanged", found->RaceName, genderName);
            else if (found->PreserveCurrentSkin)
                handler->PSendSysMessage("{} {}   skins: keep current   faces: {}", found->RaceName, genderName, ValuesToString(found->Faces));
            else
                handler->PSendSysMessage("{} {}   skins: {}   faces: {}", found->RaceName, genderName, ValuesToString(found->Skins), ValuesToString(found->Faces));
        }
    }

    void ShowHairRace(ChatHandler* handler, std::string const& raceName)
    {
        std::string normalized = NormalizeRaceName(raceName);
        std::string canonicalName = raceName;

        for (auto const& appearance : GetAppearanceMap())
        {
            if (MatchesRace(normalized, appearance.second.RaceName))
            {
                canonicalName = appearance.second.RaceName;
                break;
            }
        }

        for (uint8 gender : { uint8(GENDER_MALE), uint8(GENDER_FEMALE) })
        {
            std::string genderName = gender == GENDER_MALE ? "Male" : "Female";

            uint8 raceId = 0;
            for (auto const& appearance : GetAppearanceMap())
            {
                uint8 entryRace = uint8(appearance.first >> 8);
                uint8 entryGender = uint8(appearance.first & 0xFF);
                if (entryGender == gender && MatchesRace(normalized, appearance.second.RaceName))
                {
                    raceId = entryRace;
                    canonicalName = appearance.second.RaceName;
                    break;
                }
            }

            HairOptions const* hair = raceId ? GetHairOptions(raceId, gender) : nullptr;
            if (!hair || hair->HairStyles.empty() || hair->HairColors.empty())
            {
                handler->PSendSysMessage("{} {}   DK hair rows: none (automatic hair is preserved)", canonicalName, genderName);
                continue;
            }

            std::string facial = hair->FacialHairStyles.empty() ? "preserve current" : ValuesToString(hair->FacialHairStyles);
            handler->PSendSysMessage("{} {}   hairStyles: {}   hairColors: {}   facialHair: {}",
                canonicalName, genderName, ValuesToString(hair->HairStyles), ValuesToString(hair->HairColors), facial);
        }
    }
}


namespace HardcoreDkSkinSync
{
    // Fallen appearance synchronization is event-driven and intentionally
    // observer-specific. Observer repair is scheduled from later PlayerScript
    // lifecycle events (login/zone/area updates) instead of the early map-enter
    // callback. Each observer event gets a fast opportunistic pulse plus the
    // proven delayed fallback, because some busy world-map transfers continue
    // rebuilding visibility after the first pulse. Fallen subjects still use a
    // single delayed reverse pass when they enter after observers are present.
    // A UnitScript create-values hook additionally catches the exact moment a
    // fallen player is constructed for an observer who encounters them later.
    // Playerbots never schedule any side of this work.
    static constexpr uint32 SUBJECT_SYNC_DELAY_MS = 500;

    struct ObserverSyncTimers
    {
        uint32 EarlyMs = 0;
        uint32 LateMs = 0;
        bool EarlyPending = false;
        bool LatePending = false;
    };

    struct ObjectCreateSyncTimer
    {
        ObjectGuid SubjectGuid;
        uint32 DelayMs = 0;
    };

    static std::unordered_map<uint32, ObserverSyncTimers> PendingObservers;
    static std::unordered_map<uint32, uint32> PendingSubjects;
    static std::unordered_map<uint32, std::vector<ObjectCreateSyncTimer>> PendingObjectCreateSyncs;

    bool IsRealObserver(Player* player)
    {
        return player && player->GetSession() && !player->GetSession()->IsBot();
    }

    bool IsFallenAppearance(Player* player)
    {
        if (!player || !player->IsInWorld() || !player->GetSession())
            return false;
        if (player->getRace() == RACE_UNDEAD_PLAYER || player->getClass() == CLASS_DEATH_KNIGHT)
            return false;

        uint8 skin = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID);
        uint8 face = player->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID);
        return HardcoreDkAppearance::IsValidPair(player->getRace(), player->getGender(), skin, face);
    }

    uint8 GetAlternateDkSkin(Player* player, uint8 actualSkin)
    {
        if (!player)
            return actualSkin;

        HardcoreDkAppearance::AppearanceOptions const* options = HardcoreDkAppearance::GetOptions(player->getRace(), player->getGender());
        if (!options)
            return actualSkin;

        for (uint8 skin : options->Skins)
            if (skin != actualSkin)
                return skin;

        return actualSkin;
    }

    bool GetAlternateDkHairPair(Player* player, uint8 actualStyle, uint8 actualColor, uint8& alternateStyle, uint8& alternateColor)
    {
        alternateStyle = actualStyle;
        alternateColor = actualColor;

        if (!player || !HardcoreConfig::UseDeathKnightHair())
            return false;

        HardcoreDkAppearance::HairOptions const* hair = HardcoreDkAppearance::GetHairOptions(player->getRace(), player->getGender());
        if (!hair || hair->HairStyles.empty() || hair->HairColors.empty())
            return false;

        // CharSections.dbc for this server has a row for every listed DK
        // hairStyle x hairColor combination. Prefer changing only hair color so
        // the synthetic observer pulse forces a hair-texture rebuild without
        // visibly changing the hairstyle itself.
        if (HardcoreDkAppearance::Contains(hair->HairStyles, actualStyle))
        {
            for (uint8 color : hair->HairColors)
            {
                if (color != actualColor)
                {
                    alternateStyle = actualStyle;
                    alternateColor = color;
                    return true;
                }
            }
        }

        // Fallback for unusual/custom DBC data: choose any other valid pair.
        for (uint8 style : hair->HairStyles)
        {
            for (uint8 color : hair->HairColors)
            {
                if (style != actualStyle || color != actualColor)
                {
                    alternateStyle = style;
                    alternateColor = color;
                    return true;
                }
            }
        }

        return false;
    }

    void SendAppearanceBytesToObserver(Player* observer, Player* subject, uint32 playerBytes, uint32 playerBytes2)
    {
        if (!IsRealObserver(observer) || !subject || !observer->HaveAtClient(subject))
            return;

        UpdateMask mask;
        mask.SetCount(PLAYER_END);
        mask.SetBit(PLAYER_BYTES);
        mask.SetBit(PLAYER_BYTES_2);

        ByteBuffer block(72);
        block << uint8(UPDATETYPE_VALUES);
        block << subject->GetPackGUID();
        block << uint8(mask.GetBlockCount());
        mask.AppendToPacket(&block);
        block << playerBytes;
        block << playerBytes2;

        UpdateData data;
        data.AddUpdateBlock(block);
        WorldPacket packet;
        data.BuildPacket(packet);
        observer->SendDirectMessage(&packet);
    }

    void PulseFallenAppearanceForObserver(Player* observer, Player* subject)
    {
        if (!IsRealObserver(observer) || !IsFallenAppearance(subject) || observer == subject || !observer->HaveAtClient(subject))
            return;

        uint8 actualSkin = subject->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID);
        uint8 alternateSkin = GetAlternateDkSkin(subject, actualSkin);
        if (alternateSkin == actualSkin)
            return;

        uint8 actualHairStyle = subject->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID);
        uint8 actualHairColor = subject->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID);
        uint8 alternateHairStyle = actualHairStyle;
        uint8 alternateHairColor = actualHairColor;
        GetAlternateDkHairPair(subject, actualHairStyle, actualHairColor, alternateHairStyle, alternateHairColor);

        uint32 actualBytes = subject->GetUInt32Value(PLAYER_BYTES);
        uint32 actualBytes2 = subject->GetUInt32Value(PLAYER_BYTES_2);
        uint32 temporaryBytes = actualBytes;
        temporaryBytes = HardcoreDkAppearance::ReplaceAppearanceByte(temporaryBytes, PLAYER_BYTES_OFFSET_SKIN_ID, alternateSkin);
        temporaryBytes = HardcoreDkAppearance::ReplaceAppearanceByte(temporaryBytes, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID, alternateHairStyle);
        temporaryBytes = HardcoreDkAppearance::ReplaceAppearanceByte(temporaryBytes, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID, alternateHairColor);

        // Observer-only synthetic appearance update. Both PLAYER_BYTES fields are
        // sent together so skin, hair color/style and facial-hair rendering are
        // rebuilt as one customization state. The subject's real server/DB
        // values never change.
        SendAppearanceBytesToObserver(observer, subject, temporaryBytes, actualBytes2);
        SendAppearanceBytesToObserver(observer, subject, actualBytes, actualBytes2);
    }

    void SyncObserver(Player* observer)
    {
        if (!HardcoreConfig::Enabled() || !HardcoreConfig::EnableDkSkinSync() || !IsRealObserver(observer) || !observer->IsInWorld() || !observer->GetMap())
            return;

        Map::PlayerList const& players = observer->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* subject = itr->GetSource();
            if (!subject || subject == observer || !IsFallenAppearance(subject) || !observer->HaveAtClient(subject))
                continue;

            PulseFallenAppearanceForObserver(observer, subject);
        }
    }

    void SyncSubjectToObservers(Player* subject)
    {
        if (!HardcoreConfig::Enabled() || !HardcoreConfig::EnableDkSkinSync() || !IsRealObserver(subject) || !IsFallenAppearance(subject) || !subject->GetMap())
            return;

        Map::PlayerList const& players = subject->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* observer = itr->GetSource();
            if (!observer || observer == subject || !IsRealObserver(observer) || !observer->HaveAtClient(subject))
                continue;
            PulseFallenAppearanceForObserver(observer, subject);
        }
    }

    void ScheduleObserver(Player* observer, uint32 earlyDelayMs, uint32 lateDelayMs)
    {
        if (!HardcoreConfig::Enabled() || !HardcoreConfig::EnableDkSkinSync() || !IsRealObserver(observer))
            return;

        // Run an early opportunistic repair, then keep the delayed repair as a
        // fallback. Both timings are config-driven so they can be tuned without
        // rebuilding the module. Re-scheduling from paired zone/area hooks
        // restarts both timers from the latest lifecycle signal.
        ObserverSyncTimers& timers = PendingObservers[observer->GetGUID().GetCounter()];
        timers.EarlyMs = earlyDelayMs;
        timers.LateMs = lateDelayMs;
        timers.EarlyPending = true;
        timers.LatePending = true;
    }

    void ScheduleZoneObserver(Player* observer)
    {
        ScheduleObserver(observer, HardcoreConfig::ZoneSyncTime(), HardcoreConfig::ZoneSyncTimeRetry());
    }

    void ScheduleLoginObserver(Player* observer)
    {
        ScheduleObserver(observer, HardcoreConfig::LogonSyncTime(), HardcoreConfig::LogonSyncTimeRetry());
    }

    void ScheduleSubject(Player* subject)
    {
        // Only real, already-fallen-looking players need the reverse sync pass.
        // Normal players and playerbots do not schedule subject-side work.
        if (!HardcoreConfig::Enabled() || !HardcoreConfig::EnableDkSkinSync() || !IsRealObserver(subject) || !IsFallenAppearance(subject))
            return;
        PendingSubjects[subject->GetGUID().GetCounter()] = SUBJECT_SYNC_DELAY_MS;
    }

    void ScheduleObjectCreateSync(Player* observer, Player const* subject)
    {
        if (!HardcoreConfig::Enabled() || !HardcoreConfig::EnableDkSkinObjectCreateSync() || !IsRealObserver(observer) || !subject || observer == subject)
            return;

        Player* mutableSubject = const_cast<Player*>(subject);
        if (!IsFallenAppearance(mutableSubject))
            return;

        uint32 observerGuid = observer->GetGUID().GetCounter();
        ObjectGuid subjectGuid = subject->GetGUID();
        uint32 delay = HardcoreConfig::ObjectCreateSyncTime();
        std::vector<ObjectCreateSyncTimer>& timers = PendingObjectCreateSyncs[observerGuid];

        // A cached create packet can be built more than once while visibility is
        // settling. Keep only one pending repair for this exact observer/subject
        // pair and restart its short delay from the latest create event.
        for (ObjectCreateSyncTimer& timer : timers)
        {
            if (timer.SubjectGuid == subjectGuid)
            {
                timer.DelayMs = delay;
                return;
            }
        }

        timers.push_back({ subjectGuid, delay });
    }

    void Cancel(Player* player)
    {
        if (!player)
            return;

        uint32 guid = player->GetGUID().GetCounter();
        PendingObservers.erase(guid);
        PendingSubjects.erase(guid);
        PendingObjectCreateSyncs.erase(guid);
    }

    void Update(Player* player, uint32 diff)
    {
        if (!player)
            return;

        uint32 guid = player->GetGUID().GetCounter();

        if (!HardcoreConfig::Enabled() || !IsRealObserver(player))
        {
            PendingObservers.erase(guid);
            PendingSubjects.erase(guid);
            PendingObjectCreateSyncs.erase(guid);
            return;
        }

        if (!HardcoreConfig::EnableDkSkinSync())
        {
            PendingObservers.erase(guid);
            PendingSubjects.erase(guid);
        }

        if (!HardcoreConfig::EnableDkSkinObjectCreateSync())
            PendingObjectCreateSyncs.erase(guid);

        // Do not count stabilization timers while the player is not yet fully
        // in-world or is in the middle of another far transfer.
        if (!player->IsInWorld() || player->IsBeingTeleportedFar())
            return;

        if (HardcoreConfig::EnableDkSkinSync())
        {
            if (auto itr = PendingObservers.find(guid); itr != PendingObservers.end())
            {
                ObserverSyncTimers& timers = itr->second;
                bool runEarlySync = false;
                bool runLateSync = false;

                if (timers.EarlyPending)
                {
                    if (timers.EarlyMs > diff)
                        timers.EarlyMs -= diff;
                    else
                    {
                        timers.EarlyMs = 0;
                        timers.EarlyPending = false;
                        runEarlySync = true;
                    }
                }

                if (timers.LatePending)
                {
                    if (timers.LateMs > diff)
                        timers.LateMs -= diff;
                    else
                    {
                        timers.LateMs = 0;
                        timers.LatePending = false;
                        runLateSync = true;
                    }
                }

                if (!timers.EarlyPending && !timers.LatePending)
                    PendingObservers.erase(itr);

                if (runEarlySync)
                    SyncObserver(player);
                if (runLateSync)
                    SyncObserver(player);
            }

            if (auto itr = PendingSubjects.find(guid); itr != PendingSubjects.end())
            {
                if (!IsFallenAppearance(player))
                {
                    PendingSubjects.erase(itr);
                }
                else if (itr->second > diff)
                {
                    itr->second -= diff;
                }
                else
                {
                    PendingSubjects.erase(itr);
                    SyncSubjectToObservers(player);
                }
            }
        }

        if (HardcoreConfig::EnableDkSkinObjectCreateSync())
        {
            if (auto itr = PendingObjectCreateSyncs.find(guid); itr != PendingObjectCreateSyncs.end())
            {
                std::vector<ObjectCreateSyncTimer>& timers = itr->second;
                for (auto timerItr = timers.begin(); timerItr != timers.end();)
                {
                    if (timerItr->DelayMs > diff)
                    {
                        timerItr->DelayMs -= diff;
                        ++timerItr;
                        continue;
                    }

                    // This is not a visibility scan. The pair was queued by the
                    // exact create-object values packet for this observer. Resolve
                    // only that one subject and perform the proven pulse once the
                    // create packet has had a moment to reach the client.
                    Player* subject = ObjectAccessor::GetPlayer(*player, timerItr->SubjectGuid);
                    if (subject)
                        PulseFallenAppearanceForObserver(player, subject);

                    timerItr = timers.erase(timerItr);
                }

                if (timers.empty())
                    PendingObjectCreateSyncs.erase(itr);
            }
        }
    }
}

namespace HardcoreState
{
    enum class DeathCategory : uint8
    {
        UNKNOWN = 0,
        PVE = 1,
        ENVIRONMENTAL = 2,
        WORLD_PVP = 3,
        BATTLEGROUND = 4,
        ARENA = 5,
        MAKGORA = 6
    };

    enum class ResurrectionMethod : uint8
    {
        PLAYER = 0,
        SPIRIT_HEALER = 1,
        GM = 2
    };

    struct PendingDeath
    {
        DeathCategory Category = DeathCategory::ENVIRONMENTAL;
        std::string Cause = "Died from environmental or other damage.";
    };

    struct PendingResurrectionFinalize
    {
        uint32 StableOnDestinationMs = 0;
    };

    struct ApprovedResurrectionInfo
    {
        ResurrectionMethod Method = ResurrectionMethod::PLAYER;
        bool AppearanceAppliedBeforeResurrection = false;
    };

    static std::unordered_map<uint32, PendingDeath> PendingDeaths;
    static std::unordered_map<uint32, PendingDeath> RecentDeathCauses;
    static std::unordered_map<uint32, PendingDeath> ForcedDeaths;
    static std::set<uint32> SuppressedDeaths;
    static std::set<uint32> QualifyingDeaths;
    static std::set<uint32> IgnoredDeaths;
    static std::unordered_map<uint32, ApprovedResurrectionInfo> ApprovedResurrections;
    static std::unordered_map<uint32, PendingResurrectionFinalize> PendingResurrectionFinalizes;
    static std::unordered_map<uint32, uint32> DeathMessageMinLevels;

    bool BaseEligible(Player* player)
    {
        if (!player || !player->GetSession() || player->IsGameMaster() || !HardcoreConfig::Enabled())
            return false;
        if (!HardcoreConfig::AllowBots() && player->GetSession()->IsBot())
            return false;
        return true;
    }

    bool InConfiguredLevelRange(uint8 level)
    {
        return int32(level) >= HardcoreConfig::StartLevel() && int32(level) <= HardcoreConfig::EndLevel();
    }

    bool HasToken(Player* player)
    {
        return player && player->HasItemCount(HardcoreConfig::TokenItemId(), 1, true);
    }

    bool ValidPvPDeathFlagItem()
    {
        uint32 flagItem = HardcoreConfig::PvPDeathFlagItemId();
        return flagItem != 0 && flagItem != HardcoreConfig::TokenItemId();
    }

    bool HasPvPDeathFlag(Player* player)
    {
        return player && ValidPvPDeathFlagItem() && player->HasItemCount(HardcoreConfig::PvPDeathFlagItemId(), 1, true);
    }

    void ClearPvPDeathFlag(Player* player)
    {
        if (!player || !ValidPvPDeathFlagItem())
            return;

        uint32 flagItem = HardcoreConfig::PvPDeathFlagItemId();
        uint32 count = player->GetItemCount(flagItem, true);
        if (!count)
            return;

        player->DestroyItemCount(flagItem, count, true, true);
        player->SaveToDB(false, false);
    }

    bool IsPvPCategory(DeathCategory category)
    {
        return category == DeathCategory::WORLD_PVP || category == DeathCategory::BATTLEGROUND || category == DeathCategory::ARENA;
    }

    void GrantPvPDeathFlag(Player* player, DeathCategory category)
    {
        // The marker is only for a Hardcore player whose PvP/BG/Arena death is
        // explicitly configured NOT to count. It freezes that exclusion across
        // logout/server restart without adding a custom database table.
        if (!player || !HasToken(player) || !IsPvPCategory(category) || !ValidPvPDeathFlagItem())
            return;

        uint32 flagItem = HardcoreConfig::PvPDeathFlagItemId();
        if (!player->HasItemCount(flagItem, 1, true))
        {
            if (!player->AddItem(flagItem, 1))
            {
                LOG_ERROR("mod-hardcore", "Failed to grant PvP death flag item {} to {}. Ignored-death persistence will not survive a restart.", flagItem, player->GetName());
                return;
            }
            player->SaveToDB(false, false);
        }
    }

    bool IsActive(Player* player, bool requireLevelRange = true)
    {
        if (!BaseEligible(player) || !HasToken(player))
            return false;
        return !requireLevelRange || InConfiguredLevelRange(player->GetLevel());
    }

    bool IsCurrentDeathIgnored(Player* player)
    {
        if (!player)
            return true;

        if (HasPvPDeathFlag(player))
            return true;

        uint32 guid = player->GetGUID().GetCounter();
        if (IgnoredDeaths.find(guid) != IgnoredDeaths.end())
            return true;
        if (player->InArena() && !HardcoreConfig::CountArenaDeaths())
            return true;
        if (player->InBattleground() && !HardcoreConfig::CountBattlegroundDeaths())
            return true;
        return false;
    }

    bool NeedsResurrectionRules(Player* player)
    {
        if (!player || !player->isDead() || !IsActive(player))
            return false;
        return !IsCurrentDeathIgnored(player);
    }

    uint32 GetDeathMessageMinLevel(Player* player)
    {
        if (!player)
            return HardcoreConfig::DefaultPlayerDeathMessageLevel();
        auto itr = DeathMessageMinLevels.find(player->GetGUID().GetCounter());
        return itr != DeathMessageMinLevels.end() ? itr->second : HardcoreConfig::DefaultPlayerDeathMessageLevel();
    }

    void SetDeathMessageMinLevel(Player* player, uint32 level)
    {
        if (player)
            DeathMessageMinLevels[player->GetGUID().GetCounter()] = std::min<uint32>(255, level);
    }

    void ResetDeathMessageMinLevel(Player* player)
    {
        if (player)
            DeathMessageMinLevels.erase(player->GetGUID().GetCounter());
    }

    std::string FormatDeathAnnouncement(Player* player, std::string cause)
    {
        if (!player)
            return {};

        if (cause.rfind("Slain by ", 0) == 0)
        {
            cause[0] = 's';
            return Acore::StringFormat("[Hardcore] {} has been {}", player->GetName(), cause);
        }
        if (cause == "Died in an arena.")
            return Acore::StringFormat("[Hardcore] {} has died in an arena.", player->GetName());
        if (cause == "Died in a battleground.")
            return Acore::StringFormat("[Hardcore] {} has died in a battleground.", player->GetName());
        if (cause == "Died in PvP combat.")
            return Acore::StringFormat("[Hardcore] {} has died in PvP combat.", player->GetName());
        if (cause.rfind("Slain in Mak'gora by ", 0) == 0)
        {
            std::string opponent = cause.substr(std::string("Slain in Mak'gora by ").size());
            return Acore::StringFormat("[Hardcore] {} has fallen in Mak'gora to {}", player->GetName(), opponent);
        }
        return Acore::StringFormat("[Hardcore] {} has died. {}", player->GetName(), cause);
    }

    void BroadcastDeath(Player* player, std::string const& cause)
    {
        if (!player || !HardcoreConfig::AnnounceDeaths())
            return;

        std::string message = FormatDeathAnnouncement(player, cause);
        uint32 victimLevel = player->GetLevel();
        ChatHandler(nullptr).DoForAllValidSessions([&](Player* online)
        {
            if (online && online->GetSession() && victimLevel >= GetDeathMessageMinLevel(online))
                ChatHandler(online->GetSession()).SendWorldTextOptional(message, ANNOUNCER_FLAG_DISABLE_AUTOBROADCAST);
        });
    }

    void SendWorldAnnouncement(std::string const& message)
    {
        ChatHandler(nullptr).DoForAllValidSessions([&](Player* online)
        {
            if (online && online->GetSession() && !message.empty())
                ChatHandler(online->GetSession()).SendWorldTextOptional(message, ANNOUNCER_FLAG_DISABLE_AUTOBROADCAST);
        });
    }

    bool IsAtDeathMemorial(Player* player)
    {
        if (!player || player->GetMapId() != HardcoreConfig::DeathTeleportMap())
            return false;

        return player->IsWithinDist3d(
            HardcoreConfig::DeathTeleportX(),
            HardcoreConfig::DeathTeleportY(),
            HardcoreConfig::DeathTeleportZ(),
            10.0f);
    }

    void TeleportToDeathMemorial(Player* player)
    {
        if (!player || !HardcoreConfig::TeleportOnHardcoreDeath())
            return;
        player->TeleportTo(HardcoreConfig::DeathTeleportMap(), HardcoreConfig::DeathTeleportX(), HardcoreConfig::DeathTeleportY(), HardcoreConfig::DeathTeleportZ(), HardcoreConfig::DeathTeleportO());
    }

    DeathCategory MapCategory(Player* player)
    {
        if (player->InArena())
            return DeathCategory::ARENA;
        if (player->InBattleground())
            return DeathCategory::BATTLEGROUND;
        return DeathCategory::ENVIRONMENTAL;
    }

    bool CountsAsHardcoreDeath(DeathCategory category)
    {
        switch (category)
        {
            case DeathCategory::BATTLEGROUND: return HardcoreConfig::CountBattlegroundDeaths();
            case DeathCategory::ARENA:        return HardcoreConfig::CountArenaDeaths();
            case DeathCategory::WORLD_PVP:    return HardcoreConfig::CountPvPDeaths();
            default:                          return true;
        }
    }

    void SuppressNextDeath(Player* player)
    {
        if (player)
            SuppressedDeaths.insert(player->GetGUID().GetCounter());
    }

    void ForceNextDeath(Player* player, DeathCategory category, std::string cause)
    {
        if (player)
            ForcedDeaths[player->GetGUID().GetCounter()] = { category, std::move(cause) };
    }

    PendingDeath* GetPending(Player* player)
    {
        if (!player)
            return nullptr;
        auto itr = PendingDeaths.find(player->GetGUID().GetCounter());
        return itr != PendingDeaths.end() ? &itr->second : nullptr;
    }

    void QueueDeath(Player* player)
    {
        if (!player)
            return;

        uint32 guid = player->GetGUID().GetCounter();
        ApprovedResurrections.erase(guid);

        // A PvP flag belongs to exactly one previous death/resurrection cycle.
        // If one somehow survived while the player was alive, never let it
        // exempt this new death; an ignored PvP death will grant a fresh flag.
        if (HasPvPDeathFlag(player))
            ClearPvPDeathFlag(player);

        if (SuppressedDeaths.erase(guid))
        {
            RecentDeathCauses.erase(guid);
            ForcedDeaths.erase(guid);
            return;
        }

        // Master gate: without the Hardcore Key this is an ordinary death.
        if (!IsActive(player))
        {
            RecentDeathCauses.erase(guid);
            ForcedDeaths.erase(guid);
            QualifyingDeaths.erase(guid);
            IgnoredDeaths.erase(guid);
            return;
        }

        auto forced = ForcedDeaths.find(guid);
        if (forced != ForcedDeaths.end())
        {
            PendingDeaths[guid] = forced->second;
            ForcedDeaths.erase(forced);
            RecentDeathCauses.erase(guid);
            return;
        }

        auto classified = RecentDeathCauses.find(guid);
        if (classified != RecentDeathCauses.end())
        {
            PendingDeaths[guid] = classified->second;
            RecentDeathCauses.erase(classified);
            return;
        }

        PendingDeath pending;
        pending.Category = MapCategory(player);
        if (pending.Category == DeathCategory::ARENA)
            pending.Cause = "Died in an arena.";
        else if (pending.Category == DeathCategory::BATTLEGROUND)
            pending.Cause = "Died in a battleground.";
        PendingDeaths[guid] = std::move(pending);
    }

    void ResetHearthstoneCooldown(Player* player)
    {
        if (!player || !HardcoreConfig::ResetHearthstoneOnDeath())
            return;

        // Standard Hearthstone and the WotLK alternate hearth spell used by
        // hearth-style items. Remove the server-side cooldown and explicitly
        // clear the client display so the Hearthstone is ready after revival.
        static constexpr std::array<uint32, 2> HearthSpells = { 8690, 39937 };
        for (uint32 spellId : HearthSpells)
        {
            player->RemoveSpellCooldown(spellId, true);
            player->SendClearCooldown(spellId, player);
        }
    }

    void DestroyEquipmentAndMoney(Player* player)
    {
        if (!player || !player->GetSession() || player->GetSession()->IsBot())
            return;

        for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (item->GetTemplate())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffDA70D6You have lost your |cffffffff|Hitem:{}:0:0:0:0:0:0:0:0|h[{}]|h|r",
                        item->GetEntry(), item->GetTemplate()->Name1);
                player->DestroyItem(INVENTORY_SLOT_BAG_0, item->GetSlot(), true);
            }
        }
        player->SetMoney(0);
    }

    void ProcessPendingDeath(Player* player)
    {
        if (!player)
            return;

        uint32 guid = player->GetGUID().GetCounter();
        auto itr = PendingDeaths.find(guid);
        if (itr == PendingDeaths.end())
            return;

        PendingDeath pending = itr->second;
        PendingDeaths.erase(itr);

        // Re-check the Key at processing time. A forfeited/non-HC character can
        // never leak into HC penalties or announcements.
        if (!IsActive(player))
            return;

        if (!CountsAsHardcoreDeath(pending.Category))
        {
            IgnoredDeaths.insert(guid);
            QualifyingDeaths.erase(guid);
            GrantPvPDeathFlag(player, pending.Category);
            return;
        }

        IgnoredDeaths.erase(guid);
        QualifyingDeaths.insert(guid);

        // MakeUndeadOnRez now establishes the visual fallen state at the
        // qualifying death itself. If release happens later, AzerothCore's
        // CreateCorpse() naturally copies these already-DK PLAYER_BYTES. If a
        // corpse already exists because death classification finished after
        // release, the helper repairs that corpse to match the player.
        if (HardcoreConfig::MakeUndeadOnRez())
            HardcoreDkAppearance::EnsureDeathKnightAppearance(player, true);

        // Only a death that passed the Hardcore Key + PvP/BG/Arena filters
        // reaches this point, so excluded PvP deaths never reset Hearthstone.
        ResetHearthstoneCooldown(player);

        if (HardcoreConfig::DestroyEquipmentOnDeath())
            DestroyEquipmentAndMoney(player);

        BroadcastDeath(player, pending.Cause);

        // Memorial behavior is intentionally one-shot:
        // - If BOTH normal player resurrection and Spirit Healer resurrection
        //   are disabled, memorialize the dead keyed character immediately.
        // - Otherwise wait until a permitted resurrection succeeds.
        if (!HardcoreConfig::AllowPlayerRez() && !HardcoreConfig::AllowSpiritRez())
            TeleportToDeathMemorial(player);

        if (player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("You have died in Hardcore mode.");

        if (HardcoreConfig::AllowBots() && player->GetSession() && player->GetSession()->IsBot())
            player->GetSession()->LogoutPlayer(true);
    }

    void UpdatePendingCauseFromPvP(Player* killed, Player* killer)
    {
        if (!killed || !IsActive(killed))
            return;

        uint32 guid = killed->GetGUID().GetCounter();
        if (ForcedDeaths.find(guid) != ForcedDeaths.end())
            return;

        PendingDeath details;
        if (killed->InArena())
            details.Category = DeathCategory::ARENA;
        else if (killed->InBattleground())
            details.Category = DeathCategory::BATTLEGROUND;
        else
            details.Category = DeathCategory::WORLD_PVP;

        if (killer && killer != killed)
            details.Cause = Acore::StringFormat("Slain by {}.", killer->GetName());
        else
            details.Cause = "Died in PvP combat.";

        if (PendingDeath* pending = GetPending(killed))
        {
            if (pending->Category != DeathCategory::MAKGORA)
                *pending = details;
        }
        else
            RecentDeathCauses[guid] = std::move(details);
    }

    void UpdatePendingCauseFromCreature(Player* killed, Creature* killer)
    {
        if (!killed || !IsActive(killed))
            return;

        uint32 guid = killed->GetGUID().GetCounter();
        if (ForcedDeaths.find(guid) != ForcedDeaths.end())
            return;

        PendingDeath details;
        Player* owner = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (killed->InArena())
            details.Category = DeathCategory::ARENA;
        else if (killed->InBattleground())
            details.Category = DeathCategory::BATTLEGROUND;
        else if (owner && owner != killed)
            details.Category = DeathCategory::WORLD_PVP;
        else
            details.Category = DeathCategory::PVE;

        if (owner && owner != killed)
            details.Cause = Acore::StringFormat("Slain by {}.", owner->GetName());
        else if (killer)
            details.Cause = Acore::StringFormat("Slain by {}.", killer->GetName());
        else
            details.Cause = "Slain by a creature.";

        if (PendingDeath* pending = GetPending(killed))
        {
            if (pending->Category != DeathCategory::MAKGORA)
                *pending = details;
        }
        else
            RecentDeathCauses[guid] = std::move(details);
    }

    void ApproveResurrection(Player* player, ResurrectionMethod method)
    {
        if (!player)
            return;

        uint32 guid = player->GetGUID().GetCounter();
        auto existing = ApprovedResurrections.find(guid);
        if (existing != ApprovedResurrections.end())
        {
            // Do not rerun an already-approved resurrection path if duplicate
            // acceptance packets arrive before the resurrection completes.
            existing->second.Method = method;
            return;
        }

        bool appearanceApplied = false;

        // Death is the primary appearance application point. This accepted-res
        // path is deliberately an idempotent validation/repair pass: keep the
        // same valid DK customization, repair only missing/invalid pieces, and
        // make any existing corpse advertise the exact same customization.
        if (HardcoreConfig::MakeUndeadOnRez())
            appearanceApplied = HardcoreDkAppearance::EnsureDeathKnightAppearance(player, true);

        ApprovedResurrections[guid] = ApprovedResurrectionInfo{ method, appearanceApplied };
    }

    bool PopApprovedResurrection(Player* player, ResurrectionMethod& method, bool& appearanceAppliedBeforeResurrection)
    {
        if (!player)
            return false;
        uint32 guid = player->GetGUID().GetCounter();
        auto itr = ApprovedResurrections.find(guid);
        if (itr == ApprovedResurrections.end())
            return false;
        method = itr->second.Method;
        appearanceAppliedBeforeResurrection = itr->second.AppearanceAppliedBeforeResurrection;
        ApprovedResurrections.erase(itr);
        return true;
    }

    std::vector<uint32> ParseItemIdList(std::string const& value)
    {
        std::vector<uint32> items;
        std::stringstream stream(value);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            size_t last = token.find_last_not_of(" \t\r\n");
            if (last == std::string::npos)
                continue;
            token.erase(last + 1);

            char* end = nullptr;
            unsigned long parsed = std::strtoul(token.c_str(), &end, 10);
            if (end == token.c_str() || *end != '\0' || parsed == 0 || parsed > 0xFFFFFFFFul)
            {
                LOG_ERROR("mod-hardcore", "Ignoring invalid resurrection item id '{}' in config list.", token);
                continue;
            }
            items.push_back(uint32(parsed));
        }
        return items;
    }

    void GrantResurrectionItems(Player* player)
    {
        if (!player || !HardcoreConfig::DestroyEquipmentOnDeath() || !HardcoreConfig::RespawnWithRezItems() || player->GetLevel() < HardcoreConfig::MinLvlGetRezItem())
            return;

        std::vector<uint32> equipItems = ParseItemIdList(HardcoreConfig::RespawnRandomEquip());
        if (!equipItems.empty())
        {
            uint32 itemId = equipItems[urand(0, uint32(equipItems.size() - 1))];
            uint16 destination = 0;
            if (player->CanEquipNewItem(NULL_SLOT, destination, itemId, false) == EQUIP_ERR_OK)
            {
                if (!player->EquipNewItem(destination, itemId, true))
                    LOG_ERROR("mod-hardcore", "Failed to equip resurrection item {} for {}.", itemId, player->GetName());
            }
            else
            {
                // Let AzerothCore choose the item's natural equipment slot. If
                // the configured item is not equippable for this character, fall
                // back to inventory instead of forcing it into the wrong slot.
                LOG_ERROR("mod-hardcore", "Resurrection random equipment item {} could not be equipped by {}. Attempting inventory fallback.", itemId, player->GetName());
                if (!player->AddItem(itemId, 1))
                    LOG_ERROR("mod-hardcore", "Failed to add resurrection random equipment item {} to {} inventory.", itemId, player->GetName());
            }
        }

        for (uint32 itemId : ParseItemIdList(HardcoreConfig::RespawnItemInv()))
        {
            if (!player->AddItem(itemId, 1))
                LOG_ERROR("mod-hardcore", "Failed to add resurrection inventory item {} to {}.", itemId, player->GetName());
        }
    }

    void GrantFallenTitleAndAchievement(Player* player, bool fallenResurrectionEnabled)
    {
        if (!player || !fallenResurrectionEnabled)
            return;

        if (HardcoreConfig::TitleOnDeath())
        {
            uint32 titleId = HardcoreConfig::TitleOnDeathId();
            if (CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleId))
                player->SetTitle(titleInfo);
            else
                LOG_ERROR("mod-hardcore", "Invalid fallen title ID {}!", titleId);
        }

        if (HardcoreConfig::AchievementOnDeath())
        {
            uint32 achievementId = HardcoreConfig::AchievementOnDeathId();
            if (AchievementEntry const* achievementInfo = sAchievementStore.LookupEntry(achievementId))
                player->CompletedAchievement(achievementInfo);
            else
                LOG_ERROR("mod-hardcore", "Invalid fallen achievement ID {}!", achievementId);
        }
    }

    void FinishAllowedResurrection(Player* player)
    {
        if (!player || !BaseEligible(player) || !HasToken(player))
            return;

        // Re-send the cooldown clear after resurrection/map transfer so the
        // client's Hearthstone display agrees with the server-side reset.
        ResetHearthstoneCooldown(player);

        // Resurrection is the forfeit point. The Key is deliberately kept
        // through the teleport so Hardcore remains authoritative until the
        // fallen character has been finalized on the destination map.
        player->DestroyItemCount(HardcoreConfig::TokenItemId(), 1, true, true);
        player->SaveToDB(false, false);

        uint32 guid = player->GetGUID().GetCounter();
        PendingResurrectionFinalizes.erase(guid);
        QualifyingDeaths.erase(guid);
        IgnoredDeaths.erase(guid);

        if (player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hardcore mode is forfeited by resurrection. Your Hardcore Key has been destroyed.");
    }

    void CompleteAllowedResurrection(Player* player, ResurrectionMethod /*method*/, bool appearanceAppliedBeforeResurrection = false)
    {
        if (!player || !BaseEligible(player) || !HasToken(player) || !InConfiguredLevelRange(player->GetLevel()))
            return;

        if (GetPending(player))
            ProcessPendingDeath(player);

        if (IsCurrentDeathIgnored(player))
            return;

        bool fallenEnabled = HardcoreConfig::MakeUndeadOnRez();
        bool appearanceApplied = appearanceAppliedBeforeResurrection;

        // Run one final ensure pass after resurrection as well. This never
        // rerolls a valid appearance; it only repairs an invalid/missing tuple.
        // Direct/bypassed GM resurrection therefore follows the same fallen
        // process as player and Spirit Healer resurrection when MakeUndeadOnRez
        // is enabled, with no separate GM appearance setting.
        if (fallenEnabled)
            appearanceApplied = HardcoreDkAppearance::EnsureDeathKnightAppearance(player, true) || appearanceApplied;

        if (appearanceApplied && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("You return changed, bearing a Death Knight-like fallen appearance.");

        // Fallen-only rewards remain tied to MakeUndeadOnRez. Resurrection
        // equipment remains independent and still follows the equipment-loss gate.
        if (fallenEnabled)
            HardcoreDkAppearance::GrantFallenRacials(player);
        GrantResurrectionItems(player);
        GrantFallenTitleAndAchievement(player, fallenEnabled);

        if (HardcoreConfig::TeleportOnHardcoreDeath())
        {
            uint32 guid = player->GetGUID().GetCounter();
            PendingResurrectionFinalizes[guid] = PendingResurrectionFinalize{};

            // When a permadead character was already memorialized at death,
            // an exceptional GM resurrection can already be on this map. Do
            // not issue a redundant teleport in that case; OnPlayerUpdate will
            // still wait for an alive/in-world stable state before finalizing.
            if (!IsAtDeathMemorial(player))
                TeleportToDeathMemorial(player);
            return;
        }

        // No memorial teleport: preserve the proven immediate behavior.
        FinishAllowedResurrection(player);
    }

    void UpdatePendingResurrectionFinalize(Player* player, uint32 diff)
    {
        if (!player)
            return;

        uint32 guid = player->GetGUID().GetCounter();
        auto itr = PendingResurrectionFinalizes.find(guid);
        if (itr == PendingResurrectionFinalizes.end())
            return;

        PendingResurrectionFinalize& finalize = itr->second;

        // A far teleport is asynchronous. The fallen PLAYER_BYTES were already
        // applied and saved before TeleportTo; this stage only waits until the
        // resurrected character is alive and stable on the destination map before
        // completing the Hardcore forfeit/key cleanup.
        if (!player->IsAlive() || !player->IsInWorld() || !IsAtDeathMemorial(player) || player->IsBeingTeleportedFar())
        {
            finalize.StableOnDestinationMs = 0;
            return;
        }

        // Give the destination map/client a brief stabilization window. This
        // is non-blocking; it uses normal PlayerScript updates rather than
        // sleeping the world thread.
        finalize.StableOnDestinationMs += diff;
        if (finalize.StableOnDestinationMs < 500)
            return;

        FinishAllowedResurrection(player);
    }

}

namespace HardcoreHelper
{
    bool GetHardcoreEnabledForPlayer(Player* player)
    {
        return HardcoreState::IsActive(player);
    }
}

class HardcoreDkAppearanceCommands : public CommandScript
{
public:
    HardcoreDkAppearanceCommands() : CommandScript("mod_hardcore_dk_appearance_commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable hcDkCommandTable =
        {
            { "info",    HandleInfoCommand,    rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "auras",   HandleAurasCommand,   rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "apply",   HandleApplyCommand,   rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "refresh", HandleRefreshCommand, rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "sync",    HandleSyncCommand,    rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "restore", HandleRestoreCommand, rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "save",    HandleSaveCommand,    rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "show",     HandleShowCommand,     rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "showhair", HandleShowHairCommand, rbac::RBAC_PERM_COMMAND_MORPH, Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "hcdk", hcDkCommandTable }
        };
        return commandTable;
    }

private:
    static bool HandleInfoCommand(ChatHandler* handler)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;
        Player* target = HardcoreDkAppearance::GetTarget(handler);
        if (!target)
            return false;
        HardcoreDkAppearance::PrintAppearance(handler, target);
        return true;
    }

    static bool HandleAurasCommand(ChatHandler* handler)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;
        Player* target = HardcoreDkAppearance::GetTarget(handler);
        if (!target)
            return false;
        HardcoreDkAppearance::DumpAurasToConsole(handler, target);
        return true;
    }

    static bool HandleApplyCommand(ChatHandler* handler, uint32 skin, uint32 face, Optional<uint32> hairColor, Optional<uint32> hairStyle, Optional<uint32> facialHair)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;

        if (skin > 255 || face > 255 || (hairColor && *hairColor > 255) || (hairStyle && *hairStyle > 255) || (facialHair && *facialHair > 255))
        {
            handler->SendErrorMessage("Skin, face, hairColor, hairStyle and facialHair must be byte values from 0 to 255.");
            return false;
        }

        Player* target = HardcoreDkAppearance::GetTarget(handler);
        if (!target)
            return false;

        uint32 guid = target->GetGUID().GetCounter();
        if (HardcoreDkAppearance::Backups.find(guid) == HardcoreDkAppearance::Backups.end())
        {
            HardcoreDkAppearance::Backups.emplace(guid, HardcoreDkAppearance::AppearanceBackup
            {
                target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID),
                target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID),
                target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID),
                target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID),
                target->GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE)
            });
        }

        auto const& original = HardcoreDkAppearance::Backups.at(guid);

        uint8 finalHairStyle = hairStyle ? uint8(*hairStyle) : target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID);
        uint8 finalHairColor = hairColor ? uint8(*hairColor) : target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID);
        uint8 finalFacialHair = facialHair ? uint8(*facialHair) : target->GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE);

        // Construct PLAYER_BYTES once so debug testing can never emit an
        // intermediate skin/face/hair combination between individual writes.
        uint32 appearanceBytes = target->GetUInt32Value(PLAYER_BYTES);
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_SKIN_ID, uint8(skin));
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_FACE_ID, uint8(face));
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID, finalHairStyle);
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID, finalHairColor);
        target->SetUInt32Value(PLAYER_BYTES, appearanceBytes);

        if (facialHair)
        {
            uint32 appearanceBytes2 = target->GetUInt32Value(PLAYER_BYTES_2);
            appearanceBytes2 = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE, finalFacialHair);
            target->SetUInt32Value(PLAYER_BYTES_2, appearanceBytes2);
        }

        handler->PSendSysMessage(
            "Applied HC DK test appearance to {}: skin={} face={} hairColor={} hairStyle={} facialHair={}. Backup: skin={} face={} hairColor={} hairStyle={} facialHair={}. Not force-saved yet.",
            target->GetName(), skin, face, uint32(finalHairColor), uint32(finalHairStyle), uint32(finalFacialHair),
            uint32(original.Skin), uint32(original.Face), uint32(original.HairColor), uint32(original.HairStyle), uint32(original.FacialHair));

        HardcoreDkAppearance::PrintAppearance(handler, target);
        return true;
    }

    static bool HandleRefreshCommand(ChatHandler* handler)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;

        Player* target = HardcoreDkAppearance::GetTarget(handler);
        if (!target)
            return false;

        // Force the current customization update fields to be rebuilt and sent
        // even when SetByteValue() sees no value change. This is intentionally a
        // debug-only diagnostic for fresh-object/login visibility testing.
        target->ForceValuesUpdateAtIndex(PLAYER_BYTES);
        target->ForceValuesUpdateAtIndex(PLAYER_BYTES_2);

        handler->PSendSysMessage("Forced PLAYER_BYTES/PLAYER_BYTES_2 refresh for {}. Current appearance was not changed or saved.", target->GetName());
        HardcoreDkAppearance::PrintAppearance(handler, target);
        return true;
    }

    static bool HandleSyncCommand(ChatHandler* handler)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;

        Player* observer = handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        Player* target = HardcoreDkAppearance::GetTarget(handler);
        if (!observer || !target)
            return false;

        bool observerReal = HardcoreDkSkinSync::IsRealObserver(observer);
        bool observerInWorld = observer->IsInWorld();
        bool targetInWorld = target->IsInWorld();
        bool sameMap = observerInWorld && targetInWorld && observer->GetMap() && target->GetMap() && observer->GetMap() == target->GetMap();
        bool haveAtClient = sameMap && observer->HaveAtClient(target);
        bool fallen = HardcoreDkSkinSync::IsFallenAppearance(target);

        uint8 skin = target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID);
        uint8 face = target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID);
        uint8 hairStyle = target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID);
        uint8 hairColor = target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID);
        uint8 alternateSkin = HardcoreDkSkinSync::GetAlternateDkSkin(target, skin);
        uint8 alternateHairStyle = hairStyle;
        uint8 alternateHairColor = hairColor;
        bool hasAlternateHair = HardcoreDkSkinSync::GetAlternateDkHairPair(target, hairStyle, hairColor, alternateHairStyle, alternateHairColor);
        bool anySyncEnabled = HardcoreConfig::EnableDkSkinSync() || HardcoreConfig::EnableDkSkinObjectCreateSync();
        bool canPulse = HardcoreConfig::Enabled() && anySyncEnabled && observerReal && observer != target && sameMap && haveAtClient && fallen && alternateSkin != skin;

        handler->PSendSysMessage("HC DK sync diagnostic: observer={} target={}", observer->GetName(), target->GetName());
        handler->PSendSysMessage("  Hardcore enabled={} TimerSync={} ObjectCreateSync={} observerReal={} observerInWorld={} targetInWorld={}",
            HardcoreConfig::Enabled() ? "YES" : "NO",
            HardcoreConfig::EnableDkSkinSync() ? "YES" : "NO",
            HardcoreConfig::EnableDkSkinObjectCreateSync() ? "YES" : "NO",
            observerReal ? "YES" : "NO",
            observerInWorld ? "YES" : "NO",
            targetInWorld ? "YES" : "NO");
        handler->PSendSysMessage("  sameMap={} HaveAtClient={} fallenAppearance={}",
            sameMap ? "YES" : "NO", haveAtClient ? "YES" : "NO", fallen ? "YES" : "NO");
        handler->PSendSysMessage("  target appearance: skin={} face={} hairStyle={} hairColor={}",
            uint32(skin), uint32(face), uint32(hairStyle), uint32(hairColor));
        handler->PSendSysMessage("  pulse appearance: alternateSkin={} alternateHairStyle={} alternateHairColor={} hairNudge={}",
            uint32(alternateSkin), uint32(alternateHairStyle), uint32(alternateHairColor), hasAlternateHair ? "YES" : "NO");

        if (!canPulse)
        {
            handler->SendErrorMessage("HC DK sync pulse NOT sent because one or more diagnostic gates failed.");
            return true;
        }

        HardcoreDkSkinSync::PulseFallenAppearanceForObserver(observer, target);
        handler->PSendSysMessage("HC DK sync pulse SENT to observer {} for target {}.", observer->GetName(), target->GetName());
        return true;
    }

    static bool HandleRestoreCommand(ChatHandler* handler)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;
        Player* target = HardcoreDkAppearance::GetTarget(handler);
        if (!target)
            return false;

        uint32 guid = target->GetGUID().GetCounter();
        auto itr = HardcoreDkAppearance::Backups.find(guid);
        if (itr == HardcoreDkAppearance::Backups.end())
        {
            handler->SendErrorMessage("No HC DK appearance backup exists for this character in the current worldserver session.");
            return false;
        }

        uint32 appearanceBytes = target->GetUInt32Value(PLAYER_BYTES);
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_SKIN_ID, itr->second.Skin);
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_FACE_ID, itr->second.Face);
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID, itr->second.HairStyle);
        appearanceBytes = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID, itr->second.HairColor);
        target->SetUInt32Value(PLAYER_BYTES, appearanceBytes);

        uint32 appearanceBytes2 = target->GetUInt32Value(PLAYER_BYTES_2);
        appearanceBytes2 = HardcoreDkAppearance::ReplaceAppearanceByte(appearanceBytes2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE, itr->second.FacialHair);
        target->SetUInt32Value(PLAYER_BYTES_2, appearanceBytes2);

        target->SaveToDB(false, false);
        handler->PSendSysMessage("Restored and saved {}: skin={} face={} hairColor={} hairStyle={} facialHair={}.",
            target->GetName(), uint32(itr->second.Skin), uint32(itr->second.Face), uint32(itr->second.HairColor), uint32(itr->second.HairStyle), uint32(itr->second.FacialHair));
        HardcoreDkAppearance::Backups.erase(itr);
        HardcoreDkAppearance::PrintAppearance(handler, target);
        return true;
    }

    static bool HandleSaveCommand(ChatHandler* handler)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;
        Player* target = HardcoreDkAppearance::GetTarget(handler);
        if (!target)
            return false;
        target->SaveToDB(false, false);
        handler->PSendSysMessage("Saved {}'s current skin/face/hairStyle/hairColor/facialHair values.", target->GetName());
        return true;
    }

    static bool HandleShowCommand(ChatHandler* handler, char const* args)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;

        std::string race = args ? args : "";
        std::string normalized = HardcoreDkAppearance::NormalizeRaceName(race);
        if (normalized.empty())
        {
            handler->SendErrorMessage("Usage: .hcdk show <race|all>");
            return false;
        }

        if (normalized == "all")
        {
            std::array<std::string, 10> races = { "Human", "Orc", "Dwarf", "Night Elf", "Undead", "Tauren", "Gnome", "Troll", "Blood Elf", "Draenei" };
            for (std::string const& name : races)
                HardcoreDkAppearance::ShowRace(handler, name);
            return true;
        }

        std::array<std::string, 10> races = { "Human", "Orc", "Dwarf", "Night Elf", "Undead", "Tauren", "Gnome", "Troll", "Blood Elf", "Draenei" };
        for (std::string const& name : races)
        {
            if (HardcoreDkAppearance::MatchesRace(normalized, name))
            {
                HardcoreDkAppearance::ShowRace(handler, name);
                return true;
            }
        }

        handler->SendErrorMessage("Unknown race. Use human, orc, dwarf, nightelf, undead/forsaken, tauren, gnome, troll, bloodelf, draenei, or all.");
        return false;
    }
    static bool HandleShowHairCommand(ChatHandler* handler, char const* args)
    {
        if (!HardcoreDkAppearance::DebugEnabled(handler))
            return true;

        std::string race = args ? args : "";
        std::string normalized = HardcoreDkAppearance::NormalizeRaceName(race);
        if (normalized.empty())
        {
            handler->SendErrorMessage("Usage: .hcdk showhair <race|all>");
            return false;
        }

        std::array<std::string, 10> races = { "Human", "Orc", "Dwarf", "Night Elf", "Undead", "Tauren", "Gnome", "Troll", "Blood Elf", "Draenei" };
        if (normalized == "all")
        {
            for (std::string const& name : races)
                HardcoreDkAppearance::ShowHairRace(handler, name);
            return true;
        }

        for (std::string const& name : races)
        {
            if (HardcoreDkAppearance::MatchesRace(normalized, name))
            {
                HardcoreDkAppearance::ShowHairRace(handler, name);
                return true;
            }
        }

        handler->SendErrorMessage("Unknown race. Use human, orc, dwarf, nightelf, undead/forsaken, tauren, gnome, troll, bloodelf, draenei, or all.");
        return false;
    }
};


class HardcoreCommands : public CommandScript
{
public:
    HardcoreCommands() : CommandScript("mod_hardcore_commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "hc", HandleHardcoreCommand, rbac::RBAC_PERM_COMMAND_HELP, Console::No }
        };
        return commandTable;
    }

private:
    static std::string Normalize(std::string value)
    {
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), value.end());
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    }

    static void ShowRules(ChatHandler* handler)
    {
        Player* player = handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        handler->SendSysMessage("=== Hardcore Rules ===");
        handler->PSendSysMessage("Hardcore level range: {} through {}.", HardcoreConfig::StartLevel(), HardcoreConfig::EndLevel());
        handler->PSendSysMessage("Player resurrection: {} | Spirit Healer: {} | GM resurrection: {}.",
            HardcoreConfig::AllowPlayerRez() ? "Allowed" : "Disabled",
            HardcoreConfig::AllowSpiritRez() ? "Allowed" : "Disabled",
            HardcoreConfig::AllowGMRez() ? "Allowed" : "Disabled");
        handler->PSendSysMessage("Destroy equipped gear/money on qualifying death: {}.", HardcoreConfig::DestroyEquipmentOnDeath() ? "Yes" : "No");
        handler->PSendSysMessage("Reset Hearthstone cooldown on qualifying death: {}.", HardcoreConfig::ResetHearthstoneOnDeath() ? "Yes" : "No");
        handler->PSendSysMessage("Battleground deaths count: {} | Arena deaths count: {} | World PvP deaths count: {}.",
            HardcoreConfig::CountBattlegroundDeaths() ? "Yes" : "No",
            HardcoreConfig::CountArenaDeaths() ? "Yes" : "No",
            HardcoreConfig::CountPvPDeaths() ? "Yes" : "No");
        handler->PSendSysMessage("Mak'gora: {} | Fallen appearance: {} | Fallen racial rewards: {} | Memorial teleport: {}.",
            HardcoreConfig::EnableMakgora() ? "Enabled" : "Disabled",
            HardcoreConfig::MakeUndeadOnRez() ? "Enabled" : "Disabled",
            HardcoreConfig::GiveFallenHCUndeadRacials() ? "Enabled" : "Disabled",
            HardcoreConfig::TeleportOnHardcoreDeath() ? "Enabled" : "Disabled");
        handler->PSendSysMessage("Death broadcasts: {} (server default level {}+).",
            HardcoreConfig::AnnounceDeaths() ? "Enabled" : "Disabled", HardcoreConfig::DefaultPlayerDeathMessageLevel());

        if (player)
        {
            handler->PSendSysMessage("Your Hardcore Key: {}.", HardcoreState::HasToken(player) ? "Present" : "Not present");
            handler->PSendSysMessage("Your PvP death flag (item {}): {}.", HardcoreConfig::PvPDeathFlagItemId(), HardcoreState::HasPvPDeathFlag(player) ? "Present" : "Not present");
            uint32 current = HardcoreState::GetDeathMessageMinLevel(player);
            if (current >= 255)
                handler->SendSysMessage("Your HC death broadcasts: Off for this login session.");
            else
                handler->PSendSysMessage("Your HC death broadcasts: level {}+ victims.", current);
        }
        handler->SendSysMessage("Use .hc death <level>, .hc death off, or .hc death default to change your death-message filter.");
    }

    static bool HandleHardcoreCommand(ChatHandler* handler, char const* args)
    {
        if (!HardcoreConfig::Enabled())
        {
            handler->SendSysMessage("Hardcore mode is disabled on this server.");
            return true;
        }

        std::string input = Normalize(args ? args : "");
        if (input.empty() || input == "rules" || input == "info")
        {
            ShowRules(handler);
            return true;
        }

        if (input.rfind("death", 0) == 0)
        {
            Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
            if (!player)
                return false;

            std::string value = Normalize(input.substr(5));
            if (value.empty())
            {
                uint32 current = HardcoreState::GetDeathMessageMinLevel(player);
                if (current >= 255)
                    handler->SendSysMessage("Hardcore death announcements are currently disabled for you.");
                else
                    handler->PSendSysMessage("You currently receive Hardcore death announcements for level {}+ victims.", current);
                return true;
            }
            if (value == "off")
            {
                HardcoreState::SetDeathMessageMinLevel(player, 255);
                handler->SendSysMessage("Hardcore death announcements disabled for this login session.");
                return true;
            }
            if (value == "default")
            {
                HardcoreState::ResetDeathMessageMinLevel(player);
                handler->PSendSysMessage("Hardcore death-announcement filter reset to server default: level {}+.", HardcoreConfig::DefaultPlayerDeathMessageLevel());
                return true;
            }

            char* end = nullptr;
            long parsed = std::strtol(value.c_str(), &end, 10);
            if (!end || *end != '\0' || parsed < 0 || parsed > 254)
            {
                handler->SendErrorMessage("Usage: .hc death <0-254|off|default>");
                return false;
            }
            HardcoreState::SetDeathMessageMinLevel(player, uint32(parsed));
            handler->PSendSysMessage("You will now receive Hardcore death announcements for level {}+ victims.", parsed);
            return true;
        }

        handler->SendErrorMessage("Usage: .hc | .hc death <level|off|default>");
        return false;
    }
};


class HardcoreMode : public PlayerScript
{
public:
    explicit HardcoreMode() : PlayerScript("mod_hardcore") { }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (!player || !HardcoreConfig::Enabled() || player->IsGameMaster())
            return;
        if (!HardcoreConfig::AllowBots() && player->GetSession() && player->GetSession()->IsBot())
            return;

        if (!HardcoreState::HasToken(player))
            player->AddItem(HardcoreConfig::TokenItemId(), 1);
        SendHardcoreStatus(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !HardcoreConfig::Enabled())
            return;

        if (HardcoreState::HasToken(player) && HardcoreState::InConfiguredLevelRange(player->GetLevel()) && player->isDead() && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("You died while Hardcore. Your Hardcore Key remains while you are dead.");

        SendHardcoreStatus(player);

        // Login can occur before the player is fully in-world. Neither observer
        // timer advances until OnPlayerUpdate sees a stable in-world player.
        // A later zone/area update during initialization replaces the login
        // pair with the independently configurable zone/area timing pair.
        HardcoreDkSkinSync::ScheduleLoginObserver(player);
    }

    void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        HardcoreDkSkinSync::ScheduleZoneObserver(player);
    }

    void OnPlayerUpdateArea(Player* player, uint32 /*oldArea*/, uint32 /*newArea*/) override
    {
        HardcoreDkSkinSync::ScheduleZoneObserver(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        uint32 guid = player->GetGUID().GetCounter();
        HardcoreDkSkinSync::Cancel(player);

        // If the player logs out during the short post-teleport stabilization
        // window, finalize the already-successful resurrection before logout
        // so they cannot return alive while still holding the Hardcore Key.
        auto finalizeItr = HardcoreState::PendingResurrectionFinalizes.find(guid);
        if (finalizeItr != HardcoreState::PendingResurrectionFinalizes.end() && player->IsAlive())
        {
            HardcoreState::FinishAllowedResurrection(player);
        }

        HardcoreState::PendingDeaths.erase(guid);
        HardcoreState::RecentDeathCauses.erase(guid);
        HardcoreState::ForcedDeaths.erase(guid);
        HardcoreState::SuppressedDeaths.erase(guid);
        HardcoreState::QualifyingDeaths.erase(guid);
        HardcoreState::IgnoredDeaths.erase(guid);
        HardcoreState::ApprovedResurrections.erase(guid);
        HardcoreState::PendingResurrectionFinalizes.erase(guid);
        HardcoreState::DeathMessageMinLevels.erase(guid);
        HardcoreDkAppearance::Backups.erase(guid);
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!HardcoreState::BaseEligible(player) || !HardcoreState::HasToken(player))
            return;

        uint8 newLevel = player->GetLevel();

        // Reward eligibility is based on the OLD level being inside the HC
        // range and the Key still being present. This intentionally runs before
        // completion handling, so 59->60 can award level-60 rewards when
        // EndLevel is either 59 or 60.
        if (HardcoreState::InConfiguredLevelRange(oldLevel))
        {
            UpdateAchievementsAndTitles(player, oldLevel, newLevel);
            UpdateChallengeRewards(player, oldLevel, newLevel);
        }

        int32 endLevel = HardcoreConfig::EndLevel();
        if (int32(oldLevel) <= endLevel && int32(newLevel) > endLevel)
        {
            if (HardcoreConfig::AnnounceLevelUp())
                SendWorldAnnouncement(Acore::StringFormat("{} completed the Hardcore challenge at level {}!", player->GetName(), newLevel));
            return;
        }

        if (HardcoreConfig::AnnounceLevelUp() && HardcoreState::InConfiguredLevelRange(newLevel))
        {
            if (newLevel == 10 || newLevel == 20 || newLevel == 30 || newLevel == 40 || newLevel == 50 || newLevel == 60 || newLevel == 70 || newLevel == 80)
                SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {} in Hardcore mode!", player->GetName(), newLevel));
        }
    }

    void OnPlayerJustDied(Player* player) override
    {
        HardcoreState::QueueDeath(player);
        if (HardcoreState::PendingDeath* pending = HardcoreState::GetPending(player))
        {
            if (pending->Category == HardcoreState::DeathCategory::MAKGORA)
                HardcoreState::ProcessPendingDeath(player);
        }
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        HardcoreDkSkinSync::Update(player, diff);

        if (player && player->isDead() && HardcoreState::GetPending(player))
            HardcoreState::ProcessPendingDeath(player);

        if (player && player->IsAlive())
            HardcoreState::UpdatePendingResurrectionFinalize(player, diff);
    }

    void OnPlayerReleasedGhost(Player* player) override
    {
        if (HardcoreState::GetPending(player))
            HardcoreState::ProcessPendingDeath(player);

        if (HardcoreState::NeedsResurrectionRules(player) && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("This was a qualifying Hardcore death. Corpse reclaim/self-resurrection is disabled; use an allowed resurrection method.");
    }

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        HardcoreState::UpdatePendingCauseFromPvP(killed, killer);
        if (HardcoreState::GetPending(killed))
            HardcoreState::ProcessPendingDeath(killed);
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        HardcoreState::UpdatePendingCauseFromCreature(killed, killer);
        if (HardcoreState::GetPending(killed))
            HardcoreState::ProcessPendingDeath(killed);
    }

    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override
    {
        if (!HardcoreConfig::EnableMakgora() || !loser || !winner || type == DUEL_INTERRUPTED)
            return;
        if (!HardcoreState::IsActive(loser))
            return;

        HardcoreState::ForceNextDeath(loser, HardcoreState::DeathCategory::MAKGORA,
            Acore::StringFormat("Slain in Mak'gora by {}.", winner->GetName()));

        if (loser->GetSession())
            ChatHandler(loser->GetSession()).PSendSysMessage("You lost the Mak'gora to {}. The duel is now fatal.", winner->GetName());
        loser->KillPlayer();
    }

    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool& /*applySickness*/) override
    {
        if (!player)
            return;

        uint32 guid = player->GetGUID().GetCounter();

        // The item is the restart-safe authority for an excluded PvP/BG/Arena
        // death. Destroy ALL copies on every successful resurrection. If it was
        // present, this resurrection is ordinary and must bypass every HC
        // fallen/forfeit rule while preserving the Hardcore Key.
        bool hadPvPDeathFlag = HardcoreState::HasPvPDeathFlag(player);
        HardcoreState::ClearPvPDeathFlag(player);
        if (hadPvPDeathFlag)
        {
            HardcoreState::IgnoredDeaths.erase(guid);
            HardcoreState::QualifyingDeaths.erase(guid);
            HardcoreState::ApprovedResurrections.erase(guid);
            return;
        }

        // Same-session ignored deaths still have the in-memory fast path; the
        // item above is what makes the decision survive logout/server restart.
        if (HardcoreState::IgnoredDeaths.find(guid) != HardcoreState::IgnoredDeaths.end() || HardcoreState::IsCurrentDeathIgnored(player))
        {
            HardcoreState::IgnoredDeaths.erase(guid);
            HardcoreState::QualifyingDeaths.erase(guid);
            return;
        }

        HardcoreState::ResurrectionMethod method;
        bool appearanceAppliedBeforeResurrection = false;
        if (HardcoreState::PopApprovedResurrection(player, method, appearanceAppliedBeforeResurrection))
        {
            HardcoreState::CompleteAllowedResurrection(player, method, appearanceAppliedBeforeResurrection);
            return;
        }

        // Direct/bypassed resurrection (for example a GM command) is treated as
        // GM resurrection and still obeys AllowGMRez.
        if (HardcoreState::BaseEligible(player) && HardcoreState::HasToken(player) && HardcoreState::InConfiguredLevelRange(player->GetLevel()))
        {
            if (!HardcoreConfig::AllowGMRez())
            {
                HardcoreState::SuppressNextDeath(player);
                player->KillPlayer();
                return;
            }

            HardcoreState::CompleteAllowedResurrection(player, HardcoreState::ResurrectionMethod::GM);
        }
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& /*msg*/) override
    {
        return CanUseChat(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& /*msg*/, Player* /*receiver*/) override
    {
        return CanUseChat(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& /*msg*/, Group* /*group*/) override
    {
        return CanUseChat(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& /*msg*/, Guild* /*guild*/) override
    {
        return CanUseChat(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& /*msg*/, Channel* /*channel*/) override
    {
        return CanUseChat(player);
    }

    bool OnPlayerCanGroupInvite(Player* player, std::string& /*membername*/) override
    {
        if (HardcoreState::NeedsResurrectionRules(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("You can't invite players to a group while dead.");
            return false;
        }
        return true;
    }

    bool OnPlayerCanGroupAccept(Player* player, Group* /*group*/) override
    {
        if (HardcoreState::NeedsResurrectionRules(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("You can't join a group while dead.");
            return false;
        }
        return true;
    }

private:
    bool CanUseChat(Player* player)
    {
        return !(HardcoreConfig::DisableChatWhenDead() && HardcoreState::NeedsResurrectionRules(player));
    }

    void SendWorldAnnouncement(std::string const& message)
    {
        HardcoreState::SendWorldAnnouncement(message);
    }

    void SendHardcoreStatus(Player* player)
    {
        if (!player || player->IsGameMaster() || !player->GetSession() || player->GetSession()->IsBot() || !HardcoreConfig::Enabled())
            return;

        if (!HardcoreState::HasToken(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("Hardcore is disabled for this character (no Hardcore Key).");
            return;
        }

        if (int32(player->GetLevel()) > HardcoreConfig::EndLevel())
        {
            ChatHandler(player->GetSession()).SendSysMessage("Hardcore challenge completed.");
            return;
        }

        if (HardcoreState::InConfiguredLevelRange(player->GetLevel()))
            ChatHandler(player->GetSession()).PSendSysMessage("Hardcore is active. Level {}.", player->GetLevel());
        else
            ChatHandler(player->GetSession()).PSendSysMessage("Hardcore Key active; Hardcore rules begin at level {}.", HardcoreConfig::StartLevel());
    }

    std::unordered_map<uint8, uint32> LoadStringToMap(std::string const& configString)
    {
        std::unordered_map<uint8, uint32> result;
        std::string delimitedValue;
        std::stringstream configIdStream(configString);
        while (std::getline(configIdStream, delimitedValue, ','))
        {
            std::string pairOne, pairTwo;
            std::stringstream configPairStream(delimitedValue);
            configPairStream >> pairOne >> pairTwo;
            if (pairOne.empty() || pairTwo.empty())
                continue;
            result[uint8(atoi(pairOne.c_str()))] = uint32(atoi(pairTwo.c_str()));
        }
        return result;
    }

    void UpdateAchievementsAndTitles(Player* player, uint8 oldLevel, uint8 newLevel)
    {
        if (!player || !HardcoreState::HasToken(player))
            return;

        std::unordered_map<uint8, uint32> achievementRewardMap = LoadStringToMap(sConfigMgr->GetOption<std::string>("ModHardcore.AchievementReward", ""));
        std::unordered_map<uint8, uint32> titleRewardMap = LoadStringToMap(sConfigMgr->GetOption<std::string>("ModHardcore.TitleReward", ""));
        uint32 maxRewardLevel = uint32(std::max<int32>(0, HardcoreConfig::EndLevel() + 1));

        for (auto const& entry : titleRewardMap)
        {
            uint8 rewardLevel = entry.first;
            uint32 titleId = entry.second;
            if (rewardLevel <= oldLevel || rewardLevel > newLevel || rewardLevel > maxRewardLevel)
                continue;
            CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleId);
            if (!titleInfo)
                LOG_ERROR("mod-hardcore", "Invalid title ID {}!", titleId);
            else
                player->SetTitle(titleInfo);
        }

        for (auto const& entry : achievementRewardMap)
        {
            uint8 rewardLevel = entry.first;
            uint32 achievementId = entry.second;
            if (rewardLevel <= oldLevel || rewardLevel > newLevel || rewardLevel > maxRewardLevel)
                continue;
            AchievementEntry const* achievementInfo = sAchievementStore.LookupEntry(achievementId);
            if (!achievementInfo)
                LOG_ERROR("mod-hardcore", "Invalid Achievement ID {}!", achievementId);
            else
                player->CompletedAchievement(achievementInfo);
        }
    }

    void UpdateChallengeRewards(Player* player, uint8 oldLevel, uint8 newLevel)
    {
        if (!player || !HardcoreState::HasToken(player))
            return;

        uint32 rewardLevel = HardcoreConfig::ChallengeRewardLevel();
        uint32 maxRewardLevel = uint32(std::max<int32>(0, HardcoreConfig::EndLevel() + 1));
        if (!rewardLevel || rewardLevel <= oldLevel || rewardLevel > newLevel || rewardLevel > maxRewardLevel)
            return;

        if (HardcoreConfig::ChallengeRewardItemEnable())
        {
            uint32 itemId = HardcoreConfig::ChallengeRewardItemId();
            if (!itemId)
                LOG_ERROR("mod-hardcore", "ChallengeRewardItemEnable is true but ChallengeRewardItemId is 0.");
            else if (player->AddItem(itemId, 1))
            {
                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage("Hardcore challenge reward: received item {}.", itemId);
            }
            else
                LOG_ERROR("mod-hardcore", "Could not add Hardcore challenge reward item {} to {}.", itemId, player->GetName());
        }

        if (HardcoreConfig::ChallengeRewardSpellEnable())
        {
            uint32 spellId = HardcoreConfig::ChallengeRewardSpellId();
            if (!spellId)
                LOG_ERROR("mod-hardcore", "ChallengeRewardSpellEnable is true but ChallengeRewardSpellId is 0.");
            else if (!sSpellMgr->GetSpellInfo(spellId))
                LOG_ERROR("mod-hardcore", "Invalid Hardcore challenge reward spell ID {}.", spellId);
            else
            {
                player->learnSpell(spellId, false);
                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage("Hardcore challenge reward: learned spell {}.", spellId);
            }
        }
    }
};


class HardcoreDkSkinSyncUnitScript : public UnitScript
{
public:
    HardcoreDkSkinSyncUnitScript()
        : UnitScript("mod_hardcore_dk_skin_values_sync", true,
            { UNITHOOK_SHOULD_TRACK_VALUES_UPDATE_POS_BY_INDEX, UNITHOOK_ON_PATCH_VALUES_UPDATE })
    {
    }

    bool ShouldTrackValuesUpdatePosByIndex(Unit const* unit, uint8 updateType, uint16 index) override
    {
        if (!HardcoreConfig::Enabled() || !HardcoreConfig::EnableDkSkinObjectCreateSync() || !unit ||
            (index != PLAYER_BYTES && index != PLAYER_BYTES_2))
            return false;

        // Track both appearance fields while AzerothCore is constructing a fallen
        // player object for a client. Ordinary UPDATETYPE_VALUES packets are left
        // untouched; the existing manual/timer pulse remains independent.
        if (updateType != UPDATETYPE_CREATE_OBJECT && updateType != UPDATETYPE_CREATE_OBJECT2)
            return false;

        if (unit->GetTypeId() != TYPEID_PLAYER)
            return false;

        Player* subject = const_cast<Player*>(static_cast<Player const*>(unit));
        return HardcoreDkSkinSync::IsFallenAppearance(subject);
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target) override
    {
        if (!HardcoreConfig::Enabled() || !HardcoreConfig::EnableDkSkinObjectCreateSync() || !unit || !HardcoreDkSkinSync::IsRealObserver(target))
            return;
        if (unit->GetTypeId() != TYPEID_PLAYER)
            return;

        Player const* subject = static_cast<Player const*>(unit);
        if (subject == target)
            return;

        Player* mutableSubject = const_cast<Player*>(subject);
        if (!HardcoreDkSkinSync::IsFallenAppearance(mutableSubject))
            return;

        auto bytesPosItr = posPointers.other.find(PLAYER_BYTES);
        if (bytesPosItr == posPointers.other.end())
            return;

        uint8 actualSkin = subject->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID);
        uint8 alternateSkin = HardcoreDkSkinSync::GetAlternateDkSkin(mutableSubject, actualSkin);
        if (alternateSkin == actualSkin)
            return;

        uint8 actualHairStyle = subject->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID);
        uint8 actualHairColor = subject->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID);
        uint8 alternateHairStyle = actualHairStyle;
        uint8 alternateHairColor = actualHairColor;
        HardcoreDkSkinSync::GetAlternateDkHairPair(mutableSubject, actualHairStyle, actualHairColor, alternateHairStyle, alternateHairColor);

        // Build the newly-visible player with a complete DK-valid appearance
        // from the first packet. Hair color is deliberately nudged along with
        // skin so the receiving client cannot retain a stale hair texture from
        // object construction. The follow-up pair pulse restores the exact
        // saved PLAYER_BYTES + PLAYER_BYTES_2 shortly afterward.
        uint32 temporaryBytes = subject->GetUInt32Value(PLAYER_BYTES);
        temporaryBytes = HardcoreDkAppearance::ReplaceAppearanceByte(temporaryBytes, PLAYER_BYTES_OFFSET_SKIN_ID, alternateSkin);
        temporaryBytes = HardcoreDkAppearance::ReplaceAppearanceByte(temporaryBytes, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID, alternateHairStyle);
        temporaryBytes = HardcoreDkAppearance::ReplaceAppearanceByte(temporaryBytes, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID, alternateHairColor);
        valuesUpdateBuf.put(bytesPosItr->second, temporaryBytes);

        // PLAYER_BYTES_2 is not deliberately altered, but tracking and writing
        // it alongside PLAYER_BYTES keeps facial-hair customization in the same
        // object-create appearance state.
        if (auto bytes2PosItr = posPointers.other.find(PLAYER_BYTES_2); bytes2PosItr != posPointers.other.end())
            valuesUpdateBuf.put(bytes2PosItr->second, subject->GetUInt32Value(PLAYER_BYTES_2));

        HardcoreDkSkinSync::ScheduleObjectCreateSync(target, subject);
    }
};


class HardcoreDkSkinSyncMapScript : public AllMapScript
{
public:
    HardcoreDkSkinSyncMapScript() : AllMapScript("mod_hardcore_dk_skin_sync") { }

    void OnPlayerEnterAll(Map* /*map*/, Player* player) override
    {
        // Keep only the proven reverse path here: when a fallen player enters
        // after observers are already present, repair that subject for those
        // real observers. Observer-side repair is intentionally scheduled later
        // from PlayerScript login/zone/area lifecycle hooks.
        HardcoreDkSkinSync::ScheduleSubject(player);
    }
};

class HardModeServerScript : public ServerScript
{
public:
    HardModeServerScript() : ServerScript("mod_hardcore") { }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!HardcoreConfig::Enabled() || !session)
            return true;

        Player* player = session->GetPlayer();
        if (!player || player->IsGameMaster() || !player->isDead())
            return true;

        if (HardcoreState::GetPending(player))
            HardcoreState::ProcessPendingDeath(player);

        if (!HardcoreState::NeedsResurrectionRules(player))
            return true;

        switch (packet.GetOpcode())
        {
            case CMSG_HEARTH_AND_RESURRECT:
            case CMSG_RECLAIM_CORPSE:
                ChatHandler(session).SendSysMessage("You cannot self-resurrect or reclaim your corpse after a qualifying Hardcore death.");
                return false;

            case CMSG_RESURRECT_RESPONSE:
            {
                if (!HardcoreConfig::AllowPlayerRez())
                {
                    ChatHandler(session).SendSysMessage("Player resurrection is disabled by the Hardcore rules.");
                    return false;
                }

                // CMSG_RESURRECT_RESPONSE is also sent when the player rejects a
                // resurrection. Only run the pre-resurrection fallen ensure pass for an
                // accepted, currently valid resurrection request, matching the core handler.
                WorldPacket inspect(packet);
                inspect.rpos(0);
                ObjectGuid resurrectorGuid;
                uint8 status = 0;
                inspect >> resurrectorGuid >> status;
                if (status != 0 && player->isResurrectRequestedBy(resurrectorGuid))
                    HardcoreState::ApproveResurrection(player, HardcoreState::ResurrectionMethod::PLAYER);
                return true;
            }

            case CMSG_SPIRIT_HEALER_ACTIVATE:
                if (!HardcoreConfig::AllowSpiritRez())
                {
                    ChatHandler(session).SendSysMessage("Spirit Healer resurrection is disabled by the Hardcore rules.");
                    return false;
                }
                HardcoreState::ApproveResurrection(player, HardcoreState::ResurrectionMethod::SPIRIT_HEALER);
                return true;

            case CMSG_GM_RESURRECT:
                if (!HardcoreConfig::AllowGMRez())
                {
                    ChatHandler(session).SendSysMessage("GM resurrection is disabled by the Hardcore rules.");
                    return false;
                }
                HardcoreState::ApproveResurrection(player, HardcoreState::ResurrectionMethod::GM);
                return true;

            default:
                break;
        }

        return true;
    }
};

void AddSC_mod_hardcore()
{
    new HardcoreMode();
    new HardcoreDkSkinSyncUnitScript();
    new HardcoreDkSkinSyncMapScript();
    new HardModeServerScript();
    new HardcoreCommands();
    new HardcoreDkAppearanceCommands();
}
