#pragma once
void InstallNotebookPanel();
void OpenNotebookPanel();
void TickNotebookPanel();
// Checks the notebook file and the map pins of the open panel now (it also
// does every half second).
void CheckNotebookPanel();
void RestoreNotebookPanel();
bool NotebookPanelCleanupPending();
