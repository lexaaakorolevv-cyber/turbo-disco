#include "../src/DeathCycle.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using deathcomment::DeathCycle;

namespace {

void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    std::vector<std::pair<char const*, std::function<void()>>> const tests{
        {"collision callback can observe a player already marked dead", [] {
            DeathCycle cycle;
            cycle.alive();
            // The native collision code sets its flag before our hook runs.
            bool const playerDeadAtHookEntry = true;
            require(cycle.claim(playerDeadAtHookEntry),
                    "a pre-marked real death must still produce one comment");
        }},
        {"collision, dual-player and post-update callbacks share one death", [] {
            DeathCycle cycle;
            cycle.alive();
            int comments = 0;
            if (cycle.claim(true)) ++comments; // First player's collision.
            if (cycle.claim(true)) ++comments; // Dual player's collision.
            if (cycle.claim(true)) ++comments; // Frame fallback.
            if (cycle.claim(true)) ++comments; // The next frame is still dead.
            require(comments == 1, "one death must not generate duplicate comments");
        }},
        {"noclip collision does not consume a later real death", [] {
            DeathCycle cycle;
            cycle.alive();
            require(!cycle.claim(false), "surviving a collision is not a death");
            require(!cycle.claim(false), "an alive frame must not report a death");
            require(cycle.claim(true), "the later real death must still be reportable");
        }},
        {"verification probe stays silent through fallback until revival", [] {
            DeathCycle cycle;
            cycle.alive();
            cycle.suppress();
            require(!cycle.claim(true), "verification collision must be silent");
            require(!cycle.claim(true), "frame fallback must not report the probe");
            require(!cycle.claim(true), "waiting a frame must not unsuppress the probe");
            cycle.alive();
            require(!cycle.claim(false), "observed revival must stay silent");
            require(cycle.claim(true), "a real death after revival must be reported");
        }},
        {"reset with stale dead flags cannot report a reset artifact", [] {
            DeathCycle cycle;
            cycle.alive();
            require(cycle.claim(true), "the real pre-reset death is reported");
            cycle.resetBoundary(true);
            require(!cycle.claim(true), "reset-time stale flag must stay silent");
            require(!cycle.claim(true), "frame fallback must not report reset state");
            cycle.alive();
            require(cycle.claim(true), "a revived player can die on the new attempt");
        }},
        {"checkpoint revival allows another death without a full reset", [] {
            DeathCycle cycle;
            cycle.alive();
            require(cycle.claim(true), "first practice death is reported");
            require(!cycle.claim(true), "first practice death is reported once");
            // Practice respawns at a checkpoint without resetBoundary().
            cycle.alive();
            require(!cycle.claim(false), "checkpoint revival is not a death");
            require(cycle.claim(true), "the next practice death is reported");
            require(!cycle.claim(true), "the next practice death is reported once");
        }},
        {"alive reset starts a fresh attempt after a suppressed probe", [] {
            DeathCycle cycle;
            cycle.suppress();
            require(!cycle.claim(true), "the old probe is suppressed");
            cycle.resetBoundary(false);
            require(!cycle.claim(false), "the reset itself is silent");
            require(cycle.claim(true), "a real death on the fresh attempt is reported");
        }},
        {"skipping display must not let frame fallback retry the same death", [] {
            DeathCycle cycle;
            cycle.alive();
            // The caller claims the death before cooldown/frequency filtering;
            // discarding the display must not queue the death for another hook.
            require(cycle.claim(true), "the filtered death is still consumed");
            require(!cycle.claim(true), "fallback must not retry the filtered death");
            cycle.alive();
            require(cycle.claim(true), "filtering one death must not consume the next");
        }}
    };

    int failed = 0;
    for (auto const& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        } catch (std::exception const& error) {
            ++failed;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - static_cast<std::size_t>(failed) << '/'
              << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
