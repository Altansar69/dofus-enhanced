#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace jobsxp {
inline constexpr int PageSize=6, MaximumCrafts=99999;
struct Query {
    int job=0, level=0, skip=0;
    bool operator==(const Query&) const = default;
};
struct Ingredient { int id=0, quantity=0; std::string name; };
struct Recipe {
    int id=0, level=0, icon=0;
    double ratio=1;
    std::string name;
    std::vector<Ingredient> ingredients;
};
struct Page { int total=0; std::vector<Recipe> recipes; };
struct Simulation { int count=0; int64_t xp=0; bool reached=false; };
bool Valid(const Query&);
bool Number(const std::string&,int minimum,int maximum,int&);
int Level(int64_t xp);
int64_t Experience(int level);
int CraftXp(const Recipe&,int level,int coefficient=100,bool epic=false);
Simulation Simulate(const Recipe&,int64_t xp,int target,int coefficient=100,bool epic=false);
std::string Ingredients(const Recipe&,int count);
std::wstring RequestPath(const Query&);
Page ParsePage(const std::string&,const Query&);
Page Fetch(const Query&,const std::atomic_bool& cancelled);
// A result belongs only to the requesting session/selection, even after reopening.
bool AcceptResult(uint64_t currentGeneration,uint64_t resultGeneration,
    const Query& current,const Query& result,bool cancelled);
}
