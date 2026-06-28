// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <map>
#include "lexer.hpp"
#include "parser.hpp"
#include "emit_cpp.hpp"

using namespace pipegen;

TEST_CASE("emit cpp_type_ref maps primitives") {
    ast::TypeRef t;
    t.value = ast::PrimitiveType{"boolean", std::nullopt};
    CHECK(cpp_type_ref(t, "M_X") == "bool");
    t.value = ast::PrimitiveType{"long", std::nullopt};
    CHECK(cpp_type_ref(t, "M_X") == "::std::int32_t");
    t.value = ast::PrimitiveType{"unsigned long", std::nullopt};
    CHECK(cpp_type_ref(t, "M_X") == "::std::uint32_t");
    t.value = ast::PrimitiveType{"double", std::nullopt};
    CHECK(cpp_type_ref(t, "M_X") == "double");
    t.value = ast::PrimitiveType{"string", std::nullopt};
    CHECK(cpp_type_ref(t, "M_X") == "::std::string");
    t.value = ast::PrimitiveType{"string", 255};
    CHECK(cpp_type_ref(t, "M_X") == "::std::string");
}

TEST_CASE("emit cpp_type_ref maps sequences") {
    auto inner = std::make_shared<ast::TypeRef>();
    inner->value = ast::PrimitiveType{"double", std::nullopt};
    ast::TypeRef seq;
    seq.value = ast::SequenceType{inner, std::nullopt};
    CHECK(cpp_type_ref(seq, "M_X") == "::std::vector<double>");
}

TEST_CASE("emit cpp_type_ref qualifies cross-module names") {
    ast::TypeRef t;
    t.value = ast::NamedType{{"M_Common", "PositionType"}};
    CHECK(cpp_type_ref(t, "M_Sample") == "::M_Common::PositionType");
}

TEST_CASE("emit_cpp_for_module produces a struct definition + OP_DECLARE_TYPE") {
    std::string src = R"(
        module M_Sample
        {
            struct Pose
            {
                double X;
                double Y;
            };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);
    CHECK(out.types_hpp.find("namespace M_Sample") != std::string::npos);
    CHECK(out.types_hpp.find("struct Pose") != std::string::npos);
    CHECK(out.types_hpp.find("double X;") != std::string::npos);
    CHECK(out.types_hpp.find("OP_DECLARE_TYPE(::M_Sample::Pose") != std::string::npos);
}

TEST_CASE("emit_cpp_for_module produces enum class for enum sub-module") {
    std::string src = R"(
        module M_Sample
        {
            module ColorKind
            {
                enum Type { Red, Green, Blue };
            };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);
    CHECK(out.types_hpp.find("namespace ColorKind") != std::string::npos);
    CHECK(out.types_hpp.find("enum class Type") != std::string::npos);
    CHECK(out.types_hpp.find("Red") != std::string::npos);
    CHECK(out.types_hpp.find("Blue") != std::string::npos);
}

TEST_CASE("emit_cpp produces node classes for interfaces") {
    std::string src = R"(
        module M_Sample
        {
            interface IClient
            {
                void ReportPosition (
                    in string KeyId,
                    in double X
                );
            };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);
    CHECK(out.nodes_cpp.find("class M_Sample_Client_Node") != std::string::npos);
    CHECK(out.nodes_cpp.find("OutputPort<::std::string>") != std::string::npos);
    CHECK(out.nodes_cpp.find("OutputPort<double>") != std::string::npos);
    CHECK(out.nodes_cpp.find("ReportPosition.KeyId") != std::string::npos);
    // OP_REGISTER_NODE_WITH_SCHEMA is emitted inside nodes_cpp (within the namespace), not registry_snippet.
    CHECK(out.nodes_cpp.find("OP_REGISTER_NODE_WITH_SCHEMA(\"M_Sample.Client\", M_Sample_Client_Node") != std::string::npos);
    CHECK(out.registry_snippet.empty());
}

TEST_CASE("emit_cpp Client nodes inherit IClient + construct SDK handle") {
    std::string src = R"(
        module M_Foo
        {
            interface IClient
            {
                void ReportSomething (in double Value);
            };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);
    // Must inherit M_Foo::IClient
    CHECK(out.nodes_cpp.find("M_Foo::IClient") != std::string::npos);
    // Must construct the SDK handle via Client::create
    CHECK(out.nodes_cpp.find("M_Foo::Client::create") != std::string::npos);
    // Must hold a unique_ptr<M_Foo::Client> handle
    CHECK(out.nodes_cpp.find("std::unique_ptr<M_Foo::Client>") != std::string::npos);
    // Must override the IClient callback method
    CHECK(out.nodes_cpp.find("void ReportSomething(double") != std::string::npos);
}

TEST_CASE("emit_cpp emits to_json per struct") {
    std::string src = R"(
        module M_Sample
        {
            struct Inner { double X; };
            struct Outer { Inner First; long N; };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);
    // to_json for both structs, inside the M_Sample namespace:
    CHECK(out.types_hpp.find("nlohmann::json to_json(") != std::string::npos);
    CHECK(out.types_hpp.find("Inner const&") != std::string::npos);
    CHECK(out.types_hpp.find("Outer const&") != std::string::npos);
    // Field assignments inside to_json:
    CHECK(out.types_hpp.find("j[\"X\"]")     != std::string::npos);
    CHECK(out.types_hpp.find("j[\"First\"]") != std::string::npos);
    CHECK(out.types_hpp.find("j[\"N\"]")     != std::string::npos);
}

TEST_CASE("emit_cpp Client nodes have input ports for IService single-param ops") {
    // Need to have BOTH IClient (so Task-16 path triggers) and IService
    std::string src = R"(
        module M_Foo
        {
            struct Cmd { double X; };

            interface IClient
            {
                void ReportSomething (in double Value);
            };

            interface IService
            {
                void DoIt (in Cmd C);
            };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);
    CHECK(out.nodes_cpp.find("InputPort<::M_Foo::Cmd> in_DoIt_C") != std::string::npos);
    CHECK(out.nodes_cpp.find("handle_->DoIt(*_v)") != std::string::npos);
}

TEST_CASE("emit_cpp Client nodes compose multi-param IService Cmd inputs") {
    // 2-param IService op: should emit typed input per param + a bool trigger,
    // a cache_mu_, per-param latched shared_ptr cache, and an on_start composer
    // that calls handle_->DoIt(*p1, *p2) when trigger=true.
    std::string src = R"(
        module M_Foo
        {
            struct A { double X; };
            struct B { double Y; };

            interface IClient
            {
                void ReportSomething (in double Value);
            };

            interface IService
            {
                void DoIt (in A a, in B b);
            };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);

    // Param input ports + trigger
    CHECK(out.nodes_cpp.find("InputPort<::M_Foo::A> in_DoIt_a{\"DoIt.a\"}")
          != std::string::npos);
    CHECK(out.nodes_cpp.find("InputPort<::M_Foo::B> in_DoIt_b{\"DoIt.b\"}")
          != std::string::npos);
    CHECK(out.nodes_cpp.find("InputPort<bool> in_DoIt_trigger{\"DoIt.trigger\"}")
          != std::string::npos);

    // Constructor registers all three
    CHECK(out.nodes_cpp.find("register_input(&in_DoIt_a)")       != std::string::npos);
    CHECK(out.nodes_cpp.find("register_input(&in_DoIt_b)")       != std::string::npos);
    CHECK(out.nodes_cpp.find("register_input(&in_DoIt_trigger)") != std::string::npos);

    // Cache members + mutex
    CHECK(out.nodes_cpp.find("std::mutex cache_mu_;") != std::string::npos);
    CHECK(out.nodes_cpp.find("std::shared_ptr<const ::M_Foo::A> cur_DoIt_a_;")
          != std::string::npos);
    CHECK(out.nodes_cpp.find("std::shared_ptr<const ::M_Foo::B> cur_DoIt_b_;")
          != std::string::npos);

    // Composer call (appears inside the requires-guarded branch)
    CHECK(out.nodes_cpp.find("handle_->DoIt(_p_a, _p_b)") != std::string::npos);
    // SFINAE guard so the call compiles even when the stub SDK lacks the method
    CHECK(out.nodes_cpp.find("if constexpr (requires { handle_->DoIt(_p_a, _p_b); })")
          != std::string::npos);
    // Trigger sink early-return on falsy
    CHECK(out.nodes_cpp.find("if (!_t || !*_t) return;") != std::string::npos);
    // No leftover Phase 2 marker
    CHECK(out.nodes_cpp.find("input wiring deferred") == std::string::npos);
}

TEST_CASE("emit_cpp emits OP_REGISTER_NODE_WITH_SCHEMA with shared OnboardAPI schema") {
    std::string src = R"(
        module M_Foo
        {
            interface IClient { void ReportSomething (in double V); };
            interface IService { void DoIt (in double X); };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_cpp_for_module(*file.modules[0]);
    // Two registrations: Service + Client. Both use the schema'd macro.
    CHECK(out.nodes_cpp.find("OP_REGISTER_NODE_WITH_SCHEMA(\"M_Foo.Service\"") != std::string::npos);
    CHECK(out.nodes_cpp.find("OP_REGISTER_NODE_WITH_SCHEMA(\"M_Foo.Client\"")  != std::string::npos);
    // Schema mentions domainId + serviceName.
    CHECK(out.nodes_cpp.find("\"domainId\"")    != std::string::npos);
    CHECK(out.nodes_cpp.find("\"serviceName\"") != std::string::npos);
}

TEST_CASE("resolve_typedef walks chain to a non-typedef TypeRef") {
    // typedef sequence<double, 1> AngleDegreesOptionalType;
    auto seq_type = std::make_shared<pipegen::ast::TypeRef>();
    seq_type->value = pipegen::ast::SequenceType{
        std::make_shared<pipegen::ast::TypeRef>(),
        1
    };
    std::get<pipegen::ast::SequenceType>(seq_type->value).element->value =
        pipegen::ast::PrimitiveType{"double", std::nullopt};

    std::map<std::string, pipegen::ast::TypeRef> typedefs = {
        {"M_Common::AngleDegreesOptionalType", *seq_type},
    };

    pipegen::ast::TypeRef ref;
    ref.value = pipegen::ast::NamedType{{"M_Common", "AngleDegreesOptionalType"}};

    auto resolved = pipegen::resolve_typedef(ref, typedefs, "M_Mount");
    auto* s = std::get_if<pipegen::ast::SequenceType>(&resolved.value);
    REQUIRE(s != nullptr);
    CHECK(s->max_size.has_value());
    CHECK(*s->max_size == 1);
}

TEST_CASE("resolve_typedef bottoms out on a primitive without a typedef entry") {
    pipegen::ast::TypeRef ref;
    ref.value = pipegen::ast::PrimitiveType{"double", std::nullopt};

    std::map<std::string, pipegen::ast::TypeRef> typedefs;
    auto resolved = pipegen::resolve_typedef(ref, typedefs, "M_Mount");
    auto* p = std::get_if<pipegen::ast::PrimitiveType>(&resolved.value);
    REQUIRE(p != nullptr);
    CHECK(p->name == "double");
}

TEST_CASE("resolve_typedef leaves unresolved NamedType alone (struct/enum reference)") {
    pipegen::ast::TypeRef ref;
    ref.value = pipegen::ast::NamedType{{"M_Common", "PositionType"}};

    std::map<std::string, pipegen::ast::TypeRef> typedefs;
    auto resolved = pipegen::resolve_typedef(ref, typedefs, "M_Mount");
    auto* n = std::get_if<pipegen::ast::NamedType>(&resolved.value);
    REQUIRE(n != nullptr);
    CHECK(n->qualified_name == std::vector<std::string>{"M_Common", "PositionType"});
}

TEST_CASE("emit_cpp_for_module emits from_json for each struct") {
    pipegen::ast::Module mod;
    mod.name = "M_Demo";
    pipegen::ast::StructDecl s;
    s.name = "PointType";
    pipegen::ast::StructField f1;
    f1.type = std::make_shared<pipegen::ast::TypeRef>();
    f1.type->value = pipegen::ast::PrimitiveType{"double", std::nullopt};
    f1.name = "x";
    s.fields.push_back(f1);
    mod.structs.push_back(s);

    auto r = pipegen::emit_cpp_for_module(mod);
    CHECK(r.types_hpp.find("inline void from_json(::nlohmann::json const& j, PointType& v)")
          != std::string::npos);
    CHECK(r.types_hpp.find("j.at(\"x\").get_to(v.x);") != std::string::npos);
}

TEST_CASE("emit_cpp_for_module emits from_json for each enum") {
    pipegen::ast::Module mod;
    mod.name = "M_Demo";
    auto sub = std::make_shared<pipegen::ast::Module>();
    sub->name = "ModeCommand";
    pipegen::ast::EnumDecl e;
    e.name = "Type";
    e.values = {"Off", "On"};
    sub->enums.push_back(e);
    mod.submodules.push_back(sub);

    auto r = pipegen::emit_cpp_for_module(mod);
    CHECK(r.types_hpp.find("inline void from_json(::nlohmann::json const& j, Type& v)")
          != std::string::npos);
    CHECK(r.types_hpp.find("v = static_cast<Type>(j.get<std::underlying_type_t<Type>>());")
          != std::string::npos);
}

TEST_CASE("emit_cpp_for_module emits FieldDescriptor[] for each struct") {
    // typedef sequence<double, 1> AngleDegreesOptionalType in M_Common
    // typedef sequence<PropertyType> PropertyListType in M_Common
    // struct MountPositionType { AngleDegreesOptionalType Elevation;
    //                            PositionType Pos;
    //                            PropertyListType Extras; } in M_Mount

    pipegen::ast::Module m_common;
    m_common.name = "M_Common";

    pipegen::ast::TypedefDecl td_opt;
    td_opt.name = "AngleDegreesOptionalType";
    td_opt.aliased = std::make_shared<pipegen::ast::TypeRef>();
    td_opt.aliased->value = pipegen::ast::SequenceType{
        std::make_shared<pipegen::ast::TypeRef>(),
        1
    };
    std::get<pipegen::ast::SequenceType>(td_opt.aliased->value).element->value =
        pipegen::ast::PrimitiveType{"double", std::nullopt};
    m_common.typedefs.push_back(td_opt);

    pipegen::ast::TypedefDecl td_list;
    td_list.name = "PropertyListType";
    td_list.aliased = std::make_shared<pipegen::ast::TypeRef>();
    td_list.aliased->value = pipegen::ast::SequenceType{
        std::make_shared<pipegen::ast::TypeRef>(),
        std::nullopt
    };
    std::get<pipegen::ast::SequenceType>(td_list.aliased->value).element->value =
        pipegen::ast::NamedType{{"PropertyType"}};
    m_common.typedefs.push_back(td_list);

    pipegen::ast::Module m_mount;
    m_mount.name = "M_Mount";
    pipegen::ast::StructDecl s;
    s.name = "MountPositionType";
    auto add_field = [&](std::string name,
                         std::variant<pipegen::ast::PrimitiveType,
                                      pipegen::ast::NamedType,
                                      pipegen::ast::SequenceType> v) {
        pipegen::ast::StructField f;
        f.name = std::move(name);
        f.type = std::make_shared<pipegen::ast::TypeRef>();
        f.type->value = std::move(v);
        s.fields.push_back(f);
    };
    add_field("Elevation",
              pipegen::ast::NamedType{{"M_Common", "AngleDegreesOptionalType"}});
    add_field("Pos",
              pipegen::ast::NamedType{{"M_Common", "PositionType"}});
    add_field("Extras",
              pipegen::ast::NamedType{{"M_Common", "PropertyListType"}});
    m_mount.structs.push_back(s);

    std::map<std::string, pipegen::ast::TypeRef> typedefs = {
        {"M_Common::AngleDegreesOptionalType", *td_opt.aliased},
        {"M_Common::PropertyListType",         *td_list.aliased},
    };
    std::set<std::string> known = {"M_Common", "M_Mount"};

    auto r = pipegen::emit_cpp_for_module(m_mount, known, typedefs);
    CHECK(r.types_hpp.find("MountPositionType_fields[]") != std::string::npos);
    CHECK(r.types_hpp.find(R"({ "Elevation", ::flowboard::FieldKind::Optional, "flowboard::Double" })")
          != std::string::npos);
    CHECK(r.types_hpp.find(R"({ "Pos", ::flowboard::FieldKind::Scalar, "M_Common::PositionType" })")
          != std::string::npos);
    CHECK(r.types_hpp.find(R"({ "Extras", ::flowboard::FieldKind::List, "M_Common::PropertyType" })")
          != std::string::npos);
}

TEST_CASE("emit_cpp_for_module emits per-struct extract factory + registration") {
    pipegen::ast::Module mod;
    mod.name = "M_Mount";
    pipegen::ast::StructDecl s;
    s.name = "MountPositionType";
    auto add = [&](std::string name,
                   std::variant<pipegen::ast::PrimitiveType,
                                pipegen::ast::NamedType,
                                pipegen::ast::SequenceType> v) {
        pipegen::ast::StructField f;
        f.name = std::move(name);
        f.type = std::make_shared<pipegen::ast::TypeRef>();
        f.type->value = std::move(v);
        s.fields.push_back(f);
    };
    add("Elevation",
        pipegen::ast::NamedType{{"M_Common", "AngleDegreesOptionalType"}});
    add("Pos",
        pipegen::ast::NamedType{{"M_Common", "PositionType"}});
    mod.structs.push_back(s);

    pipegen::ast::TypeRef opt_alias;
    opt_alias.value = pipegen::ast::SequenceType{
        std::make_shared<pipegen::ast::TypeRef>(), 1
    };
    std::get<pipegen::ast::SequenceType>(opt_alias.value).element->value =
        pipegen::ast::PrimitiveType{"double", std::nullopt};

    std::map<std::string, pipegen::ast::TypeRef> typedefs = {
        {"M_Common::AngleDegreesOptionalType", opt_alias},
    };
    std::set<std::string> known = {"M_Common", "M_Mount"};

    auto r = pipegen::emit_cpp_for_module(mod, known, typedefs);

    CHECK(r.nodes_cpp.find("#include \"flowboard/extract_nodes.hpp\"") != std::string::npos);
    CHECK(r.nodes_cpp.find("#include \"flowboard/extract_registry.hpp\"") != std::string::npos);
    CHECK(r.nodes_cpp.find("make_extract_M_Mount__MountPositionType") != std::string::npos);
    CHECK(r.nodes_cpp.find("ExtractOptional<::M_Mount::MountPositionType, double>") != std::string::npos);
    CHECK(r.nodes_cpp.find("ExtractScalar<::M_Mount::MountPositionType, ::M_Common::PositionType>")
          != std::string::npos);
    CHECK(r.nodes_cpp.find(R"(OP_REGISTER_EXTRACT_FACTORY("M_Mount::MountPositionType", &make_extract_M_Mount__MountPositionType))")
          != std::string::npos);
}

TEST_CASE("emit_cpp_for_module emits per-struct Factory node + registration") {
    pipegen::ast::Module mod;
    mod.name = "M_Alert";
    pipegen::ast::StructDecl s;
    s.name = "AlarmConditionInfoType";
    auto add = [&](std::string name,
                   std::variant<pipegen::ast::PrimitiveType,
                                pipegen::ast::NamedType,
                                pipegen::ast::SequenceType> v) {
        pipegen::ast::StructField f;
        f.name = std::move(name);
        f.type = std::make_shared<pipegen::ast::TypeRef>();
        f.type->value = std::move(v);
        s.fields.push_back(f);
    };
    add("SubsystemName",    pipegen::ast::PrimitiveType{"string",  std::nullopt});
    add("ComponentName",    pipegen::ast::PrimitiveType{"string",  std::nullopt});
    add("Measure",          pipegen::ast::PrimitiveType{"string",  std::nullopt});
    add("Nature",           pipegen::ast::PrimitiveType{"string",  std::nullopt});
    add("AlarmDescription", pipegen::ast::PrimitiveType{"string",  std::nullopt});
    mod.structs.push_back(s);

    std::set<std::string> known = {"M_Alert"};
    std::map<std::string, pipegen::ast::TypeRef> typedefs;

    auto r = pipegen::emit_cpp_for_module(mod, known, typedefs);

    CHECK(r.nodes_cpp.find("#include <mutex>") != std::string::npos);
    CHECK(r.nodes_cpp.find("kFactorySchema") != std::string::npos);
    CHECK(r.nodes_cpp.find("class Factory_M_Alert_AlarmConditionInfoType")
          != std::string::npos);
    CHECK(r.nodes_cpp.find("\"Factory.M_Alert::AlarmConditionInfoType\"")
          != std::string::npos);
    // Trigger + out + per-field input registrations
    CHECK(r.nodes_cpp.find("trigger_(\"trigger\")") != std::string::npos);
    CHECK(r.nodes_cpp.find("out_(\"out\")")        != std::string::npos);
    CHECK(r.nodes_cpp.find("in_SubsystemName_(\"SubsystemName\")")
          != std::string::npos);
    CHECK(r.nodes_cpp.find("::flowboard::OutputPort<::M_Alert::AlarmConditionInfoType> out_;")
          != std::string::npos);
    CHECK(r.nodes_cpp.find("::flowboard::InputPort<::std::string> in_SubsystemName_;")
          != std::string::npos);
    // Cache + composition
    CHECK(r.nodes_cpp.find("std::mutex cache_mu_;") != std::string::npos);
    CHECK(r.nodes_cpp.find("if (cur_AlarmDescription_) _v->AlarmDescription = static_cast<decltype(_v->AlarmDescription)>(*cur_AlarmDescription_);")
          != std::string::npos);
    // Registration macro — schema/defaults vars are per-type-namespaced.
    CHECK(r.nodes_cpp.find(R"(OP_REGISTER_NODE_WITH_SCHEMA("Factory.M_Alert::AlarmConditionInfoType", Factory_M_Alert_AlarmConditionInfoType, kSchema_Factory_M_Alert_AlarmConditionInfoType, kDefaults_Factory_M_Alert_AlarmConditionInfoType))")
          != std::string::npos);
}

TEST_CASE("emit_cpp_for_module Factory emits element-typed ports for Optional fields") {
    pipegen::ast::Module mod;
    mod.name = "M_Mount";
    pipegen::ast::StructDecl s;
    s.name = "MountPositionType";
    auto add = [&](std::string name,
                   std::variant<pipegen::ast::PrimitiveType,
                                pipegen::ast::NamedType,
                                pipegen::ast::SequenceType> v) {
        pipegen::ast::StructField f;
        f.name = std::move(name);
        f.type = std::make_shared<pipegen::ast::TypeRef>();
        f.type->value = std::move(v);
        s.fields.push_back(f);
    };
    // Optional double via typedef alias — should be skipped
    add("Elevation", pipegen::ast::NamedType{{"M_Common", "AngleDegreesOptionalType"}});
    // Scalar struct — should produce an input
    add("Pos",       pipegen::ast::NamedType{{"M_Common", "PositionType"}});
    mod.structs.push_back(s);

    pipegen::ast::TypeRef opt_alias;
    opt_alias.value = pipegen::ast::SequenceType{
        std::make_shared<pipegen::ast::TypeRef>(), 1
    };
    std::get<pipegen::ast::SequenceType>(opt_alias.value).element->value =
        pipegen::ast::PrimitiveType{"double", std::nullopt};
    std::map<std::string, pipegen::ast::TypeRef> typedefs = {
        {"M_Common::AngleDegreesOptionalType", opt_alias},
    };
    std::set<std::string> known = {"M_Common", "M_Mount"};

    auto r = pipegen::emit_cpp_for_module(mod, known, typedefs);

    CHECK(r.nodes_cpp.find("class Factory_M_Mount_MountPositionType")
          != std::string::npos);
    CHECK(r.nodes_cpp.find("in_Pos_(\"Pos\")") != std::string::npos);
    CHECK(r.nodes_cpp.find("::flowboard::InputPort<::M_Common::PositionType> in_Pos_;")
          != std::string::npos);
    // Optional fields are now supported: an Optional<T> (sequence<T,1>) field
    // becomes an input port of the element type T — emitted, not skipped.
    CHECK(r.nodes_cpp.find("in_Elevation_") != std::string::npos);
    CHECK(r.nodes_cpp.find("// skipped factory input 'Elevation'") == std::string::npos);
}

TEST_CASE("emit_cpp_for_module synthesizes EventButton_Args struct for multi-param IClient op") {
    // Build M_HidJoystick with IClient::EventButton(unsigned long, string, boolean)
    // The emitter should synthesise an EventButton_Args struct with those three fields
    // and emit it through the normal struct pipeline (struct def, to_json, from_json,
    // OP_DECLARE_TYPE with the expected tag, FieldDescriptor[]).
    pipegen::ast::Module mod;
    mod.name = "M_HidJoystick";

    auto make_prim_field = [](std::string name, std::string prim_name) -> pipegen::ast::OpParam {
        pipegen::ast::OpParam p;
        p.name = std::move(name);
        p.direction = "in";
        p.type = std::make_shared<pipegen::ast::TypeRef>();
        p.type->value = pipegen::ast::PrimitiveType{std::move(prim_name), std::nullopt};
        return p;
    };

    pipegen::ast::InterfaceDecl iclient;
    iclient.name = "IClient";
    pipegen::ast::Operation op;
    op.name = "EventButton";
    op.params.push_back(make_prim_field("ButtonIndex", "unsigned long"));
    op.params.push_back(make_prim_field("ButtonName",  "string"));
    op.params.push_back(make_prim_field("IsSelected",  "boolean"));
    iclient.operations.push_back(op);
    mod.interfaces.push_back(iclient);

    auto r = pipegen::emit_cpp_for_module(mod);

    // Struct definition in types_hpp
    CHECK(r.types_hpp.find("struct EventButton_Args {") != std::string::npos);
    CHECK(r.types_hpp.find("::std::uint32_t ButtonIndex;") != std::string::npos);
    CHECK(r.types_hpp.find("::std::string ButtonName;") != std::string::npos);
    CHECK(r.types_hpp.find("bool IsSelected;") != std::string::npos);

    // OP_DECLARE_TYPE with correct tag
    CHECK(r.types_hpp.find("\"M_HidJoystick::EventButton_Args\"") != std::string::npos);
}

TEST_CASE("emit_cpp_for_module Factory schema uses valid JSON Schema types") {
    pipegen::ast::Module mod;
    mod.name = "M_Mount";
    pipegen::ast::StructDecl s;
    s.name = "ScaledRateType";
    auto add = [&](std::string name,
                   std::variant<pipegen::ast::PrimitiveType,
                                pipegen::ast::NamedType,
                                pipegen::ast::SequenceType> v) {
        pipegen::ast::StructField f;
        f.name = std::move(name);
        f.type = std::make_shared<pipegen::ast::TypeRef>();
        f.type->value = std::move(v);
        s.fields.push_back(f);
    };
    add("AzimuthScaledRate",   pipegen::ast::PrimitiveType{"double", std::nullopt});
    add("ElevationScaledRate", pipegen::ast::PrimitiveType{"double", std::nullopt});
    mod.structs.push_back(s);

    std::set<std::string> known = {"M_Mount"};
    std::map<std::string, pipegen::ast::TypeRef> typedefs;

    auto r = pipegen::emit_cpp_for_module(mod, known, typedefs);

    // float/double fields must surface as JSON Schema "number", never the raw
    // C++/IDL "double" — RJSF rejects "double" as an unknown field type.
    CHECK(r.nodes_cpp.find("\"AzimuthScaledRate\":{\"type\":\"number\"")
          != std::string::npos);
    CHECK(r.nodes_cpp.find("\"type\":\"double\"") == std::string::npos);
}

TEST_CASE("emit_cpp_for_module emits synthetic struct def in real-sdk mode") {
    // Build M_HidJoystick with IClient::EventButton(unsigned long, string, boolean)
    // and a normal (non-synthetic) struct Foo — then emit with target_real_sdk=true.
    //
    // Expectations:
    //  - The synthetic EventButton_Args struct definition IS emitted (it's ours, not
    //    in any SDK header, so the real-sdk guard must NOT suppress it).
    //  - to_json / from_json ARE emitted (they iterate mmod.structs unconditionally).
    //  - The real struct Foo definition is NOT emitted (still correctly suppressed
    //    under real-sdk, because it comes from the SDK type header).
    pipegen::ast::Module mod;
    mod.name = "M_HidJoystick";

    // Add a normal (non-synthetic) struct — must stay suppressed in real-sdk mode.
    {
        pipegen::ast::StructDecl foo;
        foo.name = "Foo";
        foo.synthetic = false;
        foo.source_order = 1;
        pipegen::ast::StructField f;
        f.name = "X";
        f.type = std::make_shared<pipegen::ast::TypeRef>();
        f.type->value = pipegen::ast::PrimitiveType{"double", std::nullopt};
        foo.fields.push_back(f);
        mod.structs.push_back(foo);
    }

    auto make_prim_param = [](std::string name, std::string prim) -> pipegen::ast::OpParam {
        pipegen::ast::OpParam p;
        p.name = std::move(name);
        p.direction = "in";
        p.type = std::make_shared<pipegen::ast::TypeRef>();
        p.type->value = pipegen::ast::PrimitiveType{std::move(prim), std::nullopt};
        return p;
    };

    pipegen::ast::InterfaceDecl iclient;
    iclient.name = "IClient";
    pipegen::ast::Operation op;
    op.name = "EventButton";
    op.params.push_back(make_prim_param("ButtonIndex", "unsigned long"));
    op.params.push_back(make_prim_param("ButtonName",  "string"));
    op.params.push_back(make_prim_param("IsSelected",  "boolean"));
    iclient.operations.push_back(op);
    mod.interfaces.push_back(iclient);

    // Emit with target_real_sdk = true
    auto r = pipegen::emit_cpp_for_module(mod, {}, {}, /*target_real_sdk=*/true);

    // Synthetic struct definition MUST be present even in real-sdk mode.
    CHECK(r.types_hpp.find("struct EventButton_Args {") != std::string::npos);
    CHECK(r.types_hpp.find("::std::uint32_t ButtonIndex;") != std::string::npos);
    CHECK(r.types_hpp.find("::std::string ButtonName;") != std::string::npos);
    CHECK(r.types_hpp.find("bool IsSelected;") != std::string::npos);

    // to_json / from_json for the synthetic struct (loops are already unconditional,
    // but confirm the specific function signatures are present).
    CHECK(r.types_hpp.find("to_json(EventButton_Args const& v)") != std::string::npos);
    CHECK(r.types_hpp.find("from_json(::nlohmann::json const& j, EventButton_Args& v)") != std::string::npos);

    // OP_DECLARE_TYPE still present (unconditional).
    CHECK(r.types_hpp.find("\"M_HidJoystick::EventButton_Args\"") != std::string::npos);

    // Contrast: real (non-synthetic) struct Foo definition must NOT be emitted.
    CHECK(r.types_hpp.find("struct Foo {") == std::string::npos);
}

TEST_CASE("emit_cpp_for_module Args struct includes Optional and List params (matches port set)") {
    // Regression test: before the fix, make_args_struct used empty context sets
    // and so Optional/List params (resolved via typedef) were excluded from the
    // _Args struct even though they get scalar input ports.
    // After the fix, is_portable() is the single source of truth for both.
    //
    // Setup: M_Sensor::IClient::EventScan(double Rate, AngleOptType Angle)
    //   where AngleOptType = sequence<double,1>  (Optional port)
    //   and   IClient::EventBatch(double Rate, ValListType Vals)
    //   where ValListType = sequence<double>      (List port)
    //
    // IClient ops on the Service node:
    //   - Each port gets a scalar input on the Service node (is_portable = true).
    //   - The _Args struct must contain exactly those params.
    // We also add IService with a dummy op so both node classes are emitted.

    // --- Build the typedef for AngleOptType (sequence<double,1>) ---
    pipegen::ast::TypeRef dbl_ref;
    dbl_ref.value = pipegen::ast::PrimitiveType{"double", std::nullopt};

    pipegen::ast::TypeRef opt_alias;
    {
        auto elem = std::make_shared<pipegen::ast::TypeRef>(dbl_ref);
        opt_alias.value = pipegen::ast::SequenceType{elem, /*max_size=*/1u};
    }

    // --- Build the typedef for ValListType (sequence<double>) ---
    pipegen::ast::TypeRef list_alias;
    {
        auto elem = std::make_shared<pipegen::ast::TypeRef>(dbl_ref);
        list_alias.value = pipegen::ast::SequenceType{elem, /*max_size=*/std::nullopt};
    }

    // Helper: make an OpParam with a NamedType reference
    auto named_param = [](std::string pname, std::string mod_name, std::string type_name,
                          std::string dir = "in") -> pipegen::ast::OpParam {
        pipegen::ast::OpParam p;
        p.name = std::move(pname);
        p.direction = std::move(dir);
        p.type = std::make_shared<pipegen::ast::TypeRef>();
        p.type->value = pipegen::ast::NamedType{{std::move(mod_name), std::move(type_name)}};
        return p;
    };
    auto prim_param = [](std::string pname, std::string prim) -> pipegen::ast::OpParam {
        pipegen::ast::OpParam p;
        p.name = std::move(pname);
        p.direction = "in";
        p.type = std::make_shared<pipegen::ast::TypeRef>();
        p.type->value = pipegen::ast::PrimitiveType{std::move(prim), std::nullopt};
        return p;
    };

    // Build the module
    pipegen::ast::Module mod;
    mod.name = "M_Sensor";

    // Add AngleOptType and ValListType as local typedefs so they resolve
    {
        pipegen::ast::TypedefDecl td;
        td.name = "AngleOptType";
        td.source_order = 1;
        td.aliased = std::make_shared<pipegen::ast::TypeRef>(opt_alias);
        mod.typedefs.push_back(td);
    }
    {
        pipegen::ast::TypedefDecl td;
        td.name = "ValListType";
        td.source_order = 2;
        td.aliased = std::make_shared<pipegen::ast::TypeRef>(list_alias);
        mod.typedefs.push_back(td);
    }

    // IClient with two multi-param ops (the SERVICE node registers INPUTs for these)
    pipegen::ast::InterfaceDecl iclient;
    iclient.name = "IClient";
    // EventScan(double Rate, AngleOptType Angle)
    {
        pipegen::ast::Operation op;
        op.name = "EventScan";
        op.params.push_back(prim_param("Rate", "double"));
        op.params.push_back(named_param("Angle", "M_Sensor", "AngleOptType"));
        iclient.operations.push_back(op);
    }
    // EventBatch(double Rate, ValListType Vals)
    {
        pipegen::ast::Operation op;
        op.name = "EventBatch";
        op.params.push_back(prim_param("Rate", "double"));
        op.params.push_back(named_param("Vals", "M_Sensor", "ValListType"));
        iclient.operations.push_back(op);
    }
    mod.interfaces.push_back(iclient);

    // IService with one dummy op so a Service node class is also emitted.
    pipegen::ast::InterfaceDecl isvc;
    isvc.name = "IService";
    {
        pipegen::ast::Operation op;
        op.name = "Ping";
        op.params.push_back(prim_param("Id", "long"));
        isvc.operations.push_back(op);
    }
    mod.interfaces.push_back(isvc);

    // all_typedefs so the resolver can see the local typedef aliases
    std::map<std::string, pipegen::ast::TypeRef> all_typedefs = {
        {"M_Sensor::AngleOptType", opt_alias},
        {"M_Sensor::ValListType",  list_alias},
    };
    std::set<std::string> known = {"M_Sensor"};

    auto r = pipegen::emit_cpp_for_module(mod, known, all_typedefs);

    // EventScan_Args must contain BOTH Rate (scalar) AND Angle (Optional port).
    // Before the fix, Angle was excluded (is_safe_type with empty context rejected
    // the typedef); after the fix is_portable() resolves the chain correctly.
    CHECK(r.types_hpp.find("struct EventScan_Args {") != std::string::npos);
    // Rate: primitive double — emitted as "double Rate;"
    CHECK(r.types_hpp.find("    double Rate;") != std::string::npos);
    // Angle: NamedType{M_Sensor, AngleOptType} — cross-module reference emits as
    // "::M_Sensor::AngleOptType" (M_Sensor is not a local submodule of M_Sensor)
    CHECK(r.types_hpp.find("    ::M_Sensor::AngleOptType Angle;") != std::string::npos);

    // EventBatch_Args must contain BOTH Rate AND Vals (List port).
    CHECK(r.types_hpp.find("struct EventBatch_Args {") != std::string::npos);
    // Vals: NamedType{M_Sensor, ValListType} — same cross-module rule
    CHECK(r.types_hpp.find("    ::M_Sensor::ValListType Vals;") != std::string::npos);

    // Both must have OP_DECLARE_TYPE entries
    CHECK(r.types_hpp.find("\"M_Sensor::EventScan_Args\"")  != std::string::npos);
    CHECK(r.types_hpp.find("\"M_Sensor::EventBatch_Args\"") != std::string::npos);

    // The Service node registers INPUTS for multi-param IClient ops (it composes
    // events to send to connected clients). Both Rate AND Angle must be registered.
    CHECK(r.nodes_cpp.find("register_input(&in_EventScan_Rate)") != std::string::npos);
    CHECK(r.nodes_cpp.find("register_input(&in_EventScan_Angle)") != std::string::npos);
    CHECK(r.nodes_cpp.find("register_input(&in_EventBatch_Rate)") != std::string::npos);
    CHECK(r.nodes_cpp.find("register_input(&in_EventBatch_Vals)") != std::string::npos);
}

TEST_CASE("emit_cpp_for_module emits atomic args port for multi-param IClient op (Service node)") {
    // M_HidJoystick: IClient::EventButton(unsigned long ButtonIndex, string ButtonName, boolean IsSelected)
    // This is a multi-param op with 3 portable params, so:
    //   - The Service node (which sends IClient ops) should get an args port in addition to scalar ports.
    //   - Member declaration: InputPort<::M_HidJoystick::EventButton_Args> in_EventButton_args{"EventButton.args"};
    //   - Constructor: register_input(&in_EventButton_args);
    //   - The existing scalar ports and trigger must still be present (supplement, not replace).
    pipegen::ast::Module mod;
    mod.name = "M_HidJoystick";

    auto make_prim_param = [](std::string name, std::string prim) -> pipegen::ast::OpParam {
        pipegen::ast::OpParam p;
        p.name = std::move(name);
        p.direction = "in";
        p.type = std::make_shared<pipegen::ast::TypeRef>();
        p.type->value = pipegen::ast::PrimitiveType{std::move(prim), std::nullopt};
        return p;
    };

    // IClient has the multi-param EventButton op
    pipegen::ast::InterfaceDecl iclient;
    iclient.name = "IClient";
    pipegen::ast::Operation op;
    op.name = "EventButton";
    op.params.push_back(make_prim_param("ButtonIndex", "unsigned long"));
    op.params.push_back(make_prim_param("ButtonName",  "string"));
    op.params.push_back(make_prim_param("IsSelected",  "boolean"));
    iclient.operations.push_back(op);
    mod.interfaces.push_back(iclient);

    // IService with a dummy single-param op so both node classes are emitted
    pipegen::ast::InterfaceDecl isvc;
    isvc.name = "IService";
    pipegen::ast::Operation ping;
    ping.name = "Ping";
    ping.params.push_back(make_prim_param("Id", "long"));
    isvc.operations.push_back(ping);
    mod.interfaces.push_back(isvc);

    auto r = pipegen::emit_cpp_for_module(mod);

    // --- Service node: registers args port for multi-param IClient op ---

    // Member declaration: the args port alongside the scalar ports
    CHECK(r.nodes_cpp.find(
        "::flowboard::InputPort<::M_HidJoystick::EventButton_Args> in_EventButton_args{\"EventButton.args\"}")
          != std::string::npos);

    // Constructor: args port registered
    CHECK(r.nodes_cpp.find("register_input(&in_EventButton_args)") != std::string::npos);

    // Supplement check: existing scalar ports and trigger must still be present
    CHECK(r.nodes_cpp.find("in_EventButton_ButtonIndex") != std::string::npos);
    CHECK(r.nodes_cpp.find("in_EventButton_trigger")    != std::string::npos);

    // --- Task 5: atomic args sink ---
    // The args sink must be set on in_EventButton_args in on_start().
    CHECK(r.nodes_cpp.find("in_EventButton_args.set_internal_sink(") != std::string::npos);
    // The sink lambda receives a shared_ptr<const M_HidJoystick::EventButton_Args> (_v)
    // and reads each field from it.
    CHECK(r.nodes_cpp.find("_v->ButtonIndex") != std::string::npos);
    CHECK(r.nodes_cpp.find("_v->ButtonName")  != std::string::npos);
    CHECK(r.nodes_cpp.find("_v->IsSelected")  != std::string::npos);
    // The sink must invoke handle_->EventButton(...) (the SDK op, atomically).
    CHECK(r.nodes_cpp.find("->EventButton(")  != std::string::npos);
}

TEST_CASE("emit_cpp_for_module emits atomic args OUTPUT port for multi-param received op (Client node)") {
    // M_HidJoystick: IClient::EventButton(unsigned long ButtonIndex, string ButtonName, boolean IsSelected)
    // The CLIENT node implements IClient (receive side). For a multi-param op it should emit:
    //   - A scalar OutputPort per portable param (existing, must remain)
    //   - ONE extra atomic OutputPort<::M_HidJoystick::EventButton_Args> out_EventButton_args{"EventButton.args"}
    //   - register_output(&out_EventButton_args) in the constructor
    //   - In the override body: build an EventButton_Args struct and emit it on out_EventButton_args
    pipegen::ast::Module mod;
    mod.name = "M_HidJoystick";

    auto make_prim_param = [](std::string name, std::string prim) -> pipegen::ast::OpParam {
        pipegen::ast::OpParam p;
        p.name = std::move(name);
        p.direction = "in";
        p.type = std::make_shared<pipegen::ast::TypeRef>();
        p.type->value = pipegen::ast::PrimitiveType{std::move(prim), std::nullopt};
        return p;
    };

    // IClient has the multi-param EventButton op
    pipegen::ast::InterfaceDecl iclient;
    iclient.name = "IClient";
    pipegen::ast::Operation op;
    op.name = "EventButton";
    op.params.push_back(make_prim_param("ButtonIndex", "unsigned long"));
    op.params.push_back(make_prim_param("ButtonName",  "string"));
    op.params.push_back(make_prim_param("IsSelected",  "boolean"));
    iclient.operations.push_back(op);
    mod.interfaces.push_back(iclient);

    // IService with a dummy single-param op so both node classes are emitted
    pipegen::ast::InterfaceDecl isvc;
    isvc.name = "IService";
    pipegen::ast::Operation ping;
    ping.name = "Ping";
    ping.params.push_back(make_prim_param("Id", "long"));
    isvc.operations.push_back(ping);
    mod.interfaces.push_back(isvc);

    auto r = pipegen::emit_cpp_for_module(mod);

    // --- Client node: atomic struct OUTPUT for multi-param received IClient op ---

    // 1. Member declaration must be present
    CHECK(r.nodes_cpp.find(
        "::flowboard::OutputPort<::M_HidJoystick::EventButton_Args> out_EventButton_args{\"EventButton.args\"}")
          != std::string::npos);

    // 2. Registration must be present
    CHECK(r.nodes_cpp.find("register_output(&out_EventButton_args)") != std::string::npos);

    // 3. Override body: struct is built and emitted
    CHECK(r.nodes_cpp.find("out_EventButton_args.emit(") != std::string::npos);
    CHECK(r.nodes_cpp.find("_args->ButtonIndex = ButtonIndex;") != std::string::npos);
    CHECK(r.nodes_cpp.find("_args->ButtonName = ButtonName;")   != std::string::npos);
    CHECK(r.nodes_cpp.find("_args->IsSelected = IsSelected;")   != std::string::npos);

    // 4. Supplement: existing scalar outputs must still be present
    CHECK(r.nodes_cpp.find("out_EventButton_ButtonIndex") != std::string::npos);
    CHECK(r.nodes_cpp.find("out_EventButton_ButtonName")  != std::string::npos);
    CHECK(r.nodes_cpp.find("out_EventButton_IsSelected")  != std::string::npos);
}
