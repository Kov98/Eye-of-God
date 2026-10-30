#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deep::cloud {

// Enterprise role model. Permissions are intentionally explicit: a new
// capability must be granted deliberately, not inherited from a broad role.
enum class Role {
    Admin,
    Examiner,
    Reviewer,
    Auditor
};

enum class Permission : std::uint32_t {
    ViewCase = 1u << 0,
    RunCollection = 1u << 1,
    CancelJob = 1u << 2,
    VerifyEvidence = 1u << 3,
    ViewAudit = 1u << 4,
    ManageUsers = 1u << 5,
    ManageConnectors = 1u << 6
};

struct Principal {
    std::string id;
    std::string display_name;
    Role role = Role::Auditor;
    bool active = false;
};

struct Decision {
    bool allowed = false;
    std::string reason;
};

Decision authorize(const Principal& principal, Permission permission);
bool has_permission(Role role, Permission permission);
std::string role_name(Role role);
std::string permission_name(Permission permission);
bool parse_role(std::string_view text, Role& role);

} // namespace deep::cloud
