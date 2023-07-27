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
#include "WorldBosses.h"

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
    GuidSet groupPlayerGUIDS;
};

WorldBosses* WorldBosses::instance()
{
    static WorldBosses instance;
    return &instance;
}

bool WorldBosses::IsWorldBoss(uint32 entry)
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

bool WorldBosses::IsPlayerSaved(Player* player, uint32 entry)
{
    return player->GetPlayerSetting(sWorldBosses->GetSettingSourceStr(entry), SETTING_BOSS_STATUS).value;
}

class mod_instanced_worldbosses_worldscript : public WorldScript
{
public:
    mod_instanced_worldbosses_worldscript() : WorldScript("mod_instanced_worldbosses_worldscript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sWorldBosses->IsEnabled = sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Enable", false);
        sWorldBosses->IsPhasingEnabled = sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.PhaseBosses", false);
        sWorldBosses->IsTuningEnabled = sConfigMgr->GetOption<bool>("ModInstancedWorldBosses.Tuning", false);
    }
};

class GlobalModInstancedBossesScript : public GlobalScript
{
public:
    GlobalModInstancedBossesScript() : GlobalScript("GlobalModInstancedBossesScript") { }

    bool OnAllowedForPlayerLootCheck(Player const* player, ObjectGuid source) override
    {
        if (!sWorldBosses->IsEnabled)
        {
            return false;
        }

        if (source.IsCreature())
        {
            if (Creature* creature = ObjectAccessor::GetCreature(*player, source))
            {
                if (sWorldBosses->IsWorldBoss(creature->GetEntry()))
                {
                    if (Player* looter = ObjectAccessor::FindConnectedPlayer(player->GetGUID()))
                    {
                        uint32 currentTimer = looter->GetPlayerSetting(sWorldBosses->GetSettingSourceStr(source.GetEntry()), SETTING_BOSS_TIME).value;

                        if (!sWorldBosses->IsPlayerSaved(looter, source.GetEntry()))
                        {
                            return false;
                        }

                        if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                        {
                            looter->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(source.GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                            looter->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(source.GetEntry()), SETTING_BOSS_STATUS, 1);
                            return false;
                        }

                        return time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.RespawnTimerSecs", HOUR));
                    }
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
        if (!sWorldBosses->IsEnabled)
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
                    player->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(token), SETTING_BOSS_TIME, 0);
                    player->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(token), SETTING_BOSS_STATUS, 0);
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
        if (!sWorldBosses->IsEnabled)
        {
            return;
        }

        if (me->ToCreature() && sWorldBosses->IsWorldBoss(me->GetEntry()))
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

            if (sWorldBosses->IsPhasingEnabled)
            {
                me->SetPhaseMask(PHASE_OUTRO, true);
                PhaseOutPlayers(player, PHASE_OUTRO, me->ToCreature());
            }

            SaveData data;
            data.creatureId = me->GetEntry();
            data._ownerGuid = player->GetGUID();
            saveData[me->GetEntry()] = data;
        }
    }

    void OnUnitEnterEvadeMode(Unit* me, uint8 /*evadeReason*/) override
    {
        if (!sWorldBosses->IsEnabled)
        {
            return;
        }

        if (sWorldBosses->IsWorldBoss(me->GetEntry()))
        {
            if (sWorldBosses->IsPhasingEnabled)
            {
                if (Player* player = ObjectAccessor::FindConnectedPlayer(saveData[me->GetEntry()]._ownerGuid))
                    PhaseOutPlayers(player, PHASE_NORMAL, me->ToCreature());
            }

            me->SetPhaseMask(PHASE_NORMAL, true);

            saveData.erase(me->GetEntry());
        }
    }

    void OnUnitDeath(Unit* me, Unit* killer) override
    {
        if (!sWorldBosses->IsEnabled)
        {
            return;
        }

        if (sWorldBosses->IsWorldBoss(me->GetEntry()))
        {
            me->ToCreature()->SetRespawnTime(sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.RespawnTimerSecs", HOUR));
            me->SaveRespawnTime();

            ObjectGuid playerGUID = saveData[me->GetEntry()]._ownerGuid;

            if (sWorldBosses->IsPhasingEnabled)
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

                if (uint32 currentTimer = groupGuy->GetPlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_TIME).value)
                {
                    if (time(nullptr) >= (currentTimer + sConfigMgr->GetOption<uint32>("ModInstancedWorldBosses.ResetTimerSecs", 259200))) // 3 days
                    {
                        groupGuy->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_TIME, 0);
                        groupGuy->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_STATUS, 0);
                        if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(me->GetEntry()))
                        {
                            ChatHandler(groupGuy->GetSession()).PSendSysMessage("Your lock for %s has reset.", creature->Name);
                        }
                    }
                }

                if (!sWorldBosses->IsPlayerSaved(groupGuy, me->GetEntry()))
                {
                    groupGuy->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                    groupGuy->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_STATUS, 1);
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
                    source->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_TIME, 0);
                    source->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_STATUS, 0);
                    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(me->GetEntry()))
                    {
                        ChatHandler(source->GetSession()).PSendSysMessage("Your lock for %s has reset.", creature->Name);
                    }
                }
            }

            if (!sWorldBosses->IsPlayerSaved(source, me->GetEntry()))
            {
                source->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_TIME, time(nullptr));
                source->UpdatePlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_STATUS, 1);
                if (source->GetSession())
                {
                    ChatHandler(source->GetSession()).PSendSysMessage("You are now locked to this boss (%s) and may not receive loot until the lock expires.", me->GetNameForLocaleIdx(source->GetSession()->GetSessionDbLocaleIndex()));
                }
            }
        }
    }

    void PhaseOutPlayers(Player* source, uint8 phase, Creature* me)
    {
        GuidSet groupPlayerGUIDS;

        if (Group* group = source->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* groupGuy = itr->GetSource();

                if (!groupGuy)
                {
                    continue;
                }

                saveData[me->GetEntry()].groupPlayerGUIDS.insert(groupGuy->GetGUID());

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
                    if (groupGuy->GetPlayerSetting(sWorldBosses->GetSettingSourceStr(me->GetEntry()), SETTING_BOSS_STATUS).value && phase == PHASE_OUTRO)
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

                if (sWorldBosses->IsTuningEnabled)
                {
                    HandleDebuffs(groupGuy, phase);
                }

                // This will lock loot and only allow those who are engaged to loot it.
                // groupGuy->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_ALLOW_LOOT, phase == PHASE_OUTRO ? 0 : 1);
            }
        }
        else
        {
            if (sWorldBosses->IsPlayerSaved(source, me->GetEntry()))
            {
                me->RemoveAllowedLooter(source->GetGUID());

                if (source->GetSession() && phase == PHASE_OUTRO)
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

            if (sWorldBosses->IsTuningEnabled)
            {
                HandleDebuffs(source, phase);
            }

            // This will lock loot and only allow those who are engaged to loot it.
            // source->UpdatePlayerSetting(ModInstancedBosses + Acore::ToString(me->GetEntry()), SETTING_ALLOW_LOOT, phase == PHASE_OUTRO ? 0 : 1);
        }

        if (sWorldBosses->IsTuningEnabled)
        {
            for (ObjectGuid guid : saveData[me->GetEntry()].groupPlayerGUIDS)
            {
                if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                {
                    HandleDebuffs(player, phase);
                }
            }
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
                // The auras can stack...
                if (!player->HasAura(spellId))
                {
                    player->CastCustomSpell(spellId, SPELLVALUE_BASE_POINT0, pct, player);
                }
            }
            else
            {
                player->RemoveAura(*Acore::StringTo<uint32>(itemData.at(0)));
            }
        }
    }

    private:
        std::map<uint32, SaveData> saveData;
};


void Addmod_instanced_worldbosses_script()
{
    new GlobalModInstancedBossesScript();
    new ModInstancedBossesPlayerScript();
    new unit_worldbosses_script();
}
