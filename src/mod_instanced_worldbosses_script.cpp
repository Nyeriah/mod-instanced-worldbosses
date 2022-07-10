/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "Group.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "Tokenize.h"

enum Settings
{
    SETTING_BOSS_TIME,
    SETTING_BOSS_STATUS
};

class GlobalModInstancedBossesScript : public GlobalScript
{
public:
    GlobalModInstancedBossesScript() : GlobalScript("GlobalModInstancedBossesScript") { }

    bool OnAllowedForPlayerLootCheck(Player const* player, ObjectGuid source) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", 0))
        {
            return false;
        }

        if (source.IsCreature())
        {
            if (Creature* creature = ObjectAccessor::GetCreature(*player, source))
            {
                switch (creature->GetEntry())
                {
                    case 6109: // Azuregos
                    case 12397: // Lord Kazzak
                    case 14888: // Lethon
                    case 14889: // Emeriss
                    case 14890: // Tauerar
                        if (Player* looter = ObjectAccessor::FindConnectedPlayer(player->GetGUID()))
                        {
                            uint32 currentTimer = looter->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(source.GetEntry()), SETTING_BOSS_TIME).value;

                            if (!looter->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(creature->GetEntry()), SETTING_BOSS_STATUS).value)
                            {
                                looter->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(creature->GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                                looter->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(creature->GetEntry()), SETTING_BOSS_STATUS, 1);
                                if (looter->GetSession())
                                {
                                    ChatHandler(looter->GetSession()).PSendSysMessage("You are now locked to this boss (%s) and may not receive loot until the lock expires.", creature->GetNameForLocaleIdx(looter->GetSession()->GetSessionDbLocaleIndex()));
                                }

                                return false;
                            }

                            if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                            {
                                looter->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(source.GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                                looter->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(source.GetEntry()), SETTING_BOSS_STATUS, 1);
                                return false;
                            }

                            return time(nullptr) >= (currentTimer + creature->GetRespawnDelay());
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        return false;
    }
};

class ModInstancedBossesPlayerScript : public PlayerScript
{
public:
    ModInstancedBossesPlayerScript() : PlayerScript("ModInstancedBossesPlayerScript") { }

    void OnLogin(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", 0))
        {
            return;
        }

        std::vector<uint32> bossIds = { 6109, 12397, 14888, 14889, 14890 };

        for (auto token : bossIds)
        {
            if (uint32 currentTimer = player->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(token), SETTING_BOSS_TIME).value)
            {
                if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                {
                    player->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(token), SETTING_BOSS_TIME, 0);
                    player->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(token), SETTING_BOSS_STATUS, 0);
                    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(token))
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage("Your lock for %s has reset.", creature->Name);
                    }
                }
            }
        }
    }

    void OnCreatureKill(Player* /*killer*/, Creature* creature) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", 0))
        {
            return;
        }

        switch (creature->GetEntry())
        {
            case 6109: // Azuregos
            case 12397: // Lord Kazzak
            case 14888: // Lethon
            case 14889: // Emeriss
            case 14890: // Tauerar
                creature->SetRespawnTime(sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.RespawnTimerSecs", HOUR));
                creature->SaveRespawnTime();
                break;
            default:
                break;
        }
    }
};


void Addmod_instanced_worldbosses_script()
{
    new GlobalModInstancedBossesScript();
    new ModInstancedBossesPlayerScript();
}
