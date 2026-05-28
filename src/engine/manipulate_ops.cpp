// SPDX-License-Identifier: MIT
#include "flowboard/manipulate_ops.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

namespace flowboard {
namespace {

// Recursive-descent evaluator for a small arithmetic grammar:
//   expr   := term (('+'|'-') term)*
//   term   := factor (('*'|'/'|'%') factor)*
//   factor := unary ('^' factor)?           (right-associative power)
//   unary  := ('+'|'-') unary | primary
//   primary:= number | 'x' | ident | ident '(' args ')' | '(' expr ')'
struct Parser {
    std::string const& s;
    double x;
    std::size_t i = 0;

    void skip_ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eof() { skip_ws(); return i >= s.size(); }
    char peek() { skip_ws(); return i < s.size() ? s[i] : '\0'; }
    bool accept(char c) { if (peek() == c) { ++i; return true; } return false; }
    void expect(char c) { if (!accept(c)) throw std::runtime_error("expected token"); }

    double parse_expr() {
        double v = parse_term();
        for (;;) {
            char c = peek();
            if (c == '+') { ++i; v += parse_term(); }
            else if (c == '-') { ++i; v -= parse_term(); }
            else return v;
        }
    }
    double parse_term() {
        double v = parse_factor();
        for (;;) {
            char c = peek();
            if (c == '*') { ++i; v *= parse_factor(); }
            else if (c == '/') { ++i; v /= parse_factor(); }
            else if (c == '%') { ++i; v = std::fmod(v, parse_factor()); }
            else return v;
        }
    }
    double parse_factor() {
        double base = parse_unary();
        if (peek() == '^') { ++i; return std::pow(base, parse_factor()); }
        return base;
    }
    double parse_unary() {
        char c = peek();
        if (c == '+') { ++i; return parse_unary(); }
        if (c == '-') { ++i; return -parse_unary(); }
        return parse_primary();
    }
    double parse_number() {
        skip_ws();
        std::size_t start = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' ||
                                s[i] == 'e' || s[i] == 'E' ||
                                ((s[i] == '+' || s[i] == '-') && i > start &&
                                 (s[i-1] == 'e' || s[i-1] == 'E'))))
            ++i;
        return std::stod(s.substr(start, i - start));
    }
    std::string parse_ident() {
        skip_ws();
        std::size_t start = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
        return s.substr(start, i - start);
    }
    double call(std::string const& fn) {
        expect('(');
        double a = parse_expr();
        double b = 0;
        bool has_b = accept(',');
        if (has_b) b = parse_expr();
        double c = 0;
        bool has_c = accept(',');
        if (has_c) c = parse_expr();
        expect(')');
        if (fn == "abs")   return std::fabs(a);
        if (fn == "sqrt")  return std::sqrt(a);
        if (fn == "sin")   return std::sin(a);
        if (fn == "cos")   return std::cos(a);
        if (fn == "tan")   return std::tan(a);
        if (fn == "exp")   return std::exp(a);
        if (fn == "ln" || fn == "log") return std::log(a);
        if (fn == "log10") return std::log10(a);
        if (fn == "floor") return std::floor(a);
        if (fn == "ceil")  return std::ceil(a);
        if (fn == "round") return std::round(a);
        if (fn == "sign")  return (a > 0) - (a < 0);
        if (fn == "min")   return std::fmin(a, b);
        if (fn == "max")   return std::fmax(a, b);
        if (fn == "pow")   return std::pow(a, b);
        if (fn == "clamp") return std::fmax(b, std::fmin(a, c));
        throw std::runtime_error("unknown function: " + fn);
    }
    double parse_primary() {
        char c = peek();
        if (c == '(') { ++i; double v = parse_expr(); expect(')'); return v; }
        if (std::isalpha((unsigned char)c) || c == '_') {
            std::string id = parse_ident();
            if (peek() == '(') return call(id);
            if (id == "x")  return x;
            if (id == "pi") return 3.14159265358979323846;
            if (id == "e")  return 2.71828182845904523536;
            throw std::runtime_error("unknown identifier: " + id);
        }
        return parse_number();
    }
};

}  // namespace

double eval_formula(std::string const& expr, double x, double fallback) {
    if (expr.empty()) return x;
    try {
        Parser p{expr, x};
        double v = p.parse_expr();
        if (!p.eof()) return fallback;  // trailing garbage
        return v;
    } catch (...) {
        return fallback;
    }
}

std::string apply_string_op(std::string const& input, std::string const& mode,
                            std::string const& find, std::string const& repl,
                            std::string const& text) {
    if (mode == "set")     return text;
    if (mode == "prepend") return text + input;
    if (mode == "append")  return input + text;
    if (mode == "replace") {
        if (find.empty()) return input;
        std::string out;
        out.reserve(input.size());
        std::size_t pos = 0;
        for (;;) {
            auto next = input.find(find, pos);
            if (next == std::string::npos) { out += input.substr(pos); break; }
            out += input.substr(pos, next - pos);
            out += repl;
            pos = next + find.size();
        }
        return out;
    }
    return input;
}

}  // namespace flowboard
