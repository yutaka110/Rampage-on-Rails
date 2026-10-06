#include "EditorVfxGraph.h"

#include "../../EffectRuntime.h"
#include "../EditorSelection.h"
#include "../EditorAssetRegistry.h"
#include "../EditorTransactionStack.h"
#include "../core/EditorExecutionContext.h"
#include "../core/EditorUndoCommand.h"
#include "../documents/EditorDocumentManager.h"
#include "../documents/EditorVfxGraphDocumentProvider.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

namespace editor {
namespace {

EditorGraphPinDefinition InputPin(std::string id, std::string label, std::string type,
    bool required = false, bool multiple = false) {
    return {std::move(id), std::move(label), std::move(type),
        EditorGraphPinDirection::Input, required, multiple};
}

EditorGraphPinDefinition OutputPin(std::string id, std::string label, std::string type) {
    return {std::move(id), std::move(label), std::move(type),
        EditorGraphPinDirection::Output, false, true};
}

std::string Property(const EditorGraphNode& node, std::string_view key, std::string fallback) {
    const auto found = node.properties.find(std::string(key));
    return found == node.properties.end() ? std::move(fallback) : found->second;
}

bool ParseFloat(std::string_view text, float& output) {
    std::istringstream input{std::string(text)};
    std::string extra;
    return static_cast<bool>(input >> output) && std::isfinite(output) && !(input >> extra);
}

template <std::size_t Count>
bool ParseVector(std::string_view text, float (&output)[Count]) {
    std::string values(text);
    std::replace(values.begin(), values.end(), ',', ' ');
    std::istringstream input(values);
    for (float& value : output) {
        if (!(input >> value) || !std::isfinite(value)) return false;
    }
    std::string extra;
    return !(input >> extra);
}

uint64_t HashText(std::string_view value) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : value) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

const EditorGraphNode* InputSource(const EditorVfxGraphAsset& asset,
    const EditorGraphNode& node, std::string_view pinId) {
    const EditorGraphLink* link = FindEditorGraphIncomingLink(asset.graph, node.id, pinId);
    return link != nullptr ? FindEditorGraphNode(asset.graph, link->fromNodeId) : nullptr;
}

void AddDiagnostic(EditorVfxCompileArtifact& artifact, EditorGraphIssueSeverity severity,
    std::string code, std::string nodeId, std::string message) {
    artifact.diagnostics.push_back(
        {severity, std::move(code), std::move(nodeId), std::move(message)});
}

bool HasErrors(const EditorVfxCompileArtifact& artifact) {
    return std::any_of(artifact.diagnostics.begin(), artifact.diagnostics.end(),
        [](const auto& issue) { return issue.severity == EditorGraphIssueSeverity::Error; });
}

float FloatFromNode(const EditorGraphNode* node, std::string_view key, float fallback,
    EditorVfxCompileArtifact& artifact, std::string_view code, float minimum, float maximum) {
    if (node == nullptr) return fallback;
    float value = fallback;
    if (!ParseFloat(Property(*node, key, {}), value) || value < minimum || value > maximum) {
        AddDiagnostic(artifact, EditorGraphIssueSeverity::Error, std::string(code), node->id,
            "VFX numeric property is invalid or outside its supported range.");
        return fallback;
    }
    return value;
}

template <std::size_t Count>
void VectorFromNode(const EditorGraphNode* node, std::string_view key,
    const float (&fallback)[Count], float (&output)[Count],
    EditorVfxCompileArtifact& artifact, std::string_view code) {
    std::copy(std::begin(fallback), std::end(fallback), std::begin(output));
    if (node == nullptr) return;
    if (!ParseVector(Property(*node, key, {}), output)) {
        std::copy(std::begin(fallback), std::end(fallback), std::begin(output));
        AddDiagnostic(artifact, EditorGraphIssueSeverity::Error, std::string(code), node->id,
            "VFX vector property must contain finite numeric values.");
    }
}

class EditorVfxGraphUndoCommand final : public IEditorUndoCommand {
public:
    EditorVfxGraphUndoCommand(EditorDocumentId document, EditorVfxGraphAsset before,
        EditorVfxGraphAsset after)
        : document_(std::move(document)), before_(std::move(before)), after_(std::move(after)) {}

    EditorUndoResult Apply(EditorTransactionApplyMode mode,
        EditorExecutionContext& context) const override {
        auto* service = dynamic_cast<EditorVfxGraphService*>(
            context.Find(EditorVfxGraphService::kServiceId));
        if (service == nullptr) {
            return EditorUndoResult::Failure(EditorErrorCode::MissingService,
                "VFX Graph execution service is unavailable.");
        }
        std::string error;
        const EditorVfxGraphAsset& asset =
            mode == EditorTransactionApplyMode::Undo ? before_ : after_;
        if (!service->PublishFromCommand(document_, asset, error)) {
            return EditorUndoResult::Failure(EditorErrorCode::ApplyFailed, std::move(error));
        }
        return EditorUndoResult::Success();
    }

    std::size_t EstimatedBytes() const noexcept override {
        const auto bytes = [](const EditorVfxGraphAsset& asset) {
            std::size_t result = sizeof(asset) + asset.assetGuid.size() + asset.name.size();
            for (const EditorGraphNode& node : asset.graph.nodes) {
                result += sizeof(node) + node.id.size() + node.typeId.size() + node.label.size();
                for (const auto& [key, value] : node.properties) result += key.size() + value.size();
            }
            result += asset.graph.links.size() * sizeof(EditorGraphLink);
            return result;
        };
        return sizeof(*this) + bytes(before_) + bytes(after_);
    }

    std::string_view DomainId() const noexcept override { return "vfx-graph"; }
    std::string_view TypeId() const noexcept override { return "vfx-graph.snapshot"; }

private:
    EditorDocumentId document_;
    EditorVfxGraphAsset before_;
    EditorVfxGraphAsset after_;
};

} // namespace

EditorGraphSchema BuildEditorVfxGraphSchema() {
    EditorGraphSchema schema;
    schema.RegisterNodeType({"vfx.system.output", "VFX System Output", "Contexts", {
        InputPin("emitters", "Emitters", "vfx.emitter", true, true)}});
    schema.RegisterNodeType({"vfx.emitter", "Emitter", "Contexts", {
        InputPin("spawnRate", "Spawn Rate", "float"),
        InputPin("burst", "Burst", "float"),
        InputPin("lifetime", "Lifetime", "float"),
        InputPin("initialize", "Initialize", "vfx.initialize", true),
        InputPin("update", "Update", "vfx.update", true),
        InputPin("renderer", "Renderer", "vfx.renderer", true),
        OutputPin("emitter", "Emitter", "vfx.emitter")}});
    schema.RegisterNodeType({"vfx.spawn.rate", "Spawn Rate", "Spawn", {
        OutputPin("value", "Rate", "float")}});
    schema.RegisterNodeType({"vfx.spawn.burst", "Burst", "Spawn", {
        OutputPin("value", "Count", "float")}});
    schema.RegisterNodeType({"vfx.constant.float", "Float", "Parameters", {
        OutputPin("value", "Value", "float")}});
    schema.RegisterNodeType({"vfx.initialize.velocity", "Initialize Velocity", "Initialize", {
        OutputPin("initialize", "Initialize", "vfx.initialize")}});
    schema.RegisterNodeType({"vfx.update.gravity", "Gravity + Drag", "Update", {
        OutputPin("update", "Update", "vfx.update")}});
    schema.RegisterNodeType({"vfx.renderer.sprite", "Sprite Renderer", "Render", {
        OutputPin("renderer", "Renderer", "vfx.renderer")}});
    schema.RegisterNodeType({"vfx.renderer.ribbon", "Ribbon Renderer", "Render", {
        OutputPin("renderer", "Renderer", "vfx.renderer")}});
    schema.RegisterNodeType({"vfx.renderer.beam", "Beam Renderer", "Render", {
        OutputPin("renderer", "Renderer", "vfx.renderer")}});
    return schema;
}

EditorVfxGraphAsset MakeDefaultEditorVfxGraph(std::string assetGuid, std::string name) {
    EditorVfxGraphAsset asset;
    asset.assetGuid = std::move(assetGuid);
    asset.name = name.empty() ? "VFX System" : std::move(name);

    const auto makeNode = [](std::string type, std::string label, float x, float y) {
        EditorGraphNode node;
        node.id = MakeEditorGraphElementId("node");
        node.typeId = std::move(type);
        node.label = std::move(label);
        node.positionX = x;
        node.positionY = y;
        return node;
    };
    EditorGraphNode output = makeNode("vfx.system.output", "VFX System Output", 720.0f, 80.0f);
    EditorGraphNode emitter = makeNode("vfx.emitter", "Main Emitter", 470.0f, 80.0f);
    emitter.properties["name"] = "Main";
    EditorGraphNode spawn = makeNode("vfx.spawn.rate", "Spawn Rate", 40.0f, 20.0f);
    spawn.properties["rate"] = "32";
    EditorGraphNode lifetime = makeNode("vfx.constant.float", "Lifetime", 40.0f, 100.0f);
    lifetime.properties["value"] = "1.5";
    EditorGraphNode initialize = makeNode("vfx.initialize.velocity", "Initialize Velocity", 210.0f, 180.0f);
    initialize.properties["velocity"] = "0, 2, 0";
    EditorGraphNode update = makeNode("vfx.update.gravity", "Gravity + Drag", 210.0f, 270.0f);
    update.properties["gravity"] = "0, -9.81, 0";
    update.properties["drag"] = "0.05";
    EditorGraphNode renderer = makeNode("vfx.renderer.sprite", "Sprite Renderer", 210.0f, 370.0f);
    renderer.properties["size"] = "0.12";
    renderer.properties["color"] = "1, 0.75, 0.2, 1";
    renderer.properties["materialAssetGuid"] = "";
    renderer.properties["textureAssetGuid"] = "";
    asset.graph.nodes = {output, emitter, spawn, lifetime, initialize, update, renderer};
    const auto link = [&](const EditorGraphNode& from, std::string fromPin,
        const EditorGraphNode& to, std::string toPin) {
        asset.graph.links.push_back({MakeEditorGraphElementId("link"), from.id,
            std::move(fromPin), to.id, std::move(toPin)});
    };
    link(emitter, "emitter", output, "emitters");
    link(spawn, "value", emitter, "spawnRate");
    link(lifetime, "value", emitter, "lifetime");
    link(initialize, "initialize", emitter, "initialize");
    link(update, "update", emitter, "update");
    link(renderer, "renderer", emitter, "renderer");
    asset.graph.revision = 1;
    asset.revision = 1;
    return asset;
}

EditorVfxCompileArtifact CompileEditorVfxGraph(
    const EditorVfxGraphAsset& asset,
    const EditorGraphSchema& schema) {
    EditorVfxCompileArtifact artifact;
    const EditorGraphValidationReport validation = ValidateEditorGraph(asset.graph, schema);
    for (const EditorGraphIssue& issue : validation.issues) {
        artifact.diagnostics.push_back({issue.severity, issue.code, issue.nodeId, issue.message});
    }
    const std::vector<const EditorGraphNode*> outputs = [&]() {
        std::vector<const EditorGraphNode*> result;
        for (const EditorGraphNode& node : asset.graph.nodes) {
            if (node.typeId == "vfx.system.output") result.push_back(&node);
        }
        return result;
    }();
    if (outputs.size() != 1) {
        AddDiagnostic(artifact, EditorGraphIssueSeverity::Error, "vfx.output_count", {},
            "VFX Graph must contain exactly one VFX System Output node.");
    }
    if (asset.maxParticles == 0 || asset.maxParticles > 16u * 1024u * 1024u) {
        AddDiagnostic(artifact, EditorGraphIssueSeverity::Error, "vfx.max_particles", {},
            "VFX max particle capacity must be between 1 and 16777216.");
    }
    if (!std::isfinite(asset.fixedTimeStep) || asset.fixedTimeStep <= 0.0f ||
        asset.fixedTimeStep > 0.1f) {
        AddDiagnostic(artifact, EditorGraphIssueSeverity::Error, "vfx.fixed_timestep", {},
            "VFX fixed time step must be finite and in the range (0, 0.1].");
    }
    if (validation.HasErrors() || outputs.size() != 1 || HasErrors(artifact)) return artifact;

    std::vector<const EditorGraphNode*> emitters;
    for (const EditorGraphLink& link : asset.graph.links) {
        if (link.toNodeId != outputs.front()->id || link.toPinId != "emitters") continue;
        const EditorGraphNode* emitter = FindEditorGraphNode(asset.graph, link.fromNodeId);
        if (emitter != nullptr && emitter->typeId == "vfx.emitter") emitters.push_back(emitter);
    }
    std::sort(emitters.begin(), emitters.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->id < rhs->id;
    });
    if (emitters.empty() || emitters.size() > kEditorVfxGraphMaxEmitters) {
        AddDiagnostic(artifact, EditorGraphIssueSeverity::Error, "vfx.emitter_count", {},
            "VFX Graph must connect between 1 and 64 Emitters to System Output.");
        return artifact;
    }

    std::set<std::string> dependencies;
    for (const EditorGraphNode* emitterNode : emitters) {
        EditorVfxEmitterProgram program;
        program.nodeId = emitterNode->id;
        program.name = Property(*emitterNode, "name", "Emitter");
        program.spawnRate = FloatFromNode(InputSource(asset, *emitterNode, "spawnRate"),
            "rate", 32.0f, artifact, "vfx.spawn_rate", 0.0f, 1000000.0f);
        if (const EditorGraphNode* spawn = InputSource(asset, *emitterNode, "spawnRate");
            spawn != nullptr && spawn->typeId == "vfx.constant.float") {
            program.spawnRate = FloatFromNode(spawn, "value", 32.0f, artifact,
                "vfx.spawn_rate", 0.0f, 1000000.0f);
        }
        program.burstCount = static_cast<uint32_t>(FloatFromNode(
            InputSource(asset, *emitterNode, "burst"), "count", 0.0f, artifact,
            "vfx.burst_count", 0.0f, 1000000.0f));
        program.lifetime = FloatFromNode(InputSource(asset, *emitterNode, "lifetime"),
            "value", 1.0f, artifact, "vfx.lifetime", 0.001f, 86400.0f);

        const EditorGraphNode* initialize = InputSource(asset, *emitterNode, "initialize");
        const float defaultVelocity[3] = {0.0f, 1.0f, 0.0f};
        VectorFromNode(initialize, "velocity", defaultVelocity, program.initialVelocity,
            artifact, "vfx.initial_velocity");
        const EditorGraphNode* update = InputSource(asset, *emitterNode, "update");
        const float defaultGravity[3] = {0.0f, -9.81f, 0.0f};
        VectorFromNode(update, "gravity", defaultGravity, program.gravity,
            artifact, "vfx.gravity");
        program.drag = FloatFromNode(update, "drag", 0.0f, artifact,
            "vfx.drag", 0.0f, 10000.0f);

        const EditorGraphNode* renderer = InputSource(asset, *emitterNode, "renderer");
        if (renderer == nullptr) continue;
        if (renderer->typeId == "vfx.renderer.ribbon") program.renderer = EditorVfxRendererKind::Ribbon;
        else if (renderer->typeId == "vfx.renderer.beam") program.renderer = EditorVfxRendererKind::Beam;
        else program.renderer = EditorVfxRendererKind::Sprite;
        program.size = FloatFromNode(renderer, "size", 0.1f, artifact,
            "vfx.renderer_size", 0.0001f, 100000.0f);
        const float defaultColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        VectorFromNode(renderer, "color", defaultColor, program.color,
            artifact, "vfx.renderer_color");
        program.materialAssetGuid = Property(*renderer, "materialAssetGuid", {});
        program.textureAssetGuid = Property(*renderer, "textureAssetGuid", {});
        if (!program.materialAssetGuid.empty()) dependencies.insert(program.materialAssetGuid);
        if (!program.textureAssetGuid.empty()) dependencies.insert(program.textureAssetGuid);
        artifact.emitters.push_back(std::move(program));
    }
    if (HasErrors(artifact)) return artifact;

    std::ostringstream programText;
    programText << "VFX_PROGRAM 1\nASSET " << std::quoted(asset.assetGuid) << "\nTARGET "
                << ToString(asset.simulationTarget) << "\nMAX_PARTICLES " << asset.maxParticles
                << "\nFIXED_TIMESTEP " << std::setprecision(9) << asset.fixedTimeStep << "\n";
    for (const EditorVfxEmitterProgram& emitter : artifact.emitters) {
        programText << "EMITTER " << std::quoted(emitter.nodeId) << ' ' << std::quoted(emitter.name)
                    << ' ' << emitter.spawnRate << ' ' << emitter.burstCount << ' ' << emitter.lifetime
                    << ' ' << ToString(emitter.renderer) << ' ' << emitter.size << '\n';
    }
    programText << "END\n";
    artifact.generatedProgram = programText.str();

    std::ostringstream hlsl;
    hlsl << "// Generated VFX simulation artifact. Do not edit.\n"
         << "// Asset " << asset.assetGuid << "\n"
         << "struct VfxParticle { float3 position; float3 velocity; float4 color; float age; float lifetime; float size; };\n"
         << "[numthreads(64, 1, 1)] void UpdateVfx(uint3 id : SV_DispatchThreadID) {\n"
         << "    // Bounds checks and buffer bindings are supplied by the runtime pipeline.\n"
         << "}\n";
    artifact.simulationHlsl = hlsl.str();
    artifact.assetDependencies.assign(dependencies.begin(), dependencies.end());
    artifact.sourceFingerprint = HashText(artifact.generatedProgram + artifact.simulationHlsl);
    artifact.succeeded = true;
    return artifact;
}

void EditorVfxGraphService::Bind(EditorVfxGraphDocumentProvider* provider,
    EditorTransactionStack* transactions, EditorDocumentManager* documents,
    EffectRuntime* runtime, const EditorAssetRegistry* assets) {
    provider_ = provider;
    transactions_ = transactions;
    documents_ = documents;
    runtime_ = runtime;
    assets_ = assets;
}

void EditorVfxGraphService::SetActiveDocument(EditorDocumentId document) {
    activeDocument_ = std::move(document);
    if (const EditorVfxGraphAsset* asset = ActiveAsset()) UpdateCompileArtifact(*asset);
}

EditorVfxGraphAsset* EditorVfxGraphService::ActiveAsset() {
    return provider_ != nullptr ? provider_->Asset(activeDocument_) : nullptr;
}

const EditorVfxGraphAsset* EditorVfxGraphService::ActiveAsset() const {
    return const_cast<EditorVfxGraphService*>(this)->ActiveAsset();
}

bool EditorVfxGraphService::ValidateConnection(const EditorVfxGraphAsset& asset,
    std::string_view fromNodeId, std::string_view fromPinId,
    std::string_view toNodeId, std::string_view toPinId,
    std::string& errorMessage) const {
    const EditorGraphNode* from = FindEditorGraphNode(asset.graph, fromNodeId);
    const EditorGraphNode* to = FindEditorGraphNode(asset.graph, toNodeId);
    const EditorGraphPinDefinition* fromPin = from != nullptr
        ? schema_.FindPin(from->typeId, fromPinId) : nullptr;
    const EditorGraphPinDefinition* toPin = to != nullptr
        ? schema_.FindPin(to->typeId, toPinId) : nullptr;
    if (fromPin == nullptr || toPin == nullptr ||
        fromPin->direction != EditorGraphPinDirection::Output ||
        toPin->direction != EditorGraphPinDirection::Input) {
        errorMessage = "VFX Graph connection references an unavailable node or pin.";
        return false;
    }
    if (!schema_.CanConnect(fromPin->typeId, toPin->typeId)) {
        errorMessage = "VFX Graph pin types are incompatible: " + fromPin->typeId +
            " -> " + toPin->typeId + ".";
        return false;
    }
    if (EditorGraphWouldCreateCycle(asset.graph, fromNodeId, toNodeId)) {
        errorMessage = "VFX Graph connection would create a cycle.";
        return false;
    }
    return true;
}

bool EditorVfxGraphService::AddNode(std::string_view nodeTypeId, float positionX,
    float positionY, std::string* createdNodeId, std::string& errorMessage) {
    EditorVfxGraphAsset* asset = ActiveAsset();
    const EditorGraphNodeTypeDefinition* type = schema_.FindNodeType(nodeTypeId);
    if (asset == nullptr || type == nullptr || !std::isfinite(positionX) || !std::isfinite(positionY)) {
        errorMessage = "Active VFX Graph, node type, or node position is invalid.";
        return false;
    }
    if (asset->graph.nodes.size() >= kEditorGraphMaxNodes) {
        errorMessage = "VFX Graph node safety limit reached.";
        return false;
    }
    if (nodeTypeId == "vfx.system.output" &&
        std::any_of(asset->graph.nodes.begin(), asset->graph.nodes.end(), [](const auto& node) {
            return node.typeId == "vfx.system.output";
        })) {
        errorMessage = "VFX Graph already has a System Output node.";
        return false;
    }
    EditorVfxGraphAsset before = *asset;
    EditorVfxGraphAsset after = before;
    EditorGraphNode node;
    node.id = MakeEditorGraphElementId("node");
    node.typeId = type->typeId;
    node.label = type->displayName;
    node.positionX = positionX;
    node.positionY = positionY;
    if (node.typeId == "vfx.emitter") node.properties["name"] = "Emitter";
    if (node.typeId == "vfx.spawn.rate") node.properties["rate"] = "32";
    if (node.typeId == "vfx.spawn.burst") node.properties["count"] = "16";
    if (node.typeId == "vfx.constant.float") node.properties["value"] = "1";
    if (node.typeId == "vfx.initialize.velocity") node.properties["velocity"] = "0, 1, 0";
    if (node.typeId == "vfx.update.gravity") {
        node.properties["gravity"] = "0, -9.81, 0";
        node.properties["drag"] = "0";
    }
    if (node.typeId.rfind("vfx.renderer.", 0) == 0) {
        node.properties["size"] = "0.1";
        node.properties["color"] = "1, 1, 1, 1";
        node.properties["materialAssetGuid"] = "";
        node.properties["textureAssetGuid"] = "";
    }
    const std::string id = node.id;
    after.graph.nodes.push_back(std::move(node));
    ++after.graph.revision;
    ++after.revision;
    if (!CommitMutation("Add VFX Graph Node", std::move(before), std::move(after), errorMessage)) return false;
    if (createdNodeId != nullptr) *createdNodeId = id;
    return true;
}

bool EditorVfxGraphService::RemoveNode(std::string_view nodeId, std::string& errorMessage) {
    EditorVfxGraphAsset* asset = ActiveAsset();
    const EditorGraphNode* node = asset != nullptr ? FindEditorGraphNode(asset->graph, nodeId) : nullptr;
    if (node == nullptr || node->typeId == "vfx.system.output") {
        errorMessage = node == nullptr ? "VFX Graph node was not found." : "VFX System Output cannot be removed.";
        return false;
    }
    EditorVfxGraphAsset before = *asset;
    EditorVfxGraphAsset after = before;
    RemoveEditorGraphNode(after.graph, nodeId);
    ++after.revision;
    return CommitMutation("Remove VFX Graph Node", std::move(before), std::move(after), errorMessage);
}

bool EditorVfxGraphService::Connect(std::string_view fromNodeId, std::string_view fromPinId,
    std::string_view toNodeId, std::string_view toPinId, std::string& errorMessage) {
    EditorVfxGraphAsset* asset = ActiveAsset();
    if (asset == nullptr || !ValidateConnection(*asset, fromNodeId, fromPinId,
            toNodeId, toPinId, errorMessage)) return false;
    if (asset->graph.links.size() >= kEditorGraphMaxLinks) {
        errorMessage = "VFX Graph link safety limit reached.";
        return false;
    }
    EditorVfxGraphAsset before = *asset;
    EditorVfxGraphAsset after = before;
    const EditorGraphNode* to = FindEditorGraphNode(after.graph, toNodeId);
    const EditorGraphPinDefinition* pin = to != nullptr ? schema_.FindPin(to->typeId, toPinId) : nullptr;
    if (pin != nullptr && !pin->allowMultipleLinks) {
        after.graph.links.erase(std::remove_if(after.graph.links.begin(), after.graph.links.end(),
            [&](const auto& link) { return link.toNodeId == toNodeId && link.toPinId == toPinId; }),
            after.graph.links.end());
    }
    after.graph.links.push_back({MakeEditorGraphElementId("link"), std::string(fromNodeId),
        std::string(fromPinId), std::string(toNodeId), std::string(toPinId)});
    ++after.graph.revision;
    ++after.revision;
    return CommitMutation("Connect VFX Graph Pins", std::move(before), std::move(after), errorMessage);
}

bool EditorVfxGraphService::Disconnect(std::string_view linkId, std::string& errorMessage) {
    EditorVfxGraphAsset* asset = ActiveAsset();
    if (asset == nullptr || FindEditorGraphLink(asset->graph, linkId) == nullptr) {
        errorMessage = "VFX Graph link was not found.";
        return false;
    }
    EditorVfxGraphAsset before = *asset;
    EditorVfxGraphAsset after = before;
    after.graph.links.erase(std::remove_if(after.graph.links.begin(), after.graph.links.end(),
        [&](const auto& link) { return link.id == linkId; }), after.graph.links.end());
    ++after.graph.revision;
    ++after.revision;
    return CommitMutation("Disconnect VFX Graph Pins", std::move(before), std::move(after), errorMessage);
}

bool EditorVfxGraphService::SetNodeProperty(std::string_view nodeId, std::string key,
    std::string value, std::string& errorMessage) {
    EditorVfxGraphAsset* asset = ActiveAsset();
    if (asset == nullptr || key.empty()) {
        errorMessage = "Active VFX Graph or property key is unavailable.";
        return false;
    }
    EditorVfxGraphAsset before = *asset;
    EditorVfxGraphAsset after = before;
    EditorGraphNode* node = FindEditorGraphNode(after.graph, nodeId);
    if (node == nullptr) {
        errorMessage = "VFX Graph node was not found.";
        return false;
    }
    if (node->properties[key] == value) return true;
    node->properties[std::move(key)] = std::move(value);
    ++after.graph.revision;
    ++after.revision;
    return CommitMutation("Edit VFX Graph Property", std::move(before), std::move(after), errorMessage);
}

bool EditorVfxGraphService::MoveNode(std::string_view nodeId, float positionX,
    float positionY, std::string& errorMessage) {
    if (!std::isfinite(positionX) || !std::isfinite(positionY)) {
        errorMessage = "VFX Graph node position must be finite.";
        return false;
    }
    EditorVfxGraphAsset* asset = ActiveAsset();
    if (asset == nullptr) {
        errorMessage = "Active VFX Graph is unavailable.";
        return false;
    }
    EditorVfxGraphAsset before = *asset;
    EditorVfxGraphAsset after = before;
    EditorGraphNode* node = FindEditorGraphNode(after.graph, nodeId);
    if (node == nullptr) {
        errorMessage = "VFX Graph node was not found.";
        return false;
    }
    node->positionX = positionX;
    node->positionY = positionY;
    ++after.graph.revision;
    ++after.revision;
    return CommitMutation("Move VFX Graph Node", std::move(before), std::move(after), errorMessage);
}

bool EditorVfxGraphService::SetSimulationSettings(EditorVfxSimulationTarget target,
    uint32_t maxParticles, float fixedTimeStep, std::string& errorMessage) {
    EditorVfxGraphAsset* asset = ActiveAsset();
    if (asset == nullptr || maxParticles == 0 || maxParticles > 16u * 1024u * 1024u ||
        !std::isfinite(fixedTimeStep) || fixedTimeStep <= 0.0f || fixedTimeStep > 0.1f) {
        errorMessage = "VFX simulation settings are outside their supported safety limits.";
        return false;
    }
    if (asset->simulationTarget == target && asset->maxParticles == maxParticles &&
        asset->fixedTimeStep == fixedTimeStep) return true;
    EditorVfxGraphAsset before = *asset;
    EditorVfxGraphAsset after = before;
    after.simulationTarget = target;
    after.maxParticles = maxParticles;
    after.fixedTimeStep = fixedTimeStep;
    ++after.revision;
    return CommitMutation("Edit VFX Simulation Settings", std::move(before),
        std::move(after), errorMessage);
}

bool EditorVfxGraphService::CommitMutation(std::string_view label, EditorVfxGraphAsset before,
    EditorVfxGraphAsset after, std::string& errorMessage) {
    if (provider_ == nullptr || transactions_ == nullptr || !activeDocument_.IsValid()) {
        errorMessage = "VFX Graph document or transaction service is unavailable.";
        return false;
    }
    if (!provider_->Publish(activeDocument_, after)) {
        errorMessage = "VFX Graph mutation could not be published.";
        return false;
    }
    EditorObjectHandle target;
    target.domain = EditorDomainId::VfxGraphNode;
    target.stableId = activeDocument_.assetGuid;
    target.displayName = after.name;
    EditorError error;
    if (!transactions_->PushCommand(std::string(label), std::move(target),
            std::make_shared<EditorVfxGraphUndoCommand>(activeDocument_, before, after), &error)) {
        provider_->Publish(activeDocument_, std::move(before));
        errorMessage = error.message;
        return false;
    }
    UpdateCompileArtifact(after);
    if (documents_ != nullptr) documents_->MarkDirty(activeDocument_, label);
    if (mutationCallback_) mutationCallback_(activeDocument_, label);
    return true;
}

bool EditorVfxGraphService::PublishFromCommand(const EditorDocumentId& document,
    const EditorVfxGraphAsset& asset, std::string& errorMessage) {
    if (provider_ == nullptr || !provider_->Publish(document, asset)) {
        errorMessage = "VFX Graph transaction could not publish its snapshot.";
        return false;
    }
    if (document == activeDocument_) UpdateCompileArtifact(asset);
    if (documents_ != nullptr) documents_->MarkDirty(document, "VFX Graph Undo/Redo");
    if (mutationCallback_) mutationCallback_(document, "VFX Graph Undo/Redo");
    return true;
}

void EditorVfxGraphService::UpdateCompileArtifact(const EditorVfxGraphAsset& asset) {
    lastCompileArtifact_ = CompileEditorVfxGraph(asset, schema_);
    if (lastCompileArtifact_.succeeded) lastSuccessfulArtifact_ = lastCompileArtifact_;
}

bool EditorVfxGraphService::Recompile(std::string& errorMessage) {
    const EditorVfxGraphAsset* asset = ActiveAsset();
    if (asset == nullptr) {
        errorMessage = "Active VFX Graph is unavailable.";
        return false;
    }
    UpdateCompileArtifact(*asset);
    if (!lastCompileArtifact_.succeeded) {
        errorMessage = lastCompileArtifact_.diagnostics.empty()
            ? "VFX Graph compile failed." : lastCompileArtifact_.diagnostics.front().message;
        return false;
    }
    return true;
}

bool EditorVfxGraphService::ApplyPreview(std::string& errorMessage) {
    const EditorVfxGraphAsset* graphAsset = ActiveAsset();
    if (runtime_ == nullptr || graphAsset == nullptr) {
        errorMessage = "VFX runtime or active Graph is unavailable.";
        return false;
    }
    if (!Recompile(errorMessage)) return false;
    EffectAsset runtimeAsset;
    runtimeAsset.name = graphAsset->name;
    float longestLifetime = 0.001f;
    uint32_t componentId = 1;
    for (const EditorVfxEmitterProgram& emitter : lastSuccessfulArtifact_.emitters) {
        EffectComponentCommon common;
        common.id = componentId++;
        common.name = emitter.name;
        common.duration = emitter.lifetime;
        common.size = {emitter.size, emitter.size, emitter.size};
        common.color = {emitter.color[0], emitter.color[1], emitter.color[2], emitter.color[3]};
        if (!emitter.textureAssetGuid.empty()) {
            const EditorAssetRecord* texture = assets_ != nullptr
                ? assets_->FindByGuid(emitter.textureAssetGuid) : nullptr;
            if (texture == nullptr || texture->kind != EditorAssetKind::Texture || texture->missing) {
                errorMessage = "VFX preview cannot resolve Texture Asset GUID: " + emitter.textureAssetGuid;
                return false;
            }
            common.texture = texture->sourcePath;
        }
        if (!emitter.materialAssetGuid.empty()) {
            const EditorAssetRecord* material = assets_ != nullptr
                ? assets_->FindByGuid(emitter.materialAssetGuid) : nullptr;
            if (material == nullptr || material->kind != EditorAssetKind::MaterialGraph || material->missing) {
                errorMessage = "VFX preview cannot resolve Material Asset GUID: " + emitter.materialAssetGuid;
                return false;
            }
        }
        longestLifetime = (std::max)(longestLifetime, emitter.lifetime);
        if (emitter.renderer == EditorVfxRendererKind::Ribbon) {
            common.type = EffectComponentType::Trail;
            common.techniqueId = "TrailRibbon";
            common.rendererId = "TrailRenderer";
            common.simulationId = "CpuTimeline";
            TrailComponentAsset component;
            component.common = common;
            component.settings.width = emitter.size;
            runtimeAsset.MutableComponents().Add(std::move(component));
        } else if (emitter.renderer == EditorVfxRendererKind::Beam) {
            common.type = EffectComponentType::Beam;
            common.techniqueId = "BeamLightning";
            common.rendererId = "BeamRenderer";
            common.simulationId = "CpuTimeline";
            BeamComponentAsset component;
            component.common = common;
            runtimeAsset.MutableComponents().Add(std::move(component));
        } else {
            common.type = EffectComponentType::Particle;
            common.techniqueId = "ParticleAdditive";
            common.rendererId = "ParticleRenderer";
            common.simulationId = graphAsset->simulationTarget == EditorVfxSimulationTarget::GPU
                ? "GpuSimulation" : "CpuSpawnGpuSim";
            ParticleComponentAsset component;
            component.common = common;
            component.settings.spawnCount = static_cast<float>(emitter.burstCount);
            component.settings.spawnFrequency = emitter.spawnRate > 0.0f
                ? 1.0f / emitter.spawnRate : 0.0f;
            component.settings.spawnRadius = emitter.size;
            runtimeAsset.MutableComponents().Add(std::move(component));
        }
    }
    runtimeAsset.lifetime = longestLifetime;
    if (!runtime_->RegisterAsset(std::move(runtimeAsset), &errorMessage)) return false;
    runtime_->ClearInstances();
    runtime_->PlayEffect(graphAsset->name, {0.0f, 0.0f, 0.0f});
    return true;
}

const char* ToString(EditorVfxSimulationTarget value) {
    return value == EditorVfxSimulationTarget::CPU ? "CPU" : "GPU";
}

const char* ToString(EditorVfxRendererKind value) {
    switch (value) {
    case EditorVfxRendererKind::Sprite: return "Sprite";
    case EditorVfxRendererKind::Ribbon: return "Ribbon";
    case EditorVfxRendererKind::Beam: return "Beam";
    }
    return "Sprite";
}

bool EditorVfxSimulationTargetFromString(std::string_view value,
    EditorVfxSimulationTarget& output) {
    if (value == "CPU") output = EditorVfxSimulationTarget::CPU;
    else if (value == "GPU") output = EditorVfxSimulationTarget::GPU;
    else return false;
    return true;
}

} // namespace editor
