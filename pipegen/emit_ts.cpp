// SPDX-License-Identifier: MIT
#include "emit_ts.hpp"
#include "emit_cpp.hpp"  // for resolve_typedef
#include <sstream>
#include <variant>

namespace pipegen {

namespace {

struct TsClassified {
    std::string kind;        // "Scalar" | "Optional" | "List"
    std::string element_tag;
};

TsClassified classify_field_ts(
    ast::TypeRef const& original,
    std::map<std::string, ast::TypeRef> const& typedefs,
    std::string const& current_module,
    std::set<std::string> const& known_modules)
{
    auto resolved = resolve_typedef(original, typedefs, current_module);

    // Track owning module so cross-module typedef element resolution works.
    std::string owning_module = current_module;
    if (auto* n = std::get_if<ast::NamedType>(&original.value);
        n && n->qualified_name.size() == 2 && known_modules.count(n->qualified_name[0])) {
        owning_module = n->qualified_name[0];
    }

    std::string kind;
    ast::TypeRef element;
    if (auto* seq = std::get_if<ast::SequenceType>(&resolved.value)) {
        kind = seq->max_size.has_value() && *seq->max_size == 1 ? "Optional" : "List";
        element = resolve_typedef(*seq->element, typedefs, owning_module);
    } else {
        kind = "Scalar";
        element = resolved;
    }
    std::string tag;
    if (auto* p = std::get_if<ast::PrimitiveType>(&element.value)) {
        tag = primitive_type_tag(p->name);
    } else if (auto* n = std::get_if<ast::NamedType>(&element.value)) {
        auto const& qn = n->qualified_name;
        if (qn.size() == 1) {
            tag = owning_module + "::" + qn[0];
        } else if (qn.size() == 2 && known_modules.count(qn[0])) {
            tag = qn[0] + "::" + qn[1];
        }
    }
    return {kind, tag};
}

}  // anonymous

std::string emit_ts_module(ast::Module const& mod,
                           std::set<std::string> const& known_modules,
                           std::map<std::string, ast::TypeRef> const& all_typedefs) {
    std::ostringstream ts;
    ts << "// AUTO-GENERATED. Do not edit.\n";
    ts << "// SPDX-License-Identifier: MIT\n";

    // 1. Existing struct list export.
    ts << "export const " << mod.name << "_types = [\n";
    for (auto const& s : mod.structs) {
        ts << "  { name: \"" << s.name << "\", tag: \"" << mod.name << "::" << s.name << "\" },\n";
    }
    ts << "] as const;\n\n";

    // 2. Shared field-descriptor types (idempotent — same shape across every module file).
    ts << "export type FieldKind = 'Scalar' | 'Optional' | 'List';\n";
    ts << "export interface FieldDescriptor {\n";
    ts << "  name: string;\n";
    ts << "  kind: FieldKind;\n";
    ts << "  elementTypeTag: string;\n";
    ts << "}\n\n";

    // 3. Per-module FIELD_DESCRIPTORS map.
    ts << "export const FIELD_DESCRIPTORS_" << mod.name
       << ": Record<string, FieldDescriptor[]> = {\n";
    for (auto const& s : mod.structs) {
        ts << "  '" << mod.name << "::" << s.name << "': [\n";
        for (auto const& f : s.fields) {
            auto c = classify_field_ts(*f.type, all_typedefs, mod.name, known_modules);
            if (c.element_tag.empty()) continue;
            ts << "    { name: '" << f.name
               << "', kind: '"     << c.kind
               << "', elementTypeTag: '" << c.element_tag << "' },\n";
        }
        ts << "  ],\n";
    }
    ts << "};\n";
    return ts.str();
}

std::string emit_ts_barrel(std::vector<std::string> const& module_names) {
    std::ostringstream ts;
    ts << "// AUTO-GENERATED. Do not edit.\n";
    ts << "// SPDX-License-Identifier: MIT\n";
    ts << "import type { FieldDescriptor } from './M_Common_types';\n";
    for (auto const& n : module_names)
        ts << "import { FIELD_DESCRIPTORS_" << n << " } from './" << n << "_types';\n";
    ts << "\n";
    ts << "export type { FieldKind, FieldDescriptor } from './M_Common_types';\n";
    ts << "export const ALL_FIELD_DESCRIPTORS: Record<string, FieldDescriptor[]> = {\n";
    for (auto const& n : module_names)
        ts << "  ...FIELD_DESCRIPTORS_" << n << ",\n";
    ts << "};\n";
    return ts.str();
}

}  // namespace pipegen
