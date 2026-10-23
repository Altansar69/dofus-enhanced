#include "maps/resource-map-data.hpp"
#include "maps/resource-map-generated.hpp"
namespace resourceMap {
std::span<const Resource> Catalog(){return generated::Resources;}
std::span<const Placement> Placements(){return generated::Nodes;}
}
