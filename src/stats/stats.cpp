#include "stats/stats.hpp"
// Starting point (r115 prep): the store, its file and the options section
// replace these no-op bodies.

namespace stats {
void Add(int64_t, Counter, int64_t) {}
void Raise(int64_t, Record, int64_t) {}
void AddTo(int64_t, Table, int, int64_t) {}
void Name(int64_t, const std::string&) {}
bool Counting() { return false; }
}

void InstallStats() {}
void TickStats() {}
void FlushStats() {}
