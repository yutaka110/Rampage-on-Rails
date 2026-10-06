#include "AppEffectInstancePanel.h"

#include "AppEffectAuthoringUi.h"
#include "EffectAuthoringRegistry.h"
#include "EffectAssetLoader.h"
#include "EffectRuntime.h"

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
using namespace EffectAuthoringUi;

const EffectComponentCommon* FindComponentCommon(
    const EffectAsset& asset,
    uint32_t componentId) {
    const EffectComponentCommon* found = nullptr;
    asset.Components().ForEachComponentCommon(
        [&found, componentId](const EffectComponentCommon& common) {
            if (common.id == componentId) {
                found = &common;
            }
        });
    return found;
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

void CountDiagnostics(
    const LoadedEffectAsset* loaded,
    uint32_t& registryWarnings,
    uint32_t& metadataOverrides,
    uint32_t& otherDiagnostics) {
    registryWarnings = 0;
    metadataOverrides = 0;
    otherDiagnostics = 0;
    if (loaded == nullptr) {
        return;
    }
    for (const EffectAssetDiagnostic& diagnostic : loaded->diagnostics) {
        if (IsRegistryDiagnostic(diagnostic)) {
            ++registryWarnings;
        } else if (IsMetadataOverrideDiagnostic(diagnostic)) {
            ++metadataOverrides;
        } else {
            ++otherDiagnostics;
        }
    }
}

void DrawLoaderDiagnosticSummary(const LoadedEffectAsset* loaded) {
    uint32_t registryWarnings = 0;
    uint32_t metadataOverrides = 0;
    uint32_t otherDiagnostics = 0;
    CountDiagnostics(loaded, registryWarnings, metadataOverrides, otherDiagnostics);
    const uint32_t total = registryWarnings + metadataOverrides + otherDiagnostics;
    ImGui::Text(
        "loaderDiagnostics=%s registryWarnings=%u metadataOverrides=%u other=%u",
        registryWarnings > 0 || otherDiagnostics > 0 ? "attention" : "ok",
        registryWarnings,
        metadataOverrides,
        otherDiagnostics);
    if (loaded == nullptr || total == 0 || !ImGui::TreeNode("Loader Diagnostics")) {
        return;
    }
    for (const EffectAssetDiagnostic& diagnostic : loaded->diagnostics) {
        ImGui::BulletText(
            "code=%s source=%s field=%s line=%u",
            diagnostic.code.empty() ? "generic" : diagnostic.code.c_str(),
            diagnostic.source.empty() ? "loader" : diagnostic.source.c_str(),
            diagnostic.field.empty() ? "none" : diagnostic.field.c_str(),
            diagnostic.lineNumber);
        ImGui::TextWrapped("%s", diagnostic.message.c_str());
    }
    ImGui::TreePop();
}

bool HasTechniqueMetadataOverride(const EffectComponentCommon& common) {
    return common.techniqueMetadataSource != EffectTechniqueMetadataSource::Registry ||
        common.techniqueDisplayNameSource != EffectTechniqueMetadataSource::Registry ||
        common.techniqueCategorySource != EffectTechniqueMetadataSource::Registry ||
        common.techniqueDescriptionSource != EffectTechniqueMetadataSource::Registry;
}

bool HasRegistryAttention(
    const EffectComponentCommon& common,
    const EffectAuthoringRegistry& authoringRegistry) {
    const TechniqueRegistry& techniques = authoringRegistry.Techniques();
    const RendererRegistry& renderers = authoringRegistry.Renderers();
    const SimulationRegistry& simulations = authoringRegistry.Simulations();
    const bool techniqueResolved =
        !common.techniqueId.empty() && techniques.Find(common.techniqueId) != nullptr;
    const bool rendererResolved =
        !common.rendererId.empty() && renderers.Find(common.rendererId) != nullptr;
    const bool simulationResolved =
        !common.simulationId.empty() && simulations.Find(common.simulationId) != nullptr;
    return !techniqueResolved || !rendererResolved || !simulationResolved;
}

const char* TechniqueFallbackId(
    const EffectComponentCommon& common,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!common.techniqueId.empty() &&
        authoringRegistry.Techniques().Find(common.techniqueId) != nullptr) {
        return "none";
    }
    if (const EffectTechniqueDescriptor* fallback =
            authoringRegistry.Techniques().FindByLegacyTechnique(common.technique)) {
        return fallback->id.c_str();
    }
    return "unresolved";
}

void CountComponentAuthoringSummary(
    const EffectInstance& instance,
    const EffectAuthoringRegistry& authoringRegistry,
    uint32_t& components,
    uint32_t& registryAttention,
    uint32_t& metadataOverrides) {
    components = 0;
    registryAttention = 0;
    metadataOverrides = 0;
    if (instance.asset == nullptr) {
        return;
    }

    for (const EffectComponentInstance& componentInstance : instance.components) {
        ++components;
        const EffectComponentCommon* common =
            FindComponentCommon(*instance.asset, componentInstance.componentId);
        if (common == nullptr) {
            ++registryAttention;
            continue;
        }
        if (HasRegistryAttention(*common, authoringRegistry)) {
            ++registryAttention;
        }
        if (HasTechniqueMetadataOverride(*common)) {
            ++metadataOverrides;
        }
    }
}

const EffectInstance* FindLatestInstance(const std::vector<const EffectInstance*>& instances) {
    if (instances.empty()) {
        return nullptr;
    }
    return *std::max_element(
        instances.begin(),
        instances.end(),
        [](const EffectInstance* lhs, const EffectInstance* rhs) {
            return lhs->id < rhs->id;
        });
}

const EffectInstance* FindInstanceById(
    const std::vector<const EffectInstance*>& instances,
    uint32_t instanceId) {
    const auto it = std::find_if(
        instances.begin(),
        instances.end(),
        [instanceId](const EffectInstance* instance) {
            return instance->id == instanceId;
        });
    return it != instances.end() ? *it : nullptr;
}

void DrawActiveAuthoringSummary(
    const EffectInstance* instance,
    const std::vector<LoadedEffectAsset>* loadedEffectAssets,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (instance == nullptr || instance->asset == nullptr) {
        ImGui::TextDisabled("activeAuthoringSummary=none");
        return;
    }

    const LoadedEffectAsset* loaded =
        FindLoadedEffectAsset(loadedEffectAssets, instance->asset->name);
    uint32_t registryWarnings = 0;
    uint32_t loaderMetadataOverrides = 0;
    uint32_t otherDiagnostics = 0;
    CountDiagnostics(loaded, registryWarnings, loaderMetadataOverrides, otherDiagnostics);

    uint32_t components = 0;
    uint32_t componentRegistryAttention = 0;
    uint32_t componentMetadataOverrides = 0;
    CountComponentAuthoringSummary(
        *instance,
        authoringRegistry,
        components,
        componentRegistryAttention,
        componentMetadataOverrides);
    const EffectComponentCommon* primaryCommon = nullptr;
    if (!instance->components.empty()) {
        primaryCommon = FindComponentCommon(
            *instance->asset,
            instance->components.front().componentId);
    }
    const bool primaryTechniqueResolved =
        primaryCommon != nullptr &&
        !primaryCommon->techniqueId.empty() &&
        authoringRegistry.Techniques().Find(primaryCommon->techniqueId) != nullptr;

    const bool attention =
        registryWarnings > 0 || otherDiagnostics > 0 || componentRegistryAttention > 0;
    ImGui::Text(
        "activeAuthoringSummary=%s latestInstanceId=%u asset=%s age=%.2f components=%u",
        attention ? "attention" : "ok",
        instance->id,
        instance->asset->name.c_str(),
        instance->age,
        components);
    ImGui::Text(
        "diagnostics registryWarnings=%u metadataOverrides=%u other=%u",
        registryWarnings,
        loaderMetadataOverrides,
        otherDiagnostics);
    ImGui::Text(
        "primaryComponent techniqueId=%s techniqueRegistry=%s fallback=%s",
        primaryCommon == nullptr || primaryCommon->techniqueId.empty()
            ? "none"
            : primaryCommon->techniqueId.c_str(),
        RegistryState(primaryCommon != nullptr && !primaryCommon->techniqueId.empty(), primaryTechniqueResolved),
        primaryCommon != nullptr ? TechniqueFallbackId(*primaryCommon, authoringRegistry) : "none");
    ImGui::Text(
        "componentAuthoring registryAttention=%u metadataOverrides=%u",
        componentRegistryAttention,
        componentMetadataOverrides);
}

void DrawComponentAuthoringDebug(
    const EffectComponentInstance& componentInstance,
    const EffectComponentCommon& common,
    const EffectAuthoringRegistry& authoringRegistry) {
    const TechniqueRegistry& techniques = authoringRegistry.Techniques();
    const RendererRegistry& renderers = authoringRegistry.Renderers();
    const SimulationRegistry& simulations = authoringRegistry.Simulations();
    const bool techniqueResolved =
        !common.techniqueId.empty() && techniques.Find(common.techniqueId) != nullptr;
    const bool rendererResolved =
        !common.rendererId.empty() && renderers.Find(common.rendererId) != nullptr;
    const bool simulationResolved =
        !common.simulationId.empty() && simulations.Find(common.simulationId) != nullptr;
    const bool attention = HasRegistryAttention(common, authoringRegistry);

    ImGui::Text(
        "authoringDiagnostics=%s componentId=%u type=%s active=%s age=%.2f",
        attention ? "attention" : "ok",
        componentInstance.componentId,
        ComponentTypeLabel(common.type),
        componentInstance.active ? "yes" : "no",
        componentInstance.age);
    ImGui::Text(
        "techniqueId=%s techniqueRegistry=%s fallback=%s",
        common.techniqueId.empty() ? "empty" : common.techniqueId.c_str(),
        RegistryState(!common.techniqueId.empty(), techniqueResolved),
        TechniqueFallbackId(common, authoringRegistry));
    ImGui::Text(
        "rendererId=%s rendererRegistry=%s",
        common.rendererId.empty() ? "empty" : common.rendererId.c_str(),
        RegistryState(!common.rendererId.empty(), rendererResolved));
    ImGui::Text(
        "simulationId=%s simulationRegistry=%s",
        common.simulationId.empty() ? "empty" : common.simulationId.c_str(),
        RegistryState(!common.simulationId.empty(), simulationResolved));
    ImGui::Text(
        "compatMirror technique=%d(%s) renderer=%d(%s) simulation=%d(%s)",
        static_cast<int>(common.technique),
        EffectTechniqueLabel(common.technique),
        static_cast<int>(common.rendererType),
        EffectRendererTypeLabel(common.rendererType),
        static_cast<int>(common.simulationType),
        EffectSimulationTypeLabel(common.simulationType));
    ImGui::Text(
        "metadataPriority=registry<asset<component effectiveSource=%s",
        MetadataSourceLabel(common.techniqueMetadataSource));
    ImGui::Text(
        "displayNameSource=%s techniqueDisplayName=%s",
        MetadataSourceLabel(common.techniqueDisplayNameSource),
        common.techniqueDisplayName.empty() ? "none" : common.techniqueDisplayName.c_str());
    ImGui::Text(
        "categorySource=%s category=%s",
        MetadataSourceLabel(common.techniqueCategorySource),
        common.techniqueCategory.empty() ? "uncategorized" : common.techniqueCategory.c_str());
    ImGui::TextWrapped(
        "descriptionSource=%s techniqueDescription=%s",
        MetadataSourceLabel(common.techniqueDescriptionSource),
        common.techniqueDescription.empty() ? "none" : common.techniqueDescription.c_str());
}
} // namespace

void DrawEffectInstancePanel(
    const EffectInstancePanelInput& input) {
    if (input.effectRuntime == nullptr || input.selectedInstanceId == nullptr) {
        return;
    }

    EffectRuntime& effectRuntime = *input.effectRuntime;
    const EffectAuthoringRegistry& authoringRegistry = input.authoringRegistry;
    uint32_t& selectedInstanceId = *input.selectedInstanceId;
    std::vector<const EffectInstance*> instances;
    for (const EffectInstance& instance : effectRuntime.Instances()) {
        if (instance.asset != nullptr) {
            instances.push_back(&instance);
        }
    }

    std::sort(
        instances.begin(),
        instances.end(),
        [](const EffectInstance* lhs, const EffectInstance* rhs) {
            return lhs->id < rhs->id;
        });

    bool selectedInstanceExists = false;
    for (const EffectInstance* instance : instances) {
        if (instance->id == selectedInstanceId) {
            selectedInstanceExists = true;
            break;
        }
    }
    if (!selectedInstanceExists) {
        const EffectInstance* latestInstance = FindLatestInstance(instances);
        selectedInstanceId = latestInstance != nullptr ? latestInstance->id : 0;
    }

    if (selectedInstanceId != 0) {
        const auto selectedIt = std::find_if(
            instances.begin(),
            instances.end(),
            [&selectedInstanceId](const EffectInstance* instance) {
                return instance->id == selectedInstanceId;
            });
        if (selectedIt != instances.end() && selectedIt != instances.begin()) {
            std::rotate(instances.begin(), selectedIt, selectedIt + 1);
        }
    }

    DrawActiveAuthoringSummary(
        FindLatestInstance(instances),
        input.loadedEffectAssets,
        authoringRegistry);
    ImGui::Separator();

    if (ImGui::BeginTable("EffectInstancesLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Instances", ImGuiTableColumnFlags_WidthFixed, 240.0f);
        ImGui::TableSetupColumn("Selected", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("EffectInstanceList", ImVec2(0.0f, 240.0f), true);
        for (const EffectInstance* instance : instances) {
            const bool isSelected = (selectedInstanceId == instance->id);
            std::string label = isSelected ?
                "[Pinned] id=" + std::to_string(instance->id) + " " + instance->asset->name :
                "id=" + std::to_string(instance->id) + " " + instance->asset->name;
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectedInstanceId = instance->id;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "age=%.2f  components=%u",
                    instance->age,
                    static_cast<unsigned int>(instance->components.size()));
            }
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("EffectInstanceDetails", ImVec2(0.0f, 240.0f), true);
        const EffectInstance* selectedInstance = FindInstanceById(instances, selectedInstanceId);

        if (selectedInstance != nullptr) {
            ImGui::PushID(static_cast<int>(selectedInstance->id));
            ImGui::Text("id=%u asset=%s age=%.2f components=%u",
                selectedInstance->id,
                selectedInstance->asset->name.c_str(),
                selectedInstance->age,
                static_cast<unsigned int>(selectedInstance->components.size()));
            DrawLoaderDiagnosticSummary(
                FindLoadedEffectAsset(input.loadedEffectAssets, selectedInstance->asset->name));
            float timelineAge = selectedInstance->age;
            if (ImGui::DragFloat("Timeline Age", &timelineAge, 0.02f, 0.0f, 60.0f, "%.2f")) {
                effectRuntime.SetInstanceAge(selectedInstance->id, timelineAge);
            }
            Transform appearance = selectedInstance->transform;
            Vector4 color = selectedInstance->color;
            bool changed = ImGui::DragFloat3("Position", &appearance.translate.x, 0.05f, -100.0f, 100.0f);
            changed |= ImGui::DragFloat3("Scale", &appearance.scale.x, 0.02f, 0.01f, 20.0f);
            changed |= ImGui::ColorEdit4("Color", &color.x);
            if (changed && !effectRuntime.SetInstanceAppearance(selectedInstance->id, appearance, color, selectedInstance->attached)) {
                ImGui::TextUnformatted("Effect appearance was rejected.");
            }
            if (ImGui::Button("Restart")) {
                effectRuntime.RestartInstance(selectedInstance->id);
            }
            ImGui::SameLine();
            const bool stopRequested = ImGui::Button("Stop");
            if (ImGui::TreeNode("Component Authoring Detail")) {
                for (const EffectComponentInstance& componentInstance : selectedInstance->components) {
                    const EffectComponentCommon* common =
                        FindComponentCommon(*selectedInstance->asset, componentInstance.componentId);
                    ImGui::PushID(static_cast<int>(componentInstance.componentId));
                    if (common == nullptr) {
                        ImGui::TextColored(
                            ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                            "componentId=%u authoringDiagnostics=attention reason=missingCommon",
                            componentInstance.componentId);
                    } else if (ImGui::TreeNode(common->name.empty() ? "Unnamed Component" : common->name.c_str())) {
                        DrawComponentAuthoringDebug(
                            componentInstance,
                            *common,
                            authoringRegistry);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            const uint32_t instanceId = selectedInstance->id;
            ImGui::PopID();
            if (stopRequested) { effectRuntime.StopEffect(instanceId); selectedInstanceId = 0; }
        } else {
            ImGui::TextDisabled("No effect instance selected.");
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}
