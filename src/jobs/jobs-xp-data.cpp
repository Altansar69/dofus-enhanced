#include "jobs/jobs-xp-data.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace jobsxp {
namespace {
int Integer(const nlohmann::json& value,int lo,int hi) {
    if(!value.is_number_integer()) throw std::runtime_error("Nombre DofusDB invalide.");
    const auto n=value.get<int64_t>();
    if(n<lo||n>hi) throw std::runtime_error("Nombre DofusDB hors limites.");
    return static_cast<int>(n);
}
std::string Name(const nlohmann::json& value) {
    const auto name=value.at("fr").get<std::string>();
    if(name.empty()||name.size()>240||std::any_of(name.begin(),name.end(),[](unsigned char ch){
        return ch<32||ch=='<'||ch=='>';
    })) throw std::runtime_error("Libellé DofusDB invalide.");
    return name;
}
}
bool Valid(const Query& q) { return q.job>0&&q.job<=1000&&q.level>=1&&q.level<=200&&q.skip>=0&&q.skip<=10000; }
bool Number(const std::string& text,int minimum,int maximum,int& result) {
    int value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||value<minimum||value>maximum) return false;
    result=value; return true;
}
int Level(int64_t xp) {
    if(xp<0) throw std::runtime_error("Expérience métier invalide.");
    return std::clamp(static_cast<int>(std::floor((std::sqrt(1+.4*static_cast<double>(std::min<int64_t>(xp,398000)))+1)/2)),1,200);
}
int64_t Experience(int level) {
    if(level<1||level>200) throw std::runtime_error("Niveau métier invalide.");
    return 10LL*level*(level-1);
}
int CraftXp(const Recipe& recipe,int level,int coefficient,bool epic) {
    if(level<1||level>200||recipe.level<1||recipe.level>level||level-recipe.level>100||
        coefficient<1||coefficient>600||!std::isfinite(recipe.ratio)||recipe.ratio<0||recipe.ratio>100) return 0;
    const double base=20.*recipe.level*(epic?3:1)/(std::pow(level-recipe.level,1.1)/10+1);
    return static_cast<int>(std::floor(std::floor(base*recipe.ratio)*(coefficient/100.)));
}
Simulation Simulate(const Recipe& recipe,int64_t xp,int target,int coefficient,bool epic) {
    const auto goal=Experience(target);
    if(xp<0||xp>INT64_MAX/2) throw std::runtime_error("Expérience métier invalide.");
    Simulation result{0,xp,xp>=goal};
    while(!result.reached&&result.count<MaximumCrafts) {
        const int level=Level(result.xp);
        const int gain=CraftXp(recipe,level,coefficient,epic);
        if(gain<=0) break;
        // XP per craft stays constant until crossing a level. Batch that entire
        // interval, preserving the final craft's overshoot and double rounding.
        // The native UI therefore needs at most 199 iterations, not 99999.
        const auto boundary=std::min(goal,Experience(std::min(200,level+1)));
        const auto needed=(boundary-result.xp+gain-1)/gain;
        const auto count=std::min<int64_t>(needed,MaximumCrafts-result.count);
        if(count<=0)break;
        result.xp+=count*gain;result.count+=static_cast<int>(count);result.reached=result.xp>=goal;
    }
    return result;
}
std::string Ingredients(const Recipe& recipe,int count) {
    if(count<1||count>MaximumCrafts) return {};
    std::string text;
    for(const auto& ingredient:recipe.ingredients) {
        if(!text.empty())text+=" · ";
        text+=std::to_string(static_cast<int64_t>(ingredient.quantity)*count)+" × "+ingredient.name;
    }
    return text;
}
std::wstring RequestPath(const Query& q) {
    if(!Valid(q)) throw std::runtime_error("Sélection métier invalide.");
    return L"/recipes?jobId="+std::to_wstring(q.job)+L"&resultLevel%5B%24lte%5D="+std::to_wstring(q.level)+
        L"&%24sort%5BresultLevel%5D=-1&%24skip="+std::to_wstring(q.skip)+L"&%24limit="+
        std::to_wstring(PageSize)+L"&lang=fr";
}
Page ParsePage(const std::string& body,const Query& query) {
    if(!Valid(query)||body.size()>4*1024*1024) throw std::runtime_error("Réponse DofusDB trop volumineuse.");
    const auto json=nlohmann::json::parse(body);
    Page page; page.total=Integer(json.at("total"),0,10000);
    if(Integer(json.at("skip"),0,10000)!=query.skip) throw std::runtime_error("Pagination DofusDB invalide.");
    const auto& rows=json.at("data");
    if(!rows.is_array()||rows.size()>PageSize||query.skip+rows.size()>static_cast<size_t>(page.total)||
        (rows.empty()&&query.skip<page.total)) throw std::runtime_error("Recettes DofusDB incohérentes.");
    std::set<int> ids;
    for(const auto& row:rows) {
        Recipe r; r.id=Integer(row.at("resultId"),1,1000000); r.level=Integer(row.at("resultLevel"),1,query.level);
        if(Integer(row.at("jobId"),1,1000)!=query.job||!ids.insert(r.id).second)
            throw std::runtime_error("Recette d'un autre métier ou dupliquée.");
        r.name=Name(row.at("resultName"));
        const auto& item=row.at("result"); r.icon=Integer(item.at("iconId"),1,1000000);
        int ratio=Integer(item.at("craftXpRatio"),-1,10000);
        if(ratio<0&&row.contains("resultType")) ratio=Integer(row.at("resultType").at("craftXpRatio"),-1,10000);
        r.ratio=ratio<0?1:ratio/100.;
        const auto& ingredientIds=row.at("ingredientIds");const auto& quantities=row.at("quantities");
        const auto& ingredients=row.at("ingredients");
        if(!ingredientIds.is_array()||!quantities.is_array()||!ingredients.is_array()||
            ingredientIds.empty()||ingredientIds.size()>32||ingredientIds.size()!=quantities.size()||ingredients.size()>32)
            throw std::runtime_error("Ingrédients DofusDB invalides.");
        std::map<int,std::string> names;
        for(const auto& ingredient:ingredients) {
            if(!names.emplace(Integer(ingredient.at("id"),1,1000000),Name(ingredient.at("name"))).second)
                throw std::runtime_error("Ingrédient DofusDB dupliqué.");
        }
        std::set<int> used;
        for(size_t i=0;i<ingredientIds.size();++i) {
            const int id=Integer(ingredientIds[i],1,1000000);
            if(!used.insert(id).second||!names.contains(id)) throw std::runtime_error("Ingrédient DofusDB absent.");
            r.ingredients.push_back({id,Integer(quantities[i],1,1000000),names.at(id)});
        }
        page.recipes.push_back(std::move(r));
    }
    return page;
}
bool AcceptResult(uint64_t generation,uint64_t resultGeneration,const Query& current,const Query& result,bool cancelled) {
    return !cancelled&&generation==resultGeneration&&current==result;
}
}
