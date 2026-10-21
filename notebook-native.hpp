#pragma once
#include "notebook-data.hpp"
namespace notebook {
void InstallNative();
Entry CurrentPosition();
// The game's world maps (id and localized name), ascending id; never empty.
struct World { int id = 0; std::string name; };
std::vector<World> Worlds();
void Travel(const Entry& entry);
void PrepareMessage(const std::string& text);
// Sends through the game's own chat entry point; channel 0 keeps the chat's
// current channel, otherwise a notebook::Channels value.
void SendChatMessage(const std::string& text, int channel);
void Copy(const std::string& text);
}
