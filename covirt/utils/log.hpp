#pragma once

#include <print>
#include <format>
#include <chrono>

#include "termcolors.h"

namespace out {
    enum class prefix : bool {
        no_prefix = false,
        with_prefix = true
    };

    enum class fmt : bool {
        x = false,
        X = true
    };

    static inline void clear() { std::print("\33[2K\r"); }

    static inline std::string get_timestamp() 
    {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::localtime(&now_time_t);
        return std::format("{}{:04}-{:02}-{:02} {:02}:{:02}:{:02}{}", COLOR_INPUT,
                        now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
                        now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec, COLOR_RESET);
    }

    template<typename... Args>
    static inline void error(std::format_string<Args...> fmt, Args&&... args) 
    {
        std::print("{}error: {}", COLOR_ERROR, COLOR_RESET);
        std::print(fmt, std::forward<Args>(args)...);
    }

    // template<typename... Args>
    // static inline void warn(std::format_string<Args...> fmt, Args&&... args) 
    // {
    //     std::print("{}warn: {}", COLOR_WARN, COLOR_RESET);
    //     std::print(fmt, std::forward<Args>(args)...);
    // }

    template<typename... Args>
    static inline void special(std::format_string<Args...> fmt, Args&&... args) 
    {
        std::print("{}", COLOR_WARN);
        std::print(fmt, std::forward<Args>(args)...);
        std::print("{}", COLOR_RESET);
    }

    static inline std::string indent(int lvl = 1)
    {
        return std::string((lvl * 4) - 1, ' ');
    }

    template <typename T = uint64_t>
    static inline std::string value(T v)
    {
        return std::format("{}{}{}", COLOR_VALUE, v, COLOR_RESET);
    }

    static inline std::string name(const std::string &s)
    {
        return std::format("{}{}{}", COLOR_NAME, s, COLOR_RESET);
    }

    template <typename T>
    static inline std::string green(T v)
    {
        return std::format("{}{}{}", COLOR_GREEN, v, COLOR_RESET);
    }

    template <typename T>
    static inline std::string red(T v)
    {
        return std::format("{}{}{}", COLOR_RED, v, COLOR_RESET);
    }

    template <typename T>
    static inline std::string purple(T v)
    {
        return std::format("{}{}{}", COLOR_WARN, v, COLOR_RESET);
    }

    template <typename T>
    static inline std::string yellow(T v)
    {
        return std::format("{}{}{}", COLOR_YELLOW, v, COLOR_RESET);
    }

    template <typename T>
    static inline std::string good(T v)
    {
        return std::format("{}{}{}", COLOR_GOOD, v, COLOR_RESET);
    }

    template <typename T>
    static inline std::string bad(T v)
    {
        return std::format("{}{}{}", COLOR_BAD, v, COLOR_RESET);
    }

    template <typename T>
    static inline std::string blue(T v)
    {
        return std::format("{}{}{}", COLOR_NOTE, v, COLOR_RESET);
    }

    template <typename... Tx>
    static inline void ok(std::format_string<Tx...> fmt, Tx&&... args)
    {
        std::print("[{}] [{}] ", get_timestamp(), out::good("done"));
        std::println(fmt, std::forward<Tx>(args)...);
    }

    template <typename... Tx>
    static inline void fail(std::format_string<Tx...> fmt, Tx&&... args)
    {
        std::print("[{}] [{}] ", get_timestamp(), out::bad("fail"));
        std::println(fmt, std::forward<Tx>(args)...);
    }

    template <typename... Tx>
    static inline void warn(std::format_string<Tx...> fmt, Tx&&... args)
    {
        std::print("[{}] [{}] ",get_timestamp(), out::purple("warn"));
        std::println(fmt, std::forward<Tx>(args)...);
    }
    
    template <typename... Tx>
    static inline void info(std::format_string<Tx...> fmt, Tx&&... args)
    {
        std::print("[{}] [{}] ", get_timestamp(), out::blue("info"));
        std::println(fmt, std::forward<Tx>(args)...);
    }

    template <typename... Tx>
    static inline void wait(std::format_string<Tx...> fmt, Tx&&... args)
    {
        std::print("[{}] [{}] ", get_timestamp(), out::yellow("wait"));
        std::print(fmt, std::forward<Tx>(args)...);
    }

    template <typename... Tx>
    static inline void assertion(bool expr, std::format_string<Tx...> fmt, Tx&&... args)
    {
        if (!expr) {
            fail(fmt, std::forward<Tx>(args)...);
            abort();
        }
    }

    template<typename... Args>
    [[nodiscard]] static inline auto vformat(std::string_view fmt, Args&&... args)
    {
        return std::vformat(fmt, std::make_format_args(args...));
    }

    template <int bytes = 16>
    static inline std::string address(uint64_t v, fmt c = fmt::x, prefix p = prefix::no_prefix)
    {
        auto prefix = static_cast<bool>(p) ? "0x" : "";
        auto letter_case = static_cast<bool>(c) ? "X" : "x";
        return vformat(vformat("{{}}{}{{:0{}{}}}{{}}", prefix, bytes, letter_case), COLOR_ADDRESS, v, COLOR_RESET);
    }

    template <int bytes = 16>
    static inline std::string value_hex(uint64_t v, fmt c = fmt::x, prefix p = prefix::no_prefix)
    {
        auto prefix = static_cast<bool>(p) ? "0x" : "";
        auto letter_case = static_cast<bool>(c) ? "X" : "x";
        return vformat(vformat("{{}}{}{{:0{}{}}}{{}}", prefix, bytes, letter_case), COLOR_VALUE, v, COLOR_RESET);
    }

    static inline std::string hex_arr(uint8_t *bytes, size_t length, const std::string &suffix = "")
    {
        std::string result;
        for (int i = 0; i < length; i++)
            result = vformat("{}{:02x}{}", result, bytes[i], suffix);
        return result;
    }

    static inline std::string char_arr(uint8_t *bytes, size_t length, const std::string &suffix = "")
    {
        std::string result;
        for (int i = 0; i < length; i++)
            result = vformat("{}{:c}{}", result, std::isprint(bytes[i]) ? bytes[i] : '.', suffix);
        return result;
    }

    static inline std::string align(int max_count, int count, int spaces = 1)
    {
        auto rem = max_count - count;
        return rem <= 0 ? "" : std::string(rem * spaces, ' ');
    }
}
