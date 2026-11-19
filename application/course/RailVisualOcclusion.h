#pragma once

#include "utils/math/Vector.h"
#include <functional>
#include <utility>

// A frame-scoped query of the actual rendered stone and terrain. Combat/lock
// systems share this visibility decision without owning renderer resources.
namespace RailVisualOcclusion {
using Query = std::function<bool(Vector3, Vector3)>;
inline thread_local const Query* current = nullptr;
inline thread_local Vector3 currentEye{};
class Scope final {
public:
    explicit Scope(Query query, Vector3 eye = {}) : query_(std::move(query)), previous_(current), previousEye_(currentEye) { current = &query_; currentEye = eye; }
    ~Scope() { current = previous_; currentEye = previousEye_; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Query query_;
    const Query* previous_;
    Vector3 previousEye_;
};
inline bool Blocked(Vector3 eye, Vector3 target) { return current && (*current)(eye, target); }
inline bool FromCamera(Vector3 target) { return Blocked(currentEye, target); }
}
