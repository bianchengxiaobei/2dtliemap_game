#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "game/WorldEventDirector.h"

namespace zl::ui {

inline constexpr int kWorldEventOptionRows = 8;
inline constexpr int kWorldEventLogRows = 6;

const char* WorldEventStageLabel(game::WorldEventStageKind kind);
std::string WorldEventCountdownText(std::int64_t minutes);
bool WorldEventCanChoose(const game::WorldEventDef& def,
                         const game::WorldEventInstance& event,
                         float playerX, float playerY);
std::string WorldEventChoiceLine(int index,
                                 const game::WorldEventChoiceDef& choice);
std::string WorldEventResultLine(const game::WorldEventDef& def,
                                 const game::WorldEventInstance& event,
                                 std::string_view choiceLabel,
                                 int reputationDelta, float xp,
                                 int pressureDelta, bool itemDropped,
                                 std::string_view item, int itemCount);

}  // namespace zl::ui
