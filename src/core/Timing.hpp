#pragma once
// The fixed simulation rate shared by every system.

namespace pm {

constexpr int kTickRate = 60;
constexpr float kTickDt = 1.0f / static_cast<float>(kTickRate);

} // namespace pm
