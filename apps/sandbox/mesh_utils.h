#pragma once

#include <vector>
#include <cstdint>
#include "mesh/imeshsys.h"

// The types are in global namespace
using ::RAWFormatHeader;
using ::RAWFormatVertex;

std::vector<uint8_t> createQuadMeshBinary();
std::vector<uint8_t> createCubeMeshBinary();
