// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ast.hpp"

namespace pipegen {

// Emit a module's TS types file (struct type list + FIELD_DESCRIPTORS_<module>).
std::string emit_ts_module(ast::Module const& mod,
                           std::set<std::string> const& known_modules = {},
                           std::map<std::string, ast::TypeRef> const& all_typedefs = {});

// Emit a flat barrel file aggregating every module's FIELD_DESCRIPTORS.
std::string emit_ts_barrel(std::vector<std::string> const& module_names);

}  // namespace pipegen
