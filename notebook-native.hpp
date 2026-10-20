#pragma once
#include "notebook-data.hpp"
namespace notebook {
void InstallNative();
Entry CurrentPosition();
void Travel(const Entry& entry);
void PrepareMessage(const std::string& text);
void Copy(const std::string& text);
}
