#include "rbac.hpp"

#include <algorithm>
#include <cctype>

namespace deep::cloud {

std::string role_name(Role role) {
    switch (role) {
    case Role::Admin: return "admin";
    case Role::Examiner: return "examiner";
    case Role::Reviewer: return "reviewer";
    case Role::Auditor: return "auditor";
    }
    return "unknown";
}

std::string permission_name(Permission permission) {
    switch (permission) {
    case Permission::ViewCase: return "view_case";
    case Permission::RunCollection: return "run_collection";
    case Permission::CancelJob: return "cancel_job";
    case Permission::VerifyEvidence: return "verify_evidence";
    case Permission::ViewAudit: return "view_audit";
    case Permission::ManageUsers: return "manage_users";
    case Permission::ManageConnectors: return "manage_connectors";
    }
    return "unknown";
}

bool parse_role(std::string_view text, Role& role) {
    std::string value(text);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "admin") { role = Role::Admin; return true; }
    if (value == "examiner") { role = Role::Examiner; return true; }
    if (value == "reviewer") { role = Role::Reviewer; return true; }
    if (value == "auditor") { role = Role::Auditor; return true; }
    return false;
}

bool has_permission(Role role, Permission permission) {
    switch (role) {
    case Role::Admin:
        return true;
    case Role::Examiner:
        return permission == Permission::ViewCase ||
               permission == Permission::RunCollection ||
               permission == Permission::CancelJob ||
               permission == Permission::VerifyEvidence ||
               permission == Permission::ViewAudit;
    case Role::Reviewer:
        return permission == Permission::ViewCase ||
               permission == Permission::VerifyEvidence ||
               permission == Permission::ViewAudit;
    case Role::Auditor:
        return permission == Permission::ViewCase ||
               permission == Permission::ViewAudit;
    }
    return false;
}

Decision authorize(const Principal& principal, Permission permission) {
    if (principal.id.empty()) {
        return {false, "principal has no id"};
    }
    if (!principal.active) {
        return {false, "principal is inactive"};
    }
    if (!has_permission(principal.role, permission)) {
        return {false, "role " + role_name(principal.role) +
            " lacks " + permission_name(permission)};
    }
    return {true, {}};
}

} // namespace deep::cloud
