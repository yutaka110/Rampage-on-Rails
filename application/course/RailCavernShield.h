#pragma once

#include "RailVehicleRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// The cave entrance unlocks a rechargeable shield against live projectile contacts.
class RailCavernShield final {
    static Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    static Vector3 Subtract(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static Vector3 Scale(Vector3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
    static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vector3 Cross(Vector3 a, Vector3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

public:
    static constexpr int MaxHealth = 100;
    static constexpr float Bottom = -.29f;
    static constexpr float EntryOffset = 28.0f;
    static constexpr float PromptOffset = 18.0f;
    static constexpr float FadeDuration = .64f;
    static constexpr float DeployDuration = .64f;
    static constexpr float BreakDuration = 1.16f;
    static constexpr float RechargeDuration = 5.0f;
    static constexpr float BreakHoldDuration = .25f;
    static constexpr float CrackSpeed = 11.5f;
    static constexpr int Around = 32, Up = 12;
    static constexpr int SurfaceVertices = Around * Up * 6;
    static constexpr int InnerSurfaceStep = 2;
    static constexpr int InnerSurfaceVertices = Around * (Up / InnerSurfaceStep) * 6;

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

    struct Contact {
        Vector3 unit{}, worldPosition{}, incoming{};
    };

    struct Impact {
        Vector3 unit{}, origin{}, normal{}, incoming{};
        float age = 10.0f;
        unsigned serial = 0;
    };

    void Reset() { *this = {}; }

    void Update(float dt, bool unlocked, bool deploy, const Pose& current) {
        if (dt <= 0.0f) return;
        unlocked_ = unlocked;
        rechargeReadyAge_ = (std::min)(10.0f, rechargeReadyAge_ + dt);
        if (started_) {
            previous_ = pose_;
            pose_ = current;
            poseStep_ = dt;
            age_ += dt;
            for (auto& hit : impacts_) hit.age += dt;
            const auto* impact = RecentImpact();
            if (!unlocked_ || Broken() || fade_ > 0.0f || (impact && impact->age > .80f))
                fade_ = (std::min)(FadeDuration, fade_ + dt);
            const bool restored = Broken() && BreakImpact().age >= RechargeDuration - .0001f;
            if (fade_ >= FadeDuration && (!Broken() || restored)) {
                started_ = false;
                age_ = fade_ = 0.0f;
                impacts_ = {};
                if (restored) {
                    health_ = MaxHealth;
                    rechargeReadyAge_ = 0.0f;
                }
                preHitHealth_ = health_;
            }
        }
        if (Available() && deploy) {
            started_ = true;
            previous_ = pose_ = current;
            poseStep_ = dt;
            age_ = dt;
            rechargeReadyAge_ = 10.0f;
        }
    }

    bool Intersect(Vector3 before, Vector3 after, Contact& contact) const {
        if (!Active()) return false;
        const auto start = previous_.ToUnit(before);
        const auto delta = Subtract(pose_.ToUnit(after), start);
        const float a = Dot(delta, delta);
        const float b = Dot(start, delta);
        const float c = Dot(start, start) - 1.0f;
        const float discriminant = b * b - a * c;
        if (a < 1e-7f || c < 0.0f || b >= 0.0f || discriminant < 0.0f) return false;
        const float t = (-b - std::sqrt(discriminant)) / a;
        if (t < 0.0f || t > 1.0f) return false;
        const auto unit = Normalize(Add(start, Scale(delta, t)));
        if (unit.y < Bottom) return false;
        const auto travel = Subtract(after, before);
        const auto normal = pose_.Normal(unit);
        const auto incoming = Dot(travel, travel) > 1e-7f ? Normalize(travel) : Scale(normal, -1.0f);
        contact = {unit, Add(before, Scale(travel, t)), incoming};
        return true;
    }

    void Record(const Contact& contact, float damage) {
        if (!Active() || damage <= 0.0f) return;
        const auto* recent = RecentImpact();
        if (!recent || recent->age > 0.0f) preHitHealth_ = health_;
        impacts_[sequence_ % impacts_.size()] = {
            contact.unit, contact.worldPosition, pose_.Normal(contact.unit), contact.incoming, 0.0f, sequence_ + 1};
        ++sequence_;
        health_ = (std::max)(0.0f, health_ - damage);
        if (Broken()) {
            breakPose_ = pose_;
            breakVelocity_ = Scale(Subtract(pose_.center, previous_.center), 1.0f / poseStep_);
        }
    }

    bool Unlocked() const { return unlocked_; }
    bool Available() const { return unlocked_ && !started_; }
    bool Active() const { return started_ && fade_ == 0.0f && health_ > 0.0f; }
    bool Broken() const { return health_ <= 0.0f; }
    bool Breaking() const { return Broken() && BreakImpact().age < BreakDuration; }
    bool Visible() const { return started_ && (fade_ < FadeDuration || Breaking()); }
    float Age() const { return age_; }
    int Health() const { return static_cast<int>(std::ceil(health_)); }
    float HealthRatio() const { return health_ / MaxHealth; }
    float PreHitRatio() const { return preHitHealth_ / MaxHealth; }
    float HitAge() const { const auto* hit = RecentImpact(); return hit ? hit->age : 10.0f; }
    float RechargeRatio() const {
        return Broken() ? (std::clamp)((BreakImpact().age - BreakHoldDuration) /
            (RechargeDuration - BreakHoldDuration), 0.0f, 1.0f) : 0.0f;
    }
    float RechargeReadyAge() const { return rechargeReadyAge_; }
    unsigned HitCount() const { return sequence_; }
    const Pose& CurrentPose() const { return pose_; }
    const auto& Impacts() const { return impacts_; }
    const Impact& BreakImpact() const { return impacts_[(sequence_ - 1) % impacts_.size()]; }
    Pose BreakPose(float seconds) const {
        auto pose = breakPose_;
        pose.center = Add(pose.center, Scale(breakVelocity_, seconds));
        return pose;
    }
    float PanelReleaseTime(Vector3 unit) const {
        const float alignment = (std::clamp)(Dot(BreakImpact().unit, unit), -1.0f, 1.0f);
        return .076f + std::acos(alignment) / CrackSpeed;
    }

    float Opacity() const { return Smooth(age_ / .24f) * (1.0f - Smooth(fade_ / FadeDuration)); }
    float MembraneOpacity() const {
        if (Broken()) return 1.0f - Smooth((BreakImpact().age - .24f) / .15f);
        const auto* impact = RecentImpact();
        const float afterglow = impact ? 1.0f - .87f * Smooth((impact->age - .46f) / .35f) : 1.0f;
        return Smooth(age_ / .24f) * afterglow * (1.0f - Smooth(fade_ / .15f));
    }

    template <class Emit>
    void DrawSurface(Vector3 eye, bool allowInnerSurface, Emit emit) const {
        struct Wave {
            Vector3 contact;
            float outerCos, crestCos, inverseFront, inverseWake, strength;
        };
        std::array<Wave, std::tuple_size_v<decltype(impacts_)>> waves;
        size_t waveCount = 0;
        constexpr float duration = .52f;
        for (const auto& hit : impacts_) {
            if (hit.age <= 0.0f || hit.age >= duration) continue;
            const float radius = (std::min)(2.76f, .07f + hit.age * 7.1f);
            const float outer = std::cos(radius + .29f);
            const float crest = std::cos(radius);
            const float inner = std::cos((std::max)(0.0f, radius - .79f));
            const float strength = Smooth(hit.age / .054f) * (1.0f - Smooth((hit.age - .23f) / (duration - .23f)));
            waves[waveCount++] = {hit.unit, outer, crest, 1.0f / (crest - outer), 1.0f / (inner - crest), strength * .86f};
        }

        // A broad crest and wake travel along the shield from each contact.
        struct Vertex {
            Vector3 point;
            Vector2 uv;
            Vector4 color;
            Vector3 innerPoint;
            float innerFacing;
        };
        std::array<std::array<Vertex, Up + 1>, Around + 1> surface;
        const float opacity = MembraneOpacity();
        const bool broken = Broken();
        const float breakAge = broken ? BreakImpact().age : 0.0f;
        const auto surfacePose = broken ? BreakPose(breakAge) : pose_;
        const float visualAge = broken ? (std::max)(age_, DeployDuration + .075f) : age_;
        // Share the vertex channel with the hit response. Idle motion is negative
        // and only returns when the last impact wave has passed.
        const float idle = Active() && waveCount == 0
            ? Smooth((age_ - DeployDuration - .075f) / .22f) * Smooth((HitAge() - .50f) / .26f)
            : 0.0f;
        const float innerOpacity = opacity * Smooth((age_ - DeployDuration - .075f) / .22f) * std::lerp(.52f, 1.0f, idle);
        const bool drawInner = allowInnerSurface && !broken && innerOpacity > .001f;
        for (int x = 0; x <= Around; ++x) {
            const float u = float(x) / Around;
            const float azimuth = u * 6.2831853f;
            const float right = std::cos(azimuth);
            const float forward = std::sin(azimuth);
            for (int y = 0; y <= Up; ++y) {
                const float v = float(y) / Up;
                const float elevation = std::lerp(std::asin(Bottom), 1.5707963f, v);
                const float horizontal = std::cos(elevation);
                const Vector3 unit{right * horizontal, std::sin(elevation), forward * horizontal};
                const auto point = surfacePose.Point(unit);
                const auto normal = surfacePose.Normal(unit);
                const float facing = Dot(normal, Normalize(Subtract(eye, point)));
                float light = 0.0f;
                for (size_t i = 0; i < waveCount; ++i) {
                    const auto& wave = waves[i];
                    const float alignment = Dot(unit, wave.contact);
                    const float front = Smooth((alignment - wave.outerCos) * wave.inverseFront);
                    const float wake = 1.0f - Smooth((alignment - wave.crestCos) * wave.inverseWake);
                    light += (1.0f - light) * (front * wake * wave.strength);
                }
                const float panelOpacity = broken ? 1.0f - Smooth((breakAge - PanelReleaseTime(unit)) / .068f) : 1.0f;
                auto& sample = surface[x][y];
                sample.point = point;
                sample.uv = {u, v};
                sample.color = {facing, visualAge, light * panelOpacity - idle, opacity * panelOpacity};
                if (drawInner && y % InnerSurfaceStep == 0) {
                    // A recessed surface shifts behind the outer rim as the camera moves.
                    sample.innerPoint = surfacePose.Point(Scale(unit, .92f));
                    sample.innerFacing = Dot(normal, Normalize(Subtract(eye, sample.innerPoint)));
                }
            }
        }
        const auto drawGrid = [](int step, const auto& vertex) {
            for (int y = 0; y < Up; y += step) for (int x = 0; x < Around; ++x) {
                vertex(x, y); vertex(x + 1, y); vertex(x, y + step);
                vertex(x, y + step); vertex(x + 1, y); vertex(x + 1, y + step);
            }
        };
        drawGrid(1, [&](int x, int y) {
            const auto& sample = surface[x][y];
            emit(sample.point, Vector2{140.0f + sample.uv.x, sample.uv.y}, sample.color);
        });
        if (drawInner) {
            drawGrid(InnerSurfaceStep, [&](int x, int y) {
                const auto& sample = surface[x][y];
                emit(sample.innerPoint, Vector2{160.0f + sample.uv.x, sample.uv.y},
                    Vector4{sample.innerFacing, visualAge, 0.0f, innerOpacity});
            });
        }
    }

    template <class Emit>
    void DrawFracture(Vector3 eye, size_t vertexBudget, Emit emit) const {
        if (!Breaking()) return;
        const auto& impact = BreakImpact();
        const float age = impact.age;
        const auto fracture = BreakPose(age);
        constexpr Vector3 worldUp{0, 1, 0};
        constexpr Vector3 localRight{1, 0, 0};
        constexpr float tau = 6.2831853f;
        const auto axis = Normalize(Cross(impact.unit, std::abs(impact.unit.y) < .94f ? worldUp : localRight));
        const auto bitangent = Cross(impact.unit, axis);
        const float reach = CrackSpeed * (age + .009f);
        // Rays and their branches lie on the same dome as the panels.
        const auto drawCrack = [&](float start, float end, float heading, float bend, float seed) {
            if (reach <= start || age >= .43f) return;
            const auto sample = [&](float distance) {
                const float wiggle = (std::sin(distance * 29.0f + seed) * .082f +
                    std::sin(distance * 11.0f + seed * .71f) * .074f) * Smooth(distance / .32f);
                const float angle = heading + wiggle + bend * Smooth((distance - start) / .66f);
                const auto tangent = Add(Scale(axis, std::cos(angle)), Scale(bitangent, std::sin(angle)));
                return Add(Scale(impact.unit, std::cos(distance)), Scale(tangent, std::sin(distance)));
            };
            const auto opacity = [&](Vector3 unit, Vector3 point, float distance) {
                const float facing = Dot(fracture.Normal(unit), Normalize(Subtract(eye, point)));
                const float peel = Smooth((age - (.076f + distance / CrackSpeed)) / .09f);
                return (1.0f - peel) * Smooth((unit.y - Bottom) * 13.0f) *
                    std::lerp(.41f, 1.0f, Smooth((facing + .18f) * 3.0f));
            };
            float distance = start;
            for (float next = start + .17f; distance < end && distance < reach; next += .17f) {
                if (vertexBudget < 6) return;
                const float last = (std::min)({next, end, reach});
                const auto u = sample(distance);
                const auto v = sample(last);
                const auto a = fracture.Point(Scale(u, 1.01f));
                const auto b = fracture.Point(Scale(v, 1.01f));
                const auto along = Subtract(b, a);
                const bool branch = bend != 0.0f;
                const float width = (branch ? .074f : .132f) * (1.0f - .38f * Smooth(last / end));
                const auto widthA = Scale(Normalize(Cross(fracture.Normal(u), along)), width);
                const auto widthB = Scale(Normalize(Cross(fracture.Normal(v), along)), width);
                const float strength = branch ? .72f : 1.0f;
                const Vector4 colorA{age, seed, strength, opacity(u, a, distance)};
                const Vector4 colorB{age, seed, strength, opacity(v, b, last)};
                emit(Subtract(a, widthA), Vector2{154, distance}, colorA);
                emit(Add(a, widthA), Vector2{155, distance}, colorA);
                emit(Subtract(b, widthB), Vector2{154, last}, colorB);
                emit(Subtract(b, widthB), Vector2{154, last}, colorB);
                emit(Add(a, widthA), Vector2{155, distance}, colorA);
                emit(Add(b, widthB), Vector2{155, last}, colorB);
                vertexBudget -= 6;
                distance = last;
            }
        };
        for (int ray = 0; ray < 9; ++ray) {
            const float seed = float(ray) * 3.79f + float(impact.serial % 17);
            const float heading = float(ray) * tau / 9.0f + .20f * std::sin(seed) + .12f * float(impact.serial);
            const float end = 2.41f + .48f * (.5f + .5f * std::cos(seed * 1.51f));
            drawCrack(0.0f, end, heading, 0.0f, seed);
            if (ray % 3 != 1)
                drawCrack(.61f + .10f * float(ray % 5), end * .90f, heading, ray % 2 == 0 ? .48f : -.54f, seed);
        }

        // Use the two lattices of the membrane shader to release real, spinning hexagonal pieces.
        constexpr float rowHeight = 1.7320508f;
        constexpr std::array<Vector2, 6> corners{{
            {0, -.5773503f}, {.5f, -.2886751f}, {.5f, .2886751f},
            {0, .5773503f}, {-.5f, .2886751f}, {-.5f, -.2886751f}}};
        const auto dome = [](float x, float y) {
            const float azimuth = x * 6.2831853f / 24.0f;
            const float elevation = std::lerp(std::asin(Bottom), 1.5707963f, (std::clamp)(y / 8.4f, 0.0f, 1.0f));
            const float radius = std::cos(elevation);
            return Vector3{radius * std::cos(azimuth), std::sin(elevation), radius * std::sin(azimuth)};
        };
        for (int row = 0; row < 10; ++row) for (int column = 0; column < 24; ++column) {
            const unsigned seed = (unsigned(row) * 193u + unsigned(column) * 113u + impact.serial * 61u);
            if (seed % 5 < 2) continue;
            const float x = float(column) + (row % 2 == 0 ? 0.0f : .5f);
            const float y = float(row) * rowHeight * .5f;
            const auto unit = dome(x, y);
            const float release = PanelReleaseTime(unit);
            const float flight = age - release;
            const float lifetime = .58f + .028f * float(seed % 9);
            if (flight <= 0.0f || flight >= lifetime) continue;
            if (vertexBudget < corners.size() * 3) return;

            const auto normal = breakPose_.Normal(unit);
            const auto side = Normalize(Cross(normal, std::abs(Dot(normal, worldUp)) < .93f ? worldUp : breakPose_.right));
            const auto up = Cross(normal, side);
            const float heading = tau * float(seed % 43) / 43.0f;
            const auto tangent = Add(Scale(side, std::cos(heading)), Scale(up, std::sin(heading)));
            const float scale = .64f + .30f * float(seed % 11) / 10.0f;
            const float speed = 5.7f + .49f * float(seed % 19);
            const auto outward = Add(Scale(normal, speed), Scale(tangent, 2.0f + .19f * float(seed % 13)));
            const auto pivot = breakPose_.Point(unit);
            const auto travel = Add(Scale(breakVelocity_, release + flight * .94f),
                Add(Scale(outward, flight), Scale(worldUp, 1.25f * flight - 6.4f * flight * flight)));
            const auto center = Add(pivot, travel);
            const auto spinAxis = Normalize(Add(tangent, Scale(normal, .33f)));
            const float rotation = flight * (5.0f + .55f * float(seed % 17));
            const float c = std::cos(rotation), s = std::sin(rotation);
            const auto rotate = [&](Vector3 vector) {
                return Add(Add(Scale(vector, c), Scale(Cross(spinAxis, vector), s)),
                    Scale(spinAxis, Dot(spinAxis, vector) * (1.0f - c)));
            };
            const float facing = std::abs(Dot(rotate(normal), Normalize(Subtract(eye, center))));
            const float phase = flight / lifetime;
            const float fade = Smooth(flight / .065f) * (1.0f - Smooth((phase - .23f) / .77f));
            const Vector4 color{facing, float(seed % 37) / 37.0f, phase, fade};
            const auto corner = [&](size_t index) {
                const auto offset = corners[index];
                const auto original = breakPose_.Point(dome(x + offset.x * scale, y + offset.y * scale));
                emit(Add(center, rotate(Subtract(original, pivot))),
                    Vector2{156.5f + offset.x, .5f + offset.y * .8660254f}, color);
            };
            for (size_t face = 0; face < corners.size(); ++face) {
                emit(center, Vector2{156.5f, .5f}, color);
                corner(face);
                corner((face + 1) % corners.size());
            }
            vertexBudget -= corners.size() * 3;
        }
    }

    static float Smooth(float value) {
        const float t = (std::clamp)(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

private:
    const Impact* RecentImpact() const {
        if (sequence_ == 0) return nullptr;
        const auto& hit = impacts_[(sequence_ - 1) % impacts_.size()];
        return hit.serial != 0 ? &hit : nullptr;
    }

    bool started_ = false;
    bool unlocked_ = false;
    float age_ = 0.0f;
    float fade_ = 0.0f;
    float health_ = MaxHealth;
    float preHitHealth_ = MaxHealth;
    float rechargeReadyAge_ = 10.0f;
    float poseStep_ = 0.0f;
    unsigned sequence_ = 0;
    Pose pose_{}, previous_{};
    Pose breakPose_{};
    Vector3 breakVelocity_{};
    std::array<Impact, 16> impacts_{};
};
