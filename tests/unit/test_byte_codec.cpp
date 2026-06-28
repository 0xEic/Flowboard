// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/byte_codec.hpp"
#include <vector>
#include <cstdint>

using namespace flowboard;
using nlohmann::json;

TEST_CASE("byte_width of supported tags") {
    CHECK(byte_width("flowboard::Bool")   == 1);
    CHECK(byte_width("flowboard::Char")   == 1);
    CHECK(byte_width("flowboard::UInt8")  == 1);
    CHECK(byte_width("flowboard::Int16")  == 2);
    CHECK(byte_width("flowboard::UInt32") == 4);
    CHECK(byte_width("flowboard::Float")  == 4);
    CHECK(byte_width("flowboard::Double") == 8);
    CHECK(byte_width("flowboard::String") == 0);
}

TEST_CASE("Int32 round-trips in both endians") {
    std::vector<std::uint8_t> buf;
    REQUIRE(byte_write(buf, 0, "flowboard::Int32", json(0x01020304), true));
    REQUIRE(buf.size() == 4);
    CHECK(buf[0] == 0x04); CHECK(buf[1] == 0x03); CHECK(buf[2] == 0x02); CHECK(buf[3] == 0x01);
    CHECK(byte_read(buf, 0, "flowboard::Int32", true).value().get<long long>() == 0x01020304);

    std::vector<std::uint8_t> big;
    REQUIRE(byte_write(big, 0, "flowboard::Int32", json(0x01020304), false));
    CHECK(big[0] == 0x01); CHECK(big[1] == 0x02); CHECK(big[2] == 0x03); CHECK(big[3] == 0x04);
    CHECK(byte_read(big, 0, "flowboard::Int32", false).value().get<long long>() == 0x01020304);
}

TEST_CASE("Int16 negative two's complement") {
    std::vector<std::uint8_t> buf;
    REQUIRE(byte_write(buf, 0, "flowboard::Int16", json(-2), true));
    CHECK(buf[0] == 0xFE); CHECK(buf[1] == 0xFF);
    CHECK(byte_read(buf, 0, "flowboard::Int16", true).value().get<long long>() == -2);
}

TEST_CASE("UInt8 / Bool at offset, gaps zero-filled") {
    std::vector<std::uint8_t> buf;
    REQUIRE(byte_write(buf, 2, "flowboard::UInt8", json(0xAB), true));
    REQUIRE(buf.size() == 3);
    CHECK(buf[0] == 0); CHECK(buf[1] == 0); CHECK(buf[2] == 0xAB);
    REQUIRE(byte_write(buf, 0, "flowboard::Bool", json(true), true));
    CHECK(buf[0] == 1);
    CHECK(byte_read(buf, 0, "flowboard::Bool", true).value().get<bool>() == true);
    CHECK(byte_read(buf, 2, "flowboard::UInt8", true).value().get<int>() == 0xAB);
}

TEST_CASE("Double round-trips") {
    std::vector<std::uint8_t> buf;
    REQUIRE(byte_write(buf, 0, "flowboard::Double", json(3.14159), true));
    REQUIRE(buf.size() == 8);
    CHECK(byte_read(buf, 0, "flowboard::Double", true).value().get<double>() == doctest::Approx(3.14159));
}

TEST_CASE("read past end is nullopt; unsupported tag is nullopt/false") {
    std::vector<std::uint8_t> buf = {1, 2};
    CHECK_FALSE(byte_read(buf, 1, "flowboard::Int32", true).has_value());
    CHECK_FALSE(byte_read(buf, 0, "flowboard::String", true).has_value());
    std::vector<std::uint8_t> b2;
    CHECK_FALSE(byte_write(b2, 0, "flowboard::String", json("x"), true));
}

TEST_CASE("bit_width: Bool is 1 bit, others byte_width*8") {
    CHECK(bit_width("flowboard::Bool")   == 1);
    CHECK(bit_width("flowboard::UInt8")  == 8);
    CHECK(bit_width("flowboard::Int16")  == 16);
    CHECK(bit_width("flowboard::UInt32") == 32);
    CHECK(bit_width("flowboard::Double") == 64);
    CHECK(bit_width("flowboard::String") == 0);
}

TEST_CASE("bit_write UInt8 at bit offset 4 -> bytes B0 0A; round-trips") {
    std::vector<std::uint8_t> buf;
    REQUIRE(bit_write(buf, 4, "flowboard::UInt8", json(0xAB)));
    REQUIRE(buf.size() == 2);
    CHECK(buf[0] == 0xB0);
    CHECK(buf[1] == 0x0A);
    CHECK(bit_read(buf, 4, "flowboard::UInt8").value().get<int>() == 0xAB);
}

TEST_CASE("eight Bool flags pack into one byte") {
    std::vector<std::uint8_t> buf;
    const bool vals[8] = {true,false,true,true,false,false,false,true}; // 0x8D
    for (std::size_t i = 0; i < 8; ++i)
        REQUIRE(bit_write(buf, i, "flowboard::Bool", json(vals[i])));
    REQUIRE(buf.size() == 1);
    CHECK(buf[0] == 0x8D);
    for (std::size_t i = 0; i < 8; ++i)
        CHECK(bit_read(buf, i, "flowboard::Bool").value().get<bool>() == vals[i]);
}

TEST_CASE("UInt16 at non-aligned bit offset spans 3 bytes; round-trips") {
    std::vector<std::uint8_t> buf;
    REQUIRE(bit_write(buf, 4, "flowboard::UInt16", json(0x1234)));
    REQUIRE(buf.size() == 3);   // bits 4..19
    CHECK(bit_read(buf, 4, "flowboard::UInt16").value().get<int>() == 0x1234);
}

TEST_CASE("signed value sign-extends at bit width") {
    std::vector<std::uint8_t> buf;
    REQUIRE(bit_write(buf, 4, "flowboard::Int16", json(-2)));
    CHECK(bit_read(buf, 4, "flowboard::Int16").value().get<long long>() == -2);
}

TEST_CASE("Float round-trips at a bit offset") {
    std::vector<std::uint8_t> buf;
    REQUIRE(bit_write(buf, 3, "flowboard::Float", json(1.5f)));
    CHECK(bit_read(buf, 3, "flowboard::Float").value().get<float>() == doctest::Approx(1.5f));
}

TEST_CASE("bit_read past end is nullopt; unsupported tag nullopt/false") {
    std::vector<std::uint8_t> buf = {0xFF};
    CHECK_FALSE(bit_read(buf, 4, "flowboard::UInt8").has_value()); // needs bits 4..11 > 8
    CHECK_FALSE(bit_read(buf, 0, "flowboard::String").has_value());
    std::vector<std::uint8_t> b2;
    CHECK_FALSE(bit_write(b2, 0, "flowboard::String", json("x")));
}
