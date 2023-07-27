#ifndef DEF_WORLDBOSSES_H
#define DEF_WORLDBOSSES_H

#include "Player.h"
#include "Config.h"

const std::string ModInstancedBosses = "mod-instanced-worldbosses#";

class WorldBosses
{
public:
    static WorldBosses* instance();

    bool IsEnabled{ false };
    bool IsPhasingEnabled{ false };
    [[nodiscard]] bool IsWorldBoss(uint32 entry);
    [[nodiscard]] std::string GetSettingSourceStr(uint32 entry) { return ModInstancedBosses + Acore::ToString(entry); };
};

#define sWorldBosses WorldBosses::instance()

#endif
