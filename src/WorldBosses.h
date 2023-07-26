#ifndef DEF_WORLDBOSSES_H
#define DEF_WORLDBOSSES_H

#include "Player.h"
#include "Config.h"

class WorldBosses
{
public:
    static WorldBosses* instance();

    bool IsEnabled{ false };
    [[nodiscard]] bool IsWorldBoss(uint32 entry);
};

#define sWorldBosses WorldBosses::instance()

#endif
