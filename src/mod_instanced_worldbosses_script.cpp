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
    SETTING_BOSS_STATUS,
    SETTING_ALLOW_LOOT
};

enum Phases
{
    PHASE_NORMAL = 1,
    PHASE_OUTRO  = 2
};

struct SaveData
{
    uint32 creatureId;
    ObjectGuid _ownerGuid;
};

const std::string ModInstancedBosses = "mod-instanced-worldbosses#";

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
                    case 14890: // Taerar
                    case 17711: // Doomwalker
                    case 18728: // Doom Lord Kazzak
                        if (Player* looter = ObjectAccessor::FindConnectedPlayer(player->GetGUID()))
                        {
                            uint32 currentTimer = looter->GetPlayerSetting(ModInstancedBosses + Acore::ToString(source.GetEntry()), SETTING_BOSS_TIME).value;

                            if (!looter->GetPlayerSetting(ModInstancedBosses + Acore::ToString(creature->GetEntry()), SETTING_BOSS_STATUS).value)
                            {
                                return false;
                            }

                            if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                            {
                                looter->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(source.GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                                looter->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(source.GetEntry()), SETTING_BOSS_STATUS, 1);
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

        std::vector<uint32> bossIds = { 6109, 12397, 14887, 14888, 14889, 14890, 17711, 18728 };

        for (auto token : bossIds)
        {
            if (uint32 currentTimer = player->GetPlayerSetting(ModInstancedBosses + Acore::ToString(token), SETTING_BOSS_TIME).value)
            {
                if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                {
                    player->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(token), SETTING_BOSS_TIME, 0);
                    player->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(token), SETTING_BOSS_STATUS, 0);
                    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(token))
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage("Your lock for %s has reset.", creature->Name);
                    }
                }
            }
        }
    }
};

class unit_worldbosses_script : public UnitScript
{
public:
    unit_worldbosses_script() : UnitScript("unit_worldbosses_script") { }

    void OnUnitEnterCombat(Unit* me, Unit* enemy) override
    {
        if (me->ToCreature() && IsWorldBoss(me->GetEntry()))
        {
            Player* player = enemy->ToPlayer();

            if (!player)
            {
                player = enemy->GetCharmerOrOwnerPlayerOrPlayerItself();
            }

            if (!player)
            {
                return;
            }

            _owner = player->GetGUID();

            if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.PhaseBosses", 0))
            {
                me->SetPhaseMask(PHASE_OUTRO, true);
                PhaseOutPlayers(player, PHASE_OUTRO, me->ToCreature());
            }

            SaveData data;
            data.creatureId = me->GetEntry();
            data._ownerGuid = _owner;
            saveData[me->GetEntry()] = data;
        }
    }

    void OnUnitEnterEvadeMode(Unit* me, uint8 /*evadeReason*/) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", 0))
        {
            return;
        }

        if (IsWorldBoss(me->GetEntry()))
        {
            if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.PhaseBosses", 0))
            {
                if (Player* player = ObjectAccessor::FindConnectedPlayer(_owner))
                    PhaseOutPlayers(player, PHASE_NORMAL, me->ToCreature());
            }

            me->SetPhaseMask(PHASE_NORMAL, true);

            _owner.Clear();

            saveData.erase(me->GetEntry());
        }
    }

    void OnUnitDeath(Unit* me, Unit* killer) override
    {
        if (!sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", 0))
        {
            return;
        }

        if (IsWorldBoss(me->GetEntry()))
        {
            me->ToCreature()->SetRespawnTime(sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.RespawnTimerSecs", HOUR));
            me->SaveRespawnTime();

            ObjectGuid playerGUID = saveData[me->GetEntry()]._ownerGuid;

            if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.PhaseBosses", 0))
            {
                if (Player* player = ObjectAccessor::FindConnectedPlayer(playerGUID))
                    PhaseOutPlayers(player, PHASE_NORMAL, me->ToCreature());
            }

            me->SetPhaseMask(PHASE_NORMAL, true);
            if (Player* player = ObjectAccessor::FindConnectedPlayer(playerGUID))
                LockPlayers(player, me->ToCreature());
        }
    }

    void LockPlayers(Player* source, Creature* me)
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

                if (uint32 currentTimer = groupGuy->GetPlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_TIME).value)
                {
                    if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                    {
                        groupGuy->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_TIME, 0);
                        groupGuy->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS, 0);
                        if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(me->GetEntry()))
                        {
                            ChatHandler(groupGuy->GetSession()).PSendSysMessage("Your lock for %s has reset.", creature->Name);
                        }
                    }
                }

                if (!groupGuy->GetPlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS).value)
                {
                    groupGuy->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                    groupGuy->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS, 1);
                    if (groupGuy->GetSession())
                    {
                        ChatHandler(groupGuy->GetSession()).PSendSysMessage("You are now locked to this boss (%s) and may not receive loot until the lock expires.", me->GetNameForLocaleIdx(groupGuy->GetSession()->GetSessionDbLocaleIndex()));
                    }
                }
            }
        }
        else
        {
            if (uint32 currentTimer = source->GetPlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_TIME).value)
            {
                if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                {
                    source->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_TIME, 0);
                    source->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS, 0);
                    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(me->GetEntry()))
                    {
                        ChatHandler(source->GetSession()).PSendSysMessage("Your lock for %s has reset.", creature->Name);
                    }
                }
            }

            if (!source->GetPlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS).value)
            {
                source->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                source->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS, 1);
                if (source->GetSession())
                {
                    ChatHandler(source->GetSession()).PSendSysMessage("You are now locked to this boss (%s) and may not receive loot until the lock expires.", me->GetNameForLocaleIdx(source->GetSession()->GetSessionDbLocaleIndex()));
                }
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
                    if (groupGuy->GetPlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS).value && phase == PHASE_OUTRO)
                    {
                        if (groupGuy->GetSession())
                        {
                            ChatHandler(groupGuy->GetSession()).PSendSysMessage("Phasing failed: you are already saved to this boss (%s).", me->GetNameForLocaleIdx(groupGuy->GetSession()->GetSessionDbLocaleIndex()));
                        }

                        continue;
                    }
                }

                groupGuy->SetPhaseMask(phase, true);

                if (!groupGuy->IsAlive() && phase == PHASE_NORMAL)
                {
                    if (Corpse* corpse = groupGuy->GetCorpse())
                    {
                        corpse->SetPhaseMask(PHASE_NORMAL, true);
                    }
                }

                if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Tuning", 0))
                {
                    HandleDebuffs(groupGuy, phase);
                }

                // This will lock loot and only allow those who are engaged to loot it.
                // groupGuy->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_ALLOW_LOOT, phase == PHASE_OUTRO ? 0 : 1);
            }
        }
        else
        {
            if (source->GetPlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_BOSS_STATUS).value && phase == PHASE_OUTRO)
            {
                if (source->GetSession())
                {
                    ChatHandler(source->GetSession()).PSendSysMessage("Phasing failed: you are already saved to this boss (%s).", me->GetNameForLocaleIdx(source->GetSession()->GetSessionDbLocaleIndex()));
                }

                return;
            }

            source->SetPhaseMask(phase, true);

            if (!source->IsAlive() && phase == PHASE_NORMAL)
            {
                if (Corpse* corpse = source->GetCorpse())
                {
                    corpse->SetPhaseMask(PHASE_NORMAL, true);
                }
            }

            if (sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Tuning", 0))
            {
                HandleDebuffs(source, phase);
            }

            // This will lock loot and only allow those who are engaged to loot it.
            // source->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_ALLOW_LOOT, phase == PHASE_OUTRO ? 0 : 1);
        }
    }

    void HandleDebuffs(Player* player, uint8 phase)
    {
        std::string items = sConfigMgr->GetOption<std::string>("ModInstancedWorldBosses.DebuffCastList", "");

        std::vector<std::string_view> tokens = Acore::Tokenize(items, ' ', false);

        for (auto token : tokens)
        {
            if (token.empty())
            {
                continue;
            }

            std::vector<std::string_view> itemData = Acore::Tokenize(token, ':', false);

            uint32 spellId = *Acore::StringTo<uint32>(itemData.at(0));
            int32 pct = *Acore::StringTo<int32>(itemData.at(1));

            if (phase == PHASE_OUTRO)
            {
                player->CastCustomSpell(spellId, SPELLVALUE_BASE_POINT0, pct, player);
            }
            else
            {
                player->RemoveAura(*Acore::StringTo<uint32>(itemData.at(0)));
            }
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
            case 14890: // Taerar
            case 17711: // Doomwalker
            case 18728: // Doom Lord Kazzak
                return true;
        }

        return false;
    }

    private:
        std::map<uint32, SaveData> saveData;
        ObjectGuid _owner;
};


void Addmod_instanced_worldbosses_script()
{
    new GlobalModInstancedBossesScript();
    new ModInstancedBossesPlayerScript();
    new unit_worldbosses_script();
}
