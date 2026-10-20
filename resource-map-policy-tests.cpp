#include "resource-map-policy.hpp"
#include "resource-map-icons.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <tuple>
using namespace resourceMap;
void Check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
    const Resource catalog[]{{303,2,1,303,"Frêne"},{421,26,1,421,"Ortie"}};
    // Sorted by world,x,y,itemId,mapId. Includes an intentionally duplicated row.
    const Placement data[]{{1,1,0,0,303,4,false},{1,1,0,0,303,4,false},{2,1,0,0,303,2,true},
        {1,1,0,0,421,3,false},{3,1,1,1,303,8,false},{4,2,0,0,303,20,false}};
    std::vector<int> selected{303,421};
    auto r=Query(catalog,data,selected,1,{0,0,0,0});
    Check(r.cells.size()==1 && r.cells[0].counts.size()==2,"Selected resources must stay separate");
    Check(r.cells[0].counts[0].count==6 && r.cells[0].counts[0].mapCount==2 &&
        r.cells[0].counts[0].interiorCount==2,"Map dedup and interior provenance");
    Check(r.cells[0].counts[1].count==3,"Independent resource count");
    Check(Describe(catalog,r.cells[0]).find("Frêne : 6")!=std::string::npos,"Count tooltip names");
    r=Query(catalog,data,selected,2,{-1,-1,1,1});Check(r.cells.size()==1 && r.cells[0].counts[0].count==20,"World identities cannot mix");
    r=Query(catalog,data,selected,1,{1,1,1,1});Check(r.cells.size()==1&&r.cells[0].x==1,"Inclusive visible bounds");
    selected={421};r=Query(catalog,data,selected,1,{-5,-5,5,5});Check(r.cells.size()==1&&r.cells[0].counts.size()==1,"Deselection removes previous resource");
    selected={421,303};Check(!ValidSelection(selected)&&Query(catalog,data,selected,1,{-5,-5,5,5}).cells.empty(),"Unsorted selection fails closed");
    selected={303,303};Check(!ValidSelection(selected),"Duplicate selection rejected");
    selected={0};Check(!ValidSelection(selected),"Zero id rejected");
    selected={1000001};Check(!ValidSelection(selected),"ID bounds");
    selected.resize(129);Check(!ValidSelection(selected),"Selection budget");
    selected={999};Check(Query(catalog,data,selected,1,{-5,-5,5,5}).cells.empty(),"Unknown resource has no invented placement");
    selected={303};r=Query(catalog,data,selected,1,{-1,-1,3,3},1);
    Check(r.limited&&r.cells.size()==1&&r.cells[0].x==1,"Density cap must retain central cells and report truncation");
    r=Query(catalog,data,selected,1,{-1,-1,3,3},0);Check(r.limited&&r.cells.empty(),"Zero drawing budget");
    Check(Query(catalog,data,selected,1,{2,0,1,1}).cells.empty(),"Inverted bounds rejected");
    Check(Query(catalog,data,selected,1,{-100001,0,1,1}).cells.empty(),"Oversized coordinates rejected");
    const auto actual=Catalog();const auto placements=Placements();
    for (const auto [job, icon] : std::array<std::pair<int,int>,6>{{
        {1,15026},{2,38017},{24,39024},{26,36642},{28,34009},{36,41294}}})
        Check(RepresentativeResourceIcon(actual,job)==icon,"Job shortcuts show water, ash, iron, nettle, wheat and gudgeon");
    const Resource shuffledIcons[]{{900,24,100,990,"High"},{312,24,1,39024,"Iron"},
        {400,24,1,991,"Tie"},{1,24,0,0,"Missing"},{1,36,0,997,"Other job"}};
    Check(RepresentativeResourceIcon(shuffledIcons,24)==39024,"Representative resource uses level then item ID, independent of catalog order");
    Check(RepresentativeResourceIcon(shuffledIcons,999)==0&&RepresentativeResourceIcon({},24)==0,"Unknown or empty categories have no invented icon");
    Check(actual.size()==86,"Complete DofusDB standard harvest catalog");
    Check(std::count_if(actual.begin(),actual.end(),[](const Resource& r){return r.jobId==24;})==16,"All sixteen ores selectable");
    std::set<int> covered;std::set<std::pair<int64_t,int>> unique;
    auto key=[](const Placement& p){return std::tuple(p.world,p.x,p.y,p.itemId,p.mapId);};
    Check(std::is_sorted(placements.begin(),placements.end(),[&](const Placement& a,const Placement& b){return key(a)<key(b);}),"Generated data must support spatial queries");
    for(const auto& p:placements){
        Check(LookupResource(actual,p.itemId)&&p.count>0&&p.count<=560,"Source references a valid resource");
        Check(unique.emplace(p.mapId,p.itemId).second,"No duplicate map/item counts");
        covered.insert(p.itemId);
    }
    Check(covered.size()==actual.size(),"Every selected resource has recorded positions");
    for(int id:{23824,23825,23902,23903,27375})Check(LookupResource(actual,id)&&covered.contains(id),"Previously missing endgame harvest resources retained");
    const std::vector<int> crystals{23902,23903};
    r=Query(actual,placements,crystals,28,{-100,-100,100,100});
    Check(!r.cells.empty(),"Pandala crystals appear in their own world");
    std::cout<<"ResourceMapPolicyTests: PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
