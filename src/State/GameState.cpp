#include "State/GameState.h"

namespace GK {
    GameState* GameState::GetSingleton() {
        static GameState singleton;
        return &singleton;
    }

    void GameState::Reset() {
        auto lock = Lock();
        _actors.Clear();
        _labyrinths.Clear();
        _resources.Clear();
        _aliasReservations.clear();
        // _keywords intentionally retained (live session config, not save state).
    }
}
