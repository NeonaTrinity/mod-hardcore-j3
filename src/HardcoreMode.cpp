/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * Notes:
 * List of unused items: https://pastebin.com/sirbRBJY
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Corpse.h"
#include "RBAC.h"
#include "Log.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <set>
#include <unordered_map>



using namespace Acore::ChatCommands;

namespace HardcoreDkAppearanceTest
{
    struct AppearanceBackup
    {
        uint8 Skin;
        uint8 Face;
    };

    static std::unordered_map<uint32, AppearanceBackup> Backups;

    Player* GetTarget(ChatHandler* handler)
    {
        if (!handler || !handler->GetSession())
        {
            return nullptr;
        }

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendErrorMessage("Select a player or use the command on yourself.");
            return nullptr;
        }

        if (handler->HasLowerSecurity(target))
        {
            return nullptr;
        }

        return target;
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
        {
            auraSpellIds.insert(auraPair.first);
        }

        LOG_INFO("server.loading", "===== HC DK AURA DUMP: {} =====", target->GetName());
        LOG_INFO(
            "server.loading",
            "race={} class={} gender={} display={} nativeDisplay={} uniqueAppliedAuras={}",
            uint32(target->getRace()),
            uint32(target->getClass()),
            uint32(target->getGender()),
            target->GetDisplayId(),
            target->GetNativeDisplayId(),
            auraSpellIds.size());

        for (uint32 spellId : auraSpellIds)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo)
            {
                LOG_INFO("server.loading", "[HC-DK-AURA] id={} | SpellInfo not found", spellId);
                continue;
            }

            char const* spellName = spellInfo->SpellName[0] ? spellInfo->SpellName[0] : "<unnamed>";

            LOG_INFO(
                "server.loading",
                "[HC-DK-AURA] id={} name='{}' passive={} deathPersistent={} "
                "visual0={} visual1={} attrs={} attrsEx3={} | "
                "e0:aura={} misc={} miscB={} trigger={} | "
                "e1:aura={} misc={} miscB={} trigger={} | "
                "e2:aura={} misc={} miscB={} trigger={}",
                spellId,
                spellName,
                spellInfo->IsPassive() ? 1 : 0,
                spellInfo->IsDeathPersistent() ? 1 : 0,
                spellInfo->SpellVisual[0],
                spellInfo->SpellVisual[1],
                spellInfo->Attributes,
                spellInfo->AttributesEx3,
                uint32(spellInfo->Effects[EFFECT_0].ApplyAuraName),
                spellInfo->Effects[EFFECT_0].MiscValue,
                spellInfo->Effects[EFFECT_0].MiscValueB,
                spellInfo->Effects[EFFECT_0].TriggerSpell,
                uint32(spellInfo->Effects[EFFECT_1].ApplyAuraName),
                spellInfo->Effects[EFFECT_1].MiscValue,
                spellInfo->Effects[EFFECT_1].MiscValueB,
                spellInfo->Effects[EFFECT_1].TriggerSpell,
                uint32(spellInfo->Effects[EFFECT_2].ApplyAuraName),
                spellInfo->Effects[EFFECT_2].MiscValue,
                spellInfo->Effects[EFFECT_2].MiscValueB,
                spellInfo->Effects[EFFECT_2].TriggerSpell);
        }

        LOG_INFO("server.loading", "===== END HC DK AURA DUMP: {} =====", target->GetName());

        handler->PSendSysMessage(
            "Dumped {} unique applied auras for {} to the worldserver console.",
            auraSpellIds.size(),
            target->GetName());
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
            { "restore", HandleRestoreCommand, rbac::RBAC_PERM_COMMAND_MORPH, Console::No },
            { "save",    HandleSaveCommand,    rbac::RBAC_PERM_COMMAND_MORPH, Console::No }
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
        Player* target = HardcoreDkAppearanceTest::GetTarget(handler);
        if (!target)
        {
            return false;
        }

        HardcoreDkAppearanceTest::PrintAppearance(handler, target);
        return true;
    }

    static bool HandleAurasCommand(ChatHandler* handler)
    {
        Player* target = HardcoreDkAppearanceTest::GetTarget(handler);
        if (!target)
        {
            return false;
        }

        HardcoreDkAppearanceTest::DumpAurasToConsole(handler, target);
        return true;
    }

    static bool HandleApplyCommand(ChatHandler* handler, uint32 skin, uint32 face)
    {
        if (skin > 255 || face > 255)
        {
            handler->SendErrorMessage("Skin and face must be byte values from 0 to 255.");
            return false;
        }

        Player* target = HardcoreDkAppearanceTest::GetTarget(handler);
        if (!target)
        {
            return false;
        }

        uint32 guidLow = target->GetGUID().GetCounter();
        if (HardcoreDkAppearanceTest::Backups.find(guidLow) == HardcoreDkAppearanceTest::Backups.end())
        {
            HardcoreDkAppearanceTest::Backups.emplace(guidLow, HardcoreDkAppearanceTest::AppearanceBackup
            {
                target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID),
                target->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID)
            });
        }

        auto const& original = HardcoreDkAppearanceTest::Backups.at(guidLow);

        target->SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID, uint8(skin));
        target->SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID, uint8(face));

        handler->PSendSysMessage(
            "Applied test DK appearance to {}: skin={}, face={}. Original backup: skin={}, face={}. This is NOT force-saved yet.",
            target->GetName(), skin, face, uint32(original.Skin), uint32(original.Face));
        handler->SendSysMessage("Use .hcdk save to persist for a relog test, or .hcdk restore to undo it.");
        HardcoreDkAppearanceTest::PrintAppearance(handler, target);
        return true;
    }

    static bool HandleRestoreCommand(ChatHandler* handler)
    {
        Player* target = HardcoreDkAppearanceTest::GetTarget(handler);
        if (!target)
        {
            return false;
        }

        uint32 guidLow = target->GetGUID().GetCounter();
        auto itr = HardcoreDkAppearanceTest::Backups.find(guidLow);
        if (itr == HardcoreDkAppearanceTest::Backups.end())
        {
            handler->SendErrorMessage("No HC DK appearance backup exists for this character in the current worldserver session.");
            return false;
        }

        target->SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID, itr->second.Skin);
        target->SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID, itr->second.Face);
        target->SaveToDB(false, false);

        handler->PSendSysMessage(
            "Restored and saved {} to original appearance: skin={}, face={}.",
            target->GetName(), uint32(itr->second.Skin), uint32(itr->second.Face));

        HardcoreDkAppearanceTest::Backups.erase(itr);
        HardcoreDkAppearanceTest::PrintAppearance(handler, target);
        return true;
    }

    static bool HandleSaveCommand(ChatHandler* handler)
    {
        Player* target = HardcoreDkAppearanceTest::GetTarget(handler);
        if (!target)
        {
            return false;
        }

        target->SaveToDB(false, false);
        handler->PSendSysMessage("Saved {}. You can now relog to test whether the DK skin/face persists on this class.", target->GetName());
        return true;
    }
};

namespace HardcoreHelper
{
    const uint32 DEFAULT_TOKEN_ID = 11100;

    bool GetHardcoreEnabledForPlayer(Player* player)
    {
        if (player->IsGameMaster() || (sConfigMgr->GetOption<bool>("ModHardcore.Enable", false) == false))
        {
            return false;
        }
        if ((sConfigMgr->GetOption<bool>("ModHardcore.AllowHardcorePlayerBots", false) == false) && player->GetSession()->IsBot())
        {
            return false;
        }

        if (player->GetLevel() >= sConfigMgr->GetOption<int>("ModHardcore.StartLevel", 1) &&
                player->GetLevel() <= sConfigMgr->GetOption<int>("ModHardcore.EndLevel", 80))
        {
            uint32 hcTokenItem = sConfigMgr->GetOption<int>("ModHardcore.TokenItemId", DEFAULT_TOKEN_ID);
            if (player->HasItemCount(hcTokenItem, 1, true))
            {
                return true;
            }
        
        }
        return false;
    }
}

class HardcoreMode : public PlayerScript
{
public:
    explicit HardcoreMode() : PlayerScript("mod_hardcore")
    {
    }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (sConfigMgr->GetOption<bool>("ModHardcore.Enable", false)) {
            uint32 hcTokenItem = sConfigMgr->GetOption<int>("ModHardcore.TokenItemId", HardcoreHelper::DEFAULT_TOKEN_ID);
            if (!player->HasItemCount(hcTokenItem, 1, true)) {
                player->AddItem(hcTokenItem, 1);
            }
            SendHardcoreStatus(player);
        }
    }

    void OnPlayerLogin(Player* player) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player))
        {
            if (player->isDead())
            {
                ChatHandler(player->GetSession()).PSendSysMessage("You died during hardcore session, so you are a permanent ghost.");
            }
        }
        if (sConfigMgr->GetOption<bool>("ModHardcore.Enable", false)) {
            SendHardcoreStatus(player);
        }
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldlevel*/) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player))
        {
            if (sConfigMgr->GetOption<bool>("ModHardcore.AnnounceLevelUp", false)) {
                int level = player->GetLevel();
                int endLevel = sConfigMgr->GetOption<int>("ModHardcore.EndLevel", 80);
                if (level == 10) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == 20) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == 30) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == 40) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == 50) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == 60 && level < endLevel) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == 70 && level < endLevel) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == 80 && level < endLevel) {
                    SendWorldAnnouncement(Acore::StringFormat("{} has made it to level {}!", player->GetName(), level));
                } else if (level == endLevel) {
                    SendWorldAnnouncement(Acore::StringFormat("{} knows no defeat at level {}! Congratulations!", player->GetName(), level));
                }
            }
            UpdateAchievementsAndTitles(player);
        }
    }



    void OnPlayerJustDied(Player* player) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player))
        {
            if (sConfigMgr->GetOption<bool>("ModHardcore.DestroyEquipmentOnDeath", true) && !player->GetSession()->IsBot()) {
                for (uint8 i = 0; i < EQUIPMENT_SLOT_END; ++i)
                {
                    if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    {
                        if (pItem->GetTemplate() && !pItem->IsEquipped())
                            continue;
                        uint8 slot = pItem->GetSlot();
                        ChatHandler(player->GetSession()).PSendSysMessage("|cffDA70D6You have lost your |cffffffff|Hitem:%d:0:0:0:0:0:0:0:0|h[%s]|h|r", pItem->GetEntry(), pItem->GetTemplate()->Name1.c_str());
                        player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                    }
                }
                player->SetMoney(0);
            }
            // Delete PLayerbot on Death
            if (sConfigMgr->GetOption<bool>("ModHardcore.AllowHardcorePlayerBots", false) && player->GetSession()->IsBot()) {
                player->GetSession()->LogoutPlayer(true);
                
                //Get world session
                // WorldSession* session = player->GetSession();
                // if (session) {
                //     WorldPacket packet;
                //     packet << player->GetGUID();    
                //     session->HandleCharDeleteOpcode(packet);
                //     LOG_ERROR("mod-hardcore", "Deleted player bot {}!", player->GetName());
                // }
            }
        }
    }

    void OnPlayerReleasedGhost(Player* player) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("You'll be a ghost forever...unless you find help");
            return;
        }
    }

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(killed))
        {
            ChatHandler(killed->GetSession()).PSendSysMessage("You have died in Hardcore mode.");
            if (killed->GetName() != killer->GetName())
            {
                SendWorldAnnouncement(Acore::StringFormat("Hardcore level {} player {} was killed in a dual to the death by {}!", killed->GetLevel(), killed->GetPlayerName(), killer->GetPlayerName()));
            }
        }
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(killed))
        {
            SendWorldAnnouncement(Acore::StringFormat("Hardcore level {} player {} was killed by {}!", killed->GetLevel(), killed->GetPlayerName(), killer->GetName()));
            ChatHandler(killed->GetSession()).PSendSysMessage("You have died in Hardcore mode.");
            return;
        }
    }

    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool& /*applySickness*/) override
    { // We keep this function just to prevent some exploits for reviving
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player))
        {
            if (!sConfigMgr->GetOption<bool>("ModHardcore.AllowPlayerRez", false) || !sConfigMgr->GetOption<bool>("ModHardcore.AllowSpiritRez", false)) {
                player->KillPlayer();
                return;
            }
            return;
        }
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& /*msg*/) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player) && player->isDead())
        {
            return false;
        }
        return true;
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
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player) && player->isDead())
        {
            ChatHandler(player->GetSession()).PSendSysMessage("You can't invite players to a group while dead.");
            return false;
        }
        return true;
    }

    bool OnPlayerCanGroupAccept(Player* player, Group* /*group*/) override
    {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player) && player->isDead())
        {
            ChatHandler(player->GetSession()).PSendSysMessage("You can't be a part of a group.");
            return false;
        }
        return true;
    }

private:

    bool CanUseChat(Player* player) {
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player) && player->isDead()) {
            if (sConfigMgr->GetOption<bool>("ModHardcore.DisableChatWhenDead", false)) {
                return false;
            }
        }
        return true;
    }

    void SendWorldAnnouncement(std::string message)
    {

        // Send localized messages to all sessions
        ChatHandler(nullptr).DoForAllValidSessions([&](Player* player)
        {
            // Check if there is a localized message if not use default one.
            if (message.empty())
                return;
            // Send the localized or fallback message
            ChatHandler(player->GetSession()).SendWorldTextOptional(message, ANNOUNCER_FLAG_DISABLE_AUTOBROADCAST);
        });

    }

    void SendHardcoreStatus(Player* player)
    {
        if (player->IsGameMaster() || player->GetSession()->IsBot() || !sConfigMgr->GetOption<bool>("ModHardcore.Enable", false))
        {
            return;
        }
        if (HardcoreHelper::GetHardcoreEnabledForPlayer(player))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Hardcore is enabled, good luck.");
        }
        else 
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Hardcore is disabled.");
        }
    }

    void UpdateAchievementsAndTitles(Player* player) {

        std::unordered_map<uint8, uint32>* achievementRewardMap = LoadStringToMap(sConfigMgr->GetOption<std::string>("ModHardcore.AchievementReward", ""));
        std::unordered_map<uint8, uint32>* titleRewardMap = LoadStringToMap(sConfigMgr->GetOption<std::string>("ModHardcore.TitleReward", ""));
        uint8 level = player->GetLevel();
    
        if (MapContainsKey(titleRewardMap, level))
        {
            CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleRewardMap->at(level));
            if (!titleInfo)
            {
                LOG_ERROR("mod-hardcore", "Invalid title ID {}!", titleRewardMap->at(level));
                return;
            }
            ChatHandler handler(player->GetSession());
            std::string tNameLink = handler.GetNameLink(player);
            std::string titleNameStr = Acore::StringFormat(player->getGender() == GENDER_MALE ? titleInfo->nameMale[handler.GetSessionDbcLocale()] : titleInfo->nameFemale[handler.GetSessionDbcLocale()], player->GetName());
            player->SetTitle(titleInfo);
        }

        if (MapContainsKey(achievementRewardMap, level))
        {
            uint32 achievementId = achievementRewardMap->at(level);
            AchievementEntry const* achievementInfo = sAchievementStore.LookupEntry(achievementId);
            if (!achievementInfo)
            {
                LOG_ERROR("mod-hardcore", "Invalid Achievement ID {}!", achievementId);
                return;
            }
    
            ChatHandler handler(player->GetSession());
            player->CompletedAchievement(achievementInfo);
        }
    }

    bool MapContainsKey(const std::unordered_map<uint8, uint32>* mapToCheck, uint8 key)
    {
        return (mapToCheck->find(key) != mapToCheck->end());
    }

    std::unordered_map<uint8, uint32>*  LoadStringToMap(const std::string &configString)
    {
        std::unordered_map<uint8, uint32>* map = new std::unordered_map<uint8, uint32>();
        std::string delimitedValue;
        std::stringstream configIdStream;
        configIdStream.str(configString);
        // Process each config ID in the string, delimited by the comma - "," and then space " "
        while (std::getline(configIdStream, delimitedValue, ','))
        {
            std::string pairOne, pairTwo;
            std::stringstream configPairStream(delimitedValue);
            configPairStream>>pairOne>>pairTwo;
            auto configLevel = atoi(pairOne.c_str());
            auto rewardValue = atoi(pairTwo.c_str());
            (*map)[configLevel] = rewardValue;
        }
        return map;
    }
};

class HardModeServerScript : ServerScript
{
public:
    HardModeServerScript() : ServerScript("mod_hardcore")
    {
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        if (!sConfigMgr->GetOption<bool>("ModHardcore.Enable", false))
        {
            return true;
        }

        if (!session)
        {
            return true;
        }

        auto player = session->GetPlayer();
        if (!player || !player->isDead() || player->IsGameMaster() || !HardcoreHelper::GetHardcoreEnabledForPlayer(player))
        {
            return true;
        }

        if (player->GetLevel() >= sConfigMgr->GetOption<int>("ModHardcore.StartLevel", 1) && 
        player->GetLevel() <= sConfigMgr->GetOption<int>("ModHardcore.EndLevel", 80))
        {
            auto opCode = packet.GetOpcode();
            switch (opCode)
            {
                case SMSG_PRE_RESURRECT: // No idea
                {
                    ChatHandler(player->GetSession()).PSendSysMessage("SMSG_PRE_RESURRECT.");
                    return false;
                }
                case CMSG_HEARTH_AND_RESURRECT: // No idea
                case CMSG_RECLAIM_CORPSE:  // Reviving going to corpse
                {
                    ChatHandler(player->GetSession()).PSendSysMessage("You cannot self resurrect in Hardcore mode. Travel as a ghost or find help.");
                    return false;
                }
                case SMSG_RESURRECT_REQUEST:
                case CMSG_RESURRECT_RESPONSE: // Someone reviving your corpse
                {
                    if (!sConfigMgr->GetOption<bool>("ModHardcore.AllowPlayerRez", false)) {
                        return false;
                    }
                    RezPlayer(player, RezBy::PLAYER);
                    return true;
                }
                case CMSG_SPIRIT_HEALER_ACTIVATE: // Spirit talking
                case SMSG_SPIRIT_HEALER_CONFIRM: // Spirit reviving, keep both spirit packets
                {
                    if (!sConfigMgr->GetOption<bool>("ModHardcore.AllowSpiritRez", false)) {
                        return false;
                    }
                    RezPlayer(player, RezBy::SPIRIT_HEALER);
                    return true;
                }
                case CMSG_GM_RESURRECT:
                {
                    if (!sConfigMgr->GetOption<bool>("ModHardcore.AllowGMRez", false))
                    {
                        return false;
                    }
                    RezPlayer(player, RezBy::GM);
                    return true;
                }
            }
        }

        return true;
    }
private:

    enum RezBy {
        PLAYER,
        SPIRIT_HEALER,
        GM
    };

    void RezPlayer(Player* player, RezBy rezBy) {
        if (rezBy == RezBy::GM) {
            if (sConfigMgr->GetOption<bool>("ModHardcore.MakeUndeadOnGMRez", false) == true) {
                ChatHandler(player->GetSession()).PSendSysMessage("If you accept being resurrected you will never be the same. Hardcore mode will be forfeited.");
                MakeUndead(player);
                DestroyHardcoreToken(player);
            } else {
                // Rez by GM does not reset hardcore mode. Use this in exceptional circumstances.
                ChatHandler(player->GetSession()).PSendSysMessage("Your life has been fully restored.");
            }
            return;
        }

        if (sConfigMgr->GetOption<bool>("ModHardcore.MakeUndeadOnRez", true) == true) {
            ChatHandler(player->GetSession()).PSendSysMessage("You have been resurrected. Hardcore mode is forfeit. You are forsaken.");
            MakeUndead(player);
        } else {
            ChatHandler(player->GetSession()).PSendSysMessage("You have been resurrected. Hardcore mode is forfeit.");
        }
        DestroyHardcoreToken(player);
    }

    void DestroyHardcoreToken(Player* player) {
        uint32 hcTokenItem = sConfigMgr->GetOption<int>("ModHardcore.TokenItemId", HardcoreHelper::DEFAULT_TOKEN_ID);
        player->DestroyItemCount(hcTokenItem, 1, true, true);
    }


    void MakeUndead(Player* player) {

        if (player->GetSession()->IsBot()) {
            return;
        }

        if (sConfigMgr->GetOption<bool>("ModHardcore.MakeUndeadOnRez", true) == false) {
            return;
        }

        if (player->getRace() == FORM_UNDEAD)
        {
            player->removeSpell(20579, SPEC_MASK_ALL, false);  // unlearn Shadow Resistance
        }
        else
        {
            RemoveAllRacials(player);
            AddUndeadRacials(player);
        }
        player->CastSpell(player, 20577); // Cannibalize.
        player->SetDrunkValue(128);
        // player->AddAura(SPELL_AURA_MOD_SHAPESHIFT, player);
    }

    void AddUndeadRacials(Player* player) {
        // Undead:
        player->learnSpell(20577, false); // Cannibalize
        //player->learnSpell(20579, false); // Shadow Resistance
        player->learnSpell(5227, false);  // Underwater Breating
        player->learnSpell(7744, false);  // Will of the Forsaken
    }

    // New Function - remove all existing racials
    void RemoveAllRacials(Player* player)
    {
        // Draenei:
       // player->removeSpell(28875, SPEC_MASK_ALL, false); // unlearn Gemcutting
        player->removeSpell(6562, SPEC_MASK_ALL, false);  // unlearn Heroic Presence (Warrior, Paladin, Hunter, DK)
        player->removeSpell(28878, SPEC_MASK_ALL, false); // unlearn Heroic Presence (Priest, Shaman)
        player->removeSpell(28880, SPEC_MASK_ALL, false); // unlearn Gift of Naaru (Warrior, increased by AP)
        player->removeSpell(59542, SPEC_MASK_ALL, false); // unlearn Gift of Naaru (Paladin, increase by AP or SP, whichever is higher)
        player->removeSpell(59543, SPEC_MASK_ALL, false); // unlearn Gift of Naaru (Hunter, increase by AP)
        player->removeSpell(59544, SPEC_MASK_ALL, false); // unlearn Gift of Naaru (Priest, increase by SP)
        player->removeSpell(59545, SPEC_MASK_ALL, false); // unlearn Gift of Naaru (DK, increased by AP)
        player->removeSpell(59547, SPEC_MASK_ALL, false); // unlearn Gift of Narru (Shaman, increase by AP or SP, whichever is higher)
        player->removeSpell(59548, SPEC_MASK_ALL, false); // unlearn Gift of Naaru (Mage, increase by SP)
        player->removeSpell(59221, SPEC_MASK_ALL, false); // unlearn Shadow Resistance (Warrior)
        player->removeSpell(59535, SPEC_MASK_ALL, false); // unlearn Shadow Resistance (Paladin)
        player->removeSpell(59536, SPEC_MASK_ALL, false); // unlearn Shadow Resistance (Hunter)
        player->removeSpell(59538, SPEC_MASK_ALL, false); // unlearn Shadow Resistance (Priest)
        player->removeSpell(59539, SPEC_MASK_ALL, false); // unlearn Shadow Resistance (DK)
        player->removeSpell(59540, SPEC_MASK_ALL, false); // unlearn Shadow Resistance (Shaman)
        player->removeSpell(59541, SPEC_MASK_ALL, false); // unlearn Shadow Resistance (Mage)

        // Dwarf:
        player->removeSpell(2481, SPEC_MASK_ALL, false);  // unlearn Find Treasure
        player->removeSpell(20596, SPEC_MASK_ALL, false); // unlearn Frost Resistance
        player->removeSpell(20595, SPEC_MASK_ALL, false); // unlearn Gun Specialization
        player->removeSpell(59224, SPEC_MASK_ALL, false); // unlearn Mace Specialization
        //player->removeSpell(20594, SPEC_MASK_ALL, false); // unlearn Stoneform

        // Gnome:
        player->removeSpell(20592, SPEC_MASK_ALL, false); // unlearn Arcane Resistance
        //player->removeSpell(20593, SPEC_MASK_ALL, false); // unlearn Engineering Specialization
        player->removeSpell(20589, SPEC_MASK_ALL, false); // unlearn Escape Artist
        player->removeSpell(20591, SPEC_MASK_ALL, false); // unlearn Expansive Mind

        // Human:
        player->removeSpell(20599, SPEC_MASK_ALL, false); // unlearn Diplomacy
        player->removeSpell(59752, SPEC_MASK_ALL, false); // unlearn Every man for himself
        player->removeSpell(20864, SPEC_MASK_ALL, false); // unlearn Mace Specialization
        //player->removeSpell(58985, SPEC_MASK_ALL, false); // unlearn Perception
        player->removeSpell(20597, SPEC_MASK_ALL, false); // unlearn Sword Specialization
        player->removeSpell(20598, SPEC_MASK_ALL, false); // unlearn The Human Spirit

        // Night Elf:
        player->removeSpell(20583, SPEC_MASK_ALL, false); // unlearn Nature Resistance
        player->removeSpell(20582, SPEC_MASK_ALL, false); // unlearn Quickness
        //player->removeSpell(58984, SPEC_MASK_ALL, false); // unlearn Shadowmeld
        player->removeSpell(20585, SPEC_MASK_ALL, false); // unlearn Wisp Spirit
        player->removeSpell(21009, SPEC_MASK_ALL, false); // unlearn Elusiveness

        // Orc:
        player->removeSpell(20574, SPEC_MASK_ALL, false); // unlearn Axe Specialization
        //player->removeSpell(20573, SPEC_MASK_ALL, false); // unlearn Hardiness
        player->removeSpell(54562, SPEC_MASK_ALL, false); // unlearn Command (DK)
        player->removeSpell(20576, SPEC_MASK_ALL, false); // unlearn Command (Hunter)
        player->removeSpell(65222, SPEC_MASK_ALL, false); // unlearn Command (Shaman)
        player->removeSpell(20575, SPEC_MASK_ALL, false); // unlearn Command (Warlock)
        player->removeSpell(21563, SPEC_MASK_ALL, false); // unlearn Command (non-pet class)
        player->removeSpell(20572, SPEC_MASK_ALL, false); // unlearn Blood Fury (AP only)
        player->removeSpell(33697, SPEC_MASK_ALL, false); // unlearn Blood fury (AP + SP)
        player->removeSpell(33702, SPEC_MASK_ALL, false); // unlearn Blood Fury (SP only)

        // Tauren:
        player->removeSpell(20552, SPEC_MASK_ALL, false); // unlearn Cultivation
        player->removeSpell(20550, SPEC_MASK_ALL, false); // unlearn Endurance
        player->removeSpell(20551, SPEC_MASK_ALL, false); // unlearn Nature Resistance
        //player->removeSpell(20549, SPEC_MASK_ALL, false); // unlearn Warstomp

        // Troll:
        player->removeSpell(20557, SPEC_MASK_ALL, false); // unlearn Beast Slaying
        player->removeSpell(26297, SPEC_MASK_ALL, false); // unlearn Berserking
        player->removeSpell(26290, SPEC_MASK_ALL, false); // unlearn Bow Specialization
        player->removeSpell(20558, SPEC_MASK_ALL, false); // unlearn Throwing Specialization
        //player->removeSpell(58943, SPEC_MASK_ALL, false); // unlearn Da Voodoo Shuffle
        player->removeSpell(20555, SPEC_MASK_ALL, false); // unlearn Regeneration

        // Undead:
        player->removeSpell(20577, SPEC_MASK_ALL, false); // unlearn Cannibalize
        player->removeSpell(20579, SPEC_MASK_ALL, false); // unlearn Shadow Resistance
        player->removeSpell(5227, SPEC_MASK_ALL, false);  // unlearn Underwater Breating
        //player->removeSpell(7744, SPEC_MASK_ALL, false);  // unlearn Will of the Forsaken

        // Blood Elf:
        //player->removeSpell(28877, SPEC_MASK_ALL, false); // unlearn Arcane Affinity
        player->removeSpell(28730, SPEC_MASK_ALL, false); // unlearn Arcane Torrent (mana)
        player->removeSpell(25046, SPEC_MASK_ALL, false); // unlearn Arcane Torrent (energy)
        player->removeSpell(50613, SPEC_MASK_ALL, false); // unlearn Arcane Torrent (runic power)
        player->removeSpell(822, SPEC_MASK_ALL, false);   // unlearn Magic Resistance
    }
};


void AddSC_mod_hardcore()
{
    new HardcoreMode();
    new HardModeServerScript();
    new HardcoreDkAppearanceCommands();
}
