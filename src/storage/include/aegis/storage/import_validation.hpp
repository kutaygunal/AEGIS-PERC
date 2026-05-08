#pragma once

#include "aegis/storage/project_package.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace aegis::storage {

struct RoleDetectionCandidate {
    ArtifactRole role = ArtifactRole::Unknown;
    ArtifactCategory category = ArtifactCategory::Unknown;
    double confidence = 0.0;
    std::string reason;
};

struct FileRoleDetection {
    std::filesystem::path path;
    std::vector<RoleDetectionCandidate> candidates;

    const RoleDetectionCandidate* best() const noexcept;
    bool is_ambiguous() const noexcept;
};

class ImportPreflightValidator {
public:
    FileRoleDetection detect_file_role(const std::filesystem::path& path) const;
    ProjectPackage scan_project_folder(const std::filesystem::path& root,
                                       const std::string& project_name = "") const;
    std::vector<ImportDiagnostic> validate(const ProjectPackage& package) const;
    ValidationStatus derive_status(const std::vector<ImportDiagnostic>& diagnostics) const noexcept;
};

} // namespace aegis::storage
