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
    SETTING_BOSS_TIME
};

class GlobalModInstancedBossesScript : public GlobalScript
{
public:
    GlobalModInstancedBossesScript() : GlobalScript("GlobalModInstancedBossesScript") { }

    bool OnAllowedForPlayerLootCheck(Player const* player, ObjectGuid source) override
    {
        if (source.IsCreature())
        {
            if (Creature* creature = ObjectAccessor::GetCreature(*player, source))
            {
                switch (creature->GetEntry())
                {
                    case 6109: // Azuregos
                    case 12397: // Lord Kazzak
                        if (Player* looter = ObjectAccessor::FindConnectedPlayer(player->GetGUID()))
                        {
                            uint32 currentTimer = looter->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(source.GetEntry()), SETTING_BOSS_TIME).value;

                            if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                            {
                                looter->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(source.GetEntry()), SETTING_BOSS_TIME, time(nullptr));
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
        std::vector<uint32> bossIds = { 6109, 12397 };

        for (auto token : bossIds)
        {
            if (uint32 currentTimer = player->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(token), SETTING_BOSS_TIME).value)
            {
                if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                {
                    player->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(token), SETTING_BOSS_TIME, 0);
                    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(token))
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage("Your lock for %s has reset.", creature->Name);
                    }
                }
            }
        }
    }

    void OnCreatureKill(Player* killer, Creature* creature) override
    {
        switch (creature->GetEntry())
        {
            case 6109: // Azuregos
            case 12397: // Lord Kazzak

                if (Group* group = killer->GetGroup())
                {
                    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                    {
                        if (Player* groupMember = itr->GetSource())
                        {
                            if (groupMember->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(creature->GetEntry()), SETTING_BOSS_TIME).value)
                                continue;

                            groupMember->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(creature->GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                            if (WorldSession* session = groupMember->GetSession())
                            {
                                ChatHandler(session).PSendSysMessage("You are now locked to this boss (%s) and may not receive loot until the lock expires.", creature->GetNameForLocaleIdx(session->GetSessionDbLocaleIndex()));
                            }
                        }
                    }
                }
                else
                {
                    if (killer->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(creature->GetEntry()), SETTING_BOSS_TIME).value)
                        return;

                    killer->UpdatePlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(creature->GetEntry()), SETTING_BOSS_TIME, time(nullptr));

                    if (WorldSession* session = killer->GetSession())
                    {
                        ChatHandler(session).PSendSysMessage("You are now locked to this boss (%s) and may not receive loot until the lock expires.", creature->GetNameForLocaleIdx(session->GetSessionDbLocaleIndex()));
                    }
                }
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
