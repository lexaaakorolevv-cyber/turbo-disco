#pragma once
#include <array>
class PlayLayer;

namespace autobot {
class InputDriver {
    PlayLayer* m_layer = nullptr;
    std::array<bool, 2> m_held{};
    bool m_injecting = false;
public:
    void attach(PlayLayer* layer) { m_layer = layer; m_held = {}; }
    void apply(std::array<bool, 2> held);
    void release();
    bool injecting() const { return m_injecting; }
};
}
