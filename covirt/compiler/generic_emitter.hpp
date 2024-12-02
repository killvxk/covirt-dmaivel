#pragma once

#include <cassert>
#include <cstdint>
#include <vector>
#include <variant>

#include <utils/log.hpp>

namespace covirt {
    class generic_emitter {
    protected:
        std::vector<uint8_t> bytes;
        int count = 0;
        int cached_size = 0;

    public:
        constexpr std::vector<uint8_t>& get() { return bytes; }
        constexpr int get_count() const { return count; }

        template <typename O, typename S, typename... Tx>
        std::vector<uint8_t> emit(O opcode, S size, Tx&&... args)
        {
            bytes.push_back(opcode(opcode, size));

            ([&](auto &x) {
                bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&x), reinterpret_cast<uint8_t*>(&x) + sizeof(x));
            }(args), ...);
        }

        template <typename O, typename S>
        uint8_t opcode(O opcode, S size)
        {
            static constexpr auto encode_size = []<typename X>(X x, int &cached_size) -> uint8_t {
                static_assert(std::is_same_v<X, int> || std::is_same_v<X, size_t>, "expected int size");
                cached_size = int(x);

                switch (x) {
                case 1: return 0b00 << 6;
                case 2: return 0b01 << 6;
                case 4: return 0b10 << 6;
                case 8: return 0b11 << 6;
                }

                return 0;
            };

            count++;
            return static_cast<uint8_t>(uint8_t(opcode) | encode_size(size, cached_size));
        }

        template <typename T>
        std::variant<uint8_t, uint16_t, uint32_t, uint64_t> cast(T x, std::optional<size_t> size_override = {}) const
        {
            auto size = size_override.value_or(cached_size);

            switch (size) {
            case 1: return uint8_t(x);
            case 2: return uint16_t(x);
            case 4: return uint32_t(x);
            case 8: return uint64_t(x);
            }

            return uint32_t(x);
        }

        template <typename T>
        auto& operator >> (T x) 
        {
            bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&x), reinterpret_cast<uint8_t*>(&x) + sizeof(T));
            return *this;
        }

        auto& operator >> (std::variant<uint8_t, uint16_t, uint32_t, uint64_t> x) 
        {
            std::visit([this](auto &&x) {
                bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&x), reinterpret_cast<uint8_t*>(&x) + sizeof(x));
            }, x);
            return *this;
        }

        template <typename... Tx>
        void emplace(Tx&&... args)
        {
            ((*this) >> ... >> std::forward<Tx>(args));
        }
    };
}