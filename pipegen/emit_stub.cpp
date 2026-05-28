// SPDX-License-Identifier: MIT
#include "emit_stub.hpp"
#include "emit_cpp.hpp"   // reuse cpp_type_ref
#include <algorithm>
#include <set>
#include <sstream>

namespace pipegen {

namespace {

// Collect all local submodule names for a module.
std::set<std::string> local_submodule_names(ast::Module const& mod) {
    std::set<std::string> names;
    for (auto const& sub : mod.submodules)
        names.insert(sub->name);
    return names;
}

// Within a stub header we are already inside the module's namespace,
// so strip the leading "::ModuleName::" qualification from single-segment names.
// Cross-module types are still fully qualified (e.g. ::M_Common::PositionType).
std::string stub_type_ref(ast::TypeRef const& ref, std::string const& mod_name,
                          std::set<std::string> const& local_submods) {
    std::string full = cpp_type_ref(ref, mod_name, &local_submods);
    // Strip leading "::<mod_name>::" for types local to this module.
    std::string prefix = "::" + mod_name + "::";
    if (full.size() > prefix.size() &&
        full.substr(0, prefix.size()) == prefix) {
        return full.substr(prefix.size());
    }
    return full;
}

// Collect names of external modules referenced via qualified NamedTypes.
// Excludes local submodule names (which are enum namespaces within this module).
void collect_cross_module_deps(ast::TypeRef const& ref,
                               std::string const& mod_name,
                               std::set<std::string> const& local_submods,
                               std::set<std::string>& deps) {
    if (auto const* n = std::get_if<ast::NamedType>(&ref.value)) {
        if (n->qualified_name.size() >= 2) {
            std::string const& first = n->qualified_name[0];
            // Only emit cross-module include if first segment is not a local submodule.
            if (first != mod_name && !local_submods.count(first))
                deps.insert(first);
        }
    } else if (auto const* s = std::get_if<ast::SequenceType>(&ref.value)) {
        collect_cross_module_deps(*s->element, mod_name, local_submods, deps);
    }
}

std::set<std::string> cross_module_deps(ast::Module const& mod,
                                        std::set<std::string> const& local_submods) {
    std::set<std::string> deps;
    for (auto const& td : mod.typedefs)
        collect_cross_module_deps(*td.aliased, mod.name, local_submods, deps);
    for (auto const& s : mod.structs)
        for (auto const& f : s.fields)
            collect_cross_module_deps(*f.type, mod.name, local_submods, deps);
    for (auto const& iface : mod.interfaces)
        for (auto const& op : iface.operations)
            for (auto const& p : op.params)
                collect_cross_module_deps(*p.type, mod.name, local_submods, deps);
    return deps;
}

}  // namespace

std::string emit_stub_header(ast::Module const& mod) {
    auto local_submods = local_submodule_names(mod);

    std::ostringstream h;
    h << "// AUTO-GENERATED stub SDK header. Do not edit.\n";
    h << "// SPDX-License-Identifier: MIT\n";
    h << "#pragma once\n";
    h << "#include <cstdint>\n#include <string>\n#include <vector>\n";

    // Include cross-module stub headers for referenced types.
    auto deps = cross_module_deps(mod, local_submods);
    for (auto const& dep : deps)
        h << "#include \"" << dep << "_stub.hpp\"\n";

    // Guard the data-type definitions (enums, typedefs, structs) so they are
    // skipped when the engine's generated types header is included first.
    // The engine's header defines FLOWBOARD_GENERATED_<MODULE>_TYPES to signal
    // that the authoritative definitions are already present.
    // IClient / IService interface shells are emitted OUTSIDE the guard so they
    // are always defined — they depend only on types that must be available via
    // one or the other path.
    std::string sentinel = "FLOWBOARD_GENERATED_" + mod.name + "_TYPES";
    h << "\n#ifndef " << sentinel << "\n";
    h << "\nnamespace " << mod.name << " {\n\n";

    // Enum sub-modules
    for (auto const& sub : mod.submodules) {
        if (sub->enums.size() != 1) continue;
        auto const& e = sub->enums[0];
        h << "namespace " << sub->name << " { enum class " << e.name << " { ";
        for (std::size_t i = 0; i < e.values.size(); ++i) {
            h << e.values[i];
            if (i + 1 < e.values.size()) h << ", ";
        }
        h << " }; }\n";
    }

    // Emit typedefs and structs in their original source declaration order.
    // This preserves forward-declaration dependencies (typedef of sequence<T> must
    // come after struct T, not before).
    {
        enum class Kind { Typedef, Struct };
        struct Item {
            std::size_t order;
            Kind kind;
            std::size_t idx;
        };
        std::vector<Item> items;
        items.reserve(mod.typedefs.size() + mod.structs.size());
        for (std::size_t i = 0; i < mod.typedefs.size(); ++i)
            items.push_back({mod.typedefs[i].source_order, Kind::Typedef, i});
        for (std::size_t i = 0; i < mod.structs.size(); ++i)
            items.push_back({mod.structs[i].source_order, Kind::Struct, i});
        std::sort(items.begin(), items.end(),
                  [](Item const& a, Item const& b) { return a.order < b.order; });

        for (auto const& item : items) {
            if (item.kind == Kind::Typedef) {
                auto const& td = mod.typedefs[item.idx];
                h << "using " << td.name << " = "
                  << stub_type_ref(*td.aliased, mod.name, local_submods) << ";\n";
            } else {
                auto const& s = mod.structs[item.idx];
                h << "struct " << s.name << " {\n";
                for (auto const& f : s.fields)
                    h << "    " << stub_type_ref(*f.type, mod.name, local_submods) << " " << f.name << "{};\n";
                h << "};\n";
            }
        }
        if (!items.empty()) h << "\n";
    }

    h << "}  // namespace " << mod.name << "\n";
    h << "\n#endif  // " << sentinel << "\n";

    // Virtual interface shells — emitted unconditionally so the generated node code
    // can always inherit IClient/IService regardless of which type source was used.
    if (!mod.interfaces.empty()) {
        h << "\nnamespace " << mod.name << " {\n";
        for (auto const& iface : mod.interfaces) {
            h << "\nclass " << iface.name << " {\npublic:\n    virtual ~" << iface.name << "() = default;\n";
            for (auto const& op : iface.operations) {
                h << "    virtual void " << op.name << "(";
                for (std::size_t i = 0; i < op.params.size(); ++i) {
                    if (i) h << ", ";
                    h << stub_type_ref(*op.params[i].type, mod.name, local_submods) << " const& " << op.params[i].name;
                }
                h << ") {}\n";
            }
            h << "};\n";
        }
        h << "\n}  // namespace " << mod.name << "\n";
    }

    return h.str();
}

}  // namespace pipegen
