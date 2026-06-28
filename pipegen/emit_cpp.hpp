// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <set>
#include <string>
#include "ast.hpp"

namespace pipegen {

struct EmitResult {
    std::string types_hpp;        // header for the module's types
    std::string nodes_cpp;        // Service/Client node classes (filled by Task 8)
    std::string registry_snippet; // single line per node, gathered in Task 9
};

// known_modules: the full set of module names being emitted in this pipegen run.
// Every module in this set is considered a "known external module" for port-type
// safety checks and cross-module dependency ordering.
// Pass an empty set to get the old Phase-2 behaviour (only kFallback built-ins).
// all_typedefs: typedef map keyed by fully-qualified name ("M_Common::FooType"),
//   used to classify struct fields into FieldKind. Empty map = best-effort scalar
//   classification (existing behaviour for unit tests that don't care about Extract).
// target_real_sdk: when true, emit code that targets the real OnboardAPI SDK
//   (headers under <onboardapi/api/...> and <onboardapi/msg/...>, templated
//   Service/Client::create returning by value, operator-> for publish). Types
//   are NOT redeclared; the real SDK's type headers are included instead.
EmitResult emit_cpp_for_module(
    ast::Module const& mod,
    std::set<std::string> const& known_modules = {},
    std::map<std::string, ast::TypeRef> const& all_typedefs = {},
    bool target_real_sdk = false);

// Append synthetic <Op>_Args structs to `mod` — one per multi-param composer op
// (IService command / IClient event) that has at least one portable in-param.
// Each struct's fields are exactly those portable in-params, in order, marked
// StructDecl::synthetic. Shared by the C++ emitter (so the engine registers them
// across every type-generic registry) and the TS emitter (so the web's generated
// type lists / FIELD_DESCRIPTORS carry the same _Args structs). Idempotent within
// a module via op-name de-dup. Returns divergence-warning text (empty normally).
std::string synthesize_args_structs(
    ast::Module& mod,
    std::set<std::string> const& known_modules = {},
    std::map<std::string, ast::TypeRef> const& all_typedefs = {});

// Helper exposed for tests.
// local_submods: optional set of submodule names for the current module
// (used to prefix unqualified local submodule type references).
std::string cpp_type_ref(ast::TypeRef const& ref, std::string const& current_module,
                         std::set<std::string> const* local_submods = nullptr);

// Map an IDL primitive name (e.g. "unsigned long") to its flowboard type tag
// (e.g. "flowboard::UInt32"). Returns "" if the name is not a primitive.
// Single source of truth shared by the C++ and TS field classifiers so every
// scalar field type — not just double/bool/int64/string — is surfaced.
std::string primitive_type_tag(std::string const& idl_primitive_name);

// Walk typedef chains to reach a non-typedef TypeRef.
// `typedefs` is keyed by fully-qualified name (e.g. "M_Common::AngleDegreesOptionalType")
// and maps to the aliased TypeRef.
// `current_module` is the module of the caller — used to qualify single-segment
// NamedType references (a NamedType `Foo` inside module M is `M::Foo`).
ast::TypeRef resolve_typedef(ast::TypeRef const& ref,
                             std::map<std::string, ast::TypeRef> const& typedefs,
                             std::string const& current_module,
                             int max_depth = 10);

}  // namespace pipegen
