#pragma once
#include <filesystem>

// « À propos » › « Dossier du mod » (1.20.1): opens Windows Explorer on the
// running module's own folder (Module::folder, the DofusMiniatures-3.7.11.11-rN
// folder that holds DofusMiniatures.dll and module.log). Returns at once: the
// folder check and ShellExecuteW run on a short-lived worker thread, one at a
// time, with no IL2CPP call there. Only that path is passed, never arguments.
void OpenModFolder(const std::filesystem::path& folder);
