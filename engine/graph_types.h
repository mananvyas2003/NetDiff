#pragma once

#include <cstdint>

using VertexID = uint32_t;
using EdgeID = uint32_t;
using NetID = uint32_t;

constexpr VertexID INVALID_VERTEX =
static_cast<VertexID>(-1);

constexpr EdgeID INVALID_EDGE =
static_cast<EdgeID>(-1);

constexpr NetID INVALID_NET =
static_cast<NetID>(-1);