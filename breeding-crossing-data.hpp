#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace breedingCrossing {
struct Crossing { int first, second, generation, gestationHours; std::string name; };
const std::array<const char*,11>& Colors();
const std::vector<Crossing>& Crossings();
using Selection = std::array<bool,66>;
Selection Parse(const std::string&);
std::string Serialize(const Selection&);
Selection Read(const std::filesystem::path&);
// Reloads under a cross-process lock, changes one cell, and atomically replaces
// the file. Independent clients cannot overwrite one another's other cells.
Selection Set(const std::filesystem::path&, size_t index, bool checked);
std::filesystem::path StoragePath(const std::filesystem::path& settingsPath);
struct Progress { int completed=0,total=0; };
Progress Completion(const Selection&, int generation=0);
}
