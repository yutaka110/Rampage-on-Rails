#include "AppEffectAssetEditorPanel.h"

#include "AppEffectAuthoringUi.h"
#include "EffectAuthoringRegistry.h"
#include "EffectAssetLoader.h"
#include "EffectRuntime.h"

#include "../../externals/imgui/imgui.h"

#include <cstdint>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace EffectAuthoringUi;

const EffectRendererDescriptor* FindRendererDescriptor(
    const EffectAuthoringRegistry& authoringRegistry,
    std::string_view rendererId) {
    const RendererRegistry& registry = authoringRegistry.Renderers();
    if (!rendererId.empty()) {
        if (const EffectRendererDescriptor* descriptor = registry.Find(rendererId)) {
            return descriptor;
        }
    }
    return nullptr;
}

const EffectTechniqueDescriptor* FindTechniqueDescriptor(
    const EffectAuthoringRegistry& authoringRegistry,
    std::string_view techniqueId) {
    const TechniqueRegistry& registry = authoringRegistry.Techniques();
    if (!techniqueId.empty()) {
        if (const EffectTechniqueDescriptor* descriptor = registry.Find(techniqueId)) {
            return descriptor;
        }
    }
    return nullptr;
}

const EffectSimulationDescriptor* FindSimulationDescriptor(
    const EffectAuthoringRegistry& authoringRegistry,
    std::string_view simulationId) {
    const SimulationRegistry& registry = authoringRegistry.Simulations();
    if (!simulationId.empty()) {
        if (const EffectSimulationDescriptor* descriptor = registry.Find(simulationId)) {
            return descriptor;
        }
    }
    return nullptr;
}

const std::vector<EffectTechniqueDescriptor>& TechniqueDescriptors(
    const EffectAuthoringRegistry& authoringRegistry) {
    return authoringRegistry.Techniques().Descriptors();
}

bool HasTechniqueDescriptor(
    const std::vector<EffectTechniqueDescriptor>& descriptors,
    std::string_view techniqueId) {
    return std::any_of(
        descriptors.begin(),
        descriptors.end(),
        [techniqueId](const EffectTechniqueDescriptor& descriptor) {
            return descriptor.id == techniqueId;
        });
}

const char* EffectLayerLabel(EffectLayer layer) {
    switch (layer) {
    case EffectLayer::OpaqueFx:
        return "OpaqueFx";
    case EffectLayer::AdditiveFx:
        return "AdditiveFx";
    case EffectLayer::DistortionFx:
        return "DistortionFx";
    case EffectLayer::TrailFx:
        return "TrailFx";
    case EffectLayer::VolumetricFx:
        return "VolumetricFx";
    }
    return "Unknown";
}

const char* BlendModeLabel(ge3::graphics::BlendMode blend) {
    switch (blend) {
    case ge3::graphics::BlendMode::Opaque:
        return "Opaque";
    case ge3::graphics::BlendMode::Alpha:
        return "Alpha";
    case ge3::graphics::BlendMode::Additive:
        return "Additive";
    case ge3::graphics::BlendMode::Distortion:
        return "Distortion";
    }
    return "Unknown";
}

void DrawTechniqueRegistryTable(const EffectAuthoringRegistry& authoringRegistry) {
    const std::vector<EffectTechniqueDescriptor>& descriptors =
        TechniqueDescriptors(authoringRegistry);
    ImGui::Text("registeredTechniques=%u", static_cast<uint32_t>(descriptors.size()));
    if (!ImGui::BeginTable(
            "TechniqueRegistryTable",
            6,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("id");
    ImGui::TableSetupColumn("category");
    ImGui::TableSetupColumn("component");
    ImGui::TableSetupColumn("rendererId");
    ImGui::TableSetupColumn("simulationId");
    ImGui::TableSetupColumn("render");
    ImGui::TableHeadersRow();
    for (const EffectTechniqueDescriptor& descriptor : descriptors) {
        const EffectRendererDescriptor* renderer =
            FindRendererDescriptor(authoringRegistry, descriptor.rendererId);
        const EffectSimulationDescriptor* simulation =
            FindSimulationDescriptor(authoringRegistry, descriptor.simulationId);
        const std::string detailLabel = std::string("Details##") + descriptor.id;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(descriptor.id.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(descriptor.category.empty() ? "uncategorized" : descriptor.category.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(EffectTypeLabel(descriptor.componentType));
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(descriptor.rendererId.empty() ? "empty" : descriptor.rendererId.c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(descriptor.simulationId.empty() ? "empty" : descriptor.simulationId.c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%s / %s / +%u",
            EffectLayerLabel(descriptor.layer),
            BlendModeLabel(descriptor.blend),
            descriptor.renderQueueOffset);
        if (ImGui::TreeNode(detailLabel.c_str())) {
            ImGui::Text("displayName=%s",
                descriptor.displayName.empty() ? "none" : descriptor.displayName.c_str());
            ImGui::TextWrapped("description=%s",
                descriptor.description.empty() ? "none" : descriptor.description.c_str());
            if (ImGui::BeginTable(
                    "TechniqueRegistryDetailTable",
                    2,
                    ImGuiTableFlags_Borders |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("field");
                ImGui::TableSetupColumn("value");
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("techniqueCompatMirror");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d(%s)",
                    static_cast<int>(descriptor.technique),
                    EffectTechniqueLabel(descriptor.technique));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("rendererRegistry");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s compatMirror=%d(%s)",
                    renderer != nullptr ? "resolved" : "unregistered",
                    static_cast<int>(descriptor.rendererType),
                    EffectRendererTypeLabel(descriptor.rendererType));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("simulationRegistry");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s compatMirror=%d(%s)",
                    simulation != nullptr ? "resolved" : "unregistered",
                    static_cast<int>(descriptor.simulationType),
                    EffectSimulationTypeLabel(descriptor.simulationType));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("blendSource");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(descriptor.overrideBlend ? "techniqueOverride" : "assetDefault");

                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndTable();
}

void DrawRendererRegistryTable(const EffectAuthoringRegistry& authoringRegistry) {
    const std::vector<EffectRendererDescriptor>& descriptors =
        authoringRegistry.Renderers().Descriptors();
    ImGui::Text("registeredRenderers=%u", static_cast<uint32_t>(descriptors.size()));
    if (!ImGui::BeginTable(
            "RendererRegistryTable",
            3,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("id");
    ImGui::TableSetupColumn("component");
    ImGui::TableSetupColumn("compatMirror");
    ImGui::TableHeadersRow();
    for (const EffectRendererDescriptor& descriptor : descriptors) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(descriptor.id.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(EffectTypeLabel(descriptor.componentType));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d(%s)",
            static_cast<int>(descriptor.rendererType),
            EffectRendererTypeLabel(descriptor.rendererType));
    }
    ImGui::EndTable();
}

void DrawSimulationRegistryTable(const EffectAuthoringRegistry& authoringRegistry) {
    const std::vector<EffectSimulationDescriptor>& descriptors =
        authoringRegistry.Simulations().Descriptors();
    ImGui::Text("registeredSimulations=%u", static_cast<uint32_t>(descriptors.size()));
    if (!ImGui::BeginTable(
            "SimulationRegistryTable",
            3,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("id");
    ImGui::TableSetupColumn("compatMirror");
    ImGui::TableSetupColumn("compatMirrorName");
    ImGui::TableHeadersRow();
    for (const EffectSimulationDescriptor& descriptor : descriptors) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(descriptor.id.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", static_cast<int>(descriptor.simulationType));
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(EffectSimulationTypeLabel(descriptor.simulationType));
    }
    ImGui::EndTable();
}

const LoadedEffectAsset* FindLoadedEffectAsset(
    const std::vector<LoadedEffectAsset>* loadedEffectAssets,
    const std::string& assetName) {
    if (loadedEffectAssets == nullptr) {
        return nullptr;
    }
    for (const LoadedEffectAsset& loaded : *loadedEffectAssets) {
        if (loaded.asset.name == assetName) {
            return &loaded;
        }
    }
    return nullptr;
}

struct EffectAssetDiagnosticCounts {
    uint32_t info = 0;
    uint32_t warning = 0;
    uint32_t error = 0;

    uint32_t Total() const {
        return info + warning + error;
    }

    uint32_t Attention() const {
        return warning + error;
    }
};

void AddEffectAssetDiagnosticCount(
    EffectAssetDiagnosticCounts& counts,
    const EffectAssetDiagnostic& diagnostic) {
    switch (diagnostic.severity) {
    case EffectAssetDiagnosticSeverity::Info:
        ++counts.info;
        break;
    case EffectAssetDiagnosticSeverity::Warning:
        ++counts.warning;
        break;
    case EffectAssetDiagnosticSeverity::Error:
        ++counts.error;
        break;
    }
}

EffectAssetDiagnosticCounts CountEffectAssetDiagnostics(
    const std::vector<LoadedEffectAsset>* loadedEffectAssets) {
    EffectAssetDiagnosticCounts counts{};
    if (loadedEffectAssets == nullptr) {
        return counts;
    }
    for (const LoadedEffectAsset& loaded : *loadedEffectAssets) {
        for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
            AddEffectAssetDiagnosticCount(counts, diagnostic);
        }
    }
    return counts;
}

EffectAssetDiagnosticCounts CountEffectAssetDiagnostics(const LoadedEffectAsset& loaded) {
    EffectAssetDiagnosticCounts counts{};
    for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
        AddEffectAssetDiagnosticCount(counts, diagnostic);
    }
    return counts;
}

ImVec4 EffectAssetDiagnosticColor(EffectAssetDiagnosticSeverity severity) {
    switch (severity) {
    case EffectAssetDiagnosticSeverity::Info:
        return ImVec4(0.58f, 0.72f, 0.95f, 1.0f);
    case EffectAssetDiagnosticSeverity::Warning:
        return ImVec4(1.0f, 0.68f, 0.25f, 1.0f);
    case EffectAssetDiagnosticSeverity::Error:
        return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

std::string ExtractDiagnosticToken(
    const std::string& message,
    std::string_view key) {
    const size_t keyStart = message.find(key);
    if (keyStart == std::string::npos) {
        return {};
    }
    const size_t valueStart = keyStart + key.size();
    const size_t valueEnd = message.find_first_of(" ;", valueStart);
    return message.substr(
        valueStart,
        valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart);
}

std::string EffectAssetDiagnosticSummary(const EffectAssetDiagnostic& diagnostic) {
    if (diagnostic.code == "unknownTechnique") {
        const std::string fallback =
            ExtractDiagnosticToken(diagnostic.message, "fallbackTechniqueId=");
        if (!fallback.empty()) {
            return "unknownTechnique -> fallbackTechniqueId=" + fallback;
        }
    }
    return {};
}

void DrawEffectAssetDiagnosticRow(const EffectAssetDiagnostic& diagnostic) {
    ImGui::TextColored(EffectAssetDiagnosticColor(diagnostic.severity), "%s", "-");
    ImGui::SameLine();
    const std::string summary = EffectAssetDiagnosticSummary(diagnostic);
    if (!summary.empty()) {
        ImGui::TextColored(
            EffectAssetDiagnosticColor(diagnostic.severity),
            "%s",
            summary.c_str());
        ImGui::TextWrapped("%s", diagnostic.message.c_str());
    } else {
        ImGui::TextWrapped("%s", diagnostic.message.c_str());
    }
    ImGui::TextDisabled(
        "code=%s scope=%s field=%s source=%s line=%u",
        diagnostic.code.empty() ? "generic" : diagnostic.code.c_str(),
        diagnostic.scope.empty() ? "loader" : diagnostic.scope.c_str(),
        diagnostic.field.empty() ? "none" : diagnostic.field.c_str(),
        diagnostic.source.empty() ? "loader" : diagnostic.source.c_str(),
        diagnostic.lineNumber);
}

uint32_t CountMetadataOverrideDiagnostics(const LoadedEffectAsset& loaded) {
    uint32_t count = 0;
    for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
        if (IsMetadataOverrideDiagnostic(diagnostic)) {
            ++count;
        }
    }
    return count;
}

uint32_t CountRegistryDiagnostics(const LoadedEffectAsset& loaded) {
    uint32_t count = 0;
    for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
        if (IsRegistryDiagnostic(diagnostic)) {
            ++count;
        }
    }
    return count;
}

uint32_t CountOtherDiagnostics(const LoadedEffectAsset& loaded) {
    uint32_t count = 0;
    for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
        if (!IsRegistryDiagnostic(diagnostic) && !IsMetadataOverrideDiagnostic(diagnostic)) {
            ++count;
        }
    }
    return count;
}

void DrawRegistryDiagnosticsGroup(const LoadedEffectAsset& loaded, uint32_t count) {
    if (count == 0) {
        return;
    }
    if (ImGui::TreeNodeEx(
            "Registry Warnings",
            ImGuiTreeNodeFlags_DefaultOpen,
            "Registry Warnings (%u)",
            count)) {
        for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
            if (IsRegistryDiagnostic(diagnostic)) {
                DrawEffectAssetDiagnosticRow(diagnostic);
            }
        }
        ImGui::TreePop();
    }
}

void DrawMetadataOverrideDiagnosticsGroup(const LoadedEffectAsset& loaded, uint32_t count) {
    if (count == 0) {
        return;
    }
    if (ImGui::TreeNodeEx(
            "Metadata Overrides",
            0,
            "Metadata Overrides (%u)",
            count)) {
        for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
            if (IsMetadataOverrideDiagnostic(diagnostic)) {
                DrawEffectAssetDiagnosticRow(diagnostic);
            }
        }
        ImGui::TreePop();
    }
}

void DrawOtherDiagnosticsGroup(const LoadedEffectAsset& loaded, uint32_t count) {
    if (count == 0) {
        return;
    }
    if (ImGui::TreeNodeEx(
            "Other Diagnostics",
            0,
            "Other Diagnostics (%u)",
            count)) {
        for (const EffectAssetDiagnostic& diagnostic : loaded.diagnostics) {
            if (!IsRegistryDiagnostic(diagnostic) && !IsMetadataOverrideDiagnostic(diagnostic)) {
                DrawEffectAssetDiagnosticRow(diagnostic);
            }
        }
        ImGui::TreePop();
    }
}

void DrawEffectAssetDiagnostics(const LoadedEffectAsset* loaded) {
    if (loaded == nullptr || loaded->diagnostics.empty()) {
        return;
    }

    const EffectAssetDiagnosticCounts counts = CountEffectAssetDiagnostics(*loaded);
    const bool attention = counts.Attention() > 0;
    ImGui::TextColored(
        attention
            ? ImVec4(1.0f, 0.68f, 0.25f, 1.0f)
            : ImVec4(0.58f, 0.72f, 0.95f, 1.0f),
        "diagnostics=%s info=%u warning=%u error=%u source=%s",
        attention ? "attention" : "info",
        counts.info,
        counts.warning,
        counts.error,
        loaded->path.string().c_str());
    if (ImGui::TreeNode("Loader Diagnostics")) {
        const uint32_t registryCount = CountRegistryDiagnostics(*loaded);
        const uint32_t metadataOverrideCount = CountMetadataOverrideDiagnostics(*loaded);
        const uint32_t otherCount = CountOtherDiagnostics(*loaded);
        DrawRegistryDiagnosticsGroup(*loaded, registryCount);
        DrawMetadataOverrideDiagnosticsGroup(*loaded, metadataOverrideCount);
        DrawOtherDiagnosticsGroup(*loaded, otherCount);
        ImGui::TreePop();
    }
}

bool DrawTrailFollowModeCombo(const char* label, EffectTrailFollowMode& mode) {
    const char* items[] = {
        "Instance Forward",
        "Instance Up",
        "World X",
        "Movement History",
    };
    int current = static_cast<int>(mode);
    if (ImGui::Combo(label, &current, items, 4)) {
        mode = static_cast<EffectTrailFollowMode>(current);
        return true;
    }
    return false;
}

bool DrawEffectTypeControls(EffectParticleSettings& particle) {
    bool changed = false;
    changed |= ImGui::SliderFloat("Particle Lifetime", &particle.lifetime, 0.0f, 10.0f, "%.2f s");
    changed |= ImGui::SliderFloat("Particle Emissive", &particle.emissive, 0.0f, 12.0f, "%.2f");
    int spawnCount = static_cast<int>(particle.spawnCount);
    if (ImGui::SliderInt("Particle Spawn Count", &spawnCount, 0, 256)) {
        particle.spawnCount = static_cast<float>(spawnCount);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Spawn Frequency", &particle.spawnFrequency, 0.0f, 2.0f, "%.3f s");
    bool randomRotation = particle.randomRotation > 0.5f;
    if (ImGui::Checkbox("Random Rotation", &randomRotation)) {
        particle.randomRotation = randomRotation ? 1.0f : 0.0f;
        changed = true;
    }
    changed |= ImGui::SliderFloat("Scale Y Min", &particle.scaleYMin, 0.01f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Scale Y Max", &particle.scaleYMax, 0.01f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Depth Fade", &particle.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
    changed |= ImGui::SliderFloat("Particle Edge", &particle.edgeSoftness, 0.0f, 1.0f, "%.2f");
    return changed;
}

bool DrawEffectTypeControls(EffectTrailSettings& trail) {
    bool changed = false;
    changed |= ImGui::SliderFloat("Depth Fade", &trail.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
    changed |= ImGui::SliderFloat("Trail Tail", &trail.trailTailFade, 0.1f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Trail Length", &trail.length, 0.01f, 16.0f, "%.2f");
    changed |= ImGui::SliderFloat("Trail Width", &trail.width, 0.001f, 2.0f, "%.3f");
    changed |= ImGui::SliderFloat("Trail Sample Distance", &trail.sampleDistance, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat("Trail Smoothing", &trail.smoothing, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Trail Width Head", &trail.widthHead, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Trail Width Tail", &trail.widthTail, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Trail Alpha Tail", &trail.alphaTail, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Trail Miter Limit", &trail.miterLimit, 1.0f, 4.0f, "%.2f");
    changed |= ImGui::ColorEdit3("Trail Color Tail", &trail.colorTail.x);
    changed |= DrawTrailFollowModeCombo("Trail Follow", trail.followMode);
    int segmentBudget = static_cast<int>(trail.segmentBudget);
    if (ImGui::SliderInt("Trail Segments", &segmentBudget, 1, 128)) {
        trail.segmentBudget = static_cast<uint32_t>(segmentBudget);
        changed = true;
    }
    return changed;
}

bool DrawEffectTypeControls(EffectDistortionSettings& distortion) {
    bool changed = false;
    changed |= ImGui::SliderFloat("Depth Fade", &distortion.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
    changed |= ImGui::SliderFloat("Distortion Attenuation", &distortion.depthAttenuation, 0.1f, 4.0f, "%.2f");
    return changed;
}

bool DrawEffectTypeControls(EffectBeamSettings&) {
    ImGui::TextDisabled("Beam has no type-specific live controls yet.");
    return false;
}

bool DrawEffectTypeControls(EffectRingSettings& ring) {
    bool changed = false;
    int divide = static_cast<int>(ring.divide);
    if (ImGui::SliderInt("Ring Divide", &divide, 8, 128)) {
        ring.divide = static_cast<uint32_t>(divide);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Outer Radius", &ring.outerRadius, 0.01f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Inner Radius", &ring.innerRadius, 0.0f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Ring Emissive", &ring.emissive, 0.0f, 12.0f, "%.2f");
    changed |= ImGui::SliderFloat("Ring UV Scroll", &ring.uvScrollSpeed, -8.0f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Ring Expansion", &ring.expansion, 0.0f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Ring Fade Out", &ring.fadeOut, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Ring Depth Fade", &ring.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
    return changed;
}

bool DrawEffectTypeControls(EffectCylinderSettings& cylinder) {
    bool changed = false;
    int divide = static_cast<int>(cylinder.divide);
    if (ImGui::SliderInt("Cylinder Divide", &divide, 3, 128)) {
        cylinder.divide = static_cast<uint32_t>(divide);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Top Radius", &cylinder.topRadius, 0.0f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Bottom Radius", &cylinder.bottomRadius, 0.0f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Cylinder Height", &cylinder.height, 0.01f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Cylinder Emissive", &cylinder.emissive, 0.0f, 12.0f, "%.2f");
    changed |= ImGui::SliderFloat("Cylinder UV Scroll", &cylinder.uvScrollSpeed, -8.0f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("Cylinder Alpha Ref", &cylinder.alphaReference, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Cylinder Fade Out", &cylinder.fadeOut, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Cylinder Depth Fade", &cylinder.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
    return changed;
}

void ResetEffectTypeToAssetDefault(EffectParticleSettings& particle, const EffectAsset& asset) {
    particle.lifetime = asset.defaultParticle.lifetime;
    particle.emissive = asset.defaultParticle.emissive;
    particle.spawnCount = asset.defaultParticle.spawnCount;
    particle.spawnFrequency = asset.defaultParticle.spawnFrequency;
    particle.randomRotation = asset.defaultParticle.randomRotation;
    particle.scaleYMin = asset.defaultParticle.scaleYMin;
    particle.scaleYMax = asset.defaultParticle.scaleYMax;
    particle.depthFadeSoftness = asset.defaultParticle.depthFadeSoftness;
    particle.edgeSoftness = asset.defaultParticle.edgeSoftness;
}

void ResetEffectTypeToAssetDefault(EffectTrailSettings& trail, const EffectAsset& asset) {
    trail.depthFadeSoftness = asset.defaultTrail.depthFadeSoftness;
    trail.trailTailFade = asset.defaultTrail.trailTailFade;
    trail.length = asset.defaultTrail.length;
    trail.width = asset.defaultTrail.width;
    trail.sampleDistance = asset.defaultTrail.sampleDistance;
    trail.smoothing = asset.defaultTrail.smoothing;
    trail.widthHead = asset.defaultTrail.widthHead;
    trail.widthTail = asset.defaultTrail.widthTail;
    trail.alphaTail = asset.defaultTrail.alphaTail;
    trail.miterLimit = asset.defaultTrail.miterLimit;
    trail.colorTail = asset.defaultTrail.colorTail;
    trail.followMode = asset.defaultTrail.followMode;
    trail.segmentBudget = asset.defaultTrail.segmentBudget;
}

void ResetEffectTypeToAssetDefault(EffectDistortionSettings& distortion, const EffectAsset& asset) {
    distortion.depthFadeSoftness = asset.defaultDistortion.depthFadeSoftness;
    distortion.depthAttenuation = asset.defaultDistortion.depthAttenuation;
}

void ResetEffectTypeToAssetDefault(EffectRingSettings& ring, const EffectAsset& asset) {
    ring = asset.defaultRing;
}

void ResetEffectTypeToAssetDefault(EffectCylinderSettings& cylinder, const EffectAsset& asset) {
    cylinder = asset.defaultCylinder;
}

template <typename ComponentAsset>
bool DrawEffectComponentNode(
    ComponentAsset& component,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (component.common.id == 0 && component.common.name.empty()) {
        return false;
    }

    bool changed = false;
    ImGui::PushID(static_cast<int>(component.common.id));
    if (ImGui::TreeNode(component.common.name.c_str())) {
        const EffectTechniqueDescriptor* technique =
            FindTechniqueDescriptor(authoringRegistry, component.common.techniqueId);
        const EffectSimulationDescriptor* simulation =
            FindSimulationDescriptor(authoringRegistry, component.common.simulationId);
        const EffectRendererDescriptor* renderer =
            FindRendererDescriptor(authoringRegistry, component.common.rendererId);
        ImGui::Text("techniqueId=%s techniqueRegistry=%s techniqueCompatMirror=%d(%s)",
            component.common.techniqueId.c_str(),
            technique != nullptr ? "resolved" : "unregistered",
            static_cast<int>(component.common.technique),
            EffectTechniqueLabel(component.common.technique));
        ImGui::Text(
            "metadataPriority=registry<asset<component effectiveSource=%s",
            TechniqueMetadataSourceLabel(component.common.techniqueMetadataSource));
        ImGui::Text(
            "displayNameSource=%s techniqueDisplayName=%s",
            TechniqueMetadataSourceLabel(component.common.techniqueDisplayNameSource),
            component.common.techniqueDisplayName.empty()
                ? "none"
                : component.common.techniqueDisplayName.c_str());
        ImGui::Text(
            "categorySource=%s category=%s",
            TechniqueMetadataSourceLabel(component.common.techniqueCategorySource),
            component.common.techniqueCategory.empty()
                ? "uncategorized"
                : component.common.techniqueCategory.c_str());
        ImGui::TextWrapped(
            "descriptionSource=%s techniqueDescription=%s",
            TechniqueMetadataSourceLabel(component.common.techniqueDescriptionSource),
            component.common.techniqueDescription.empty()
                ? "none"
                : component.common.techniqueDescription.c_str());
        ImGui::Text("rendererId=%s rendererRegistry=%s rendererCompatMirror=%d(%s)",
            component.common.rendererId.c_str(),
            renderer != nullptr ? "resolved" : "unregistered",
            static_cast<int>(component.common.rendererType),
            EffectRendererTypeLabel(component.common.rendererType));
        ImGui::Text("simulationId=%s simulationRegistry=%s simulationCompatMirror=%d(%s)",
            component.common.simulationId.c_str(),
            simulation != nullptr ? "resolved" : "unregistered",
            static_cast<int>(component.common.simulationType),
            EffectSimulationTypeLabel(component.common.simulationType));
        ImGui::Text("layer=%d queue=%u start=%.2f duration=%.2f",
            static_cast<int>(component.common.layer),
            component.common.passState.renderQueue,
            component.common.startTime,
            component.common.duration);
        ImGui::Text("blend=%s cull=%d",
            BlendModeLabel(component.common.passState.blend),
            static_cast<int>(component.common.passState.cullMode));
        changed |= ImGui::DragFloat4("UV Rect", &component.common.uvRect.x, 0.001f, 0.0f, 1.0f, "%.3f");
        changed |= DrawEffectTypeControls(component.settings);
        ImGui::TreePop();
    }
    ImGui::PopID();
    return changed;
}

ParticleComponentAsset ToComponentAsset(const ParticleComponentAssetView& view) {
    if (!view) {
        return {};
    }
    return {*view.common, *view.settings};
}

TrailComponentAsset ToComponentAsset(const TrailComponentAssetView& view) {
    if (!view) {
        return {};
    }
    return {*view.common, *view.settings};
}

BeamComponentAsset ToComponentAsset(const BeamComponentAssetView& view) {
    if (!view) {
        return {};
    }
    return {*view.common, *view.settings};
}

DistortionComponentAsset ToComponentAsset(const DistortionComponentAssetView& view) {
    if (!view) {
        return {};
    }
    return {*view.common, *view.settings};
}

RingComponentAsset ToComponentAsset(const RingComponentAssetView& view) {
    if (!view) {
        return {};
    }
    return {*view.common, *view.settings};
}

CylinderComponentAsset ToComponentAsset(const CylinderComponentAssetView& view) {
    if (!view) {
        return {};
    }
    return {*view.common, *view.settings};
}

template <typename ComponentAsset, typename DrawFn>
void QueueEditedComponent(
    std::vector<ComponentAsset>& replacements,
    ComponentAsset component,
    DrawFn drawFn) {
    if (drawFn(component)) {
        replacements.push_back(component);
    }
}

void ApplyReplacements(
    EffectAsset& asset,
    const std::vector<ParticleComponentAsset>& replacements) {
    const MutableParticleComponentStorageView storage = asset.MutableComponents().MutableParticleStorageView();
    for (const ParticleComponentAsset& replacement : replacements) {
        ReplaceParticleComponentAndSyncPacked(storage, replacement);
    }
}

void ApplyReplacements(
    EffectAsset& asset,
    const std::vector<TrailComponentAsset>& replacements) {
    const MutableTrailComponentStorageView storage = asset.MutableComponents().MutableTrailStorageView();
    for (const TrailComponentAsset& replacement : replacements) {
        ReplaceTrailComponentAndSyncPacked(storage, replacement);
    }
}

void ApplyReplacements(
    EffectAsset& asset,
    const std::vector<BeamComponentAsset>& replacements) {
    const MutableBeamComponentStorageView storage = asset.MutableComponents().MutableBeamStorageView();
    for (const BeamComponentAsset& replacement : replacements) {
        ReplaceBeamComponentAndSyncPacked(storage, replacement);
    }
}

void ApplyReplacements(
    EffectAsset& asset,
    const std::vector<DistortionComponentAsset>& replacements) {
    const MutableDistortionComponentStorageView storage = asset.MutableComponents().MutableDistortionStorageView();
    for (const DistortionComponentAsset& replacement : replacements) {
        ReplaceDistortionComponentAndSyncPacked(storage, replacement);
    }
}

void ApplyReplacements(
    EffectAsset& asset,
    const std::vector<RingComponentAsset>& replacements) {
    const MutableRingComponentStorageView storage = asset.MutableComponents().MutableRingStorageView();
    for (const RingComponentAsset& replacement : replacements) {
        ReplaceRingComponentAndSyncPacked(storage, replacement);
    }
}

void ApplyReplacements(
    EffectAsset& asset,
    const std::vector<CylinderComponentAsset>& replacements) {
    const MutableCylinderComponentStorageView storage = asset.MutableComponents().MutableCylinderStorageView();
    for (const CylinderComponentAsset& replacement : replacements) {
        ReplaceCylinderComponentAndSyncPacked(storage, replacement);
    }
}

bool DrawParticleTypeSection(
    EffectAsset& asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!HasParticleComponents(asset)) {
        return false;
    }

    bool changed = false;
    if (ImGui::TreeNode(EffectTypeLabel(EffectComponentType::Particle))) {
        std::vector<ParticleComponentAsset> replacements;
        if (ImGui::Button("Reset to Asset Default")) {
            ForEachParticleComponent(asset.Components().ParticleStorageView(), [&asset, &replacements](const ParticleComponentAssetView& particle) {
                ParticleComponentAsset replacement = ToComponentAsset(particle);
                ResetEffectTypeToAssetDefault(replacement.settings, asset);
                replacements.push_back(replacement);
            });
        }
        ImGui::Separator();
        ForEachParticleComponent(asset.Components().ParticleStorageView(), [&replacements, &authoringRegistry](const ParticleComponentAssetView& particle) {
            QueueEditedComponent(
                replacements,
                ToComponentAsset(particle),
                [&authoringRegistry](ParticleComponentAsset& component) {
                    return DrawEffectComponentNode(component, authoringRegistry);
                });
        });
        changed = !replacements.empty();
        ApplyReplacements(asset, replacements);
        ImGui::TreePop();
    }    return changed;
}

bool DrawTrailTypeSection(
    EffectAsset& asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!HasTrailComponents(asset)) {
        return false;
    }

    bool changed = false;
    if (ImGui::TreeNode(EffectTypeLabel(EffectComponentType::Trail))) {
        std::vector<TrailComponentAsset> replacements;
        if (ImGui::Button("Reset to Asset Default")) {
            ForEachTrailComponent(asset.Components().TrailStorageView(), [&asset, &replacements](const TrailComponentAssetView& trail) {
                TrailComponentAsset replacement = ToComponentAsset(trail);
                ResetEffectTypeToAssetDefault(replacement.settings, asset);
                replacements.push_back(replacement);
            });
        }
        ImGui::Separator();
        ForEachTrailComponent(asset.Components().TrailStorageView(), [&replacements, &authoringRegistry](const TrailComponentAssetView& trail) {
            QueueEditedComponent(
                replacements,
                ToComponentAsset(trail),
                [&authoringRegistry](TrailComponentAsset& component) {
                    return DrawEffectComponentNode(component, authoringRegistry);
                });
        });
        changed = !replacements.empty();
        ApplyReplacements(asset, replacements);
        ImGui::TreePop();
    }    return changed;
}

bool DrawDistortionTypeSection(
    EffectAsset& asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!HasDistortionComponents(asset)) {
        return false;
    }

    bool changed = false;
    if (ImGui::TreeNode(EffectTypeLabel(EffectComponentType::Distortion))) {
        std::vector<DistortionComponentAsset> replacements;
        if (ImGui::Button("Reset to Asset Default")) {
            ForEachDistortionComponent(asset.Components().DistortionStorageView(), [&asset, &replacements](const DistortionComponentAssetView& distortion) {
                DistortionComponentAsset replacement = ToComponentAsset(distortion);
                ResetEffectTypeToAssetDefault(replacement.settings, asset);
                replacements.push_back(replacement);
            });
        }
        ImGui::Separator();
        ForEachDistortionComponent(asset.Components().DistortionStorageView(), [&replacements, &authoringRegistry](const DistortionComponentAssetView& distortion) {
            QueueEditedComponent(
                replacements,
                ToComponentAsset(distortion),
                [&authoringRegistry](DistortionComponentAsset& component) {
                    return DrawEffectComponentNode(component, authoringRegistry);
                });
        });
        changed = !replacements.empty();
        ApplyReplacements(asset, replacements);
        ImGui::TreePop();
    }    return changed;
}

bool DrawBeamTypeSection(
    EffectAsset& asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!HasBeamComponents(asset)) {
        return false;
    }

    bool changed = false;
    if (ImGui::TreeNode(EffectTypeLabel(EffectComponentType::Beam))) {
        std::vector<BeamComponentAsset> replacements;
        ForEachBeamComponent(asset.Components().BeamStorageView(), [&replacements, &authoringRegistry](const BeamComponentAssetView& beam) {
            QueueEditedComponent(
                replacements,
                ToComponentAsset(beam),
                [&authoringRegistry](BeamComponentAsset& component) {
                    return DrawEffectComponentNode(component, authoringRegistry);
                });
        });
        changed = !replacements.empty();
        ApplyReplacements(asset, replacements);
        ImGui::TreePop();
    }    return changed;
}

bool DrawRingTypeSection(
    EffectAsset& asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!HasRingComponents(asset)) {
        return false;
    }

    bool changed = false;
    if (ImGui::TreeNode(EffectTypeLabel(EffectComponentType::Ring))) {
        std::vector<RingComponentAsset> replacements;
        if (ImGui::Button("Reset to Asset Default")) {
            ForEachRingComponent(asset.Components().RingStorageView(), [&asset, &replacements](const RingComponentAssetView& ring) {
                RingComponentAsset replacement = ToComponentAsset(ring);
                ResetEffectTypeToAssetDefault(replacement.settings, asset);
                replacements.push_back(replacement);
            });
        }
        ImGui::Separator();
        ForEachRingComponent(asset.Components().RingStorageView(), [&replacements, &authoringRegistry](const RingComponentAssetView& ring) {
            QueueEditedComponent(
                replacements,
                ToComponentAsset(ring),
                [&authoringRegistry](RingComponentAsset& component) {
                    return DrawEffectComponentNode(component, authoringRegistry);
                });
        });
        changed = !replacements.empty();
        ApplyReplacements(asset, replacements);
        ImGui::TreePop();
    }    return changed;
}

bool DrawCylinderTypeSection(
    EffectAsset& asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!HasCylinderComponents(asset)) {
        return false;
    }

    bool changed = false;
    if (ImGui::TreeNode(EffectTypeLabel(EffectComponentType::Cylinder))) {
        std::vector<CylinderComponentAsset> replacements;
        if (ImGui::Button("Reset to Asset Default")) {
            ForEachCylinderComponent(asset.Components().CylinderStorageView(), [&asset, &replacements](const CylinderComponentAssetView& cylinder) {
                CylinderComponentAsset replacement = ToComponentAsset(cylinder);
                ResetEffectTypeToAssetDefault(replacement.settings, asset);
                replacements.push_back(replacement);
            });
        }
        ImGui::Separator();
        ForEachCylinderComponent(asset.Components().CylinderStorageView(), [&replacements, &authoringRegistry](const CylinderComponentAssetView& cylinder) {
            QueueEditedComponent(
                replacements,
                ToComponentAsset(cylinder),
                [&authoringRegistry](CylinderComponentAsset& component) {
                    return DrawEffectComponentNode(component, authoringRegistry);
                });
        });
        changed = !replacements.empty();
        ApplyReplacements(asset, replacements);
        ImGui::TreePop();
    }    return changed;
}

bool DrawEffectTypeSection(
    EffectAsset& asset,
    EffectComponentType type,
    const EffectAuthoringRegistry& authoringRegistry) {
    switch (type) {
    case EffectComponentType::Particle:
        return DrawParticleTypeSection(asset, authoringRegistry);
    case EffectComponentType::Trail:
        return DrawTrailTypeSection(asset, authoringRegistry);
    case EffectComponentType::Distortion:
        return DrawDistortionTypeSection(asset, authoringRegistry);
    case EffectComponentType::Beam:
        return DrawBeamTypeSection(asset, authoringRegistry);
    case EffectComponentType::Ring:
        return DrawRingTypeSection(asset, authoringRegistry);
    case EffectComponentType::Cylinder:
        return DrawCylinderTypeSection(asset, authoringRegistry);
    }    return false;
}
} // namespace

void DrawEffectAssetEditorPanel(
    EffectRuntime& effectRuntime,
    const EffectAuthoringRegistry& authoringRegistry,
    const std::vector<LoadedEffectAsset>* loadedEffectAssets) {
    const EffectAssetDiagnosticCounts diagnosticCounts =
        CountEffectAssetDiagnostics(loadedEffectAssets);
    if (diagnosticCounts.Attention() > 0) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
            "effectAssetDiagnostics=attention info=%u warning=%u error=%u",
            diagnosticCounts.info,
            diagnosticCounts.warning,
            diagnosticCounts.error);
    } else if (diagnosticCounts.info > 0) {
        ImGui::TextColored(
            ImVec4(0.58f, 0.72f, 0.95f, 1.0f),
            "effectAssetDiagnostics=ok info=%u warning=0 error=0",
            diagnosticCounts.info);
    } else {
        ImGui::TextDisabled("effectAssetDiagnostics=ok");
    }
    const std::vector<EffectTechniqueDescriptor>& techniqueDescriptors =
        TechniqueDescriptors(authoringRegistry);
    std::string techniqueRegistryLabel =
        "Technique Registry [registered=" +
        std::to_string(static_cast<uint32_t>(techniqueDescriptors.size()));
    if (HasTechniqueDescriptor(techniqueDescriptors, "AuthoringSpark")) {
        techniqueRegistryLabel += " sample=AuthoringSpark";
    }
    techniqueRegistryLabel += "]";
    if (ImGui::TreeNode(techniqueRegistryLabel.c_str())) {
        DrawTechniqueRegistryTable(authoringRegistry);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Renderer Registry")) {
        DrawRendererRegistryTable(authoringRegistry);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Simulation Registry")) {
        DrawSimulationRegistryTable(authoringRegistry);
        ImGui::TreePop();
    }

    for (const auto& [name, currentAsset] : effectRuntime.Assets()) {
        EffectAsset asset = currentAsset;
        bool changed = false;
        ImGui::PushID(name.c_str());
        const LoadedEffectAsset* loaded = FindLoadedEffectAsset(loadedEffectAssets, name);
        std::string assetTreeLabel = name;
        if (loaded != nullptr && !loaded->diagnostics.empty()) {
            const EffectAssetDiagnosticCounts counts = CountEffectAssetDiagnostics(*loaded);
            assetTreeLabel += " [diagnostics=";
            assetTreeLabel += counts.Attention() > 0 ? "attention" : "ok";
            assetTreeLabel += " info=" + std::to_string(counts.info);
            assetTreeLabel += " warning=" + std::to_string(counts.warning);
            assetTreeLabel += " error=" + std::to_string(counts.error);
            assetTreeLabel += "]";
        }
        if (ImGui::TreeNode(assetTreeLabel.c_str())) {
            DrawEffectAssetDiagnostics(loaded);
            ImGui::Text("shader=%s tex=%s life=%.2f particleEmissive=%.2f",
                asset.shader.c_str(),
                asset.texture.c_str(),
                asset.lifetime,
                asset.defaultParticle.emissive);
            ImGui::Text("layer=%d queue=%u noise=%.2f pulse=%.2f radius=%.2f",
                static_cast<int>(asset.layer),
                asset.passState.renderQueue,
                asset.defaultParticle.noiseStrength,
                asset.defaultParticle.pulseSpeed,
                asset.defaultParticle.spawnRadius);
            ImGui::Text(
                "assetMetadataPriority=registry<asset<component assetOverride=%s",
                asset.techniqueMetadataOverridden ? "yes" : "no");
            ImGui::Text(
                "assetDisplayNameSource=%s techniqueDisplayName=%s",
                AssetMetadataSourceLabel(asset.techniqueDisplayNameOverridden),
                asset.techniqueDisplayName.empty() ? "none" : asset.techniqueDisplayName.c_str());
            ImGui::Text(
                "assetCategorySource=%s category=%s",
                AssetMetadataSourceLabel(asset.techniqueCategoryOverridden),
                asset.techniqueCategory.empty() ? "uncategorized" : asset.techniqueCategory.c_str());
            ImGui::TextWrapped(
                "assetDescriptionSource=%s assetTechniqueDescription=%s",
                AssetMetadataSourceLabel(asset.techniqueDescriptionOverridden),
                asset.techniqueDescription.empty() ? "none" : asset.techniqueDescription.c_str());

            if (ImGui::TreeNode("Asset Defaults")) {
                changed |= ImGui::DragFloat4("Default UV Rect", &asset.uvRect.x, 0.001f, 0.0f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat("Default Particle Lifetime", &asset.defaultParticle.lifetime, 0.0f, 10.0f, "%.2f s");
                changed |= ImGui::SliderFloat("Default Particle Emissive", &asset.defaultParticle.emissive, 0.0f, 12.0f, "%.2f");
                int defaultParticleSpawnCount = static_cast<int>(asset.defaultParticle.spawnCount);
                if (ImGui::SliderInt("Default Particle Spawn Count", &defaultParticleSpawnCount, 0, 256)) {
                    changed = true;
                    asset.defaultParticle.spawnCount = static_cast<float>(defaultParticleSpawnCount);
                }
                bool defaultParticleRandomRotation = asset.defaultParticle.randomRotation > 0.5f;
                if (ImGui::Checkbox("Default Particle Random Rotation", &defaultParticleRandomRotation)) {
                    changed = true;
                    asset.defaultParticle.randomRotation = defaultParticleRandomRotation ? 1.0f : 0.0f;
                }
                changed |= ImGui::SliderFloat("Default Particle Scale Y Min", &asset.defaultParticle.scaleYMin, 0.01f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Particle Scale Y Max", &asset.defaultParticle.scaleYMax, 0.01f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Particle Depth Fade", &asset.defaultParticle.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
                changed |= ImGui::SliderFloat("Default Particle Edge", &asset.defaultParticle.edgeSoftness, 0.0f, 1.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Trail Depth Fade", &asset.defaultTrail.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
                changed |= ImGui::SliderFloat("Default Trail Tail", &asset.defaultTrail.trailTailFade, 0.1f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Trail Length", &asset.defaultTrail.length, 0.01f, 16.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Trail Width", &asset.defaultTrail.width, 0.001f, 2.0f, "%.3f");
                changed |= ImGui::SliderFloat("Default Trail Sample Distance", &asset.defaultTrail.sampleDistance, 0.0f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat("Default Trail Smoothing", &asset.defaultTrail.smoothing, 0.0f, 1.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Trail Width Head", &asset.defaultTrail.widthHead, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Trail Width Tail", &asset.defaultTrail.widthTail, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Trail Alpha Tail", &asset.defaultTrail.alphaTail, 0.0f, 1.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Trail Miter Limit", &asset.defaultTrail.miterLimit, 1.0f, 4.0f, "%.2f");
                changed |= ImGui::ColorEdit3("Default Trail Color Tail", &asset.defaultTrail.colorTail.x);
                changed |= DrawTrailFollowModeCombo("Default Trail Follow", asset.defaultTrail.followMode);
                int defaultTrailSegments = static_cast<int>(asset.defaultTrail.segmentBudget);
                if (ImGui::SliderInt("Default Trail Segments", &defaultTrailSegments, 1, 128)) {
                    changed = true;
                    asset.defaultTrail.segmentBudget = static_cast<uint32_t>(defaultTrailSegments);
                }
                changed |= ImGui::SliderFloat("Default Distortion Depth Fade", &asset.defaultDistortion.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
                changed |= ImGui::SliderFloat("Default Distortion Attenuation", &asset.defaultDistortion.depthAttenuation, 0.1f, 4.0f, "%.2f");
                int defaultRingDivide = static_cast<int>(asset.defaultRing.divide);
                if (ImGui::SliderInt("Default Ring Divide", &defaultRingDivide, 8, 128)) {
                    changed = true;
                    asset.defaultRing.divide = static_cast<uint32_t>(defaultRingDivide);
                }
                changed |= ImGui::SliderFloat("Default Ring Outer Radius", &asset.defaultRing.outerRadius, 0.01f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Ring Inner Radius", &asset.defaultRing.innerRadius, 0.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Ring Emissive", &asset.defaultRing.emissive, 0.0f, 12.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Ring UV Scroll", &asset.defaultRing.uvScrollSpeed, -8.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Ring Expansion", &asset.defaultRing.expansion, 0.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Ring Fade Out", &asset.defaultRing.fadeOut, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Ring Depth Fade", &asset.defaultRing.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
                int defaultCylinderDivide = static_cast<int>(asset.defaultCylinder.divide);
                if (ImGui::SliderInt("Default Cylinder Divide", &defaultCylinderDivide, 3, 128)) {
                    changed = true;
                    asset.defaultCylinder.divide = static_cast<uint32_t>(defaultCylinderDivide);
                }
                changed |= ImGui::SliderFloat("Default Cylinder Top Radius", &asset.defaultCylinder.topRadius, 0.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Cylinder Bottom Radius", &asset.defaultCylinder.bottomRadius, 0.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Cylinder Height", &asset.defaultCylinder.height, 0.01f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Cylinder Emissive", &asset.defaultCylinder.emissive, 0.0f, 12.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Cylinder UV Scroll", &asset.defaultCylinder.uvScrollSpeed, -8.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Cylinder Alpha Ref", &asset.defaultCylinder.alphaReference, 0.0f, 1.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Cylinder Fade Out", &asset.defaultCylinder.fadeOut, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Default Cylinder Depth Fade", &asset.defaultCylinder.depthFadeSoftness, 0.001f, 0.1f, "%.3f");
                ImGui::TreePop();
            }

            const EffectComponentType orderedTypes[] = {
                EffectComponentType::Particle,
                EffectComponentType::Trail,
                EffectComponentType::Distortion,
                EffectComponentType::Beam,
                EffectComponentType::Ring,
                EffectComponentType::Cylinder,
            };

            for (EffectComponentType type : orderedTypes) {
                changed |= DrawEffectTypeSection(asset, type, authoringRegistry);
            }
            if (changed) {
                std::string error;
                if (!effectRuntime.RegisterAsset(std::move(asset), &error)) ImGui::TextWrapped("Edit rejected: %s", error.c_str());
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
}
