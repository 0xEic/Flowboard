// SPDX-License-Identifier: MIT
#pragma once
#include <string>

/// \file
/// \brief Arithmetic-formula and string-manipulation helpers for Manipulate nodes.

namespace flowboard {

/// \brief Evaluate an arithmetic formula in the single variable `x`.
/// \details Supports: + - * / % ^ (right-assoc), unary +/-, parentheses, the constants
/// `pi`/`e`, and functions abs, sqrt, sin, cos, tan, exp, ln, log, log10,
/// floor, ceil, round, sign, min, max, pow, clamp. On a parse/eval error the
/// value is left unchanged by returning `fallback` (the node's input), so a
/// malformed formula degrades to a pass-through rather than throwing.
double eval_formula(std::string const& expr, double x, double fallback);

/// \brief String manipulation used by Manipulate.String and List.MapField.
/// \details
///   mode "replace": replace every occurrence of `find` with `repl` (find=="" → unchanged)
///   mode "set":     return `text` (ignores input)
///   mode "prepend": text + input
///   mode "append":  input + text
///   anything else:  input unchanged
std::string apply_string_op(std::string const& input, std::string const& mode,
                            std::string const& find, std::string const& repl,
                            std::string const& text);

}  // namespace flowboard
