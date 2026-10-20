#include "breeding-crossing-data.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace breedingCrossing;
void Check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
int main(){
    auto folder=std::filesystem::temp_directory_path()/(L"dofus-crossing-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        const auto& rows=Crossings();Check(rows.size()==66,"66 unordered combinations");
        std::set<std::pair<int,int>> pairs;
        for(const auto& row:rows){Check(row.first<=row.second&&row.generation>=1&&row.generation<=10,"Valid unique crossing");
            Check(pairs.insert({row.first,row.second}).second,"No duplicate reversed crossings");
            Check((row.first==row.second)==(row.gestationHours!=0),"Do not invent hybrid gestation from pure-row facts");
        }
        const int totals[]={3,3,2,7,2,11,2,15,2,19};
        Selection selected{};
        Check(Completion(selected).total==66&&Completion(selected).completed==0,"No assumed collection");
        for(int i=1;i<=10;++i)Check(Completion(selected,i).total==totals[i-1],"Generation factual distribution");
        selected[0]=selected[65]=true;Check(Parse(Serialize(selected))==selected,"Persistence round-trip");
        for(auto bad:{"","DofusMiniatures.Crossings.2\n","{ \"done\": true }"}){
            bool failed=false;try{Parse(bad);}catch(...){failed=true;}Check(failed,"Malformed data rejected");
        }
        auto malformed=Serialize(selected);malformed[29]='x';
        bool failed=false;try{Parse(malformed);}catch(...){failed=true;}Check(failed,"Bad boolean rejected");
        std::filesystem::create_directory(folder);
        auto path=StoragePath(folder/"preferences.ini");Check(Read(path)==Selection{},"Missing file starts empty");
        auto clientA=Set(path,0,true),clientB=Set(path,65,true);
        Check(clientB[0]&&clientB[65],"Independent writes merge other client cells");
        auto after=Set(path,0,false);Check(!after[0]&&after[65],"Uncheck preserves unrelated cell");
        Check(Read(path)==after,"On-disk content published");
        failed=false;try{Set(path,66,true);}catch(...){failed=true;}Check(failed&&Read(path)==after,"Invalid index preserves file");
        {std::ofstream stream(path);stream<<"corrupt";}
        failed=false;try{Set(path,0,true);}catch(...){failed=true;}Check(failed,"Corrupt preferences never overwritten");
        std::ifstream saved(path);std::string text;std::getline(saved,text);Check(text=="corrupt","Corrupt source retained for repair");saved.close();
        // Only this test-owned directory, resolved before recursive cleanup.
        auto resolved=std::filesystem::weakly_canonical(folder);
        Check(resolved.parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()),"Test cleanup boundary");
        std::filesystem::remove_all(resolved);
        std::cout<<"PASS: 66 combinations, generation facts, strict persistence, merged atomic saves\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
