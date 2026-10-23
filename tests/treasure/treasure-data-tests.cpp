#include "treasure/treasure-data.hpp"
#include <iostream>
#include <stdexcept>

void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void Reject(F f) { bool rejected=false;try{f();}catch(...){rejected=true;}Check(rejected,"Invalid provider data accepted"); }
int main(int argc,char**) {
    try {
        if(argc>1) {
            std::atomic_bool cancel{false};
            auto clues=treasure::Fetch({-25,-36,0},cancel);
            Check(!clues.empty(),"Live provider returned no clues");
            std::cout<<"LIVE API: "<<clues.size()<<" distinct clues; anonymous HTTPS; no game access\n";
            return 0;
        }
        int coordinate=42;
        Check(treasure::Coordinate(" -51 ",coordinate)&&coordinate==-51,"Signed coordinate");
        for(const auto* text:{"","-","1.5","5&x=2","3z","999999999999","1001","-1001"})
            Check(!treasure::Coordinate(text,coordinate),"Invalid coordinate accepted");
        const treasure::Query east{-25,-36,0};
        Check(treasure::RequestPath(east,0).find(L"x=-25&y=-36&direction=0")!=std::wstring::npos,"Direction encoding");
        Reject([]{treasure::RequestPath({0,0,1},0);});
        const std::string page=R"({"total":2,"skip":0,"data":[
          {"posX":-20,"posY":-36,"distance":5,"pois":[{"id":1,"name":{"fr":"Crâne décoré"}}]},
          {"posX":-23,"posY":-36,"distance":2,"pois":[{"id":1,"name":{"fr":"Crâne décoré"}},{"id":2,"name":{"fr":"Épée"}}]}]})";
        auto parsed=treasure::ParsePage(page,east,0);
        auto clues=treasure::Nearest(parsed.clues);
        Check(clues.size()==2 && clues[0].id==1 && clues[0].x==-23,"Keep nearest occurrence, not provider order");
        Check(treasure::Filter(clues,"CRANE DECORE").size()==1,"Accent/case-insensitive clue search");
        Check(treasure::Filter(clues,"epee").size()==1,"French uppercase accented clue");
        Check(treasure::Filter(clues,"phorreur").empty(),"Unknown/mobile clue must not invent coordinates");
        Check(treasure::ParsePage(R"({"total":0,"skip":0,"data":[]})",east,0).clues.empty(),"Empty results");
        Reject([&]{treasure::ParsePage(page,east,1);});
        Reject([&]{treasure::ParsePage(page,{-25,-36,4},0);});
        Reject([&]{treasure::ParsePage("not json",east,0);});
        Reject([&]{treasure::ParsePage(R"({"total":2,"skip":0,"data":[]})",east,0);});
        Reject([&]{treasure::ParsePage(R"({"total":999999,"skip":0,"data":[]})",east,0);});
        Reject([&]{treasure::ParsePage(std::string(1024*1024+1,'x'),east,0);});
        std::cout<<"PASS: input, pagination, direction, nearest match, French search and malformed provider responses\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
