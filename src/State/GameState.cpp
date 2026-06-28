#include "State/GameState.h"

namespace GK {
    GameState* GameState::GetSingleton() {
        static GameState singleton;
        return &singleton;
    }

    void GameState::Reset() {
        auto lock = Lock();
        _actors.Clear();
    }
}
