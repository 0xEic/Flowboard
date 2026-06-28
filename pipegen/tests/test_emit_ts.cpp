// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "lexer.hpp"
#include "parser.hpp"
#include "emit_ts.hpp"

using namespace pipegen;

TEST_CASE("emit_ts emits a TS module exporting types") {
    std::string src = R"(
        module M_Sample
        {
            struct Pose { double X; };
            interface IClient { void ReportPos (in Pose P); };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_ts_module(*file.modules[0]);
    CHECK(out.find("export const M_Sample_types") != std::string::npos);
    CHECK(out.find("\"Pose\"") != std::string::npos);
    CHECK(out.find("\"M_Sample::Pose\"") != std::string::npos);
    CHECK(out.find("as const") != std::string::npos);
}

TEST_CASE("emit_ts_module emits FIELD_DESCRIPTORS_<module>") {
    pipegen::ast::Module mod;
    mod.name = "M_Demo";
    pipegen::ast::StructDecl s;
    s.name = "PointType";
    pipegen::ast::StructField f;
    f.type = std::make_shared<pipegen::ast::TypeRef>();
    f.type->value = pipegen::ast::PrimitiveType{"double", std::nullopt};
    f.name = "x";
    s.fields.push_back(f);
    mod.structs.push_back(s);

    std::map<std::string, pipegen::ast::TypeRef> typedefs;
    std::set<std::string> known = {"M_Demo"};
    auto ts = pipegen::emit_ts_module(mod, known, typedefs);

    CHECK(ts.find("export const FIELD_DESCRIPTORS_M_Demo") != std::string::npos);
    CHECK(ts.find(R"({ name: 'x', kind: 'Scalar', elementTypeTag: 'flowboard::Double' })")
          != std::string::npos);
}

TEST_CASE("emit_ts_barrel re-exports per-module field descriptors") {
    auto out = pipegen::emit_ts_barrel({"M_Common", "M_Mount"});
    CHECK(out.find("import { FIELD_DESCRIPTORS_M_Common } from './M_Common_types';") != std::string::npos);
    CHECK(out.find("import { FIELD_DESCRIPTORS_M_Mount } from './M_Mount_types';")  != std::string::npos);
    CHECK(out.find("export const ALL_FIELD_DESCRIPTORS") != std::string::npos);
    CHECK(out.find("...FIELD_DESCRIPTORS_M_Common,") != std::string::npos);
    CHECK(out.find("...FIELD_DESCRIPTORS_M_Mount,") != std::string::npos);
}

TEST_CASE("emit_ts_module synthesizes <Op>_Args struct for multi-param IClient op") {
    // Mirror of the C++ synthesis test: a multi-param IClient op must yield an
    // EventButton_Args struct in the TS type list AND its FIELD_DESCRIPTORS, so
    // the web's Extract / list / type-compat features can see _Args structs the
    // same way they see regular onboard structs.
    pipegen::ast::Module mod;
    mod.name = "M_HidJoystick";

    auto make_prim_param = [](std::string name, std::string prim_name) -> pipegen::ast::OpParam {
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
    op.params.push_back(make_prim_param("ButtonIndex", "unsigned long"));
    op.params.push_back(make_prim_param("ButtonName",  "string"));
    op.params.push_back(make_prim_param("IsSelected",  "boolean"));
    iclient.operations.push_back(op);
    mod.interfaces.push_back(iclient);

    std::set<std::string> known = {"M_HidJoystick"};
    std::map<std::string, pipegen::ast::TypeRef> typedefs;
    auto out = pipegen::emit_ts_module(mod, known, typedefs);

    // The synthetic struct appears in the module type list...
    CHECK(out.find("\"EventButton_Args\"") != std::string::npos);
    CHECK(out.find("\"M_HidJoystick::EventButton_Args\"") != std::string::npos);
    // ...and in FIELD_DESCRIPTORS so the web can enumerate its fields.
    CHECK(out.find("'M_HidJoystick::EventButton_Args'") != std::string::npos);
    CHECK(out.find("name: 'ButtonIndex'") != std::string::npos);
}
