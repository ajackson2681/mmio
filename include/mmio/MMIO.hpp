/**
 * @file MMIO.hpp
 * @author Adam Jackson (contact@adamjackson.dev)
 *
 * @brief Header-only wrapper around fixed-width volatile types for memory-mapped hardware
 * registers. `Register<T>` wraps the raw storage; `Field<High, Low = High>` is a compile-time
 * descriptor for a bit range within it (or a single bit, via the defaulted `Low`), used as a
 * subscript.
 *
 * ```
 * struct SomeRegisterMap {
 *     MMIO::Register<uint32_t> control; // offset 0x00
 *     MMIO::Register<uint32_t> status;  // offset 0x04
 * };
 *
 * SomeRegisterMap* regs = reinterpret_cast<SomeRegisterMap*>(0x40000000);
 *
 * regs->control = 0x01;                            // whole-register write
 * uint32_t status = regs->status;                  // whole-register read
 * regs->control[MMIO::Field<7,0>{}] = 0xFF;         // field write
 *
 * static constexpr MMIO::Field<3> Enable{};                                // ReadWrite (default)
 * static constexpr MMIO::Field<4> Busy{MMIO::AccessPolicy::ReadOnly};      // access-checked
 * ```
 *
 * Field access is checked against its `AccessPolicy`; misuse that's knowable at compile time
 * (a field that doesn't fit the register's width) fails via `static_assert`, and misuse only
 * knowable at runtime (writing a read-only field, an out-of-range bit position, etc.) sets
 * `MMIO::errnum` and throws when exceptions are enabled.
 *
 * See README.adoc for the full rationale, padding conventions, and code generation via
 * `genheader.py`.
 *
 * @version 1.1
 * @date 2026-10-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <stdint.h>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    #if defined(__has_include)
        #if __has_include(<stdexcept>)
            #define MMIO_DETAIL_EXCEPTIONS
            #include <stdexcept>
        #endif
    #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define MMIO_ALWAYS_INLINE [[gnu::always_inline]] inline
#elif defined(_MSC_VER)
  #define MMIO_ALWAYS_INLINE __forceinline
#elif defined(__IAR_SYSTEMS_ICC__)
  #define MMIO_ALWAYS_INLINE _Pragma("inline=forced") inline
#else
  #define MMIO_ALWAYS_INLINE inline
#endif

namespace MMIO
{
    // used for conditionally selecting voltaile data type without the need for
    // type_traits
    namespace detail {
        template <typename T, bool IsVolatile>
        struct Storage { using type = volatile T; };

        template <typename T>
        struct Storage<T, false> { using type = T; };
    }

enum Error
{
    None,
    WriteToReadOnly,
    ReadFromWriteOnly,
    CompoundAssignOnNonReadWrite,
    BadBitPosition,
};

inline Error errnum = Error::None;

/**
 * Reports an MMIO error by setting `errnum` and, if exceptions are enabled,
 * throwing a corresponding standard exception. When exceptions are disabled
 * the call is a no-op beyond updating `errnum`; callers should check it
 * afterward when error detection is required.
 */
inline void reportError(Error error)
{
    errnum = error;

#ifdef MMIO_USE_EXCEPTIONS
    switch (error) {
        case WriteToReadOnly:
            throw std::logic_error("MMIO: write to read-only field");
        case ReadFromWriteOnly:
            throw std::logic_error("MMIO: read from write-only field");
        case CompoundAssignOnNonReadWrite:
            throw std::logic_error("MMIO: compound assignment on non-read-write field");
        case BadBitPosition:
            throw std::out_of_range("MMIO: bit position out of range");
        default:
            break;
    }
#endif
}

template <typename T>
constexpr T GenMask(uint8_t highBit, uint8_t lowBit)
{
    T mask = (highBit - lowBit + 1 == sizeof(T) * 8) ?
                static_cast<T>(~static_cast<T>(0)) :
                ((static_cast<T>(1) << (highBit - lowBit + 1)) - 1) << lowBit;

    return mask;
}

/**
 * Follows SVD's access types. The only one that is omitted is ReadWriteOnce, since that's
 * not really easy to do without some complicated metadata. So just make sure to be careful
 * of that
 */
enum Access
{
    ReadWrite,
    ReadOnly,
    WriteOnly,
};

template <Access Policy, uint8_t High, uint8_t Low = High>
struct Field
{
    static_assert(High >= Low, "High must be >= Low");
    static_assert(High < 64, "High bit out of range");

    const Access accessPolicy = Policy;
    const uint8_t highBit = High;
    const uint8_t lowBit = Low;
};

template <typename T, bool IsVolatile = true>
class Register
{
    template <Access Policy>
    class RegisterProxy;

public:
    Register()
        : raw(0)
    {}

    Register(T initial)
        : raw(initial)
    {}

    // purposefully deleted
    Register(const Register&)            = delete;
    Register& operator=(const Register&) = delete;

    Register& operator=(T value)
    {
        raw = value;
        return *this;
    }

    Register& operator&=(T mask)
    {
        T snapshot = raw;
        snapshot &= mask;
        raw = snapshot;
        return *this;
    }

    Register& operator|=(T mask)
    {
        T snapshot = raw;
        snapshot |= mask;
        raw = snapshot;
        return *this;
    }

    Register& operator^=(T mask)
    {
        T snapshot = raw;
        snapshot ^= mask;
        raw = snapshot;
        return *this;
    }

    operator T() const
    {
        return raw;
    }

    Register& operator<<=(int shiftAmount)
    {
        T snapshot = raw;
        snapshot <<= shiftAmount;
        raw = snapshot;
        return *this;
    }

    Register& operator>>=(int shiftAmount)
    {
        T snapshot = raw;
        snapshot >>= shiftAmount;
        raw = snapshot;
        return *this;
    }

    T operator<<(int shiftAmount) const
    {
        return static_cast<T>(raw) << shiftAmount;
    }

    T operator>>(int shiftAmount) const
    {
        return static_cast<T>(raw) >> shiftAmount;
    }

    // Single bit read
    T operator[](uint8_t bitPosition) const
    {
        if (bitPosition >= sizeof(T) * 8) {
            reportError(Error::BadBitPosition);
            return static_cast<T>(0);
        }

        return GetBits(bitPosition, bitPosition);
    }

    // Single bit read/write
    RegisterProxy<Access::ReadWrite> operator[](const uint8_t bitPosition)
    {
        if (bitPosition >= sizeof(T) * 8) {
            reportError(Error::BadBitPosition);
        }

        return RegisterProxy<Access::ReadWrite>(*this, bitPosition, bitPosition);
    }

    // Field range read
    template<Access Policy, uint8_t High, uint8_t Low>
    T operator[](const Field<Policy, High, Low>& spec) const
    {
        static_assert(Policy != Access::WriteOnly, "Cannot read from a write-only field");
        static_assert(High < sizeof(T) * 8, "High bit is outside of maximum range");

        return GetBits(High, Low);
    }

    // Field range read/write
    template<Access Policy, uint8_t High, uint8_t Low>
    RegisterProxy<Policy> operator[](const Field<Policy, High, Low>& spec)
    {
        static_assert(High < sizeof(T) * 8, "High bit is outside of maximum range");

        return RegisterProxy<Policy>(*this, High, Low);
    }

    void SetBits(uint8_t highBit, uint8_t lowBit, T value)
    {
        if (highBit >= sizeof(T) * 8) {
            reportError(Error::BadBitPosition);
            return;
        }

        T snapshot = raw;
        T mask = MMIO::GenMask<T>(highBit, lowBit);
        raw = (snapshot & ~mask) | ((value << lowBit) & mask);
    }

    T GetBits(uint8_t highBit, uint8_t lowBit) const
    {
        if (highBit >= sizeof(T) * 8) {
            reportError(Error::BadBitPosition);
            return 0;
        }

        T snapshot = raw;
        T mask = MMIO::GenMask<T>(highBit, lowBit);
        return (snapshot & mask) >> lowBit;
    }

    T Value() const
    {
        return raw;
    }

private:
    using Storage = typename detail::Storage<T, IsVolatile>::type;
    Storage raw;

    template <Access Policy>
    class RegisterProxy
    {
    public:
        RegisterProxy(Register& field_, uint8_t highBit_, uint8_t lowBit_)
            : field(field_)
            , highBit(highBit_)
            , lowBit(lowBit_)
        {}

        RegisterProxy(const RegisterProxy&) = delete;

        operator T() const
        {
            static_assert(Policy != Access::WriteOnly, "Cannot read from a write-only field");

            return field.GetBits(highBit, lowBit);
        }

        MMIO_ALWAYS_INLINE
        RegisterProxy& operator=(T value)
        {
            static_assert(Policy != Access::ReadOnly, "Cannot write to a read-only field");

            if constexpr (Policy == Access::ReadWrite) {
                field.SetBits(highBit, lowBit, value);
            }
            else {
                // Write-only fields cannot safely perform a read-modify-write since the
                // read may return undefined or stale data. Instead, only the field bits
                // are written and all other bits in the register are set to zero.
                // If this is not the desired behavior, use a whole-register write directly.
                field.SetBitsNoRead(highBit, lowBit, value);
            }

            return *this;
        }

        RegisterProxy& operator|=(T mask)
        {
            static_assert(Policy == Access::ReadWrite,
                          "Compound assignment requires a read-write field");

            T snapshot = field.GetBits(highBit, lowBit);
            snapshot |= mask;
            field.SetBits(highBit, lowBit, snapshot);
            return *this;
        }

        RegisterProxy& operator&=(T mask)
        {
            static_assert(Policy == Access::ReadWrite,
                          "Compound assignment requires a read-write field");

            T snapshot = field.GetBits(highBit, lowBit);
            snapshot &= mask;
            field.SetBits(highBit, lowBit, snapshot);
            return *this;
        }

        RegisterProxy& operator^=(T mask)
        {
            static_assert(Policy == Access::ReadWrite,
                          "Compound assignment requires a read-write field");

            T snapshot = field.GetBits(highBit, lowBit);
            snapshot ^= mask;
            field.SetBits(highBit, lowBit, snapshot);
            return *this;
        }

    private:
        Register& field;
        uint8_t highBit;
        uint8_t lowBit;
    };

    void SetBitsNoRead(uint8_t highBit, uint8_t lowBit, T value)
    {
        if (highBit >= sizeof(T) * 8) {
            reportError(Error::BadBitPosition);
            return;
        }

        raw = (value << lowBit) & MMIO::GenMask<T>(highBit, lowBit);
    }

    static_assert((T)-1 > 0, "Register<T> requires an unsigned type");
};

static_assert(sizeof(Register<uint32_t>) == sizeof(uint32_t), "Register<uint32_t> size mismatch");
static_assert(__is_standard_layout(Register<uint32_t>), "Register<uint32_t, false> is not standard layout");

static_assert(sizeof(Register<uint32_t, false>) == sizeof(uint32_t), "Register<uint32_t, false> size mismatch");
static_assert(__is_standard_layout(Register<uint32_t, false>), "Register<uint32_t, false> is not standard layout");

// alias for Register which defines it as non-volatile type. Useful for more general-purpose
// operations
template <typename T>
using BitField = Register<T, false>;
};
