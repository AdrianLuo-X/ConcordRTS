#pragma once

// Autonomous actors keep the same command/lifecycle rules as player units,
// but their selection is not limited by the local player's screen visibility.
class RuntimeAutonomousOrderScope {
public:
    explicit RuntimeAutonomousOrderScope(bool& active) : active_(active), saved_(active) {
        active_ = true;
    }
    ~RuntimeAutonomousOrderScope() { active_ = saved_; }
    RuntimeAutonomousOrderScope(const RuntimeAutonomousOrderScope&) = delete;
    RuntimeAutonomousOrderScope& operator=(const RuntimeAutonomousOrderScope&) = delete;
private:
    bool& active_;
    bool saved_;
};
