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
        static std::uniform_int_distribution<T> distribution(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
        return distribution(generator);
    }
}
