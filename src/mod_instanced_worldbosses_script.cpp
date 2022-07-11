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

enum Phases
{
    PHASE_NORMAL = 1,
    PHASE_OUTRO  = 2
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
                    case 14887: // Ysondre
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

                            return time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.RespawnTimerSecs", HOUR));
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

        std::vector<uint32> bossIds = { 6109, 12397, 14887, 14888, 14889, 14890 };

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

    void OnPlayerEnterCombat(Player* player, Unit* enemy) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.PhaseBosses", 0))
        {
            return;
        }

        if (enemy->ToCreature() && IsWorldBoss(enemy->GetEntry()))
        {
            enemy->SetPhaseMask(PHASE_OUTRO, true);
            PhaseOutPlayers(player, PHASE_OUTRO, enemy->ToCreature());
        }
    }

    void OnCreatureKill(Player* killer, Creature* creature) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", 0))
        {
            return;
        }

        if (IsWorldBoss(creature->GetEntry()))
        {
            creature->SetRespawnTime(sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.RespawnTimerSecs", HOUR));
            creature->SaveRespawnTime();

            if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.PhaseBosses", 0))
            {
                creature->SetPhaseMask(PHASE_NORMAL, true);
                PhaseOutPlayers(killer, PHASE_NORMAL, creature);
            }
        }
    }

    void OnCreatureKilledByPet(Player* petOwner, Creature* creature) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", 0))
        {
            return;
        }

        if (IsWorldBoss(creature->GetEntry()))
        {
            creature->SetRespawnTime(sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.RespawnTimerSecs", HOUR));
            creature->SaveRespawnTime();

            if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.PhaseBosses", 0))
            {
                creature->SetPhaseMask(PHASE_NORMAL, true);
                PhaseOutPlayers(petOwner, PHASE_NORMAL, creature);
            }
        }
    }

    void PhaseOutPlayers(Player* source, uint8 phase, Creature* me)
    {
        if (Group* group = source->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* groupGuy = itr->GetSource();
                if (!groupGuy)
                {
                    continue;
                }

                if (!groupGuy->IsInWorld())
                {
                    continue;
                }

                if (!groupGuy->IsWithinDist(me, 500.0f))
                {
                    continue;
                }

                if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.SkipSavedPlayersWhenPhasing", 0))
                {
                    if (groupGuy->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS).value && phase == PHASE_OUTRO)
                    {
                        continue;
                    }
                }

                groupGuy->SetPhaseMask(phase, true);
            }
        }
        else
        {
            if (source->GetPlayerSetting("mod-instanced-worldbosses#" + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS).value && phase == PHASE_OUTRO)
            {
                return;
            }

            source->SetPhaseMask(phase, true);
        }
    }

    bool IsWorldBoss(uint32 entry)
    {
        switch (entry)
        {
            case 6109: // Azuregos
            case 12397: // Lord Kazzak
            case 14887: // Ysondre
            case 14888: // Lethon
            case 14889: // Emeriss
            case 14890: // Tauerar
                return true;
        }

        return false;
    }
};


void Addmod_instanced_worldbosses_script()
{
    new GlobalModInstancedBossesScript();
    new ModInstancedBossesPlayerScript();
}
