#pragma once
#include "interpreter.h"
#include <cmath>
#include <functional>

struct PointHash
{
    std::size_t operator()(const Point& p) const
    {
        // We round to a specific precision to match your PointEqual tolerance (1e-6)
        // This ensures points that are 'equal' also have the 'same' hash.
        auto hash_double = [](double d) {
            return std::hash<long long>{}(static_cast<long long>(d * 1000000.0));
        };

        std::size_t h1 = hash_double(p.x);
        std::size_t h2 = hash_double(p.y);

        // Combine hashes
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

struct PointEqual
{
    bool operator()(const Point& a, const Point& b) const
    {
        return std::abs(a.x - b.x) < 1e-6 &&
            std::abs(a.y - b.y) < 1e-6;
    }
};


class point_hash
{
};

