#include "custom_component_github.hpp"

#include "custom_component_update_utils.hpp"
#include "download.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <yyjson.h>

namespace sphaira::custom::github {
namespace {

bool IsRepoCharacter(char character) {
    const auto value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '-' || character == '_' || character == '.';
}

bool ParseGitHubRepo(std::string repo, std::string& owner, std::string& name) {
    constexpr std::string_view https_prefix{"https://github.com/"};
    constexpr std::string_view http_prefix{"http://github.com/"};
    if (repo.starts_with(https_prefix)) {
        repo.erase(0, https_prefix.size());
    } else if (repo.starts_with(http_prefix)) {
        repo.erase(0, http_prefix.size());
    }
    while (!repo.empty() && repo.back() == '/') {
        repo.pop_back();
    }
    if (repo.ends_with(".git")) {
        repo.resize(repo.size() - 4);
    }
    const auto slash = repo.find('/');
    if (slash == std::string::npos || slash == 0 || slash == repo.size() - 1 ||
        repo.find('/', slash + 1) != std::string::npos) {
        return false;
    }
    owner = repo.substr(0, slash);
    name = repo.substr(slash + 1);
    const auto valid_segment = [](const std::string& segment) {
        if (segment == "." || segment == "..") {
            return false;
        }
        return std::all_of(segment.begin(), segment.end(), IsRepoCharacter);
    };
    return valid_segment(owner) && valid_segment(name);
}

std::string EncodeUrlPathSegment(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) || character == '-' || character == '_' || character == '.' || character == '~') {
            encoded += character;
        } else {
            encoded += '%';
            encoded += hex[byte >> 4];
            encoded += hex[byte & 0x0F];
        }
    }
    return encoded;
}

} // namespace

bool FetchLatestReleaseTag(ui::ProgressBox* progress, const std::string& repository, std::string& tag) {
    std::string owner;
    std::string name;
    if (!ParseGitHubRepo(repository, owner, name)) {
        return false;
    }
    const auto response = curl::Api().ToMemory(
        curl::Url{"https://api.github.com/repos/" + owner + "/" + name + "/releases/latest"},
        curl::Header{{"Accept", "application/vnd.github+json"}},
        curl::OnProgress{progress->OnDownloadProgressCallback()});
    if (!response.success || response.data.empty()) {
        return false;
    }
    auto* doc = yyjson_read(
        reinterpret_cast<const char*>(response.data.data()), response.data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    auto* root = yyjson_doc_get_root(doc);
    auto* tag_value = yyjson_obj_get(root, "tag_name");
    if (!yyjson_is_obj(root) || !yyjson_is_str(tag_value)) {
        return false;
    }
    const auto* tag_name = yyjson_get_str(tag_value);
    if (!tag_name || !*tag_name) {
        return false;
    }
    tag = tag_name;
    return true;
}

bool FetchReleaseAssets(
    ui::ProgressBox* progress,
    const std::string& repository,
    const std::string& version,
    std::vector<ReleaseAsset>& assets) {
    std::string owner;
    std::string name;
    if (!ParseGitHubRepo(repository, owner, name) || version.empty()) {
        return false;
    }
    const auto url = "https://api.github.com/repos/" + owner + "/" + name +
        "/releases/tags/" + EncodeUrlPathSegment(version);
    const auto response = curl::Api().ToMemory(
        curl::Url{url},
        curl::Header{{"Accept", "application/vnd.github+json"}},
        curl::OnProgress{progress->OnDownloadProgressCallback()});
    if (!response.success || response.data.empty()) {
        return false;
    }
    auto* doc = yyjson_read(
        reinterpret_cast<const char*>(response.data.data()), response.data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        return false;
    }
    auto* tag_value = yyjson_obj_get(root, "tag_name");
    auto* prerelease_value = yyjson_obj_get(root, "prerelease");
    auto* asset_values = yyjson_obj_get(root, "assets");
    if (!yyjson_is_str(tag_value) || !yyjson_is_bool(prerelease_value) ||
        yyjson_get_bool(prerelease_value) || !yyjson_is_arr(asset_values)) {
        return false;
    }
    const auto* tag = yyjson_get_str(tag_value);
    if (!tag || version != tag) {
        return false;
    }
    size_t index, max;
    yyjson_val* asset_value;
    yyjson_arr_foreach(asset_values, index, max, asset_value) {
        auto* name_value = yyjson_obj_get(asset_value, "name");
        auto* url_value = yyjson_obj_get(asset_value, "browser_download_url");
        if (!yyjson_is_str(name_value) || !yyjson_is_str(url_value)) {
            continue;
        }
        const auto* asset_name = yyjson_get_str(name_value);
        const auto* asset_url = yyjson_get_str(url_value);
        if (asset_name && *asset_name && asset_url && *asset_url &&
            std::string_view{asset_name}.find_first_of("/\\") == std::string_view::npos) {
            assets.push_back({asset_name, asset_url});
        }
    }
    return true;
}

bool DownloadReleaseAsset(ui::ProgressBox* progress, const std::string& url, const fs::FsPath& destination) {
    const auto result = curl::Api().ToFile(
        curl::Url{url},
        curl::Path{destination},
        curl::OnProgress{progress->OnDownloadProgressCallback()});
    return result.success;
}

bool AssetMatchesPattern(const std::string& pattern, const std::string& asset_name) {
    return update_detail::WildcardMatch(pattern, asset_name);
}

} // namespace sphaira::custom::github
