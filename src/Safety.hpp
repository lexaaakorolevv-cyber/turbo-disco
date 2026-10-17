#pragma once

namespace autobot::safety {
    // Checked before the first generated input. A missing/disabled safety hook
    // must prevent activation, rather than silently training without protection.
    bool ready();
}

namespace autobot {
    bool safetyAvailable();
}
