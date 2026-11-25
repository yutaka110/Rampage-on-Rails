#pragma once

#include "EnemyCombatSystem.h"

#include <functional>
#include <initializer_list>
#include <span>
#include <vector>

struct EnemyCombatEventBatch {
    std::span<const EnemyCombatEvent> events;
    bool shortCourse = false;
    bool postBossEnabled = false;
};

class EnemyCombatEventPublisher final {
public:
    using Observer = std::function<void(const EnemyCombatEventBatch&)>;

    explicit EnemyCombatEventPublisher(std::initializer_list<Observer> observers)
        : observers_(observers) {}

    EnemyCombatEventPublisher(const EnemyCombatEventPublisher&) = delete;
    EnemyCombatEventPublisher& operator=(const EnemyCombatEventPublisher&) = delete;

    // Deliver the frame's borrowed events synchronously in registration order.
    void Publish(const EnemyCombatEventBatch& batch) const {
        if (batch.events.empty()) return;
        for (const auto& observer : observers_) observer(batch);
    }

private:
    const std::vector<Observer> observers_;
};
