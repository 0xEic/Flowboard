// SPDX-License-Identifier: MIT
#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#include "ast.hpp"
#include "lexer.hpp"

namespace pipegen {

class ParseError : public std::runtime_error {
public:
    ParseError(std::string msg, int line)
        : std::runtime_error(std::move(msg)), line_(line) {}
    int line() const { return line_; }
private:
    int line_;
};

ast::File parse(std::vector<Token> const& tokens);

}  // namespace pipegen
