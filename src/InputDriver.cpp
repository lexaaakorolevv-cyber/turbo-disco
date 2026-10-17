#include "InputDriver.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/PlayerButtonCommand.hpp>
#include <algorithm>

namespace autobot {
void InputDriver::apply(std::array<bool, 2> held) {
    if (!m_layer) return;
    m_injecting = true;
    for (std::size_t i = 0; i < held.size(); ++i)
        if (held[i] != m_held[i])
            m_layer->handleButton(held[i], static_cast<int>(PlayerButton::Jump), i == 0);
    m_injecting = false;
    m_held = held;
}
void InputDriver::release() {
    if (!m_layer) { m_held = {}; return; }
    // Purge queued jump presses so that a paused down event cannot reassert
    // the bot's input after the switch has been turned off.
    auto& queued = m_layer->m_queuedButtons;
    queued.erase(std::remove_if(queued.begin(), queued.end(), [](auto const& command) {
        return command.m_button == PlayerButton::Jump;
    }), queued.end());
    m_injecting = true;
    for (unsigned i = 0; i < 2; ++i) {
        m_layer->handleButton(false, static_cast<int>(PlayerButton::Jump), i == 0);
        auto player = i == 0 ? m_layer->m_player1 : m_layer->m_player2;
        if (player) player->releaseButton(PlayerButton::Jump);
    }
    m_injecting = false;
    m_held = {};
}
}
