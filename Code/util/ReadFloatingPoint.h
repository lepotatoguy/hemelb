// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_UTIL_READFLOATINGPOINT_H
#define HEMELB_UTIL_READFLOATINGPOINT_H

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <istream>
#include <string>
#include <type_traits>

namespace hemelb::util {
    // libstdc++ stream extraction does not consume C99 hexadecimal floats.
    // C conversion functions support both decimal and hexadecimal notation.
    template <typename T>
    void ReadFloatingPoint(std::istream& input, T& value) {
        static_assert(std::is_floating_point_v<T>);
        std::istream::sentry sentry(input);
        if (!sentry) return;
        std::string token;
        for (;;) {
            auto next = input.peek();
            if (next == std::char_traits<char>::eof() || next == ',' || next == ')' ||
                next == ' ' || next == '\t' || next == '\n' || next == '\r' ||
                next == '\f' || next == '\v') break;
            token.push_back(static_cast<char>(input.get()));
        }
        char* end = nullptr;
        errno = 0;
        T parsed;
        if constexpr (std::is_same_v<T, float>) parsed = std::strtof(token.c_str(), &end);
        else if constexpr (std::is_same_v<T, double>) parsed = std::strtod(token.c_str(), &end);
        else parsed = std::strtold(token.c_str(), &end);
        if (token.empty() || end != token.c_str() + token.size() ||
            !std::isfinite(parsed) || (errno == ERANGE && parsed == 0)) {
            input.setstate(std::ios_base::failbit);
            return;
        }
        value = parsed;
    }
}
#endif // HEMELB_UTIL_READFLOATINGPOINT_H
