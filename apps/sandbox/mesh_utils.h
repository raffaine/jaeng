#pragma once

#include <vector>
#include <cstdint>
#include "mesh/imeshsys.h"

// The types are in jaeng namespace
using jaeng::RAWFormatHeader;
using jaeng::RAWFormatVertex;

std::vector<uint8_t> createQuadMeshBinary();
std::vector<uint8_t> createCubeMeshBinary();
