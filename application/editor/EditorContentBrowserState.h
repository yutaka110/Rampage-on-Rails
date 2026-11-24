#pragma once

#include "EditorAssetRegistry.h"

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor {

enum class EditorContentBrowserViewMode {
    List,
    Grid,
};

enum class EditorAssetSourceControlStatus {
    Unknown,
    Untracked,
    Added,
    Tracked,
    Modified,
    Conflicted,
    Locked,
};

enum class EditorAssetCookStatus {
    Unknown,
    NotCooked,
    UpToDate,
    OutOfDate,
    Failed,
};

struct EditorAssetWorkspaceStatus {
    EditorAssetSourceControlStatus sourceControl = EditorAssetSourceControlStatus::Unknown;
    EditorAssetCookStatus cook = EditorAssetCookStatus::Unknown;
    bool dirty = false;
    std::string detail;
};

class IEditorAssetWorkspaceStatusProvider {
public:
    virtual ~IEditorAssetWorkspaceStatusProvider() = default;
    virtual EditorAssetWorkspaceStatus QueryStatus(
        const EditorAssetRecord& record) const = 0;
};

class EditorAssetWorkspaceStatusRegistry final
    : public IEditorAssetWorkspaceStatusProvider {
public:
    bool Publish(std::string guid, EditorAssetWorkspaceStatus status);
    bool Remove(std::string_view guid);
    void Clear();
    EditorAssetWorkspaceStatus QueryStatus(
        const EditorAssetRecord& record) const override;
    uint32_t Revision() const { return revision_; }

private:
    std::unordered_map<std::string, EditorAssetWorkspaceStatus> statuses_;
    uint32_t revision_ = 0;
};

struct EditorContentBrowserCollection {
    std::string name;
    std::unordered_set<std::string> assetGuids;
};

enum class EditorAssetContextAction { Duplicate, Reimport, Delete };

struct EditorAssetContextRequest {
    EditorAssetContextAction action;
    EditorAssetKind kind;
    std::string id;
};

class EditorContentBrowserState {
public:
    EditorContentBrowserState();

    void SetPath(std::filesystem::path path);
    void EnsureLoaded();
    bool Load();
    bool Save();
    void SaveIfDirty();

    void SetSelectedFolder(std::string folder);
    void SetSearchText(std::string text);
    void SetTagFilter(std::string tag);
    void SetKindFilter(EditorAssetKind kind);
    void SetViewMode(EditorContentBrowserViewMode mode);
    void SetSelectedAssetGuid(std::string guid);
    void SetActiveCollection(std::string name);
    void SetFavoritesOnly(bool enabled);

    void QueueAssetContextAction(EditorAssetContextAction action, const EditorAssetRecord& asset) {
        pendingAssetContextRequest_ = EditorAssetContextRequest{action, asset.kind, asset.id};
    }
    std::optional<EditorAssetContextRequest> TakeAssetContextRequest() {
        return std::exchange(pendingAssetContextRequest_, std::nullopt);
    }

    bool ToggleFavorite(std::string_view guid);
    bool IsFavorite(std::string_view guid) const;
    bool CreateCollection(std::string name);
    bool RemoveCollection(std::string_view name);
    bool AddToCollection(std::string_view name, std::string_view guid);
    bool RemoveFromCollection(std::string_view name, std::string_view guid);
    bool IsInCollection(std::string_view name, std::string_view guid) const;

    std::vector<std::string> BuildFolders(const EditorAssetRegistry& registry) const;
    std::vector<std::string> BuildTags(const EditorAssetRegistry& registry) const;
    std::vector<const EditorAssetRecord*> FilterAssets(
        const EditorAssetRegistry& registry) const;

    const std::filesystem::path& Path() const { return path_; }
    const std::string& SelectedFolder() const { return selectedFolder_; }
    const std::string& SearchText() const { return searchText_; }
    const std::string& TagFilter() const { return tagFilter_; }
    EditorAssetKind KindFilter() const { return kindFilter_; }
    EditorContentBrowserViewMode ViewMode() const { return viewMode_; }
    const std::string& SelectedAssetGuid() const { return selectedAssetGuid_; }
    const std::string& ActiveCollection() const { return activeCollection_; }
    bool FavoritesOnly() const { return favoritesOnly_; }
    bool Loaded() const { return loaded_; }
    bool Dirty() const { return dirty_; }
    bool LastLoadValid() const { return lastLoadValid_; }
    uint32_t Revision() const { return revision_; }
    const std::string& StatusMessage() const { return statusMessage_; }
    const std::unordered_set<std::string>& Favorites() const { return favorites_; }
    const std::unordered_map<std::string, EditorContentBrowserCollection>& Collections() const {
        return collections_;
    }

private:
    void MarkDirty();
    void Reset();

    std::filesystem::path path_;
    std::string selectedFolder_ = "Resources";
    std::string searchText_;
    std::string tagFilter_;
    EditorAssetKind kindFilter_ = EditorAssetKind::Unknown;
    EditorContentBrowserViewMode viewMode_ = EditorContentBrowserViewMode::Grid;
    std::string selectedAssetGuid_;
    std::string activeCollection_;
    bool favoritesOnly_ = false;
    std::unordered_set<std::string> favorites_;
    std::unordered_map<std::string, EditorContentBrowserCollection> collections_;
    bool loaded_ = false;
    bool dirty_ = false;
    bool lastLoadValid_ = true;
    std::chrono::steady_clock::time_point dirtyTouchedAt_{};
    uint32_t revision_ = 0;
    std::string statusMessage_;
    std::optional<EditorAssetContextRequest> pendingAssetContextRequest_;
};

const char* ToString(EditorContentBrowserViewMode mode);
const char* ToString(EditorAssetSourceControlStatus status);
const char* ToString(EditorAssetCookStatus status);

} // namespace editor
