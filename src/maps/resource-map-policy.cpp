#include "maps/resource-map-policy.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <sstream>

namespace resourceMap {
const Resource* LookupResource(std::span<const Resource> catalog,int itemId) {
    for (const auto& item : catalog) if (item.itemId == itemId) return &item;
    return nullptr;
}
bool ValidSelection(std::span<const int> selection) {
    if (selection.size() > 128) return false;
    int previous=0;
    for (int item : selection) {
        if (item<=previous || item>1000000) return false;
        previous=item;
    }
    return true;
}
Result Query(std::span<const Resource> catalog,std::span<const Placement> data,
    std::span<const int> selected,int world,Bounds bounds,size_t maxCells) {
    Result result;
    if (world<0 || !ValidSelection(selected) || selected.empty() ||
        bounds.minX>bounds.maxX || bounds.minY>bounds.maxY || bounds.minX < -100000 ||
        bounds.minY < -100000 || bounds.maxX>100000 || bounds.maxY>100000) return result;
    std::map<std::pair<int,int>,Cell> cells;
    std::set<std::pair<int64_t,int>> seen;
    const auto start=std::lower_bound(data.begin(),data.end(),std::pair{world,bounds.minX},
        [](const Placement& a,const auto& b){return std::pair{a.world,a.x}<b;});
    for (auto it=start;it!=data.end() && it->world==world && it->x<=bounds.maxX;++it) {
        const auto& p=*it;
        if (p.y<bounds.minY || p.y>bounds.maxY || p.mapId<=0 || p.count<1 || p.count>560 ||
            !std::binary_search(selected.begin(),selected.end(),p.itemId) || !LookupResource(catalog,p.itemId) ||
            !seen.emplace(p.mapId,p.itemId).second) continue;
        auto& cell=cells[{p.x,p.y}];cell.x=p.x;cell.y=p.y;
        auto entry=std::find_if(cell.counts.begin(),cell.counts.end(),[&](const Count& c){return c.itemId==p.itemId;});
        if (entry==cell.counts.end()) {cell.counts.push_back({p.itemId});entry=std::prev(cell.counts.end());}
        // A malformed source cannot overflow the displayed count.
        if (entry->count>1000000-p.count) continue;
        entry->count+=p.count;entry->interiorCount+=p.interior?p.count:0;++entry->mapCount;
    }
    for (auto& [key,cell] : cells) {
        std::sort(cell.counts.begin(),cell.counts.end(),[](const Count& a,const Count& b){return a.itemId<b.itemId;});
        result.cells.push_back(std::move(cell));
    }
    maxCells=std::min<size_t>(maxCells,1024);
    if (result.cells.size()>maxCells) {
        const int64_t cx=static_cast<int64_t>(bounds.minX)+bounds.maxX;
        const int64_t cy=static_cast<int64_t>(bounds.minY)+bounds.maxY;
        auto distance=[&](const Cell& c){auto x=2LL*c.x-cx,y=2LL*c.y-cy;return x*x+y*y;};
        std::stable_sort(result.cells.begin(),result.cells.end(),[&](const Cell& a,const Cell& b){return distance(a)<distance(b);});
        result.cells.resize(maxCells);result.limited=true;
        std::sort(result.cells.begin(),result.cells.end(),[](const Cell& a,const Cell& b){return std::pair{a.x,a.y}<std::pair{b.x,b.y};});
    }
    return result;
}
std::string Describe(std::span<const Resource> catalog,const Cell& cell) {
    std::ostringstream text;text<<'['<<cell.x<<','<<cell.y<<"] — Points de récolte recensés";
    for (const auto& c : cell.counts) {
        const auto* item=LookupResource(catalog,c.itemId);if(!item)continue;
        text<<'\n'<<item->name<<" : "<<c.count;
        if(c.interiorCount)text<<" (dont "<<c.interiorCount<<" en intérieur/souterrain)";
    }
    text<<"\nLa disponibilité après récolte n’est pas connue.";
    return text.str();
}
}
