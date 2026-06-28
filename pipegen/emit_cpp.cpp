// SPDX-License-Identifier: MIT
#include "emit_cpp.hpp"
#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pipegen {

namespace {
std::string join(std::vector<std::string> const& parts, std::string const& sep) {
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) s += sep;
        s += parts[i];
    }
    return s;
}

// Collect the names of all submodules (enum-namespace modules) in a module.
std::set<std::string> submodule_names(ast::Module const& mod) {
    std::set<std::string> names;
    for (auto const& sub : mod.submodules)
        names.insert(sub->name);
    return names;
}

// Collect the names of all typedef'd names in a module.
std::set<std::string> typedef_names(ast::Module const& mod) {
    std::set<std::string> names;
    for (auto const& td : mod.typedefs)
        names.insert(td.name);
    return names;
}

// Collect struct names for a module.
std::set<std::string> struct_names(ast::Module const& mod) {
    std::set<std::string> names;
    for (auto const& s : mod.structs)
        names.insert(s.name);
    return names;
}

// Check if a TypeRef references a local struct by name (single-segment NamedType).
// Also recurses through sequence elements.
bool typeref_needs_local_struct(ast::TypeRef const& ref,
                                std::set<std::string> const& local_structs);

bool typeref_needs_local_struct(ast::TypeRef const& ref,
                                std::set<std::string> const& local_structs) {
    if (auto const* p = std::get_if<ast::PrimitiveType>(&ref.value)) {
        (void)p;
        return false;
    }
    if (auto const* s = std::get_if<ast::SequenceType>(&ref.value)) {
        return typeref_needs_local_struct(*s->element, local_structs);
    }
    if (auto const* n = std::get_if<ast::NamedType>(&ref.value)) {
        return (n->qualified_name.size() == 1 &&
                local_structs.count(n->qualified_name[0]) > 0);
    }
    return false;
}

// Check if a typedef's aliased type depends on any local struct (directly or through sequences).
bool typedef_needs_local_struct(ast::TypedefDecl const& td,
                                std::set<std::string> const& local_structs) {
    return typeref_needs_local_struct(*td.aliased, local_structs);
}

}  // namespace

// Fallback module names used when emit_cpp_for_module is called without a known-modules set.
// This keeps existing unit-test behaviour intact (tests call emit_cpp_for_module with no set).
static std::set<std::string> const kFallbackKnownModules = {"M_Common", "M_Mount", "M_Alert"};

std::string cpp_type_ref(ast::TypeRef const& ref, std::string const& current_module,
                         std::set<std::string> const* local_submods) {
    return std::visit([&](auto const& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ast::PrimitiveType>) {
            std::string const& n = v.name;
            if (n == "boolean")            return "bool";
            if (n == "octet")              return "::std::uint8_t";
            if (n == "short")              return "::std::int16_t";
            if (n == "unsigned short")     return "::std::uint16_t";
            if (n == "long")               return "::std::int32_t";
            if (n == "unsigned long")      return "::std::uint32_t";
            if (n == "long long")          return "::std::int64_t";
            if (n == "unsigned long long") return "::std::uint64_t";
            if (n == "float")              return "float";
            if (n == "double")             return "double";
            if (n == "char")               return "char";
            if (n == "string")             return "::std::string";
            throw std::runtime_error("emit: unknown primitive '" + n + "'");
        } else if constexpr (std::is_same_v<T, ast::SequenceType>) {
            return "::std::vector<" + cpp_type_ref(*v.element, current_module, local_submods) + ">";
        } else {
            static_assert(std::is_same_v<T, ast::NamedType>);
            auto const& qn = v.qualified_name;
            if (qn.size() == 1) {
                // Single unqualified name — belongs to current_module
                return "::" + current_module + "::" + qn[0];
            }
            // Check if first segment is a local submodule name
            if (local_submods && qn.size() >= 2) {
                if (local_submods->count(qn[0])) {
                    // Local submodule reference; prefix with current_module
                    return "::" + current_module + "::" + join(qn, "::");
                }
            }
            // Otherwise emit as-is (already has module prefix like M_Common::...)
            return "::" + join(qn, "::");
        }
    }, ref.value);
}

// Compatibility overload without local_submods.
std::string cpp_type_ref(ast::TypeRef const& ref, std::string const& current_module) {
    return cpp_type_ref(ref, current_module, nullptr);
}

// IDL primitive name -> flowboard type tag. Mirrors the full set in
// cpp_type_ref so field classification covers every scalar, not just the four
// that used to be hard-coded (which silently dropped uint32 'Index' fields etc.).
std::string primitive_type_tag(std::string const& n) {
    if (n == "boolean")            return "flowboard::Bool";
    if (n == "octet")              return "flowboard::UInt8";
    if (n == "short")              return "flowboard::Int16";
    if (n == "unsigned short")     return "flowboard::UInt16";
    if (n == "long")               return "flowboard::Int32";
    if (n == "unsigned long")      return "flowboard::UInt32";
    if (n == "long long")          return "flowboard::Int64";
    if (n == "unsigned long long") return "flowboard::UInt64";
    if (n == "float")              return "flowboard::Float";
    if (n == "double")             return "flowboard::Double";
    if (n == "char")               return "flowboard::Char";
    if (n == "string")             return "flowboard::String";
    return "";
}

ast::TypeRef resolve_typedef(ast::TypeRef const& ref,
                             std::map<std::string, ast::TypeRef> const& typedefs,
                             std::string const& current_module,
                             int max_depth) {
    ast::TypeRef cur = ref;
    for (int depth = 0; depth < max_depth; ++depth) {
        auto const* n = std::get_if<ast::NamedType>(&cur.value);
        if (!n) return cur;  // primitive or sequence — done
        std::string key;
        if (n->qualified_name.size() == 1) {
            key = current_module + "::" + n->qualified_name[0];
        } else if (n->qualified_name.size() == 2) {
            key = n->qualified_name[0] + "::" + n->qualified_name[1];
        } else {
            return cur;  // 3+ segments = submodule (enum) reference, not a typedef
        }
        auto it = typedefs.find(key);
        if (it == typedefs.end()) return cur;  // not a typedef → struct or enum
        cur = it->second;
    }
    return cur;  // depth cap — return whatever we have
}

namespace {

// Emit a typedef alias (using declaration) for a typedef in a module namespace.
void emit_typedef(std::ostringstream& os, ast::TypedefDecl const& td,
                  std::string const& module,
                  std::set<std::string> const& local_submods) {
    os << "using " << td.name << " = "
       << cpp_type_ref(*td.aliased, module, &local_submods) << ";\n";
}

void emit_struct(std::ostringstream& os, ast::StructDecl const& s,
                 std::string const& module,
                 std::set<std::string> const& local_submods) {
    os << "struct " << s.name << " {\n";
    for (auto const& f : s.fields)
        os << "    " << cpp_type_ref(*f.type, module, &local_submods) << " " << f.name << ";\n";
    os << "};\n";
}

void emit_enum_namespace(std::ostringstream& os, ast::Module const& sub) {
    if (sub.enums.size() != 1) return;  // only the conventional single-enum sub-module form is emitted
    auto const& e = sub.enums[0];
    os << "namespace " << sub.name << " {\n";
    os << "enum class " << e.name << " {\n";
    for (size_t i = 0; i < e.values.size(); ++i) {
        os << "    " << e.values[i];
        if (i + 1 < e.values.size()) os << ",";
        os << "\n";
    }
    os << "};\n";
    os << "inline void from_json(::nlohmann::json const& j, " << e.name << "& v) {\n";
    os << "    v = static_cast<" << e.name << ">(j.get<std::underlying_type_t<" << e.name << ">>());\n";
    os << "}\n";
    os << "}\n";
}

// Determine if a TypeRef is safe to use as an OutputPort template argument.
// Safe = PrimitiveType (scalar) OR NamedType pointing to a struct in a known module.
// Unsafe = SequenceType, or NamedType pointing to a typedef alias for a sequence.
//
// `ObjectIdType` / `AlertIdType` typedef aliases resolve to `string` and are
// allowed through `resolve_typedef`. This is what lets `Key*: ObjectIdType`
// parameters in Report* callbacks surface as `OutputPort<std::string>` rather
// than getting silently dropped. Most other typedef aliases (scalar-suffix
// units like `MetersType`, sequence aliases like `*OptionalType`/`*ListType`)
// stay skipped via the suffix denylist below.
bool is_safe_type(ast::TypeRef const& ref,
                  std::set<std::string> const& local_submods,
                  std::set<std::string> const& local_typedefs,
                  std::map<std::string, ast::TypeRef> const& all_typedefs,
                  std::string const& current_module,
                  std::set<std::string> const& known_modules) {
    return std::visit([&](auto const& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ast::PrimitiveType>) {
            // Every IDL scalar primitive now has an OP_DECLARE_TYPE in
            // builtin_types.hpp (bool/double/string plus the integer/float/char
            // family), so all scalars are wireable as ports.
            (void)v;
            return true;
        } else if constexpr (std::is_same_v<T, ast::SequenceType>) {
            return false;  // Always skip — no type_tag for vectors
        } else {
            static_assert(std::is_same_v<T, ast::NamedType>);
            auto const& qn = v.qualified_name;

            // Single-segment: a typedef in the current module — skip if typedef alias
            if (qn.size() == 1) {
                if (local_typedefs.count(qn[0])) return false;
                return true;  // Could be a struct
            }

            // Two-segment: e.g. "M_Common::PositionType" or "ModeCommand::Type"
            if (qn.size() == 2) {
                // If the first segment is a local submodule it is an enum namespace.
                // Enums have no OP_DECLARE_TYPE registration — skip for ports.
                if (local_submods.count(qn[0])) return false;

                // If the first segment is a known external module (dynamic set),
                // resolve the typedef chain and test the underlying type:
                //   - sequence aliases (*OptionalType / *ListType / *MatrixType)
                //     resolve to a SequenceType → unsafe (no port for vectors).
                //   - scalar-unit aliases (DegreesType, MetersType, RpmType, …)
                //     and id aliases (ObjectIdType/AlertIdType → string) resolve
                //     to a registered primitive → safe.
                //   - real structs aren't typedefs → resolve returns the type
                //     unchanged → treated as a struct (which has a type tag).
                //   - enum aliases resolve to a sub-module / 3-segment name → unsafe.
                if (known_modules.count(qn[0])) {
                    auto resolved = resolve_typedef(ref, all_typedefs, current_module);
                    if (auto const* rn = std::get_if<ast::NamedType>(&resolved.value)) {
                        // Not a typedef (or self-referential): assume a struct type.
                        if (rn->qualified_name == qn) return true;
                    }
                    return is_safe_type(resolved, local_submods, local_typedefs,
                                        all_typedefs, current_module, known_modules);
                }
                return false;
            }

            // Three-segment: external module enum e.g. "M_Common::HorizontalDirection::Type"
            // Enums don't have OP_DECLARE_TYPE — skip for ports
            if (qn.size() == 3) return false;

            return false;
        }
    }, ref.value);
}

// Collect the external module names referenced in a module (for #include ordering).
void collect_deps_from_typeref(ast::TypeRef const& ref,
                               std::string const& mod_name,
                               std::set<std::string> const& known_modules,
                               std::set<std::string>& deps) {
    if (auto const* n = std::get_if<ast::NamedType>(&ref.value)) {
        if (!n->qualified_name.empty()) {
            std::string const& first = n->qualified_name[0];
            if (known_modules.count(first) && first != mod_name)
                deps.insert(first);
        }
    } else if (auto const* s = std::get_if<ast::SequenceType>(&ref.value)) {
        collect_deps_from_typeref(*s->element, mod_name, known_modules, deps);
    }
}

std::set<std::string> collect_dependencies(ast::Module const& mod,
                                           std::set<std::string> const& known_modules) {
    std::set<std::string> deps;
    for (auto const& td : mod.typedefs)
        collect_deps_from_typeref(*td.aliased, mod.name, known_modules, deps);
    for (auto const& s : mod.structs)
        for (auto const& f : s.fields)
            collect_deps_from_typeref(*f.type, mod.name, known_modules, deps);
    for (auto const& iface : mod.interfaces)
        for (auto const& op : iface.operations)
            for (auto const& p : op.params)
                collect_deps_from_typeref(*p.type, mod.name, known_modules, deps);
    return deps;
}

// Classify a struct field's resolved TypeRef into a (FieldKind, element_type_tag).
// `current_module` is the module owning the struct (used by resolve_typedef).
// Returns ("", "") for unsupported / unregistered types — caller skips emission.
struct ClassifiedField {
    std::string kind;          // "Scalar", "Optional", or "List"
    std::string element_tag;   // type_tag string, or "" if unsupported
};

ClassifiedField classify_field(
    ast::TypeRef const& original,
    std::map<std::string, ast::TypeRef> const& typedefs,
    std::string const& current_module,
    std::set<std::string> const& known_modules)
{
    // Determine the "owning module" of the resolved type. If `original` is a
    // cross-module NamedType pointing at a typedef (e.g. "M_Common::PropertyListType"),
    // then any single-segment NamedType inside that typedef's aliased TypeRef refers
    // to M_Common, not the current module. Track this so we qualify correctly.
    std::string owning_module = current_module;
    if (auto* n = std::get_if<ast::NamedType>(&original.value)) {
        auto const& qn = n->qualified_name;
        if (qn.size() == 2 && known_modules.count(qn[0])) {
            std::string key = qn[0] + "::" + qn[1];
            if (typedefs.find(key) != typedefs.end()) {
                owning_module = qn[0];
            }
        }
    }

    auto resolved = resolve_typedef(original, typedefs, current_module);

    std::string kind;
    ast::TypeRef element;
    if (auto* seq = std::get_if<ast::SequenceType>(&resolved.value)) {
        kind = seq->max_size.has_value() && *seq->max_size == 1 ? "Optional" : "List";
        element = *seq->element;
        element = resolve_typedef(element, typedefs, owning_module);
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

}  // namespace

std::string synthesize_args_structs(
    ast::Module& mod,
    std::set<std::string> const& known_modules,
    std::map<std::string, ast::TypeRef> const& all_typedefs)
{
    // Same fallback as emit_cpp_for_module so standalone callers (unit tests,
    // the TS emitter) classify cross-module types identically.
    std::set<std::string> const& km =
        known_modules.empty() ? kFallbackKnownModules : known_modules;
    auto local_submods  = submodule_names(mod);
    auto local_typedefs = typedef_names(mod);

    // Portability predicates — kept identical to the ones in emit_cpp_for_module's
    // port-emission loops so the synthesized struct's fields always match exactly
    // the scalar ports generated for the op.
    auto owning_module_of = [&](ast::TypeRef const& ref) -> std::string {
        if (auto* nt = std::get_if<ast::NamedType>(&ref.value))
            if (nt->qualified_name.size() == 2 && km.count(nt->qualified_name[0]))
                return nt->qualified_name[0];
        return mod.name;
    };
    auto opt_elem = [&](ast::TypeRef const& ref) -> std::optional<ast::TypeRef> {
        std::string owning = owning_module_of(ref);
        auto resolved = resolve_typedef(ref, all_typedefs, owning);
        if (auto* s = std::get_if<ast::SequenceType>(&resolved.value)) {
            if (s->max_size && *s->max_size == 1) {
                ast::TypeRef elem = resolve_typedef(*s->element, all_typedefs, owning);
                if (is_safe_type(elem, local_submods, local_typedefs, all_typedefs, owning, km))
                    return elem;
            }
        }
        return std::nullopt;
    };
    auto list_elem = [&](ast::TypeRef const& ref) -> std::optional<ast::TypeRef> {
        std::string owning = owning_module_of(ref);
        auto resolved = resolve_typedef(ref, all_typedefs, owning);
        if (auto* s = std::get_if<ast::SequenceType>(&resolved.value)) {
            if (!(s->max_size && *s->max_size == 1)) {
                ast::TypeRef elem = resolve_typedef(*s->element, all_typedefs, owning);
                if (is_safe_type(elem, local_submods, local_typedefs, all_typedefs, owning, km))
                    return elem;
            }
        }
        return std::nullopt;
    };
    auto is_portable = [&](ast::TypeRef const& ref) {
        return is_safe_type(ref, local_submods, local_typedefs, all_typedefs, mod.name, km)
            || opt_elem(ref).has_value()
            || list_elem(ref).has_value();
    };

    std::string synth_warnings;
    std::size_t synth_order = 1'000'000;  // emit after all real typedefs/structs
    std::set<std::string> seen_args;
    // Maps _Args name -> ordered portable in-param names, to detect signature
    // divergence when the same op name appears on both IService and IClient.
    std::map<std::string, std::vector<std::string>> seen_fields;
    auto add_args_structs = [&](ast::InterfaceDecl const* iface) {
        if (!iface) return;
        for (auto const& op : iface->operations) {
            if (op.params.size() <= 1) continue;
            std::string args_name = op.name + "_Args";

            std::vector<std::string> fields;
            for (auto const& p : op.params) {
                if (p.direction != "in") continue;
                if (!is_portable(*p.type)) continue;
                fields.push_back(p.name);
            }

            if (!seen_args.insert(args_name).second) {
                if (fields != seen_fields.at(args_name)) {
                    synth_warnings += "// WARNING: " + args_name
                        + " name reused with differing parameters"
                        + " -- second definition skipped\n";
                }
                continue;  // identical signature: correct de-dup
            }
            seen_fields[args_name] = fields;

            ast::StructDecl s;
            s.name = args_name;
            s.synthetic = true;
            s.source_order = synth_order++;
            for (auto const& p : op.params) {
                if (p.direction != "in") continue;
                if (!is_portable(*p.type)) continue;
                ast::StructField f;
                f.type = p.type;
                f.name = p.name;
                s.fields.push_back(f);
            }
            if (!s.fields.empty())
                mod.structs.push_back(std::move(s));
        }
    };
    ast::InterfaceDecl const* early_client = nullptr;
    ast::InterfaceDecl const* early_svc    = nullptr;
    for (auto const& i : mod.interfaces) {
        if (i.name == "IClient")  early_client = &i;
        if (i.name == "IService") early_svc    = &i;
    }
    add_args_structs(early_svc);
    add_args_structs(early_client);
    return synth_warnings;
}

EmitResult emit_cpp_for_module(ast::Module const& mod,
                               std::set<std::string> const& known_modules,
                               std::map<std::string, ast::TypeRef> const& all_typedefs,
                               bool target_real_sdk) {
    // When called from tests without a known-modules set, fall back to the
    // hard-coded Phase-2 module list so existing tests remain unaffected.
    std::set<std::string> const& km = known_modules.empty() ? kFallbackKnownModules : known_modules;

    // Mutable local copy of the module so we can inject synthetic structs before
    // the struct-emission passes run (mod itself is const&).
    ast::Module mmod = mod;

    // Real-SDK header short names: the real SDK lives under <onboardapi/api/Foo.hpp>
    // (interface) and <onboardapi/type/Foo.hpp> (struct types), with the leading
    // "M_" prefix stripped from filenames.
    auto strip_m = [](std::string const& full) {
        return (full.size() > 2 && full.substr(0, 2) == "M_") ? full.substr(2) : full;
    };
    std::string const mod_short = strip_m(mod.name);

    EmitResult r;
    auto local_submods  = submodule_names(mod);
    auto local_typedefs = typedef_names(mod);
    auto local_structs  = struct_names(mod);
    auto deps           = collect_dependencies(mod, km);

    // --- Portability predicates (single source of truth) ---------------------
    // These are used both by the <Op>_Args synthesis pass below and by the
    // per-interface port-generation loops further down, so they are defined
    // once here rather than duplicated inside the per-iface loop.  Any param
    // that satisfies is_portable() gets a scalar input port; the _Args struct
    // must contain exactly those params, in order.

    // Which module "owns" a cross-module named type?
    auto owning_module_of = [&](ast::TypeRef const& ref) -> std::string {
        if (auto* nt = std::get_if<ast::NamedType>(&ref.value))
            if (nt->qualified_name.size() == 2 && km.count(nt->qualified_name[0]))
                return nt->qualified_name[0];
        return mod.name;
    };
    // Returns the resolved element TypeRef when `ref` is an Optional (sequence
    // of max_size==1) whose element is itself a safe port type; else nullopt.
    auto opt_elem = [&](ast::TypeRef const& ref) -> std::optional<ast::TypeRef> {
        std::string owning = owning_module_of(ref);
        auto resolved = resolve_typedef(ref, all_typedefs, owning);
        if (auto* s = std::get_if<ast::SequenceType>(&resolved.value)) {
            if (s->max_size && *s->max_size == 1) {
                ast::TypeRef elem = resolve_typedef(*s->element, all_typedefs, owning);
                if (is_safe_type(elem, local_submods, local_typedefs, all_typedefs, owning, km))
                    return elem;
            }
        }
        return std::nullopt;
    };
    auto is_optional_port = [&](ast::TypeRef const& ref) { return opt_elem(ref).has_value(); };

    // Returns the resolved element TypeRef when `ref` is a List (unbounded
    // sequence) whose element is a safe port type; else nullopt.
    auto list_elem = [&](ast::TypeRef const& ref) -> std::optional<ast::TypeRef> {
        std::string owning = owning_module_of(ref);
        auto resolved = resolve_typedef(ref, all_typedefs, owning);
        if (auto* s = std::get_if<ast::SequenceType>(&resolved.value)) {
            if (!(s->max_size && *s->max_size == 1)) {
                ast::TypeRef elem = resolve_typedef(*s->element, all_typedefs, owning);
                if (is_safe_type(elem, local_submods, local_typedefs, all_typedefs, owning, km))
                    return elem;
            }
        }
        return std::nullopt;
    };
    auto is_list_port = [&](ast::TypeRef const& ref) { return list_elem(ref).has_value(); };

    // A param is "portable" (gets a port) if it's a safe scalar/struct, an
    // Optional of one, or a List of one.  THIS IS THE SINGLE SOURCE OF TRUTH
    // used by both the _Args synthesis below and the port-emission loops.
    auto is_portable = [&](ast::TypeRef const& ref) {
        return is_safe_type(ref, local_submods, local_typedefs, all_typedefs, mod.name, km)
            || is_optional_port(ref)
            || is_list_port(ref);
    };

    // An op qualifies for a synthesized <Op>_Args struct (and therefore an
    // in_<Op>_args port) iff it has more than one param AND at least one
    // in-param satisfies is_portable().  This exactly mirrors the synthesis
    // condition above (add_args_structs loop + the !s.fields.empty() guard).
    auto op_has_args = [&](ast::Operation const& op) -> bool {
        if (op.params.size() <= 1) return false;
        for (auto const& p : op.params) {
            if (p.direction == "in" && is_portable(*p.type)) return true;
        }
        return false;
    };

    // Synthesize <Op>_Args structs for every multi-param composer op and append
    // them to mmod.structs, so all downstream emission passes (struct def, to_json,
    // from_json, OP_DECLARE_TYPE, FieldDescriptors, extract/wire/tap/port/list/
    // throttle/sync factories, Factory node) pick them up by iterating mmod.structs.
    //
    // This is the SAME synthesis the TS emitter runs (single source of truth in
    // synthesize_args_structs), so the web's generated type lists carry exactly the
    // _Args structs the engine registers. Returns divergence warnings (emitted into
    // types_hpp below; empty in normal operation).
    std::string synth_warnings = synthesize_args_structs(mmod, known_modules, all_typedefs);

    std::ostringstream h;
    h << "// AUTO-GENERATED by pipegen. Do not edit.\n";
    h << "// SPDX-License-Identifier: MIT\n";
    h << "#pragma once\n";
    // Sentinel: lets the stub SDK headers detect that the generated types are available
    // and skip their own (potentially conflicting) definitions.
    h << "#define FLOWBOARD_GENERATED_" << mod.name << "_TYPES\n";
    h << "#include <cstdint>\n#include <string>\n#include <vector>\n";
    h << "#include \"flowboard/type_tag.hpp\"\n";
    h << "#include \"flowboard/json_helpers.hpp\"\n";

    if (target_real_sdk) {
        // When targeting the real SDK, the struct/enum/typedef definitions live in
        // <onboardapi/type/<Short>.hpp>. We MUST NOT redeclare them or we'd hit
        // class-redefinition errors. We still emit OP_DECLARE_TYPE + FieldDescriptor
        // tables for the engine's type-tag/extract systems.
        h << "#include <onboardapi/type/" << mod_short << ".hpp>\n";
    }

    // Include dependency module headers so their types are available.
    // deps is already a std::set so iteration order is alphabetical/reproducible.
    for (auto const& dep : deps) {
        h << "#include \"" << dep << "_types.hpp\"\n";
    }
    h << "\n";

    // Emit any divergence warnings collected during synthesis (rare future-proofing
    // guard; empty string in normal operation).
    if (!synth_warnings.empty())
        h << synth_warnings << "\n";

    h << "namespace " << mod.name << " {\n\n";

    if (!target_real_sdk) {
        for (auto const& sub : mod.submodules)
            emit_enum_namespace(h, *sub);
    }

    // Emit typedefs and structs in their original source declaration order.
    // This preserves forward-declaration dependencies (e.g. typedef sequence<PropertyType>
    // must come after struct PropertyType, not before).
    //
    // Under real-sdk mode, non-synthetic types already live in the included
    // <onboardapi/type/...> header — skip them to avoid redefinition.
    // Synthetic structs (e.g. <Op>_Args) are ALWAYS emitted: they are our own
    // definitions and appear in no SDK header, so they must be present in both modes.
    {
        // Build a merged, sorted list of (source_order, kind, index) triples.
        // In real-sdk mode we only include synthetic structs (typedefs and real
        // structs come from the SDK header). In stub mode we include everything.
        enum class Kind { Typedef, Struct };
        struct Item {
            std::size_t order;
            Kind kind;
            std::size_t idx;
        };
        std::vector<Item> items;
        items.reserve(mod.typedefs.size() + mmod.structs.size());
        if (!target_real_sdk) {
            for (std::size_t i = 0; i < mod.typedefs.size(); ++i)
                items.push_back({mod.typedefs[i].source_order, Kind::Typedef, i});
        }
        for (std::size_t i = 0; i < mmod.structs.size(); ++i) {
            if (target_real_sdk && !mmod.structs[i].synthetic) continue;
            items.push_back({mmod.structs[i].source_order, Kind::Struct, i});
        }
        std::sort(items.begin(), items.end(),
                  [](Item const& a, Item const& b) { return a.order < b.order; });

        for (auto const& item : items) {
            if (item.kind == Kind::Typedef)
                emit_typedef(h, mod.typedefs[item.idx], mod.name, local_submods);
            else
                emit_struct(h, mmod.structs[item.idx], mod.name, local_submods);
        }
        if (!items.empty()) h << "\n";
    }

    // Emit to_json for each struct, inside the namespace (for ADL).
    // Works in both modes — when real-SDK target, these operate on the
    // SDK's struct types (visible via the included header above).
    for (auto const& s : mmod.structs) {
        h << "inline ::nlohmann::json to_json(" << s.name << " const& v) {\n";
        h << "    ::nlohmann::json j = ::nlohmann::json::object();\n";
        for (auto const& f : s.fields) {
            bool is_primitive = std::holds_alternative<ast::PrimitiveType>(f.type->value);
            // Primitives serialise directly; everything else (structs, enums, and
            // sequences — incl. typedef'd list aliases like ButtonInfoListType)
            // goes through to_json_or_passthrough, which recurses into containers
            // of struct elements that nlohmann can't serialise on its own.
            if (is_primitive) {
                h << "    j[\"" << f.name << "\"] = v." << f.name << ";\n";
            } else {
                h << "    j[\"" << f.name << "\"] = ::flowboard::to_json_or_passthrough(v."
                  << f.name << ");\n";
            }
        }
        h << "    return j;\n";
        h << "}\n";
    }
    if (!mmod.structs.empty()) h << "\n";

    // Emit from_json for each struct, inside the namespace (for ADL).
    for (auto const& s : mmod.structs) {
        h << "inline void from_json(::nlohmann::json const& j, " << s.name << "& v) {\n";
        for (auto const& f : s.fields) {
            h << "    j.at(\"" << f.name << "\").get_to(v." << f.name << ");\n";
        }
        h << "}\n";
    }
    if (!mmod.structs.empty()) h << "\n";

    h << "\n}  // namespace " << mod.name << "\n\n";

    for (auto const& s : mmod.structs) {
        h << "OP_DECLARE_TYPE(::" << mod.name << "::" << s.name
          << ", \"" << mod.name << "::" << s.name << "\")\n";
    }

    // Emit per-struct FieldDescriptor[] tables.
    h << "\n";
    h << "#include \"flowboard/field_descriptor.hpp\"\n";
    for (auto const& s : mmod.structs) {
        // Count classifiable fields up front so we can avoid zero-sized arrays
        // (which are not valid in C++). If a struct has no supported fields,
        // we still emit a one-element sentinel array with empty name + tag,
        // and consumers can detect/skip it.
        std::size_t supported = 0;
        for (auto const& f : s.fields) {
            auto c = classify_field(*f.type, all_typedefs, mod.name, km);
            if (!c.element_tag.empty()) ++supported;
        }
        h << "namespace " << mod.name << " {\n";
        h << "inline constexpr ::flowboard::FieldDescriptor "
          << s.name << "_fields[] = {\n";
        for (auto const& f : s.fields) {
            auto c = classify_field(*f.type, all_typedefs, mod.name, km);
            if (c.element_tag.empty()) {
                h << "    // { \"" << f.name
                  << "\", -, unsupported element type — skipped\n";
                continue;
            }
            h << "    { \"" << f.name
              << "\", ::flowboard::FieldKind::" << c.kind
              << ", \"" << c.element_tag << "\" },\n";
        }
        if (supported == 0) {
            // Sentinel — ensures the array is non-zero-sized; consumers skip
            // descriptors whose element_type_tag is empty.
            h << "    { \"\", ::flowboard::FieldKind::Scalar, \"\" },\n";
        }
        h << "};\n";
        h << "}  // namespace " << mod.name << "\n";
    }

    r.types_hpp = h.str();

    // --- Node classes for IService / IClient interfaces ---
    std::ostringstream n;
    n << "// AUTO-GENERATED by pipegen. Do not edit.\n";
    n << "// SPDX-License-Identifier: MIT\n";
    n << "#include <memory>\n";
    n << "#include <mutex>\n";
    if (target_real_sdk) n << "#include <optional>\n";
    n << "#include \"flowboard/node.hpp\"\n";
    n << "#include \"flowboard/port.hpp\"\n";
    n << "#include \"flowboard/registry.hpp\"\n";
    // Include builtin_types so OP_DECLARE_TYPE for bool/double/std::string is visible.
    n << "#include \"builtin_types.hpp\"\n";
    n << "#include \"onboardapi/runtime/service_node_base.hpp\"\n";
    n << "#include \"onboardapi/runtime/client_node_base.hpp\"\n";
    // Include generated types BEFORE the SDK header so that the generated sentinel
    // (#define FLOWBOARD_GENERATED_<MOD>_TYPES) is set first.  The stub SDK headers
    // check this sentinel and skip any conflicting struct definitions they would otherwise emit.
    n << "#include \"" << mod.name << "_types.hpp\"\n";
    n << "#include \"flowboard/extract_nodes.hpp\"\n";
    n << "#include \"flowboard/extract_registry.hpp\"\n";
    n << "#include \"flowboard/log_sink.hpp\"\n";
    n << "#include \"flowboard/log_registry.hpp\"\n";
    n << "#include \"flowboard/key_value_accumulator.hpp\"\n";
    n << "#include \"flowboard/key_value_accumulator_registry.hpp\"\n";
    n << "#include \"flowboard/list_build.hpp\"\n";
    n << "#include \"flowboard/list_build_registry.hpp\"\n";
    n << "#include \"flowboard/wire_registry.hpp\"\n";
    n << "#include \"flowboard/tap_registry.hpp\"\n";
    n << "#include \"flowboard/port_factory_registry.hpp\"\n";
    n << "#include \"flowboard/throttle.hpp\"\n";
    n << "#include \"flowboard/throttle_registry.hpp\"\n";
    n << "#include \"flowboard/synchronize.hpp\"\n";
    n << "#include \"flowboard/synchronize_registry.hpp\"\n";
    n << "#include \"flowboard/coalescing_mailbox.hpp\"\n";
    if (target_real_sdk) {
        // Real OnboardAPI 9.x: interface shells in <onboardapi/msg/Foo.hpp>,
        // Service/Client classes in <onboardapi/api/Foo.hpp>.
        n << "#include <onboardapi/msg/" << mod_short << ".hpp>\n";
        n << "#include <onboardapi/api/" << mod_short << ".hpp>\n\n";
    } else {
        n << "#include \"onboardapi/" << mod.name << ".hpp\"\n\n";
    }
    n << "namespace flowboard::generated {\n";
    n << "namespace {\n";
    n << "constexpr char const* kOnboardApiSchema = R\"OPSCH({"
         "\"$schema\":\"http://json-schema.org/draft-07/schema#\","
         "\"type\":\"object\","
         "\"required\":[\"domainId\",\"serviceName\"],"
         "\"properties\":{"
           "\"domainId\":{\"type\":\"integer\",\"minimum\":0,\"default\":1},"
           "\"serviceName\":{\"type\":\"string\",\"default\":\"default\"}"
         "},"
         "\"additionalProperties\":false"
       "})OPSCH\";\n";
    n << "constexpr char const* kOnboardApiDefaults = R\"OPDEF({\"domainId\":1,\"serviceName\":\"default\"})OPDEF\";\n";
    // Factory.* nodes have no configurable params — type is implicit in node type.
    n << "constexpr char const* kFactorySchema = R\"OPSCH({"
         "\"$schema\":\"http://json-schema.org/draft-07/schema#\","
         "\"type\":\"object\",\"properties\":{},\"additionalProperties\":false"
       "})OPSCH\";\n";
    n << "constexpr char const* kFactoryDefaults = R\"OPDEF({})OPDEF\";\n";
    n << "}  // namespace\n\n";

    // Pre-scan interfaces to find client and service iface pointers (used in Client node emission).
    ast::InterfaceDecl const* client_iface = nullptr;
    ast::InterfaceDecl const* svc_iface    = nullptr;
    for (auto const& i : mod.interfaces) {
        if (i.name == "IClient")  client_iface = &i;
        if (i.name == "IService") svc_iface    = &i;
    }

    for (auto const& iface : mod.interfaces) {
        bool is_service = (iface.name == "IService");
        std::string class_name = mod.name + (is_service ? "_Service_Node" : "_Client_Node");
        std::string base = is_service ? "::flowboard::onboardapi::ServiceNodeBase"
                                      : "::flowboard::onboardapi::ClientNodeBase";
        std::string type_name_str = mod.name + (is_service ? ".Service" : ".Client");

        // The node implements the SDK interface it RECEIVES on (a Client receives
        // IClient reports; a Service receives IService commands) and drives the SDK
        // handle for the opposite "send" interface (a Client sends IService
        // commands; a Service sends IClient reports).
        ast::InterfaceDecl const* send_iface = is_service ? client_iface : svc_iface;
        std::string const sdk_handle = is_service ? "Service" : "Client";

        // owning_module_of, opt_elem, is_optional_port, list_elem, is_list_port,
        // and is_portable are defined at function scope (single source of truth
        // shared with the <Op>_Args synthesis pass above).

        // C++ type of a List element (for ListValue <-> vector conversion).
        auto list_elem_cpp = [&](ast::TypeRef const& ref) -> std::string {
            auto e = list_elem(ref);
            return cpp_type_ref(*e, owning_module_of(ref), &local_submods);
        };
        // flowboard type tag of a List element (for ListValue::element_type_tag).
        auto list_elem_tag = [&](ast::TypeRef const& ref) -> std::string {
            auto e = list_elem(ref);
            if (auto* p = std::get_if<ast::PrimitiveType>(&e->value)) return primitive_type_tag(p->name);
            if (auto* nt = std::get_if<ast::NamedType>(&e->value)) {
                auto const& qn = nt->qualified_name;
                if (qn.size() == 1) return owning_module_of(ref) + "::" + qn[0];
                if (qn.size() == 2) return qn[0] + "::" + qn[1];
            }
            return "";
        };

        // C++ type for the PORT: ListValue for List, element type for Optional,
        // else the param type itself.
        auto port_cpp = [&](ast::TypeRef const& ref) -> std::string {
            if (is_list_port(ref)) return "::flowboard::ListValue";
            if (auto e = opt_elem(ref)) return cpp_type_ref(*e, owning_module_of(ref), &local_submods);
            return cpp_type_ref(ref, mod.name, &local_submods);
        };
        // C++ type of the actual SDK parameter (the OptionalType vector itself).
        auto sdk_cpp = [&](ast::TypeRef const& ref) -> std::string {
            return cpp_type_ref(ref, mod.name, &local_submods);
        };

        // --- Reliability (prefix-based) -------------------------------------
        auto op_is_coalesce = [&](std::string const& name) {
            return name.rfind("Report", 0) == 0 || name.rfind("Config", 0) == 0;
        };
        // Build a C++ expression for the composite key from the op's Key* params.
        // var_prefix is "" on receive (raw callback args) or "_p_" on send (the
        // composer's latched locals). No Key* params -> the empty-string key.
        auto key_expr = [&](auto const& op, std::string const& var_prefix) -> std::string {
            std::string expr;
            bool any = false;
            for (auto const& p : op.params) {
                if (p.name.rfind("Key", 0) != 0) continue;
                if (any) expr += " + std::string(1, '\\x1f') + ";
                expr += "::flowboard::coalesce_key_part(" + var_prefix + p.name + ")";
                any = true;
            }
            return any ? expr : "std::string{}";
        };

        // Inherit the received SDK interface so the SDK can deliver its callbacks.
        n << "class " << class_name << " : public " << base
          << ", public ::" << mod.name << "::" << iface.name << " {\n";
        n << "public:\n";
        n << "    " << class_name << "(std::string id, ::nlohmann::json const& cfg)\n";
        n << "        : " << base << "(std::move(id), \"" << type_name_str << "\", cfg) {\n";

        for (auto const& op : iface.operations) {
            for (auto const& p : op.params) {
                if (!is_portable(*p.type)) {
                    n << "        // skipped port " << op.name << "." << p.name
                      << " — type is not a registered port type\n";
                    continue;
                }
                n << "        register_output(&out_" << op.name << "_" << p.name << ");\n";
            }
            // Atomic struct output registration.
            if (op_has_args(op))
                n << "        register_output(&out_" << op.name << "_args);\n";
        }

        // Client node: register input ports for single-param IService ops
        if (!is_service && svc_iface) {
            for (auto const& op : svc_iface->operations) {
                if (op.params.size() != 1) continue;
                auto const& p = op.params[0];
                if (!is_portable(*p.type)) continue;
                n << "        register_input(&in_" << op.name << "_" << p.name << ");\n";
            }
        }

        // Client node: register input ports for multi-param IService ops.
        // Each safe param gets a typed input; a bool "trigger" input fires the
        // composed call. Unsupported-type params skip (the SDK call uses their
        // default value at trigger time).
        if (!is_service && svc_iface) {
            for (auto const& op : svc_iface->operations) {
                if (op.params.size() <= 1) continue;
                int safe_count = 0;
                for (auto const& p : op.params) {
                    if (!is_portable(*p.type)) {
                        n << "        // skipped composer input " << op.name << "." << p.name
                          << " — unsupported port type (default-constructed at call time)\n";
                        continue;
                    }
                    n << "        register_input(&in_" << op.name << "_" << p.name << ");\n";
                    ++safe_count;
                }
                if (safe_count > 0) {
                    n << "        register_input(&in_" << op.name << "_trigger);\n";
                    if (op_has_args(op))
                        n << "        register_input(&in_" << op.name << "_args);\n";
                }
            }
        }

        // Service node: register input ports for single-param IClient ops
        // (these are the Report* callbacks the service publishes to its clients).
        if (is_service && client_iface) {
            for (auto const& op : client_iface->operations) {
                if (op.params.size() != 1) continue;
                auto const& p = op.params[0];
                if (!is_portable(*p.type)) continue;
                n << "        register_input(&in_" << op.name << "_" << p.name << ");\n";
            }
        }

        // Service node: register input ports for multi-param IClient ops
        // (typed input per safe param + a bool trigger that fires the call).
        if (is_service && client_iface) {
            for (auto const& op : client_iface->operations) {
                if (op.params.size() <= 1) continue;
                int safe_count = 0;
                for (auto const& p : op.params) {
                    if (!is_portable(*p.type)) {
                        n << "        // skipped composer input " << op.name << "." << p.name
                          << " — unsupported port type (default-constructed at call time)\n";
                        continue;
                    }
                    n << "        register_input(&in_" << op.name << "_" << p.name << ");\n";
                    ++safe_count;
                }
                if (safe_count > 0) {
                    n << "        register_input(&in_" << op.name << "_trigger);\n";
                    if (op_has_args(op))
                        n << "        register_input(&in_" << op.name << "_args);\n";
                }
            }
        }

        n << "    }\n\n";

        // Output port member declarations
        for (auto const& op : iface.operations) {
            for (auto const& p : op.params) {
                if (!is_portable(*p.type)) continue;
                std::string cpp_t = port_cpp(*p.type);
                n << "    ::flowboard::OutputPort<" << cpp_t
                  << "> out_" << op.name << "_" << p.name
                  << "{\"" << op.name << "." << p.name << "\"};\n";
            }
            // Atomic struct output: one port carrying all portable in-params as a struct.
            if (op_has_args(op)) {
                n << "    ::flowboard::OutputPort<::" << mod.name << "::" << op.name
                  << "_Args> out_" << op.name << "_args{\"" << op.name << ".args\"};\n";
            }
        }

        // Client node: input port member declarations for single-param IService ops
        if (!is_service && svc_iface) {
            for (auto const& op : svc_iface->operations) {
                if (op.params.size() != 1) continue;
                auto const& p = op.params[0];
                if (!is_portable(*p.type)) continue;
                std::string cpp_t = port_cpp(*p.type);
                n << "    ::flowboard::InputPort<" << cpp_t
                  << "> in_" << op.name << "_" << p.name
                  << "{\"" << op.name << "." << p.name << "\"};\n";
            }
        }

        // Client node: input port member declarations for multi-param IService ops
        // (typed input per safe param + a bool trigger that fires the call).
        if (!is_service && svc_iface) {
            for (auto const& op : svc_iface->operations) {
                if (op.params.size() <= 1) continue;
                int safe_count = 0;
                for (auto const& p : op.params) {
                    if (!is_portable(*p.type)) continue;
                    std::string cpp_t = port_cpp(*p.type);
                    n << "    ::flowboard::InputPort<" << cpp_t
                      << "> in_" << op.name << "_" << p.name
                      << "{\"" << op.name << "." << p.name << "\"};\n";
                    ++safe_count;
                }
                if (safe_count > 0) {
                    n << "    ::flowboard::InputPort<bool> in_" << op.name
                      << "_trigger{\"" << op.name << ".trigger\"};\n";
                    if (op_has_args(op)) {
                        n << "    ::flowboard::InputPort<::" << mod.name << "::" << op.name
                          << "_Args> in_" << op.name << "_args{\""
                          << op.name << ".args\"};\n";
                    }
                }
            }
        }

        // Service node: input port member declarations for single-param IClient ops.
        if (is_service && client_iface) {
            for (auto const& op : client_iface->operations) {
                if (op.params.size() != 1) continue;
                auto const& p = op.params[0];
                if (!is_portable(*p.type)) continue;
                std::string cpp_t = port_cpp(*p.type);
                n << "    ::flowboard::InputPort<" << cpp_t
                  << "> in_" << op.name << "_" << p.name
                  << "{\"" << op.name << "." << p.name << "\"};\n";
            }
        }

        // Service node: input port member declarations for multi-param IClient ops
        // (typed input per safe param + a bool trigger that fires the call).
        if (is_service && client_iface) {
            for (auto const& op : client_iface->operations) {
                if (op.params.size() <= 1) continue;
                int safe_count = 0;
                for (auto const& p : op.params) {
                    if (!is_portable(*p.type)) continue;
                    std::string cpp_t = port_cpp(*p.type);
                    n << "    ::flowboard::InputPort<" << cpp_t
                      << "> in_" << op.name << "_" << p.name
                      << "{\"" << op.name << "." << p.name << "\"};\n";
                    ++safe_count;
                }
                if (safe_count > 0) {
                    n << "    ::flowboard::InputPort<bool> in_" << op.name
                      << "_trigger{\"" << op.name << ".trigger\"};\n";
                    if (op_has_args(op)) {
                        n << "    ::flowboard::InputPort<::" << mod.name << "::" << op.name
                          << "_Args> in_" << op.name << "_args{\""
                          << op.name << ".args\"};\n";
                    }
                }
            }
        }

        {
            // Override each received SDK callback; forward supported-type args to output ports.
            // The real OnboardAPI SDK passes scalar primitives + enums BY VALUE
            // (bool IsRemoved, double X, ModeKind::Type Mode); strings, structs and
            // sequences BY CONST REFERENCE. The stub SDK uniformly uses const& for
            // every param. To match either signature exactly (otherwise C3668 — no
            // method overrides the base), pick the convention per-param based on the
            // target SDK + the resolved underlying type.
            auto pass_by_value_real_sdk = [&](ast::TypeRef const& original) -> bool {
                auto resolved = resolve_typedef(original, all_typedefs, mod.name);
                if (auto* p = std::get_if<ast::PrimitiveType>(&resolved.value)) {
                    return p->name != "string";  // string is the lone primitive by const&
                }
                if (std::holds_alternative<ast::SequenceType>(resolved.value)) return false;
                if (auto* nt = std::get_if<ast::NamedType>(&resolved.value)) {
                    auto const& qn = nt->qualified_name;
                    // 3-segment NamedType is always an external enum (Module::EnumKind::Type).
                    if (qn.size() == 3) return true;
                    // 2-segment NamedType whose first segment is a local enum sub-module.
                    if (qn.size() == 2 && local_submods.count(qn[0])) return true;
                }
                return false;
            };
            for (auto const& op : iface.operations) {
                n << "    void " << op.name << "(";
                bool first = true;
                for (auto const& p : op.params) {
                    if (!first) n << ", ";
                    first = false;
                    // Signature must match the SDK exactly → the full param type
                    // (the OptionalType vector for Optional params).
                    std::string cpp_t = sdk_cpp(*p.type);
                    if (target_real_sdk && pass_by_value_real_sdk(*p.type)) {
                        n << cpp_t << " " << p.name;
                    } else {
                        n << cpp_t << " const& " << p.name;
                    }
                }
                n << ") override {\n";
                if (op_is_coalesce(op.name)) {
                    n << "        std::string _k = " << key_expr(op, "") << ";\n";
                    n << "        auto _act = [this";
                    for (auto const& p : op.params)
                        if (is_portable(*p.type)) n << ", " << p.name;
                    n << "]() {\n";
                    for (auto const& p : op.params) {
                        if (!is_portable(*p.type)) continue;
                        std::string elem_t = port_cpp(*p.type);
                        if (is_optional_port(*p.type)) {
                            n << "            if (!" << p.name << ".empty()) out_" << op.name << "_" << p.name
                              << ".emit(std::make_shared<const " << elem_t << ">(" << p.name << ".front()));\n";
                        } else if (is_list_port(*p.type)) {
                            n << "            { auto _lv = std::make_shared<::flowboard::ListValue>();\n";
                            n << "              _lv->element_type_tag = \"" << list_elem_tag(*p.type) << "\";\n";
                            n << "              for (auto const& _e : " << p.name
                              << ") _lv->items.push_back(::flowboard::to_json_or_passthrough(_e));\n";
                            n << "              out_" << op.name << "_" << p.name << ".emit(std::move(_lv)); }\n";
                        } else {
                            n << "            out_" << op.name << "_" << p.name
                              << ".emit(std::make_shared<const " << elem_t << ">(" << p.name << "));\n";
                        }
                    }
                    // Atomic struct output: build and emit all portable in-params as one struct.
                    if (op_has_args(op)) {
                        n << "            {\n";
                        n << "                auto _args = std::make_shared<::" << mod.name << "::" << op.name << "_Args>();\n";
                        for (auto const& p : op.params) {
                            if (p.direction == "in" && is_portable(*p.type))
                                n << "                _args->" << p.name << " = " << p.name << ";\n";
                        }
                        n << "                out_" << op.name << "_args.emit(_args);\n";
                        n << "            }\n";
                    }
                    n << "        };\n";
                    n << "        if (mbox_recv_" << op.name << "_.put(std::move(_k), std::move(_act)))\n";
                    n << "            enqueue([this]{ mbox_recv_" << op.name << "_.drain(); });\n";
                } else {
                for (auto const& p : op.params) {
                    if (!is_portable(*p.type)) {
                        n << "        (void)" << p.name << "; // skipped — unsupported port type\n";
                        continue;
                    }
                    std::string elem_t = port_cpp(*p.type);
                    if (is_optional_port(*p.type)) {
                        // Optional: emit the contained value only when present.
                        n << "        if (!" << p.name << ".empty()) {\n";
                        n << "            auto _arg = std::make_shared<const " << elem_t
                          << ">(" << p.name << ".front());\n";
                        n << "            enqueue([this, _arg] { out_" << op.name << "_" << p.name
                          << ".emit(_arg); });\n";
                        n << "        }\n";
                    } else if (is_list_port(*p.type)) {
                        // List: pack the vector into a single ListValue (each element JSON).
                        n << "        {\n";
                        n << "            auto _lv = std::make_shared<::flowboard::ListValue>();\n";
                        n << "            _lv->element_type_tag = \"" << list_elem_tag(*p.type) << "\";\n";
                        n << "            for (auto const& _e : " << p.name
                          << ") _lv->items.push_back(::flowboard::to_json_or_passthrough(_e));\n";
                        n << "            enqueue([this, _lv] { out_" << op.name << "_" << p.name
                          << ".emit(_lv); });\n";
                        n << "        }\n";
                    } else {
                        n << "        {\n";
                        n << "            auto _arg = std::make_shared<const " << elem_t
                          << ">(" << p.name << ");\n";
                        n << "            enqueue([this, _arg] { out_" << op.name << "_" << p.name
                          << ".emit(_arg); });\n";
                        n << "        }\n";
                    }
                }
                // Atomic struct output: build and emit all portable in-params as one struct.
                if (op_has_args(op)) {
                    n << "        {\n";
                    n << "            auto _args = std::make_shared<::" << mod.name << "::" << op.name << "_Args>();\n";
                    for (auto const& p : op.params) {
                        if (p.direction == "in" && is_portable(*p.type))
                            n << "            _args->" << p.name << " = " << p.name << ";\n";
                    }
                    n << "            enqueue([this, _args] { out_" << op.name << "_args.emit(_args); });\n";
                    n << "        }\n";
                }
                }
                n << "    }\n";
            }

            // on_start: construct the SDK handle, then wire input ports for the
            // "send" interface ops (a Client sends IService commands; a Service
            // sends IClient reports via the handle's operator->() proxy).
            // Stub SDK : create() returns unique_ptr, methods callable directly.
            // Real SDK : create<int, Derived>(...) returns the handle by value,
            //            wrapped in std::optional for late init in on_start.
            n << "    void on_start() override {\n";
            if (target_real_sdk) {
                n << "        handle_.emplace(::" << mod.name
                  << "::" << sdk_handle << "::create<int, " << class_name
                  << ">(domain_id_, service_name_, *this));\n";
            } else {
                n << "        handle_ = ::" << mod.name
                  << "::" << sdk_handle << "::create(domain_id_, service_name_, *this);\n";
            }
            if (send_iface) {
                // Wire single-param send ops: input sink enqueues a call to the SDK handle
                for (auto const& op : send_iface->operations) {
                    if (op.params.size() == 1) {
                        auto const& p = op.params[0];
                        if (!is_portable(*p.type)) continue;
                        std::string cpp_t = port_cpp(*p.type);
                        // Use explicit Value type to avoid MSVC auto-in-lambda deduction issues
                        std::string value_t = "::flowboard::InputPort<" + cpp_t + ">::Value";
                        // `prep` runs inside the lambda before the SDK call; `arg` is the
                        // expression passed to it. Optional wraps the single value back into
                        // the SDK vector; List rebuilds the vector from the ListValue items.
                        std::string prep;
                        std::string arg;
                        if (is_list_port(*p.type)) {
                            prep = sdk_cpp(*p.type) + " _vec; if (_v) for (auto const& _it : _v->items) _vec.push_back(::flowboard::extract_typed<"
                                 + list_elem_cpp(*p.type) + ">(_it)); ";
                            arg = "std::move(_vec)";
                        } else if (is_optional_port(*p.type)) {
                            arg = sdk_cpp(*p.type) + "{*_v}";
                        } else {
                            arg = "*_v";
                        }
                        std::string call_lhs = target_real_sdk ? "(*handle_)->" : "handle_->";
                        std::string body = "if (handle_) { " + prep + call_lhs + op.name + "(" + arg + "); }";
                        n << "        in_" << op.name << "_" << p.name
                          << ".set_internal_sink([this](" << value_t << " _v) {\n";
                        if (op_is_coalesce(op.name)) {
                            n << "            auto _act = [this, _v]{ " << body << " };\n";
                            n << "            if (mbox_send_" << op.name << "_.put(std::string{}, std::move(_act)))\n";
                            n << "                enqueue([this]{ mbox_send_" << op.name << "_.drain(); });\n";
                        } else {
                            n << "            enqueue([this, _v] { " << body << " });\n";
                        }
                        n << "        });\n";
                    }
                }
                // Multi-param send ops: latch each input under a mutex,
                // fire handle_->Op(p1, p2, ...) when the trigger sink receives true.
                // Unsupported-type params are default-constructed at call time.
                for (auto const& op : send_iface->operations) {
                    if (op.params.size() <= 1) continue;
                    int safe_count = 0;
                    for (auto const& p : op.params) {
                        if (!is_portable(*p.type)) continue;
                        std::string cpp_t  = port_cpp(*p.type);
                        std::string value_t = "::flowboard::InputPort<" + cpp_t + ">::Value";
                        n << "        in_" << op.name << "_" << p.name
                          << ".set_internal_sink([this](" << value_t << " _v) {\n";
                        n << "            enqueue([this, _v] {\n";
                        n << "                std::scoped_lock _g(cache_mu_);\n";
                        n << "                cur_" << op.name << "_" << p.name << "_ = _v;\n";
                        n << "            });\n";
                        n << "        });\n";
                        ++safe_count;
                    }
                    if (safe_count == 0) {
                        n << "        // " << op.name
                          << ": all params unsupported — composer omitted.\n";
                        continue;
                    }
                    n << "        in_" << op.name
                      << "_trigger.set_internal_sink([this](::flowboard::InputPort<bool>::Value _t) {\n";
                    n << "            enqueue([this, _t] {\n";
                    n << "                if (!_t || !*_t) return;\n";
                    // Declare a local default for each param; copy from cache if safe and present.
                    // [[maybe_unused]] silences warnings when the SDK doesn't declare the method
                    // and the requires-guarded call below is the discarded branch.
                    for (auto const& p : op.params) {
                        // Local holds the full SDK arg type (OptionalType vector for Optional).
                        std::string cpp_t = sdk_cpp(*p.type);
                        n << "                [[maybe_unused]] " << cpp_t
                          << " _p_" << p.name << "{};\n";
                    }
                    n << "                {\n";
                    n << "                    std::scoped_lock _g(cache_mu_);\n";
                    for (auto const& p : op.params) {
                        if (!is_portable(*p.type)) continue;
                        if (is_optional_port(*p.type)) {
                            n << "                    if (cur_" << op.name << "_" << p.name
                              << "_) _p_" << p.name << " = " << sdk_cpp(*p.type)
                              << "{*cur_" << op.name << "_" << p.name << "_};\n";
                        } else if (is_list_port(*p.type)) {
                            n << "                    if (cur_" << op.name << "_" << p.name
                              << "_) for (auto const& _it : cur_" << op.name << "_" << p.name
                              << "_->items) _p_" << p.name << ".push_back(::flowboard::extract_typed<"
                              << list_elem_cpp(*p.type) << ">(_it));\n";
                        } else {
                            n << "                    if (cur_" << op.name << "_" << p.name
                              << "_) _p_" << p.name << " = *cur_" << op.name << "_" << p.name << "_;\n";
                        }
                    }
                    n << "                }\n";
                    n << "                if (!handle_) return;\n";
                    // SFINAE guard: the hand-written stub SDK Client may not declare this op.
                    // When it doesn't, the composer is a compile-time no-op rather than an error.
                    std::string const call_lhs = target_real_sdk ? "(*handle_)->" : "handle_->";
                    std::string args_csv;
                    { bool f = true; for (auto const& p : op.params) { if (!f) args_csv += ", "; f = false; args_csv += "_p_" + p.name; } }
                    if (op_is_coalesce(op.name)) {
                        n << "                std::string _k = " << key_expr(op, "_p_") << ";\n";
                        n << "                auto _act = [this";
                        for (auto const& p : op.params) n << ", _p_" << p.name;
                        n << "]{\n";
                        n << "                    if (!handle_) return;\n";
                        n << "                    if constexpr (requires { " << call_lhs << op.name << "(" << args_csv << "); }) { "
                          << call_lhs << op.name << "(" << args_csv << "); }\n";
                        n << "                };\n";
                        n << "                if (mbox_send_" << op.name << "_.put(std::move(_k), std::move(_act)))\n";
                        n << "                    enqueue([this]{ mbox_send_" << op.name << "_.drain(); });\n";
                    } else {
                        n << "                if constexpr (requires { " << call_lhs << op.name << "(" << args_csv << "); }) { "
                          << call_lhs << op.name << "(" << args_csv << "); }\n";
                    }
                    n << "            });\n";
                    n << "        });\n";
                    // Atomic args sink: one struct message = one SDK call with consistent args.
                    // Placed right after the trigger sink; both paths coexist (supplement).
                    if (op_has_args(op)) {
                        std::string args_type = "::" + mod.name + "::" + op.name + "_Args";
                        n << "        in_" << op.name
                          << "_args.set_internal_sink([this](::flowboard::InputPort<"
                          << args_type << ">::Value _v) {\n";
                        n << "            enqueue([this, _v] {\n";
                        n << "                if (!_v) return;\n";
                        // Declare locals for ALL params (default-constructed); fill portables from struct.
                        for (auto const& p : op.params) {
                            std::string cpp_t = sdk_cpp(*p.type);
                            n << "                [[maybe_unused]] " << cpp_t
                              << " _p_" << p.name << "{};\n";
                        }
                        // Copy portable params directly from the struct field.
                        // The struct field type == sdk_cpp() for all portable kinds:
                        //   - scalar primitive/struct: direct copy
                        //   - Optional (typedef of sequence<T,1>): struct field IS the typedef = sdk_cpp
                        //   - List (typedef of sequence<T>): struct field IS the typedef = sdk_cpp
                        for (auto const& p : op.params) {
                            if (!is_portable(*p.type)) continue;
                            n << "                _p_" << p.name << " = _v->" << p.name << ";\n";
                        }
                        n << "                if (!handle_) return;\n";
                        if (op_is_coalesce(op.name)) {
                            n << "                std::string _k = " << key_expr(op, "_p_") << ";\n";
                            n << "                auto _act = [this";
                            for (auto const& p : op.params) n << ", _p_" << p.name;
                            n << "]{\n";
                            n << "                    if (!handle_) return;\n";
                            n << "                    if constexpr (requires { " << call_lhs << op.name << "(" << args_csv << "); }) { "
                              << call_lhs << op.name << "(" << args_csv << "); }\n";
                            n << "                };\n";
                            n << "                if (mbox_send_" << op.name << "_.put(std::move(_k), std::move(_act)))\n";
                            n << "                    enqueue([this]{ mbox_send_" << op.name << "_.drain(); });\n";
                        } else {
                            n << "                if constexpr (requires { " << call_lhs << op.name << "(" << args_csv << "); }) { "
                              << call_lhs << op.name << "(" << args_csv << "); }\n";
                        }
                        n << "            });\n";
                        n << "        });\n";
                    }
                }
            }
            n << "    }\n";
            n << "private:\n";
            if (target_real_sdk) {
                n << "    std::optional<::" << mod.name << "::" << sdk_handle << "> handle_;\n";
            } else {
                n << "    std::unique_ptr<" << mod.name << "::" << sdk_handle << "> handle_;\n";
            }

            // Multi-param send composer: per-param latched cache + shared mutex.
            // Emitted only if at least one multi-param send op has at least
            // one safe param.
            if (send_iface) {
                bool need_cache = false;
                for (auto const& op : send_iface->operations) {
                    if (op.params.size() <= 1) continue;
                    for (auto const& p : op.params) {
                        if (is_portable(*p.type)) {
                            need_cache = true;
                            break;
                        }
                    }
                    if (need_cache) break;
                }
                if (need_cache) {
                    n << "    std::mutex cache_mu_;\n";
                    for (auto const& op : send_iface->operations) {
                        if (op.params.size() <= 1) continue;
                        for (auto const& p : op.params) {
                            if (!is_portable(*p.type)) continue;
                            std::string cpp_t = port_cpp(*p.type);  // element type for Optional
                            n << "    std::shared_ptr<const " << cpp_t
                              << "> cur_" << op.name << "_" << p.name << "_;\n";
                        }
                    }
                }
            }

            // Coalescing mailboxes for Report*/Config* ops (one per coalesce op on
            // each path). Emitted unconditionally so both receive and send wiring
            // can reference them.
            for (auto const& op : iface.operations)
                if (op_is_coalesce(op.name))
                    n << "    ::flowboard::CoalescingMailbox mbox_recv_" << op.name << "_;\n";
            if (send_iface)
                for (auto const& op : send_iface->operations)
                    if (op_is_coalesce(op.name))
                        n << "    ::flowboard::CoalescingMailbox mbox_send_" << op.name << "_;\n";
        }

        n << "};\n\n";
    }

    // Emit OP_REGISTER_NODE inside the namespace so that ClassType is an unqualified
    // name — the ## token-pasting in the macro requires a simple identifier.
    for (auto const& iface : mod.interfaces) {
        bool is_service = (iface.name == "IService");
        std::string class_name = mod.name + (is_service ? "_Service_Node" : "_Client_Node");
        std::string type_name_str = mod.name + (is_service ? ".Service" : ".Client");
        n << "OP_REGISTER_NODE_WITH_SCHEMA(\"" << type_name_str << "\", " << class_name
          << ", kOnboardApiSchema, kOnboardApiDefaults)\n";
    }

    // Per-struct extract factory: dispatches on field name.
    for (auto const& s : mmod.structs) {
        // For each field, decide which Extract* template to instantiate and what
        // C++ type the output port should be parameterised on.
        auto cpp_for_element = [&](ast::TypeRef const& original) -> std::pair<std::string, std::string> {
            // Returns (template class name without <>, cpp type expression for TElem/TOut).
            auto resolved = resolve_typedef(original, all_typedefs, mod.name);
            std::string klass;
            ast::TypeRef element;
            std::string owning_module = mod.name;
            if (auto* n = std::get_if<ast::NamedType>(&original.value);
                n && n->qualified_name.size() == 2 && km.count(n->qualified_name[0])) {
                owning_module = n->qualified_name[0];
            }
            if (auto* seq = std::get_if<ast::SequenceType>(&resolved.value)) {
                klass = seq->max_size.has_value() && *seq->max_size == 1
                          ? "ExtractOptional" : "ExtractList";
                element = resolve_typedef(*seq->element, all_typedefs, owning_module);
            } else {
                klass = "ExtractScalar";
                element = resolved;
            }
            std::string cpp;
            if (std::holds_alternative<ast::PrimitiveType>(element.value)) {
                cpp = cpp_type_ref(element, owning_module, nullptr);  // full scalar set
            } else if (auto* n2 = std::get_if<ast::NamedType>(&element.value)) {
                auto const& qn = n2->qualified_name;
                if (qn.size() == 1) {
                    cpp = "::" + owning_module + "::" + qn[0];
                } else if (qn.size() == 2 && km.count(qn[0])) {
                    cpp = "::" + qn[0] + "::" + qn[1];
                }
            }
            if (cpp.empty()) return {{}, {}};
            return {klass, cpp};
        };

        std::string fn_name = "make_extract_" + mod.name + "__" + s.name;
        n << "\n";
        n << "static std::unique_ptr<::flowboard::Node>\n";
        n << fn_name << "(std::string id, std::string field) {\n";
        for (auto const& f : s.fields) {
            auto [klass, cpp_t] = cpp_for_element(*f.type);
            if (klass.empty()) {
                n << "    // skipped field '" << f.name << "': unsupported element type\n";
                continue;
            }
            n << "    if (field == \"" << f.name << "\")\n";
            n << "        return std::make_unique<::flowboard::"
              << klass << "<::" << mod.name << "::" << s.name << ", " << cpp_t << ">>(\n";
            n << "            std::move(id), \"" << f.name << "\");\n";
        }
        n << "    return nullptr;\n";
        n << "}\n";
        n << "OP_REGISTER_EXTRACT_FACTORY(\"" << mod.name << "::" << s.name
          << "\", &" << fn_name << ")\n";

        // Sinks.Log factory for this struct. Instantiates LogSinkT<Struct>;
        // the to_json() ADL overload emitted in <Module>_types.hpp gives
        // every struct a sensible JSON representation, so the same template
        // works for all generated types without per-struct stream operators.
        std::string log_fn_name = "make_logsink_" + mod.name + "__" + s.name;
        n << "\n";
        n << "static std::unique_ptr<::flowboard::Node>\n";
        n << log_fn_name << "(std::string id, ::nlohmann::json const& cfg) {\n";
        n << "    return std::make_unique<::flowboard::LogSinkT<::"
          << mod.name << "::" << s.name << ">>(std::move(id), cfg);\n";
        n << "}\n";
        n << "OP_REGISTER_LOG_FACTORY(\"" << mod.name << "::" << s.name
          << "\", &" << log_fn_name << ")\n";

        // Transform.KeyValueAccumulator factory for this struct as the value
        // type, keyed on std::string. Every Key* parameter in the SDK aliases
        // to string (see typedef resolution in is_safe_type), so the (String,
        // Struct) pair is the only one needed in practice. The accumulator
        // template only requires to_json_or_passthrough on the value type,
        // which the generated to_json ADL overload provides.
        std::string kv_fn_name = "make_kv_accumulator_str_" + mod.name + "__" + s.name;
        n << "static std::unique_ptr<::flowboard::Node>\n";
        n << kv_fn_name << "(std::string id, ::nlohmann::json const& cfg) {\n";
        n << "    return std::make_unique<::flowboard::KeyValueAccumulator<"
          << "std::string, ::" << mod.name << "::" << s.name << ">>(std::move(id), cfg);\n";
        n << "}\n";
        n << "OP_REGISTER_KEY_VALUE_ACCUMULATOR_FACTORY("
          << "\"flowboard::String\", \"" << mod.name << "::" << s.name
          << "\", &" << kv_fn_name << ")\n";

        // Transform.List.Build / Transform.List.Accumulate factories for this
        // struct as the element type. ListBuildT/ListAccumulateT serialise each
        // element via the generated to_json ADL overload, so any struct works.
        std::string lb_fn_name = "make_listbuild_" + mod.name + "__" + s.name;
        std::string la_fn_name = "make_listaccum_" + mod.name + "__" + s.name;
        n << "static std::unique_ptr<::flowboard::Node>\n";
        n << lb_fn_name << "(std::string id, int count) {\n";
        n << "    return std::make_unique<::flowboard::ListBuildT<::"
          << mod.name << "::" << s.name << ">>(std::move(id), count);\n";
        n << "}\n";
        n << "static std::unique_ptr<::flowboard::Node>\n";
        n << la_fn_name << "(std::string id, int cap) {\n";
        n << "    return std::make_unique<::flowboard::ListAccumulateT<::"
          << mod.name << "::" << s.name << ">>(std::move(id), cap);\n";
        n << "}\n";
        n << "OP_REGISTER_LIST_ELEM_FACTORIES(\"" << mod.name << "::" << s.name
          << "\", &" << lb_fn_name << ", &" << la_fn_name << ")\n";

        // Wire factory for this struct. Lets Graph::connect build an
        // EdgeHolder<Struct> at runtime without graph.cpp needing to know
        // about it. Mirrors the per-primitive registrations in builtin_types.cpp.
        n << "OP_REGISTER_WIRE_FACTORY(\""
          << mod.name << "::" << s.name << "\", ::"
          << mod.name << "::" << s.name << ")\n";

        // Live-tap factory for this struct, so the web canvas streams its
        // value on any output port (received Report* fields, Extract output,
        // Factory output, …). Serializes via the generated to_json ADL overload.
        n << "OP_REGISTER_TAP_FACTORY(\""
          << mod.name << "::" << s.name << "\", ::"
          << mod.name << "::" << s.name << ")\n";

        // Port-factory + JSON marshaller for this struct, so Python.Script (or
        // any other dynamic-port node) can spin up a typed port for it and
        // round-trip values through JSON. Uses the same to_json/from_json ADL
        // overloads emitted above.
        n << "OP_REGISTER_PORT_FACTORY(\""
          << mod.name << "::" << s.name << "\", ::"
          << mod.name << "::" << s.name << ")\n";

        // Transform.Throttle factory for this struct. ThrottleT<T> is fully
        // type-agnostic, so the same template rate-limits struct streams too.
        std::string throttle_fn_name = "make_throttle_" + mod.name + "__" + s.name;
        n << "static std::unique_ptr<::flowboard::Node>\n";
        n << throttle_fn_name << "(std::string id, ::nlohmann::json const& cfg) {\n";
        n << "    return std::make_unique<::flowboard::ThrottleT<::"
          << mod.name << "::" << s.name << ">>(std::move(id), cfg);\n";
        n << "}\n";
        n << "OP_REGISTER_THROTTLE_FACTORY(\"" << mod.name << "::" << s.name
          << "\", &" << throttle_fn_name << ")\n";

        // Transform.Synchronize cell factory for this struct. SyncCellT<T> is a
        // pure pass-through latched pair, so struct streams synchronise too — and
        // may be mixed with other types in one node.
        std::string sync_fn_name = "make_synchronize_cell_" + mod.name + "__" + s.name;
        n << "static std::unique_ptr<::flowboard::ISyncCell>\n";
        n << sync_fn_name << "(std::string in_name, std::string out_name) {\n";
        n << "    return std::make_unique<::flowboard::SyncCellT<::"
          << mod.name << "::" << s.name << ">>(std::move(in_name), std::move(out_name));\n";
        n << "}\n";
        n << "OP_REGISTER_SYNCHRONIZE_FACTORY(\"" << mod.name << "::" << s.name
          << "\", &" << sync_fn_name << ")\n";
    }

    // Per-struct Factory node: dual of Transform.Extract. One typed input port per
    // scalar field (struct or primitive) plus a "trigger" bool input. Latches the
    // most recent value on each input; on trigger=true, composes the struct from
    // the cached values and emits via the "out" output port. Optional/List fields
    // are skipped in v1 (matching the existing Extract "unsupported element type"
    // pattern — they could be added later with append/clear semantics).
    for (auto const& s : mmod.structs) {
        // Classify a field by *primitive kind* so we know whether it can be
        // edited inline as a default config value. Empty string means
        // "not a primitive" — either a struct/typedef-to-struct or unsupported.
        // All integer widths (long, short, octet, plus their unsigned forms)
        // collapse to "integer" for the inline-edit JSON schema. The runtime
        // only carries one integer tag (flowboard::Int64), so wider/narrower
        // values flow through int64_t and get widened/narrowed at the struct
        // field assignment in the trigger handler.
        auto is_integer_primitive = [](std::string const& n) {
            return n == "long" || n == "long long"
                || n == "short"
                || n == "octet"
                || n == "unsigned long" || n == "unsigned long long"
                || n == "unsigned short";
        };
        auto is_number_primitive = [](std::string const& n) {
            return n == "double" || n == "float";
        };

        auto primitive_kind = [&](ast::TypeRef const& original) -> std::string {
            auto resolved = resolve_typedef(original, all_typedefs, mod.name);
            if (std::holds_alternative<ast::SequenceType>(resolved.value)) return {};
            if (auto* p = std::get_if<ast::PrimitiveType>(&resolved.value)) {
                // Returns a JSON Schema type keyword — float/double collapse to
                // "number" (NOT the raw "double"; RJSF rejects that as an
                // unknown field type). Integer widths collapse to "integer".
                if (is_number_primitive (p->name)) return "number";
                if (p->name == "boolean")          return "boolean";
                if (is_integer_primitive(p->name)) return "integer";
                if (p->name == "string")           return "string";
            }
            return {};
        };

        // Returns the C++ type name for a scalar field (primitive or struct), or
        // "" for sequence-typed fields (Optional/List) and unsupported primitives.
        auto cpp_for_scalar_field = [&](ast::TypeRef const& original) -> std::string {
            auto resolved = resolve_typedef(original, all_typedefs, mod.name);
            std::string owning_module = mod.name;
            if (auto* nn = std::get_if<ast::NamedType>(&original.value);
                nn && nn->qualified_name.size() == 2 && km.count(nn->qualified_name[0])) {
                owning_module = nn->qualified_name[0];
            }
            if (std::holds_alternative<ast::SequenceType>(resolved.value))
                return {};  // Optional/List skipped in v1
            if (auto* p = std::get_if<ast::PrimitiveType>(&resolved.value)) {
                if (is_number_primitive (p->name)) return "double";
                if (p->name == "boolean")          return "bool";
                if (is_integer_primitive(p->name)) return "::std::int64_t";
                if (p->name == "string")           return "::std::string";
                return {};
            }
            if (auto* nn2 = std::get_if<ast::NamedType>(&resolved.value)) {
                auto const& qn = nn2->qualified_name;
                if (qn.size() == 1) return "::" + owning_module + "::" + qn[0];
                if (qn.size() == 2 && km.count(qn[0])) return "::" + qn[0] + "::" + qn[1];
            }
            return {};
        };

        // Per-field port classification (mirrors the Cmd-op composer): every
        // struct field becomes a fillable input. A scalar/struct field is a port
        // of its own type; an Optional<T> (sequence<T,1>) is a port of element T
        // (wrapped into the 1-element vector when building the struct); a List<T>
        // (sequence<T>) is a single flowboard::ListValue port (converted to
        // std::vector<T> when building). Only fields whose element isn't a safe
        // port type are skipped.
        auto f_owning = [&](ast::TypeRef const& ref) -> std::string {
            if (auto* nt = std::get_if<ast::NamedType>(&ref.value))
                if (nt->qualified_name.size() == 2 && km.count(nt->qualified_name[0]))
                    return nt->qualified_name[0];
            return mod.name;
        };
        auto f_seq_elem = [&](ast::TypeRef const& ref, bool want_optional)
                -> std::optional<ast::TypeRef> {
            std::string owning = f_owning(ref);
            auto resolved = resolve_typedef(ref, all_typedefs, owning);
            if (auto* sq = std::get_if<ast::SequenceType>(&resolved.value)) {
                bool is_opt = sq->max_size && *sq->max_size == 1;
                if (is_opt == want_optional) {
                    ast::TypeRef elem = resolve_typedef(*sq->element, all_typedefs, owning);
                    if (is_safe_type(elem, local_submods, local_typedefs, all_typedefs, owning, km))
                        return elem;
                }
            }
            return std::nullopt;
        };

        struct FactoryFieldEmit {
            std::string name;       // field + port name
            std::string port_t;     // C++ type of the input port
            std::string kind;       // "Scalar" | "Optional" | "List"
            std::string elem_cpp;   // List element C++ type (List only)
            std::string prim_kind;  // inline-config primitive kind ("" unless a scalar primitive)
        };
        std::vector<FactoryFieldEmit> factory_fields;
        for (auto const& f : s.fields) {
            if (auto e = f_seq_elem(*f.type, /*want_optional=*/true)) {
                factory_fields.push_back({f.name,
                    cpp_type_ref(*e, f_owning(*f.type), &local_submods), "Optional", "", ""});
            } else if (auto le = f_seq_elem(*f.type, /*want_optional=*/false)) {
                factory_fields.push_back({f.name, "::flowboard::ListValue", "List",
                    cpp_type_ref(*le, f_owning(*f.type), &local_submods), ""});
            } else {
                std::string ct = cpp_for_scalar_field(*f.type);
                if (!ct.empty())
                    factory_fields.push_back({f.name, ct, "Scalar", "", primitive_kind(*f.type)});
            }
        }

        std::string class_name    = "Factory_" + mod.name + "_" + s.name;
        std::string type_name_str = "Factory." + mod.name + "::" + s.name;
        std::string struct_full   = "::" + mod.name + "::" + s.name;

        // Collect the primitive-typed fields for the per-Factory config schema:
        // these are the ones the Inspector / canvas surface as inline text
        // inputs (default value, overridable by an incoming connection).
        std::vector<FactoryFieldEmit const*> primitive_fields;
        for (auto const& ff : factory_fields) {
            if (!ff.prim_kind.empty()) primitive_fields.push_back(&ff);
        }

        // Emit per-Factory schema + defaults constants. Even when there are no
        // primitive fields we still emit them (empty properties) so every
        // factory uses the same registration pattern.
        std::string schema_var   = "kSchema_"   + class_name;
        std::string defaults_var = "kDefaults_" + class_name;

        auto default_literal = [](std::string const& prim) -> std::string {
            if (prim == "string")  return "\"\"";
            if (prim == "boolean") return "false";
            // number / integer
            return "0";
        };

        n << "\nnamespace {\n";
        n << "constexpr char const* " << schema_var
          << " = R\"OPSCH({"
          << "\"$schema\":\"http://json-schema.org/draft-07/schema#\","
          << "\"type\":\"object\",\"properties\":{"
          << "\"autoTriggerOnNewInput\":{\"type\":\"boolean\",\"default\":true,"
          << "\"description\":\"Emit the struct whenever any input port updates. "
             "If false, the struct is only built/emitted on the trigger port.\"}";
        for (std::size_t i = 0; i < primitive_fields.size(); ++i) {
            auto const& ff = *primitive_fields[i];
            n << ",\"" << ff.name << "\":{\"type\":\"" << ff.prim_kind
              << "\",\"default\":" << default_literal(ff.prim_kind) << "}";
        }
        n << "},\"additionalProperties\":false})OPSCH\";\n";

        n << "constexpr char const* " << defaults_var
          << " = R\"OPDEF({\"autoTriggerOnNewInput\":true";
        for (std::size_t i = 0; i < primitive_fields.size(); ++i) {
            auto const& ff = *primitive_fields[i];
            n << ",\"" << ff.name << "\":" << default_literal(ff.prim_kind);
        }
        n << "})OPDEF\";\n";
        n << "}  // namespace\n";

        n << "\n";
        n << "class " << class_name << " : public ::flowboard::Node {\n";
        n << "public:\n";
        n << "    " << class_name << "(std::string id, ::nlohmann::json const& cfg)\n";
        n << "        : ::flowboard::Node(std::move(id), \"" << type_name_str << "\"),\n";
        n << "          trigger_(\"trigger\"), out_(\"out\"),\n";
        n << "          triggered_(\"triggered\"), updated_(\"updated\"),\n";
        n << "          auto_trigger_(cfg.value(\"autoTriggerOnNewInput\", true))";
        for (auto const& ff : factory_fields) {
            n << ",\n          in_" << ff.name << "_(\"" << ff.name << "\")";
        }
        n << " {\n";
        n << "        register_input(&trigger_);\n";
        n << "        register_output(&out_);\n";
        // Auxiliary bool outputs:
        //   - triggered: pulses true whenever any input port (trigger or
        //     field) receives a value.
        //   - updated:   pulses true when a field input arrives with a value
        //     different from the previously cached one (or with no prior
        //     cached value). The trigger input doesn't carry a payload to
        //     compare, so it never fires `updated`.
        n << "        register_output(&triggered_);\n";
        n << "        register_output(&updated_);\n";
        for (auto const& ff : factory_fields) {
            n << "        register_input(&in_" << ff.name << "_);\n";
        }
        {
            std::set<std::string> _ported;
            for (auto const& ff : factory_fields) _ported.insert(ff.name);
            for (auto const& f : s.fields) {
                if (!_ported.count(f.name)) {
                    n << "        // skipped factory input '" << f.name
                      << "': element type is not a registered port type\n";
                }
            }
        }
        // Pre-populate the cache from the primitive defaults in the config so
        // a Factory can fire even with no inbound edges feeding it.
        if (!primitive_fields.empty()) {
            n << "        // Seed cache with inline-config defaults (overridden by\n";
            n << "        // incoming edges once they deliver a value).\n";
            for (auto const* ffp : primitive_fields) {
                auto const& ff = *ffp;
                std::string fallback = default_literal(ff.prim_kind);
                if (ff.prim_kind == "string") {
                    n << "        {\n";
                    n << "            auto _d = cfg.value(\"" << ff.name
                      << "\", ::std::string{" << fallback << "});\n";
                    n << "            cur_" << ff.name
                      << "_ = std::make_shared<const " << ff.port_t << ">(std::move(_d));\n";
                    n << "        }\n";
                } else if (ff.prim_kind == "boolean") {
                    n << "        cur_" << ff.name
                      << "_ = std::make_shared<const " << ff.port_t
                      << ">(cfg.value(\"" << ff.name << "\", " << fallback << "));\n";
                } else if (ff.prim_kind == "integer") {
                    n << "        cur_" << ff.name
                      << "_ = std::make_shared<const " << ff.port_t
                      << ">(cfg.value(\"" << ff.name << "\", static_cast<"
                      << ff.port_t << ">(" << fallback << ")));\n";
                } else {  // number / double
                    n << "        cur_" << ff.name
                      << "_ = std::make_shared<const " << ff.port_t
                      << ">(cfg.value(\"" << ff.name << "\", " << fallback << ".0));\n";
                }
            }
        } else {
            n << "        (void)cfg;\n";
        }
        n << "    }\n";

        n << "    void on_start() override {\n";
        for (auto const& ff : factory_fields) {
            n << "        in_" << ff.name << "_.set_internal_sink(\n";
            n << "            [this](::flowboard::InputPort<" << ff.port_t << ">::Value _v) {\n";
            n << "                enqueue([this, _v] {\n";
            n << "                    bool _changed;\n";
            n << "                    {\n";
            n << "                        std::scoped_lock _g(cache_mu_);\n";
            // JSON-based equality so we don't depend on operator== being defined
            // for every cached type (primitive, string, or struct field).
            n << "                        _changed = !cur_" << ff.name << "_ ||\n";
            n << "                            ::flowboard::to_json_or_passthrough(*cur_" << ff.name << "_) !=\n";
            n << "                            ::flowboard::to_json_or_passthrough(*_v);\n";
            n << "                        cur_" << ff.name << "_ = _v;\n";
            n << "                    }\n";
            n << "                    triggered_.emit(std::make_shared<const bool>(true));\n";
            n << "                    if (_changed) updated_.emit(std::make_shared<const bool>(true));\n";
            n << "                    if (auto_trigger_) build_and_emit_();\n";
            n << "                });\n";
            n << "            });\n";
        }
        n << "        trigger_.set_internal_sink(\n";
        n << "            [this](::flowboard::InputPort<bool>::Value _t) {\n";
        n << "                enqueue([this, _t] {\n";
        n << "                    triggered_.emit(std::make_shared<const bool>(true));\n";
        n << "                    if (!_t || !*_t) return;\n";
        n << "                    build_and_emit_();\n";
        n << "                });\n";
        n << "            });\n";
        n << "    }\n";
        n << "\n";
        n << "    // Build the struct from the cached field values and emit it on `out`.\n";
        n << "    void build_and_emit_() {\n";
        n << "        auto _v = std::make_shared<" << struct_full << ">();\n";
        n << "        {\n";
        n << "            std::scoped_lock _g(cache_mu_);\n";
        for (auto const& ff : factory_fields) {
            if (ff.kind == "Optional") {
                // Optional<T> field is a std::vector<T>; wrap the single cached
                // element into a 1-element vector (empty stays "not provided").
                n << "                        if (cur_" << ff.name << "_) _v->" << ff.name
                  << " = decltype(_v->" << ff.name << "){*cur_" << ff.name << "_};\n";
            } else if (ff.kind == "List") {
                // List<T> field is a std::vector<T>; rebuild it from the cached
                // ListValue's JSON items (one extract_typed per element).
                n << "                        if (cur_" << ff.name << "_) {\n";
                n << "                            _v->" << ff.name << ".clear();\n";
                n << "                            for (auto const& _it : cur_" << ff.name << "_->items)\n";
                n << "                                _v->" << ff.name
                  << ".push_back(::flowboard::extract_typed<" << ff.elem_cpp << ">(_it));\n";
                n << "                        }\n";
            } else {
                // static_cast to the struct field's actual width: int64_t inputs
                // for narrower integer fields (e.g. `long` -> int32_t) and double
                // for `float` fields would otherwise emit narrowing warnings on
                // MSVC. Struct/string fields cast trivially to themselves.
                n << "                        if (cur_" << ff.name << "_) _v->" << ff.name
                  << " = static_cast<decltype(_v->" << ff.name << ")>(*cur_" << ff.name << "_);\n";
            }
        }
        n << "        }\n";
        n << "        out_.emit(std::shared_ptr<const " << struct_full
          << ">(std::move(_v)));\n";
        n << "    }\n";

        n << "private:\n";
        n << "    ::flowboard::InputPort<bool> trigger_;\n";
        n << "    ::flowboard::OutputPort<" << struct_full << "> out_;\n";
        n << "    ::flowboard::OutputPort<bool> triggered_;\n";
        n << "    ::flowboard::OutputPort<bool> updated_;\n";
        n << "    bool auto_trigger_;\n";
        for (auto const& ff : factory_fields) {
            n << "    ::flowboard::InputPort<" << ff.port_t
              << "> in_" << ff.name << "_;\n";
        }
        n << "    std::mutex cache_mu_;\n";
        for (auto const& ff : factory_fields) {
            n << "    std::shared_ptr<const " << ff.port_t
              << "> cur_" << ff.name << "_;\n";
        }
        n << "};\n";
        n << "OP_REGISTER_NODE_WITH_SCHEMA(\"" << type_name_str << "\", "
          << class_name << ", " << schema_var << ", " << defaults_var << ")\n";
    }

    n << "}  // namespace flowboard::generated\n";

    r.nodes_cpp = n.str();
    r.registry_snippet = "";  // registration is in nodes_cpp directly

    return r;
}

}  // namespace pipegen
