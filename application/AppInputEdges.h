#pragma once
#include <array>
#include <cstddef>

// Preserve presses completed between rendered frames, delivered once only.
// Held input continues to use the platform poll.
class AppInputEdges final {
public:
    void Record(int key, bool down) {
        if (key < 0 || key >= 256) return;
        const auto index = static_cast<std::size_t>(key);
        if (down && !held_[index]) pending_[index] = true;
        held_[index] = down;
    }
    void BeginFrame() { frame_ = pending_; pending_.fill(false); }
    bool Pressed(int key) const { return key >= 0 && key < 256 && frame_[static_cast<std::size_t>(key)]; }
    bool Consume(int key) {
        const bool pressed = Pressed(key);
        if (key >= 0 && key < 256) frame_[static_cast<std::size_t>(key)] = false;
        return pressed;
    }
    void Reset() { held_.fill(false); pending_.fill(false); frame_.fill(false); }
private:
    std::array<bool, 256> held_{}, pending_{}, frame_{};
};
