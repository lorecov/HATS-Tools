#include "custom_component_update_utils.hpp"

#include "threaded_file_transfer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::custom::update_detail {

bool NormalizeRelativePath(std::string path, std::string& normalized) {
    // Archive paths must not escape the staging or destination root.
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.find(':') != std::string::npos) {
        return false;
    }

    std::vector<std::string> segments;
    size_t start = 0;
    while (start < path.size() && path[start] == '/') {
        ++start;
    }
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto length = (end == std::string::npos ? path.size() : end) - start;
        const auto segment = path.substr(start, length);
        if (segment == "..") {
            return false;
        }
        if (!segment.empty() && segment != ".") {
            segments.emplace_back(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    normalized.clear();
    for (const auto& segment : segments) {
        if (!normalized.empty()) {
            normalized += '/';
        }
        normalized += segment;
    }
    return true;
}

bool IsSafePathSegment(const std::string& segment) {
    return !segment.empty() && segment != "." && segment != ".." &&
        std::all_of(segment.begin(), segment.end(), [](char character) {
            const auto value = static_cast<unsigned char>(character);
            return std::isalnum(value) || character == '-' || character == '_' || character == '.';
        });
}

bool MakeFsPath(const std::string& value, fs::FsPath& path) {
    if (value.empty() || value.size() >= sizeof(path.s)) {
        return false;
    }
    std::snprintf(path, sizeof(path), "%s", value.c_str());
    return true;
}

std::string JoinPath(const std::string& parent, const std::string& child) {
    if (parent.empty()) {
        return child;
    }
    if (child.empty()) {
        return parent;
    }
    return parent.back() == '/' ? parent + child : parent + "/" + child;
}

char FoldCharacter(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

bool WildcardMatch(std::string_view pattern, std::string_view value) {
    while (!pattern.empty()) {
        if (pattern.front() == '*') {
            while (!pattern.empty() && pattern.front() == '*') {
                pattern.remove_prefix(1);
            }
            if (pattern.empty()) {
                return true;
            }
            for (size_t index = 0; index <= value.size(); ++index) {
                if (WildcardMatch(pattern, value.substr(index))) {
                    return true;
                }
            }
            return false;
        }

        if (value.empty()) {
            return false;
        }
        if (pattern.front() == '?') {
            pattern.remove_prefix(1);
            value.remove_prefix(1);
            continue;
        }

        if (pattern.front() == '[') {
            const auto close = pattern.find(']');
            if (close != std::string_view::npos && close > 1) {
                auto choices = pattern.substr(1, close - 1);
                const bool negate = choices.front() == '!' || choices.front() == '^';
                if (negate) {
                    choices.remove_prefix(1);
                }
                bool matched = false;
                for (size_t index = 0; index < choices.size(); ++index) {
                    if (index + 2 < choices.size() && choices[index + 1] == '-') {
                        const auto current = FoldCharacter(value.front());
                        matched |= current >= FoldCharacter(choices[index]) &&
                            current <= FoldCharacter(choices[index + 2]);
                        index += 2;
                    } else {
                        matched |= FoldCharacter(value.front()) == FoldCharacter(choices[index]);
                    }
                }
                if (matched == negate) {
                    return false;
                }
                pattern.remove_prefix(close + 1);
                value.remove_prefix(1);
                continue;
            }
        }

        if (FoldCharacter(pattern.front()) != FoldCharacter(value.front())) {
            return false;
        }
        pattern.remove_prefix(1);
        value.remove_prefix(1);
    }
    return value.empty();
}

std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string WithoutExtension(const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? filename : filename.substr(0, dot);
}

Result ExtractZip(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& zip_path,
    const fs::FsPath& output_path) {
    return thread::TransferUnzipAll(
        progress, zip_path, filesystem, output_path,
        [&output_path](const fs::FsPath& name, fs::FsPath& path) {
            std::string relative;
            if (!NormalizeRelativePath(name.s, relative) || relative.empty()) {
                return false;
            }
            const bool directory = name.ends_with("/");
            auto output = JoinPath(output_path.s, relative);
            if (directory) {
                output += '/';
            }
            return MakeFsPath(output, path);
        });
}

Result ListFiles(fs::Fs* filesystem, const fs::FsPath& directory, std::vector<fs::FsPath>& files) {
    fs::Dir dir;
    R_TRY(filesystem->OpenDirectory(directory, FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &dir));
    std::vector<FsDirectoryEntry> entries;
    R_TRY(dir.ReadAll(entries));

    for (const auto& entry : entries) {
        const std::string name = entry.name;
        if (name == "." || name == "..") {
            continue;
        }
        fs::FsPath child;
        if (!MakeFsPath(JoinPath(directory.s, name), child)) {
            R_THROW(0x1);
        }
        if (entry.type == FsDirEntryType_Dir) {
            R_TRY(ListFiles(filesystem, child, files));
        } else if (entry.type == FsDirEntryType_File) {
            files.emplace_back(child);
        }
    }
    R_SUCCEED();
}

Result ListDirectories(fs::Fs* filesystem, const fs::FsPath& directory, std::vector<fs::FsPath>& directories) {
    fs::Dir dir;
    R_TRY(filesystem->OpenDirectory(directory, FsDirOpenMode_ReadDirs, &dir));
    std::vector<FsDirectoryEntry> entries;
    R_TRY(dir.ReadAll(entries));

    for (const auto& entry : entries) {
        const std::string name = entry.name;
        if (name == "." || name == ".." || entry.type != FsDirEntryType_Dir) {
            continue;
        }
        fs::FsPath child;
        if (!MakeFsPath(JoinPath(directory.s, name), child)) {
            R_THROW(0x1);
        }
        directories.emplace_back(child);
        R_TRY(ListDirectories(filesystem, child, directories));
    }
    R_SUCCEED();
}

bool GetRelativePath(const fs::FsPath& root, const fs::FsPath& path, std::string& relative) {
    const std::string root_string = root.s;
    const std::string path_string = path.s;
    if (path_string.size() <= root_string.size() ||
        path_string.compare(0, root_string.size(), root_string) != 0 ||
        path_string[root_string.size()] != '/') {
        return false;
    }
    return NormalizeRelativePath(path_string.substr(root_string.size() + 1), relative);
}

bool PathIsShared(
    const std::string& id,
    const std::string& path,
    const manifest::Manifest& active,
    const manifest::DisabledComponents& disabled) {
    const auto has_path = [&id, &path](const auto& components) {
        for (const auto& [other_id, component] : components) {
            if (other_id == id) {
                continue;
            }
            for (const auto& file : component.files) {
                std::string relative;
                if (NormalizeRelativePath(file, relative) &&
                    fs::FsPath::path_equal(relative, path)) {
                    return true;
                }
            }
        }
        return false;
    };
    return has_path(active.components) || has_path(disabled.components);
}

Result CopyFileToPath(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& source,
    const fs::FsPath& destination) {
    R_TRY(filesystem->CreateDirectoryRecursivelyWithPath(destination));
    return progress->CopyFile(filesystem, source, destination, true);
}

Result CopyDirectoryFiles(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& source_root,
    const fs::FsPath& destination_root) {
    std::vector<fs::FsPath> files;
    R_TRY(ListFiles(filesystem, source_root, files));
    for (const auto& source : files) {
        if (progress->ShouldExit()) {
            R_THROW(0x1);
        }
        std::string relative;
        if (!GetRelativePath(source_root, source, relative)) {
            R_THROW(0x1);
        }
        fs::FsPath destination;
        if (!MakeFsPath(JoinPath(destination_root.s, relative), destination)) {
            R_THROW(0x1);
        }
        R_TRY(CopyFileToPath(progress, filesystem, source, destination));
    }
    R_SUCCEED();
}

bool GetComponentDestination(
    const PreparedComponentUpdate& prepared_update,
    const std::string& relative,
    fs::FsPath& path) {
    if (prepared_update.is_disabled) {
        return MakeFsPath(
            JoinPath(JoinPath(manifest::DISABLED_COMPONENTS_DIR_CUSTOM, prepared_update.id), relative), path);
    }
    return MakeFsPath("/" + relative, path);
}

} // namespace sphaira::custom::update_detail
