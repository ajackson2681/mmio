/**
 * @file Tests.cpp
 * @author Adam Jackson (contact@adamjackson.dev)
 *
 * @brief Just a series of "unit tests" to ensure correct behavior of the MMIO
 * library. Doesn't use any additional framework so it's lightweight and self-contained
 *
 * @date 2026-10-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "MMIO.hpp"
#include "stdio.h"
#include <cstdint>
#include <vector>

using namespace MMIO;

int failures = 0;

#define numbits(type) sizeof(type) * 8

#define CHECK(condition, expected, actual) \
    if (!(condition)) { \
        printf("FAIL: %s line %d: %s || Expected: %lx, Actual: %lx\n", __FILE__, __LINE__, #condition, \
            static_cast<uint64_t>(expected), \
            static_cast<uint64_t>(actual)); \
        ++failures; \
    }

// MMIO::reportError() throws when exceptions are enabled (the default build) and silently
// sets `errnum` otherwise. Tests that deliberately trigger an error path use this to swallow
// the expected exception either way, so the CHECKs on `errnum`/register state afterward still
// run in both configurations, and so this file still compiles under -fno-exceptions.
#ifdef __EXCEPTIONS
#define EXPECT_POSSIBLE_THROW(expr) try { expr; } catch (...) {}
#else
#define EXPECT_POSSIBLE_THROW(expr) expr
#endif

void test_set_middle_two_bits_uint8()
{
    uint64_t toSet = 0b11;
    uint64_t expected = toSet << (numbits(uint8_t) / 2 - 1);

    BitField<uint8_t> reg;
    Field<4,3> range;
    reg[range] = toSet;

    CHECK(reg == expected, expected, reg);
}

void test_set_middle_two_bits_uint64()
{
    uint64_t toSet = 0b11;
    uint64_t expected = toSet << (numbits(uint64_t) / 2 - 1);

    BitField<uint64_t> reg;
    Field<32,31> range;
    reg[range] = toSet;

    CHECK(reg == expected, expected, reg);
}

void test_set_highest_bit_uint8()
{
    uint8_t toSet = 0b1;
    uint8_t expected = toSet << (numbits(uint8_t) - 1);

    BitField<uint8_t> reg;
    Field<7> pos;

    reg[pos] = toSet;

    CHECK(reg == expected, expected, reg);
}

void test_set_highest_bit_uint64()
{
    uint64_t toSet = 0b1;
    uint64_t expected = toSet << (numbits(uint64_t) - 1);

    BitField<uint64_t> reg;
    Field<63> pos;

    reg[pos] = toSet;

    CHECK(reg == expected, expected, reg);
}

void test_set_lowest_bit_uint64()
{
    uint64_t toSet = 0b1;
    uint64_t expected = 0b1;

    BitField<uint64_t> reg;
    Field<0> pos;

    reg[pos] = toSet;

    CHECK(reg == expected, expected, reg);
}

void test_set_lowest_bit_uint8()
{
    uint8_t toSet = 0b1;
    uint8_t expected = 0b1;

    BitField<uint8_t> reg;
    Field<0> pos;

    reg[pos] = toSet;

    CHECK(reg == expected, expected, reg);
}

void test_non_FieldSpec_access()
{
    uint32_t toSet = 0b1;
    uint32_t expected = (1 << 16);

    BitField<uint32_t> reg;
    reg[16] = toSet;

    CHECK(reg == expected, expected, reg);
}

void test_register_and_equals()
{
    BitField<uint32_t> reg(0xFF);
    reg &= 0x0F;

    uint32_t expected = 0x0F;

    CHECK(reg == expected, expected, reg);
}

void test_register_or_equals()
{
    BitField<uint32_t> reg(0xF0);
    reg |= 0x0F;

    uint32_t expected = 0xFF;

    CHECK(reg == expected, expected, reg);
}

void test_register_xor_equals()
{
    BitField<uint32_t> reg(0xFF);
    reg ^= 0x0F;

    uint32_t expected = 0xF0;

    CHECK(reg == expected, expected, reg);
}

void test_register_left_shift_equals()
{
    BitField<uint32_t> reg(0x1);
    reg <<= 4;

    uint32_t expected = 0x10;

    CHECK(reg == expected, expected, reg);
}

void test_register_right_shift_equals()
{
    BitField<uint32_t> reg(0x10);
    reg >>= 4;

    uint32_t expected = 0x1;

    CHECK(reg == expected, expected, reg);
}

void test_register_left_shift_operator_is_non_mutating()
{
    BitField<uint32_t> reg(0x1);
    uint32_t result = reg << 4;

    uint32_t expectedResult = 0x10;
    uint32_t expectedReg = 0x1;

    CHECK(result == expectedResult, expectedResult, result);
    CHECK(reg == expectedReg, expectedReg, reg);
}

void test_register_right_shift_operator_is_non_mutating()
{
    BitField<uint32_t> reg(0x10);
    uint32_t result = reg >> 4;

    uint32_t expectedResult = 0x1;
    uint32_t expectedReg = 0x10;

    CHECK(result == expectedResult, expectedResult, result);
    CHECK(reg == expectedReg, expectedReg, reg);
}

void test_field_or_equals()
{
    BitField<uint32_t> reg;
    Field<7,0> range;

    reg[range] = 0x0F;
    reg[range] |= 0xF0;

    uint32_t expected = 0xFF;

    CHECK(reg == expected, expected, reg);
}

void test_field_and_equals()
{
    BitField<uint32_t> reg;
    Field<7,0> range;

    reg[range] = 0xFF;
    reg[range] &= 0x0F;

    uint32_t expected = 0x0F;

    CHECK(reg == expected, expected, reg);
}

void test_field_xor_equals()
{
    BitField<uint32_t> reg;
    Field<7,0> range;

    reg[range] = 0xFF;
    reg[range] ^= 0x0F;

    uint32_t expected = 0xF0;

    CHECK(reg == expected, expected, reg);
}

void test_field_and_equals_preserves_other_bits()
{
    BitField<uint32_t> reg(0xFFFFFFFF);
    Field<15,8> range;

    reg[range] &= 0x00;

    uint32_t expected = 0xFFFF00FF;

    CHECK(reg == expected, expected, reg);
}

void test_field_or_equals_preserves_other_bits()
{
    BitField<uint32_t> reg(0x0000FF00);
    Field<7,0> range;

    reg[range] |= 0xAB;

    uint32_t expected = 0x0000FFAB;

    CHECK(reg == expected, expected, reg);
}

void test_bitpos_or_equals()
{
    BitField<uint8_t> reg;
    Field<3> pos;

    reg[pos] |= 1;

    uint8_t expected = (1 << 3);

    CHECK(reg == expected, expected, reg);
}

void test_bitpos_and_equals()
{
    BitField<uint8_t> reg(0xFF);
    Field<3> pos;

    reg[pos] &= 0;

    uint8_t expected = static_cast<uint8_t>(0xFF & ~(1 << 3));

    CHECK(reg == expected, expected, reg);
}

void test_bitpos_xor_equals_toggles()
{
    BitField<uint8_t> reg;
    Field<3> pos;

    reg[pos] ^= 1;
    uint8_t expectedSet = (1 << 3);
    CHECK(reg == expectedSet, expectedSet, reg);

    reg[pos] ^= 1;
    uint8_t expectedCleared = 0;
    CHECK(reg == expectedCleared, expectedCleared, reg);
}

void test_non_FieldSpec_or_equals()
{
    BitField<uint32_t> reg;

    reg[16] = 1;
    reg[17] |= 1;

    uint32_t expected = (1 << 16) | (1 << 17);

    CHECK(reg == expected, expected, reg);
}

void test_read_only_field_read()
{
    BitField<uint32_t> reg(0xABCD1234);
    Field<15,8> ro(ReadOnly);

    uint32_t actual = reg[ro];
    uint32_t expected = (0xABCD1234 >> 8) & 0xFF;

    CHECK(actual == expected, expected, actual);
}

void test_write_only_field_zeroes_other_bits()
{
    BitField<uint32_t> reg(0xFFFFFFFF);
    Field<7,0> wo(WriteOnly);

    reg[wo] = 0xAB;

    uint32_t expected = 0x000000AB;

    CHECK(reg == expected, expected, reg);
}

void test_out_of_range_bit_read_returns_zero()
{
    BitField<uint8_t> reg(0xFF);
    const BitField<uint8_t>& constReg = reg;

    // With exceptions enabled, the read throws before it ever reaches its `return 0`, so
    // `actual` is left at its initial value rather than being assigned 0 -- initialize it
    // to the expected value so the CHECK is meaningful in both build configurations.
    uint8_t actual = 0;
    EXPECT_POSSIBLE_THROW(actual = constReg[10]);
    uint8_t expected = 0;

    CHECK(actual == expected, expected, actual);
}

void test_full_width_bitrange_uint8()
{
    uint8_t toSet = 0xAB;
    BitField<uint8_t> reg;
    Field<7,0> range;

    reg[range] = toSet;

    CHECK(reg == toSet, toSet, reg);
}

void test_full_width_bitrange_uint64()
{
    uint64_t toSet = 0x0123456789ABCDEF;
    BitField<uint64_t> reg;
    Field<63,0> range;

    reg[range] = toSet;

    CHECK(reg == toSet, toSet, reg);
}

void test_volatile_register_basic_read_write()
{
    Register<uint32_t> reg;
    reg = 0x1234;

    uint32_t expected = 0x1234;

    CHECK(reg == expected, expected, reg);
}

void test_errnum_clean_after_valid_field_write()
{
    errnum = None;

    BitField<uint32_t> reg;
    reg[3] = 1;

    CHECK(errnum == None, None, errnum);
}

void test_errnum_write_to_read_only_field()
{
    errnum = None;

    BitField<uint32_t> reg(0x00);
    Field<7,0> ro(ReadOnly);

    EXPECT_POSSIBLE_THROW(reg[ro] = 0xFF);

    CHECK(errnum == WriteToReadOnly, WriteToReadOnly, errnum);
    CHECK(reg == 0x00, 0x00, reg); // the write must have been blocked
}

void test_errnum_read_from_write_only_field()
{
    errnum = None;

    BitField<uint32_t> reg(0xABCD1234);
    Field<7,0> wo(WriteOnly);

    uint32_t value = 0;
    EXPECT_POSSIBLE_THROW(value = reg[wo]);

    CHECK(errnum == ReadFromWriteOnly, ReadFromWriteOnly, errnum);
    CHECK(value == 0, 0, value);
}

void test_errnum_read_from_write_only_field_via_const_register()
{
    errnum = None;

    BitField<uint32_t> reg(0xABCD1234);
    const BitField<uint32_t>& constReg = reg;
    Field<7,0> wo(WriteOnly);

    uint32_t value = 0;
    EXPECT_POSSIBLE_THROW(value = constReg[wo]);

    CHECK(errnum == ReadFromWriteOnly, ReadFromWriteOnly, errnum);
    CHECK(value == 0, 0, value);
}

void test_errnum_compound_assign_on_read_only_field()
{
    errnum = None;

    BitField<uint32_t> reg(0x0F);
    Field<7,0> ro(ReadOnly);

    EXPECT_POSSIBLE_THROW(reg[ro] |= 0xF0);

    CHECK(errnum == CompoundAssignOnNonReadWrite, CompoundAssignOnNonReadWrite, errnum);
    CHECK(reg == 0x0F, 0x0F, reg); // unchanged
}

void test_errnum_compound_assign_on_write_only_field()
{
    errnum = None;

    BitField<uint32_t> reg(0x0F);
    Field<7,0> wo(WriteOnly);

    EXPECT_POSSIBLE_THROW(reg[wo] &= 0x00);

    CHECK(errnum == CompoundAssignOnNonReadWrite, CompoundAssignOnNonReadWrite, errnum);
    CHECK(reg == 0x0F, 0x0F, reg); // unchanged
}

void test_errnum_out_of_range_bit_read()
{
    errnum = None;

    BitField<uint8_t> reg(0xFF);
    const BitField<uint8_t>& constReg = reg;

    uint8_t value = 0;
    EXPECT_POSSIBLE_THROW(value = constReg[10]);

    CHECK(errnum == BadBitPosition, BadBitPosition, errnum);
    CHECK(value == 0, 0, value);
}

void test_errnum_out_of_range_bit_write()
{
    errnum = None;

    BitField<uint8_t> reg;

    EXPECT_POSSIBLE_THROW(reg[10] = 1);

    CHECK(errnum == BadBitPosition, BadBitPosition, errnum);
    CHECK(reg == 0, 0, reg); // out-of-range write must not corrupt the register
}

std::vector<void(*)()> tests = {
    test_set_middle_two_bits_uint8,
    test_set_middle_two_bits_uint64,
    test_set_highest_bit_uint8,
    test_set_highest_bit_uint64,
    test_set_lowest_bit_uint8,
    test_set_lowest_bit_uint64,
    test_non_FieldSpec_access,
    test_register_and_equals,
    test_register_or_equals,
    test_register_xor_equals,
    test_register_left_shift_equals,
    test_register_right_shift_equals,
    test_register_left_shift_operator_is_non_mutating,
    test_register_right_shift_operator_is_non_mutating,
    test_field_or_equals,
    test_field_and_equals,
    test_field_xor_equals,
    test_field_and_equals_preserves_other_bits,
    test_field_or_equals_preserves_other_bits,
    test_bitpos_or_equals,
    test_bitpos_and_equals,
    test_bitpos_xor_equals_toggles,
    test_non_FieldSpec_or_equals,
    test_read_only_field_read,
    test_write_only_field_zeroes_other_bits,
    test_out_of_range_bit_read_returns_zero,
    test_full_width_bitrange_uint8,
    test_full_width_bitrange_uint64,
    test_volatile_register_basic_read_write,
    test_errnum_clean_after_valid_field_write,
    test_errnum_write_to_read_only_field,
    test_errnum_read_from_write_only_field,
    test_errnum_read_from_write_only_field_via_const_register,
    test_errnum_compound_assign_on_read_only_field,
    test_errnum_compound_assign_on_write_only_field,
    test_errnum_out_of_range_bit_read,
    test_errnum_out_of_range_bit_write,
};

int main()
{
    for (const auto& test : tests) {
        test();
    }

    int testsPassed = tests.size() - failures;
    int testsRun = tests.size();

    printf("Testing finished. %u/%u tests passed.\n", testsPassed, testsRun);

    return failures > 0 ? 1 : 0;
}
