#pragma once
#include "resource-map-data.hpp"
#include <vector>
#include <string>

namespace resourceMap {
struct Bounds { int minX, minY, maxX, maxY; };
struct Count { int itemId=0, count=0, interiorCount=0, mapCount=0; };
struct Cell { int x=0,y=0; std::vector<Count> counts; };
struct Result { std::vector<Cell> cells; bool limited=false; };
const Resource* LookupResource(std::span<const Resource> catalog, int itemId);
bool ValidSelection(std::span<const int> selection);
// Inclusive map coordinates. Data is sorted by (world,x,y,itemId,mapId).
// Cells nearest the viewport centre take priority if density exceeds budget.
Result Query(std::span<const Resource> catalog, std::span<const Placement> data,
    std::span<const int> selected, int world, Bounds bounds, size_t maxCells=384);
std::string Describe(std::span<const Resource> catalog,const Cell& cell);
}
