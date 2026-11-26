#pragma once

#include "RailVehicleRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// A single shield deployment at the first enclosed section after the bridge.
// The staged rounds and any live projectile contacts use the same dome space.
class RailCavernShield final {
    static Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    static Vector3 Subtract(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static Vector3 Scale(Vector3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
    static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

public:
    static constexpr float Duration = 3.35f;
    static constexpr float Bottom = -.29f;
    static constexpr float EntryOffset = 28.0f;
    static constexpr int Around = 32, Up = 12;
    static constexpr int SurfaceVertices = Around * Up * 6;

    struct Pose {
        Vector3 center{}, right{}, up{}, forward{};
        static constexpr Vector3 Radius{3.85f, 4.25f, 5.1f};

        static Pose FromCart(const Matrix4x4& world) {
            const auto right = Normalize(Vector3{world.m[0][0], world.m[0][1], world.m[0][2]});
            const auto up = Normalize(Vector3{world.m[1][0], world.m[1][1], world.m[1][2]});
            const auto forward = Normalize(Vector3{world.m[2][0], world.m[2][1], world.m[2][2]});
            const Vector3 position{world.m[3][0], world.m[3][1], world.m[3][2]};
            return {Add(position, Scale(up, .94f)), right, up, forward};
        }

        Vector3 Point(Vector3 unit) const {
            return Add(center, ToWorld({unit.x * Radius.x, unit.y * Radius.y, unit.z * Radius.z}));
        }

        Vector3 Normal(Vector3 unit) const {
            return Normalize(ToWorld({unit.x / Radius.x, unit.y / Radius.y, unit.z / Radius.z}));
        }

        Vector3 ToWorld(Vector3 local) const {
            return Add(Add(Scale(right, local.x), Scale(up, local.y)), Scale(forward, local.z));
        }

        Vector3 ToUnit(Vector3 world) const {
            const auto delta = Subtract(world, center);
            return {Dot(delta, right) / Radius.x, Dot(delta, up) / Radius.y, Dot(delta, forward) / Radius.z};
        }
    };

    struct Round {
        float time;
        Vector3 unit;
    };

    // Alternating volleys strike the two shoulders that are visible from the chase camera.
    static constexpr std::array<Round, 12> Rounds{{
        {.43f, {-.85f, .39f, -.44f}}, {.54f, {-.49f, .57f, -.67f}},
        {.66f, { .72f, .44f, -.54f}}, {.77f, { .55f, .77f, -.33f}},
        {.96f, {-.74f, .56f, -.44f}}, {1.08f, {-.36f, .77f, -.53f}},
        {1.26f, { .86f, .29f, -.43f}}, {1.39f, { .59f, .57f, -.57f}},
        {1.58f, {-.88f, .26f, -.39f}}, {1.69f, {-.44f, .70f, -.56f}},
        {1.88f, { .79f, .37f, -.50f}}, {2.04f, { .39f, .64f, -.64f}},
    }};
    static constexpr float FlightSeconds = .185f;

    struct Impact {
        Vector3 unit{}, origin{}, normal{};
        float age = 10.0f;
        unsigned serial = 0;
    };

    void Reset() { *this = {}; }

    void Update(float dt, bool enter, const Pose& current, float speed) {
        if (dt <= 0.0f || age_ >= Duration || (!started_ && !enter)) return;
        previous_ = started_ ? pose_ : current;
        pose_ = current;
        started_ = true;
        const float before = age_;
        age_ = (std::min)(Duration, age_ + dt);
        for (auto& hit : impacts_) hit.age += dt;
        for (size_t index = 0; index < Rounds.size(); ++index) {
            const auto& round = Rounds[index];
            const float launch = round.time - FlightSeconds;
            const auto direction = Normalize(round.unit);
            if (before < launch && age_ >= launch) {
                const auto aim = pose_.Point(direction);
                const auto source = pose_.ToWorld({round.unit.x * 29.0f, 3.5f + round.unit.y * 5.0f, 5.0f});
                origins_[index] = Add(aim, Add(source, Scale(pose_.forward, speed * FlightSeconds)));
            }
            if (before < round.time && age_ >= round.time) Record(direction, age_ - round.time);
        }
    }

    bool Intersect(Vector3 before, Vector3 after, Vector3& unit) const {
        if (!Visible()) return false;
        const auto start = previous_.ToUnit(before);
        const auto delta = Subtract(pose_.ToUnit(after), start);
        const float a = Dot(delta, delta);
        const float b = Dot(start, delta);
        const float c = Dot(start, start) - 1.0f;
        const float discriminant = b * b - a * c;
        if (a < 1e-7f || c < 0.0f || b >= 0.0f || discriminant < 0.0f) return false;
        const float t = (-b - std::sqrt(discriminant)) / a;
        if (t < 0.0f || t > 1.0f) return false;
        unit = Normalize(Add(start, Scale(delta, t)));
        return unit.y >= Bottom;
    }

    void Record(Vector3 unit, float age = 0.0f) {
        impacts_[sequence_ % impacts_.size()] = {unit, pose_.Point(unit), pose_.Normal(unit), age, sequence_ + 1};
        ++sequence_;
    }

    bool Visible() const { return started_ && age_ < Duration; }
    float Age() const { return age_; }
    unsigned HitCount() const { return sequence_; }
    const Pose& CurrentPose() const { return pose_; }
    const auto& Impacts() const { return impacts_; }
    const auto& Origins() const { return origins_; }
    float Opacity() const { return Smooth(age_ / .24f) * (1.0f - Smooth((age_ - 2.48f) / .87f)); }

    template <class Emit>
    void DrawSurface(Vector3 eye, Emit emit) const {
        const auto vertex = [&](int x, int y) {
            const float u = float(x) / Around;
            const float v = float(y) / Up;
            const float azimuth = u * 6.2831853f;
            const float elevation = std::lerp(std::asin(Bottom), 1.5707963f, v);
            const Vector3 unit{std::cos(azimuth) * std::cos(elevation), std::sin(elevation),
                std::sin(azimuth) * std::cos(elevation)};
            const auto point = pose_.Point(unit);
            const float facing = Dot(pose_.Normal(unit), Normalize(Subtract(eye, point)));
            emit(point, Vector2{140.0f + u, v}, Vector4{facing, age_, 0, Opacity()});
        };
        for (int y = 0; y < Up; ++y) for (int x = 0; x < Around; ++x) {
            vertex(x, y); vertex(x + 1, y); vertex(x, y + 1);
            vertex(x, y + 1); vertex(x + 1, y); vertex(x + 1, y + 1);
        }
    }

    static float Smooth(float value) {
        const float t = (std::clamp)(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

private:
    bool started_ = false;
    float age_ = 0.0f;
    unsigned sequence_ = 0;
    Pose pose_{}, previous_{};
    std::array<Vector3, Rounds.size()> origins_{};
    std::array<Impact, 8> impacts_{};
};
