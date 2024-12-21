#pragma once

#include <cstdint>
#include <random>
#include <ranges>

namespace covirt {
    template <typename T>
    inline T rand()
    {
        static std::random_device rd;
        static std::mt19937 generator(rd());

        if constexpr (!std::is_same_v<T, int8_t> && !std::is_same_v<T, uint8_t>) {
            static std::uniform_int_distribution<T> distribution(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
            return distribution(generator);
        }
        else {
            return T(rand<int>() & 0xff);
        }
    }
}
