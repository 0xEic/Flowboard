// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include "ast.hpp"

namespace pipegen {

// Emits a stub SDK header for a module: struct types + enum sub-modules + IService/IClient
// virtual shells. No node classes, no OP_DECLARE_TYPE.
std::string emit_stub_header(ast::Module const& mod);

}  // namespace pipegen
