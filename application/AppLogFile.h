#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace app {

struct RotatingLogPolicy {
    std::uintmax_t maxFileBytes = 16ull * 1024ull * 1024ull;
    uint32_t retainedGenerations = 4;
    std::uintmax_t maxDirectoryBytes = 256ull * 1024ull * 1024ull;
    std::chrono::milliseconds sizeCheckInterval{250};
    std::chrono::seconds directoryCheckInterval{10};
};

namespace detail {

inline void RotateLogFile(
    const std::filesystem::path& path,
    const RotatingLogPolicy& policy) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size < policy.maxFileBytes) return;

    if (policy.retainedGenerations == 0) {
        std::filesystem::remove(path, error);
        return;
    }

    const std::filesystem::path oldest =
        path.string() + "." + std::to_string(policy.retainedGenerations);
    std::filesystem::remove(oldest, error);
    error.clear();
    for (uint32_t generation = policy.retainedGenerations; generation > 1; --generation) {
        const std::filesystem::path source =
            path.string() + "." + std::to_string(generation - 1);
        const std::filesystem::path destination =
            path.string() + "." + std::to_string(generation);
        if (!std::filesystem::exists(source, error)) {
            error.clear();
            continue;
        }
        std::filesystem::rename(source, destination, error);
        error.clear();
    }
    std::filesystem::rename(path, path.string() + ".1", error);
    if (error) {
        error.clear();
        std::filesystem::remove(path, error);
    }
}

inline void EnforceDirectoryBudget(
    const std::filesystem::path& directory,
    const std::filesystem::path& activePath,
    std::uintmax_t budgetBytes) {
    if (budgetBytes == 0) return;
    struct Candidate {
        std::filesystem::path path;
        std::filesystem::file_time_type writeTime{};
        std::uintmax_t bytes = 0;
    };

    std::error_code error;
    std::vector<Candidate> candidates;
    std::uintmax_t totalBytes = 0;
    for (std::filesystem::directory_iterator it(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error), end;
         it != end;
         it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!it->is_regular_file(error)) {
            error.clear();
            continue;
        }
        const std::string fileName = it->path().filename().string();
        if (it->path().extension() != ".log" && fileName.find(".log.") == std::string::npos) {
            continue;
        }
        const std::uintmax_t bytes = it->file_size(error);
        if (error) {
            error.clear();
            continue;
        }
        totalBytes += bytes;
        if (it->path().lexically_normal() != activePath.lexically_normal()) {
            candidates.push_back(Candidate{it->path(), it->last_write_time(error), bytes});
            error.clear();
        }
    }
    if (totalBytes <= budgetBytes) return;

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.writeTime < b.writeTime;
    });
    for (const Candidate& candidate : candidates) {
        if (totalBytes <= budgetBytes) break;
        if (std::filesystem::remove(candidate.path, error)) {
            totalBytes = candidate.bytes <= totalBytes ? totalBytes - candidate.bytes : 0;
        }
        error.clear();
    }
}

} // namespace detail

inline std::ofstream OpenRotatingLog(
    const std::filesystem::path& path,
    bool truncate,
    const RotatingLogPolicy& policy = {}) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastSizeChecks;
    static std::chrono::steady_clock::time_point lastDirectoryCheck{};
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock(mutex);
        std::error_code error;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), error);
        }
        const std::string key = path.lexically_normal().generic_string();
        const auto found = lastSizeChecks.find(key);
        if (truncate) {
            lastSizeChecks[key] = now;
        } else if (found == lastSizeChecks.end() ||
            now - found->second >= policy.sizeCheckInterval) {
            detail::RotateLogFile(path, policy);
            lastSizeChecks[key] = now;
        }
        if (truncate || lastDirectoryCheck.time_since_epoch().count() == 0 ||
            now - lastDirectoryCheck >= policy.directoryCheckInterval) {
            const std::filesystem::path directory =
                path.parent_path().empty() ? std::filesystem::current_path() : path.parent_path();
            detail::EnforceDirectoryBudget(directory, path, policy.maxDirectoryBytes);
            lastDirectoryCheck = now;
        }
    }
    return std::ofstream(path, std::ios::out | (truncate ? std::ios::trunc : std::ios::app));
}

inline std::ofstream OpenRotatingLog(
    const std::filesystem::path& path,
    const RotatingLogPolicy& policy = {}) {
    return OpenRotatingLog(path, false, policy);
}

} // namespace app
