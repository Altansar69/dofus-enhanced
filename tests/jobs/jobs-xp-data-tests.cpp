#include "jobs/jobs-xp-data.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
using namespace jobsxp;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void Reject(F fn){bool bad=false;try{fn();}catch(...){bad=true;}Check(bad,"Invalid data accepted");}
int main(int argc,char**){
    try{
        if(argc>1){std::atomic_bool cancel{false};auto page=Fetch({28,50,0},cancel);
            Check(!page.recipes.empty(),"Live API returned no recipes");
            std::cout<<"LIVE anonymous recipes: "<<page.recipes.size()<<" of "<<page.total<<"\n";return 0;}
        Check(Experience(1)==0&&Experience(200)==398000,"XP thresholds");
        for(int i=1;i<=200;++i){Check(Level(Experience(i))==i,"XP exact level");
            if(i>1)Check(Level(Experience(i)-1)==i-1,"XP below threshold");}
        Check(Level(INT64_MAX)==200,"Huge XP bounded");
        Reject([]{Level(-1);});Reject([]{Experience(201);});
        Recipe bread{16431,50,33074,.05,"Pain",{{533,5,"Avoine"},{428,1,"Eau"}}};
        Check(CraftXp(bread,50)==50,"Type ratio used");
        Check(CraftXp(bread,51,125)==56,"Double floor XP");
        Check(CraftXp(bread,50,100,true)==150,"Epic multiplier");
        Check(CraftXp(bread,49)==0&&CraftXp(bread,151)==0,"Unavailable/obsolete recipe");
        auto plan=Simulate(bread,Experience(50),51);
        Check(plan.count==20&&plan.reached&&plan.xp==Experience(51),"Exact target craft count");
        auto beyond=Simulate(bread,Experience(50),52);
        Check(beyond.count>40&&beyond.reached,"Recalculate XP at each crossed level");
        Check(Simulate(bread,Experience(151),152).count==0,"Unreachable target terminates");
        Check(Simulate(bread,Experience(50),49).count==0,"Already reached target");
        Check(Ingredients(bread,20)=="100 × Avoine · 20 × Eau","Ingredients quantity scaling");
        const Query q{28,50,0};
        Check(RequestPath(q).find(L"resultLevel%5B%24lte%5D=50")!=std::wstring::npos,"Encoded API query");
        Reject([]{RequestPath({0,50,0});});
        auto j=nlohmann::json::parse(R"({"total":1,"skip":0,"data":[{
          "jobId":28,"resultId":16431,"resultLevel":50,"resultName":{"fr":"Pain"},
          "result":{"iconId":33074,"craftXpRatio":-1},"resultType":{"craftXpRatio":5},
          "ingredientIds":[533,428],"quantities":[5,1],
          "ingredients":[{"id":428,"name":{"fr":"Eau"}},{"id":533,"name":{"fr":"Avoine"}}]}]})");
        auto parsed=ParsePage(j.dump(),q);
        Check(parsed.recipes.size()==1&&parsed.recipes[0].ratio==.05,"Ratio fallback");
        Check(parsed.recipes[0].ingredients[0].name=="Avoine"&&parsed.recipes[0].ingredients[0].quantity==5,"Match ingredients by ID");
        auto modified=j;modified["data"][0]["result"]["craftXpRatio"]=10;
        Check(ParsePage(modified.dump(),q).recipes[0].ratio==.1,"Item ratio has precedence");
        modified=j;modified["data"][0]["jobId"]=30;Reject([&]{ParsePage(modified.dump(),q);});
        modified=j;modified["data"][0]["ingredients"].erase(1);Reject([&]{ParsePage(modified.dump(),q);});
        modified=j;modified["data"][0]["quantities"][0]=-2;Reject([&]{ParsePage(modified.dump(),q);});
        modified=j;modified["skip"]=6;Reject([&]{ParsePage(modified.dump(),q);});
        modified=j;modified["data"][0]["resultName"]["fr"]="<b>Injected";Reject([&]{ParsePage(modified.dump(),q);});
        Check(AcceptResult(4,4,q,q,false),"Current response accepted");
        Check(!AcceptResult(5,4,q,q,false),"Closed/reopened panel rejects stale response");
        Check(!AcceptResult(4,4,{28,51,0},q,false),"Changed level rejects stale response");
        Check(!AcceptResult(4,4,q,q,true),"Cancelled response rejected");
        int number=0;Check(Number("200",1,200,number)&&number==200,"Target parsing");
        Check(!Number("200x",1,200,number)&&!Number("-1",1,200,number),"Invalid target rejected");
        std::cout<<"PASS: job XP, ratios, target simulation, ingredient mapping, API validation and stale response policy\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
