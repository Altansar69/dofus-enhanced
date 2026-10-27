#pragma once
void InstallNotebookPanel();
void OpenNotebookPanel();
void TickNotebookPanel();
// Checks the notebook file and the map pins of the open panel now (it also
// does every half second).
void CheckNotebookPanel();
void RestoreNotebookPanel();
// Off-world screens: closes the notebook as the game closes its windows; an
// unsaved draft comes back in the form at the next opening.
void CloseNotebookPanelOffWorld();
bool NotebookPanelCleanupPending();
// True while the open notebook holds edits not saved yet (a layout applied
// then neither closes it nor changes its version: no draft is dropped).
bool NotebookPanelEditing();
