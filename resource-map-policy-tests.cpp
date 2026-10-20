#include "resource-map-policy.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
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
    std::cout<<"ResourceMapPolicyTests: PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
