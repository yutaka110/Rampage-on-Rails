#pragma once

#include "utils/math/Vector.h"
#include <functional>
#include <utility>

// A single gameplay frame's view of the rendered stone and terrain.
class RailVisualOcclusion final {
public:
    using Query = std::function<bool(Vector3, Vector3)>;

    explicit RailVisualOcclusion(Query query = {}, Vector3 eye = {})
        : query_(std::move(query)), eye_(eye) {}

    bool Blocked(Vector3 eye, Vector3 target) const { return query_ && query_(eye, target); }
    bool FromCamera(Vector3 target) const { return Blocked(eye_, target); }

private:
    Query query_;
    Vector3 eye_;
};
