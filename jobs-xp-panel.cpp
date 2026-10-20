#include "jobs-xp-panel.hpp"
#include "jobs-xp-data.hpp"
#include "module.hpp"
#include "soul-panel.hpp"
#include <array>
#include <algorithm>
#include <thread>
#include <cmath>

namespace {
using namespace managed;
constexpr const char* Ui="UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace="UnityEngine.UIElements";
constexpr const char* Figma="Core.UILogic.Components.Figma";
struct Request {
    jobsxp::Query query;
    uint64_t generation=0;
    std::atomic_bool cancelled{false},done{false};
    jobsxp::Page page;
    std::string error;
};
struct State {
    Ref owner,handler,root,tabs,content,tab,pane,cached,compositor;
    Ref recipes,resources,recipeTab,resourceTab,tabGroup,recipesDisplay,resourcesDisplay;
    Ref title,status,target,coefficient,epic,refresh,previous,next,details;
    std::array<Ref,jobsxp::PageSize> rows;
    jobsxp::Page page;
    jobsxp::Query query;
    std::shared_ptr<Request> request;
    int64_t xp=0;
    int selected=-1,lastTarget=0,lastCoefficient=0;
    float tabHeight=0;
    bool lastEpic=false,active=false,cleanup=false,havePage=false,requested=false,recipeWasOn=false,resourceWasOn=false;
    ULONGLONG nextPoll=0,nextRequest=0;
    uint64_t generation=0;
} state;
Ref pending;
std::shared_ptr<Request> worker;
uint64_t generation=0;
bool installed=false;
void(*originalEnable)(Obj,Method)=nullptr;
void(*originalDisable)(Obj,Method)=nullptr;
void(*originalSwitch)(Obj,int,Method)=nullptr;

bool Session() {return module&&module->ready&&module->servicesReady&&!module->stopped&&!module->stopRequested;}
Obj Style(Obj element,const char* property) {
    Ref style(api.Call(element,"get_style"));
    return api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+property).c_str());
}
void WriteStyle(Obj element,const char* property,Obj box) {
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+property).c_str(),{api.object_unbox(box)});
}
void Enum(Obj element,const char* property,int value) {
    Ref box(Style(element,property));api.Call(box.get(),"set_value",{&value});WriteStyle(element,property,box.get());
}
void Float(Obj element,const char* property,float value) {
    Ref box(Style(element,property));api.Call(box.get(),"set_value",{&value});WriteStyle(element,property,box.get());
}
void Pixels(Obj element,const char* property,float value) {
    int unit=0;Ref length(api.New(api.Class(Ui,UiSpace,"Length"),{&value,&unit}));
    Ref box(Style(element,property));api.Call(box.get(),"set_value",{api.object_unbox(length.get())});WriteStyle(element,property,box.get());
}
void Color(Obj element,const char* property,float r,float g,float b,float a=1) {
    Ref color(api.New(api.Class("UnityEngine.CoreModule.dll","UnityEngine","Color"),{&r,&g,&b,&a}));
    Ref box(Style(element,property));api.Call(box.get(),"set_value",{api.object_unbox(color.get())});WriteStyle(element,property,box.get());
}
void Text(Obj element,const std::string& text,const char* setter="set_text") {
    Ref value(api.string_new(text.c_str()));api.Call(element,setter,{value.get()});
}
void Add(Obj parent,Obj child) {api.Call(parent,"Add",{child});}
void Enabled(Obj element,bool value){api.Call(element,"SetEnabled",{&value});}
Obj Label(Obj parent,const std::string& text) {
    Ref label(api.New(api.Class(Ui,UiSpace,"Label")));Text(label.get(),text);
    Enum(label.get(),"whiteSpace",0);Pixels(label.get(),"fontSize",14);Pixels(label.get(),"marginBottom",6);
    Color(label.get(),"color",.91f,.91f,.86f);
    Add(parent,label.get());return label.get();
}
Obj Row(Obj parent) {
    Ref row(api.New(api.Class(Ui,UiSpace,"VisualElement")));Enum(row.get(),"flexDirection",2);
    Enum(row.get(),"alignItems",2);Enum(row.get(),"flexWrap",1);
    Pixels(row.get(),"marginBottom",8);Add(parent,row.get());return row.get();
}
Obj Button(Obj parent,const std::string& text) {
    Ref item(api.New(api.Class("Core.dll",Figma,"ListElementBasic")));int kind=1;
    api.Call(item.get(),"set_listElementStyle",{&kind});Text(item.get(),text);
    Pixels(item.get(),"minHeight",30);Pixels(item.get(),"minWidth",0);
    Add(parent,item.get());return item.get();
}
Obj Input(Obj parent,const std::string& value,const char* tooltip,int maxLength) {
    Ref input(api.New(api.Class("Core.dll",Figma,"TextInput")));
    Text(input.get(),value);Text(input.get(),tooltip,"set_tooltip");api.Call(input.get(),"set_maxLength",{&maxLength});
    Pixels(input.get(),"width",88);Pixels(input.get(),"minWidth",0);Pixels(input.get(),"marginRight",12);Add(parent,input.get());return input.get();
}
void TabValue(Obj tab,bool value){api.Call(tab,"SetValueWithoutNotify",{&value});}
Obj Section(Obj parent,const char* title) {
    Ref card(api.New(api.Class(Ui,UiSpace,"VisualElement")));
    for(const char* property:{"paddingLeft","paddingRight","paddingTop","paddingBottom"})Pixels(card.get(),property,12);
    Pixels(card.get(),"marginBottom",12);Color(card.get(),"backgroundColor",.14f,.15f,.13f,.88f);
    Add(parent,card.get());Ref heading(Label(card.get(),title));
    Pixels(heading.get(),"fontSize",16);Enum(heading.get(),"unityFontStyleAndWeight",1);
    Color(heading.get(),"color",.78f,.88f,.44f);Pixels(heading.get(),"marginBottom",10);
    return card.get();
}
void MatchTabHeight() {
    // Use the sibling's resolved theme geometry after layout; never a guessed pixel offset.
    Ref resolved(api.Call(state.resourceTab.get(),"get_resolvedStyle"));
    const float height=api.Value<float>(api.Call(resolved.get(),"get_height"));
    if(!std::isfinite(height)||height<16||height>128||std::abs(height-state.tabHeight)<.01f)return;
    state.tabHeight=height;
    for(const char* property:{"height","minHeight","maxHeight"})Pixels(state.tab.get(),property,height);
}
bool Current() {
    return Session()&&state.pane.get()&&!state.cleanup&&state.cached.get()==module->cached.get()&&
        state.compositor.get()==module->compositor.get()&&
        api.Field(state.owner.get(),"m_jobCraftAndSkillsHandler")==state.handler.get()&&
        api.Call(state.pane.get(),"get_parent")==state.content.get()&&api.Call(state.tab.get(),"get_parent")==state.tabs.get()&&
        api.Call(state.root.get(),"get_panel");
}
void Cancel() {
    if(state.request)state.request->cancelled.store(true);
    state.request.reset();state.requested=false;
}
void Leave(bool restoreSelection=true) {
    if(!state.active)return;
    // Restore exact original inline display styles before native tab switching.
    if(state.recipesDisplay.get())WriteStyle(state.recipes.get(),"display",state.recipesDisplay.get());
    if(state.resourcesDisplay.get())WriteStyle(state.resources.get(),"display",state.resourcesDisplay.get());
    if(restoreSelection){
        // Activate the saved native tab first: the group deliberately disallows an empty selection.
        if(state.recipeWasOn)TabValue(state.recipeTab.get(),true);
        else if(state.resourceWasOn)TabValue(state.resourceTab.get(),true);
    }
    Enum(state.pane.get(),"display",1);TabValue(state.tab.get(),false);
    state.recipesDisplay.reset();state.resourcesDisplay.reset();state.active=false;Cancel();
}
void Enter() {
    if(state.active){TabValue(state.tab.get(),true);return;}
    state.recipesDisplay.reset(Style(state.recipes.get(),"display"));
    state.resourcesDisplay.reset(Style(state.resources.get(),"display"));
    state.recipeWasOn=api.Value<bool>(api.Call(state.recipeTab.get(),"get_isOn"));
    state.resourceWasOn=api.Value<bool>(api.Call(state.resourceTab.get(),"get_isOn"));
    state.active=true; // Cleanup owns partially applied changes from here.
    Enum(state.recipes.get(),"display",1);Enum(state.resources.get(),"display",1);
    // Native API temporarily allows an empty group and restores its policy afterwards.
    // SetValueWithoutNotify(false) alone refuses to clear its final selected item.
    api.Call(state.tabGroup.get(),"ForceAllSelectionsOff");TabValue(state.tab.get(),true);
    Enum(state.pane.get(),"display",0);state.nextPoll=0;
}
bool Parameters(int& target,int& coefficient,bool& epic) {
    epic=api.Value<bool>(api.Call(state.epic.get(),"get_value"));
    return jobsxp::Number(api.String(api.Call(state.target.get(),"get_text")),1,200,target)&&
        jobsxp::Number(api.String(api.Call(state.coefficient.get(),"get_text")),1,600,coefficient);
}
void Render() {
    int target=0,coefficient=0;bool epic=false;
    const bool valid=Parameters(target,coefficient,epic);
    for(size_t i=0;i<state.rows.size();++i) {
        const bool show=state.havePage&&i<state.page.recipes.size();
        Enum(state.rows[i].get(),"display",show?0:1);
        if(!show)continue;
        const auto& r=state.page.recipes[i];
        std::string caption=r.name+" · Niv. "+std::to_string(r.level);
        if(valid) {
            const auto plan=jobsxp::Simulate(r,state.xp,target,coefficient,epic);
            caption+="\n"+std::to_string(jobsxp::CraftXp(r,state.query.level,coefficient,epic))+" XP / fabrication";
            caption+=plan.reached?" · "+std::to_string(plan.count)+" fabrications pour le niveau "+std::to_string(target):" · cible non atteignable";
        }
        Text(state.rows[i].get(),caption);Text(state.rows[i].get(),jobsxp::Ingredients(r,1),"set_tooltip");
        Float(state.rows[i].get(),"borderLeftWidth",state.selected==static_cast<int>(i)?4.f:0.f);
        Color(state.rows[i].get(),"borderLeftColor",.64f,.79f,.26f);
    }
    Enabled(state.previous.get(),state.query.skip>0&&!state.request);
    Enabled(state.next.get(),state.havePage&&state.query.skip+jobsxp::PageSize<state.page.total&&!state.request);
    if(!valid)Text(state.details.get(),"Niveau cible : 1 à 200. Coefficient d'XP : 1 à 600 %.");
    else if(state.selected>=0&&state.selected<static_cast<int>(state.page.recipes.size())) {
        const auto& r=state.page.recipes[state.selected];
        const auto plan=jobsxp::Simulate(r,state.xp,target,coefficient,epic);
        std::string text=r.name+"\n";
        if(plan.count==0&&plan.reached)text+="Niveau cible déjà atteint.";
        else {
            text+=(plan.reached?"Pour atteindre la cible : ":"Simulation limitée : ")+std::to_string(plan.count)+
                " fabrication(s), +"+std::to_string(plan.xp-state.xp)+" XP, niveau "+std::to_string(jobsxp::Level(plan.xp))+".";
            if(plan.count)text+="\n"+jobsxp::Ingredients(r,plan.count);
            if(!plan.reached)text+="\nChoisis une autre recette pour continuer.";
        }
        Text(state.details.get(),text);
    } else Text(state.details.get(),"Sélectionne une recette dans la liste ci-dessus : les ingrédients et leurs quantités totales apparaîtront ici.");
    state.lastTarget=target;state.lastCoefficient=coefficient;state.lastEpic=epic;
}
void Search() {
    if(state.request||!jobsxp::Valid(state.query)||worker||GetTickCount64()<state.nextRequest)return;
    auto request=std::make_shared<Request>();request->query=state.query;request->generation=state.generation;
    state.request=worker=request;state.requested=true;state.nextRequest=GetTickCount64()+1500;
    Text(state.status.get(),"Chargement des recettes DofusDB…");
    try {
        std::thread([request] {
            try {request->page=jobsxp::Fetch(request->query,request->cancelled);}
            catch(const std::exception& error){request->error=error.what();}
            catch(...){request->error="DofusDB : erreur de recherche.";}
            request->done.store(true);
        }).detach();
    }catch(...){state.request.reset();worker.reset();state.requested=false;throw;}
}
bool Action(Obj item) {
    const bool owned=item&&(item==state.refresh.get()||item==state.previous.get()||item==state.next.get()||
        std::any_of(state.rows.begin(),state.rows.end(),[&](const Ref& row){return item==row.get();}));
    if(!owned)return false;
    if(!Current()||!state.active)return true;
    if(item==state.refresh.get()) {Cancel();state.havePage=false;Search();Render();return true;}
    if(item==state.previous.get()||item==state.next.get()) {
        if(state.request)return true;
        const int skip=state.query.skip+(item==state.previous.get()?-jobsxp::PageSize:jobsxp::PageSize);
        if(skip<0||skip>=state.page.total)return true;
        state.query.skip=skip;state.selected=-1;state.havePage=false;state.requested=false;
        Render();Search();return true;
    }
    for(size_t i=0;i<state.rows.size();++i)if(item==state.rows[i].get()){
        if(state.havePage&&i<state.page.recipes.size()){state.selected=static_cast<int>(i);Render();}
        return true;
    }
    return false;
}
void Create(Obj owner) {
    state.owner.reset(owner);state.handler.reset(api.Field(owner,"m_jobCraftAndSkillsHandler"));
    if(!state.handler.get()||api.object_get_class(state.handler.get())!=api.Class("Core.dll","Core.UILogic.Job","JobCraftAndSkillsHandler"))
        throw std::runtime_error("Jobs handler unavailable");
    Ref binding(api.Field(state.handler.get(),"m_binding"));
    state.root.reset(api.Call(binding.get(),"GetRootElement"));
    if(!state.root.get()||!api.Call(state.root.get(),"get_panel")){state=State{};return;}
    state.tabs.reset(api.Call(binding.get(),"get_ctr_tabs"));state.content.reset(api.Call(binding.get(),"get_content"));
    state.recipeTab.reset(api.Call(binding.get(),"get_tab_recipe"));state.resourceTab.reset(api.Call(binding.get(),"get_tab_ressources"));
    state.tabGroup.reset(api.Call(state.recipeTab.get(),"get_group"));
    if(!state.tabGroup.get()||state.tabGroup.get()!=api.Call(state.resourceTab.get(),"get_group"))
        throw std::runtime_error("Jobs native tab group unavailable");
    state.recipes.reset(api.Call(binding.get(),"get_recipesUI"));state.resources.reset(api.Call(binding.get(),"get_jobSkillsUI"));
    state.cached.reset(module->cached.get());state.compositor.reset(module->compositor.get());state.generation=++generation;
    state.tab.reset(api.New(api.Class("Core.dll",Figma,"DofusTab")));Text(state.tab.get(),"XP · DofusDB");
    int size=api.Value<int>(api.Call(state.recipeTab.get(),"get_size"));api.Call(state.tab.get(),"set_size",{&size});
    int fontCase=api.Value<int>(api.Call(state.recipeTab.get(),"get_fontCase"));api.Call(state.tab.get(),"set_fontCase",{&fontCase});Text(state.tab.get(),"XP · DofusDB");
    for(const char* property:{"bottom","top","marginTop","marginBottom","marginLeft","alignSelf"}) {
        Ref value(Style(state.resourceTab.get(),property));WriteStyle(state.tab.get(),property,value.get());
    }
    Float(state.tab.get(),"flexGrow",1);Pixels(state.tab.get(),"minWidth",0);
    Text(state.tab.get(),"Recettes et simulation d'expérience du métier sélectionné.","set_tooltip");
    int vertical=0;state.pane.reset(api.New(api.Class(Ui,UiSpace,"ScrollView"),{&vertical}));
    int hidden=2,automatic=0;api.Call(state.pane.get(),"set_horizontalScrollerVisibility",{&hidden});
    api.Call(state.pane.get(),"set_verticalScrollerVisibility",{&automatic});
    Float(state.pane.get(),"flexGrow",1);Pixels(state.pane.get(),"minHeight",0);Enum(state.pane.get(),"display",1);
    Ref body(api.Call(state.pane.get(),"get_contentContainer"));
    for(const char* property:{"paddingLeft","paddingRight","paddingTop","paddingBottom"})Pixels(body.get(),property,8);
    state.title.reset(Label(body.get(),"Métier sélectionné"));
    Pixels(state.title.get(),"fontSize",18);Enum(state.title.get(),"unityFontStyleAndWeight",1);
    Ref intro(Label(body.get(),"Ton métier, ton niveau et ton XP sont lus automatiquement. Choisis un objectif, puis une recette."));
    Pixels(intro.get(),"marginBottom",14);
    Ref goal(Section(body.get(),"1. Ton objectif"));
    Ref controls(Row(goal.get()));Label(controls.get(),"Niveau à atteindre");
    state.target.reset(Input(controls.get(),"200","Niveau cible (1 à 200)",3));
    Ref bonuses(Row(goal.get()));Label(bonuses.get(),"XP de fabrication (%)");
    state.coefficient.reset(Input(bonuses.get(),"100","100 % = normal. Inclure les bonus d'XP de fabrication, par exemple l'Almanax.",3));
    state.epic.reset(api.New(api.Class("Core.dll",Figma,"CheckboxCustom")));
    Text(state.epic.get(),"Serveur Épique","set_rightText");Add(goal.get(),state.epic.get());
    Ref bonusHelp(Label(goal.get(),"Garde 100 % et Épique décoché si tu n'as pas de bonus particulier."));
    Pixels(bonusHelp.get(),"fontSize",12);Color(bonusHelp.get(),"color",.74f,.76f,.69f);
    Ref list(Section(body.get(),"2. Choisis une recette"));
    state.status.reset(Label(list.get(),"Recettes disponibles pour ton niveau, classées par niveau décroissant."));
    state.refresh.reset(Button(list.get(),"Actualiser la liste"));Pixels(state.refresh.get(),"marginBottom",8);
    bool no=false;
    for(auto& row:state.rows){
        row.reset(Button(list.get(),""));Pixels(row.get(),"minHeight",58);Pixels(row.get(),"marginBottom",5);
        Ref label(api.Field(row.get(),"m_textLabel"));api.Call(label.get(),"set_isSingleLine",{&no});
        Enum(label.get(),"whiteSpace",0);Enum(row.get(),"display",1);
    }
    Ref pages(Row(list.get()));state.previous.reset(Button(pages.get(),"← Précédentes"));state.next.reset(Button(pages.get(),"Suivantes →"));
    Float(state.previous.get(),"flexGrow",1);Float(state.next.get(),"flexGrow",1);Pixels(state.previous.get(),"marginRight",8);
    Ref result(Section(body.get(),"3. Ingrédients à préparer"));
    state.details.reset(Label(result.get(),"Sélectionne une recette pour afficher les quantités nécessaires à ton objectif."));
    Add(state.tabs.get(),state.tab.get());Add(state.content.get(),state.pane.get());
    Log("JOBS_XP_PANEL_READY nativeTab=true provider=DofusDB");
}
void UpdateCharacter() {
    int id=api.Value<int>(api.Field(state.handler.get(),"m_jobId"));
    Ref service(module->FindService("fmp"));
    if(!service.get()||api.object_get_class(service.get())!=api.Class("Core.dll","","fag"))
        throw std::runtime_error("Jobs service unavailable");
    Ref job(api.Call(service.get(),"bbxi",{&id}));
    if(!job.get()||api.object_get_class(job.get())!=api.Class("Core.dll","","ll"))
        throw std::runtime_error("Selected job unavailable");
    const int level=api.Value<int>(api.Call(job.get(),"obv"));
    const auto xp=api.Value<int64_t>(api.Call(job.get(),"obx"));
    if(level<1||level>200||xp<0||jobsxp::Level(xp)!=level)throw std::runtime_error("Jobs experience inconsistent");
    const bool changed=state.query.job!=id||state.query.level!=level;
    if(changed){
        const bool newJob=state.query.job!=id;
        Cancel();state.query={id,level,0};state.havePage=false;state.selected=-1;
        if(newJob)Text(state.target.get(),std::to_string(std::min(200,((level/(id==79?5:10))+1)*(id==79?5:10))));
    }
    const bool xpChanged=state.xp!=xp;state.xp=xp;
    Text(state.title.get(),api.String(api.Call(job.get(),"obt"))+" · Niveau "+std::to_string(level)+" · "+std::to_string(xp)+" XP");
    if(changed||xpChanged)Render();
}
void EnableHook(Obj owner,Method method){
    originalEnable(owner,method);
    try{if(Session()){RestoreJobsXp();if(!state.cleanup)pending.reset(owner);}}
    catch(const std::exception& e){Log(std::string("JOBS_XP_ENABLE_ERROR ")+e.what());}
}
void DisableHook(Obj owner,Method method){
    try{if(pending.get()==owner||state.owner.get()==owner)RestoreJobsXp();}
    catch(const std::exception& e){Log(std::string("JOBS_XP_DISABLE_ERROR ")+e.what());}
    originalDisable(owner,method);
}
void SwitchHook(Obj handler,int index,Method method){
    // Native receives only its original indices. Our tab is outside its group.
    try{if(state.handler.get()==handler){
        Leave(false);
        if(index==0)TabValue(state.recipeTab.get(),true);
        else if(index==1)TabValue(state.resourceTab.get(),true);
        if(state.tab.get())TabValue(state.tab.get(),false);
    }}
    catch(const std::exception& e){state.cleanup=true;Log(std::string("JOBS_XP_SWITCH_ERROR ")+e.what());}
    originalSwitch(handler,index,method);
}
void FieldContract(Klass klass,const char* name,size_t offset){
    const auto field=api.class_get_field_from_name(klass,name);
    if(!field||api.field_get_offset(field)!=offset)throw std::runtime_error(std::string("Jobs field layout: ")+name);
}
}
void InstallJobsXp(){
    if(installed)return;
    const auto ui=api.Class("Core.dll","Core.UILogic.Job","JobUI");
    const auto handler=api.Class("Core.dll","Core.UILogic.Job","JobCraftAndSkillsHandler");
    FieldContract(ui,"m_jobCraftAndSkillsHandler",0x150);
    FieldContract(handler,"m_binding",0x68);FieldContract(handler,"m_jobId",0x60);
    const auto binding=api.Class("Core.dll","Core.UILogic.Bindings","JobRecipesAndSkillsBinding");
    for(const auto& [name,rva]:std::array<std::pair<const char*,uintptr_t>,7>{{
        {"GetRootElement",0x68D2F0},{"get_ctr_tabs",0x2F56D50},{"get_content",0x2F56A90},
        {"get_tab_recipe",0x2F57010},{"get_tab_ressources",0x2F570C0},{"get_recipesUI",0x2F56F60},{"get_jobSkillsUI",0x2F56E00}}})
        api.FindVerified(binding,name,0,rva);
    const auto tab=api.Class("Core.dll",Figma,"DofusTab");
    api.FindVerified(tab,".ctor",0,0x1712DC0);api.FindVerified(tab,"set_text",1,0x17136F0);
    api.FindVerified(tab,"get_size",0,0x16CE880);api.FindVerified(tab,"set_size",1,0x1713550);
    api.FindVerified(tab,"get_isOn",0,0x1712F50);api.FindVerified(tab,"SetValueWithoutNotify",1,0x1711970);
    api.FindVerified(tab,"get_fontCase",0,0x1706A00);api.FindVerified(tab,"set_fontCase",1,0x1712FC0);
    api.FindVerified(tab,"get_group",0,0x155DB30);
    api.FindVerified(api.Class("Core.dll","Core.UILogic.Components","SelectableGroup"),"ForceAllSelectionsOff",0,0x15C1FC0);
    api.FindVerified(api.Class(Ui,UiSpace,"VisualElement"),"get_resolvedStyle",0,0x55F72E0);
    api.FindVerified(api.Class("Core.dll","","fag"),"bbxi",1,0x19DEA10);
    const auto job=api.Class("Core.dll","","ll");
    api.FindVerified(job,"obv",0,0x7EF1B0);api.FindVerified(job,"obx",0,0x73DC70);api.FindVerified(job,"obt",0,0x68D2D0);
    const auto input=api.Class("Core.dll",Figma,"TextInput");
    api.FindVerified(input,".ctor",0,0x178DC90);api.FindVerified(input,"get_text",0,0x178DDA0);
    api.FindVerified(input,"set_text",1,0x178EC50);api.FindVerified(input,"set_maxLength",1,0x178E9B0);
    const auto checkbox=api.Class("Core.dll",Figma,"CheckboxCustom");
    api.FindVerified(checkbox,".ctor",0,0x1704CD0);api.FindVerified(checkbox,"get_value",0,0x1705050);
    api.FindVerified(checkbox,"set_rightText",1,0x1705590);
    const auto scroll=api.Class(Ui,UiSpace,"ScrollView");
    api.FindVerified(scroll,".ctor",1,0x5741C70);api.FindVerified(scroll,"get_contentContainer",0,0x5742BE0);
    api.FindVerified(scroll,"set_horizontalScrollerVisibility",1,0x5743280);
    api.FindVerified(scroll,"set_verticalScrollerVisibility",1,0x5743CB0);
    // ListElementBasic contracts are verified by InstallSoulHelper first.
    AddHook(api.FindVerified(ui,"OnEnable",0,0x2AF67C0),0x2AF67C0,reinterpret_cast<void*>(EnableHook),reinterpret_cast<void**>(&originalEnable));
    AddHook(api.FindVerified(ui,"OnDisable",0,0x2AF6780),0x2AF6780,reinterpret_cast<void*>(DisableHook),reinterpret_cast<void**>(&originalDisable));
    AddHook(api.FindVerified(handler,"SwitchTab",1,0x2ABC770),0x2ABC770,reinterpret_cast<void*>(SwitchHook),reinterpret_cast<void**>(&originalSwitch));
    soul::SetAuxiliaryPanelAction(Action);installed=true;
}
void RestoreJobsXp(){
    pending.reset();Cancel();
    if(!state.pane.get()&&!state.tab.get()){state=State{};return;}
    state.cleanup=true;
    try{
        Leave();
        if(state.tab.get())api.Call(state.tab.get(),"RemoveFromHierarchy");
        if(state.pane.get())api.Call(state.pane.get(),"RemoveFromHierarchy");
        state=State{};
    }catch(const std::exception& e){Log(std::string("JOBS_XP_CLEANUP_ERROR ")+e.what());}
}
bool JobsXpCleanupPending(){return state.cleanup;}
void TickJobsXp(){
    if(!installed)return;
    if(worker&&worker->done.load())worker.reset();
    try{
        if(!Session()||state.cleanup||(state.pane.get()&&!Current())){RestoreJobsXp();return;}
        if(pending.get()&&!state.pane.get()){
            Create(pending.get());if(state.pane.get())pending.reset();
        }
        if(!state.pane.get())return;
        MatchTabHeight();
        if(api.Value<bool>(api.Call(state.tab.get(),"get_isOn")))Enter();
        if(!state.active)return;
        TabValue(state.tab.get(),true);
        const auto now=GetTickCount64();
        if(now>=state.nextPoll){state.nextPoll=now+500;UpdateCharacter();}
        if(state.request&&state.request->done.load()){
            auto request=std::move(state.request);
            if(jobsxp::AcceptResult(state.generation,request->generation,state.query,request->query,request->cancelled.load())){
                if(request->error.empty()){
                    state.page=std::move(request->page);state.havePage=true;state.selected=-1;
                    Text(state.status.get(),state.page.recipes.empty()?"Aucune recette disponible pour ce métier à ce niveau.":
                        std::to_string(state.page.total)+" recettes · "+std::to_string(state.query.skip+1)+" à "+
                        std::to_string(state.query.skip+state.page.recipes.size())+" · DofusDB");
                }else Text(state.status.get(),request->error);
                Render();
            }
        }
        if(!state.requested&&!state.havePage)Search();
        Enabled(state.refresh.get(),!state.request&&!worker&&now>=state.nextRequest);
        int target=0,coefficient=0;bool epic=false;
        Parameters(target,coefficient,epic);
        if(target!=state.lastTarget||coefficient!=state.lastCoefficient||epic!=state.lastEpic)Render();
    }catch(const std::exception& e){
        Log(std::string("JOBS_XP_PANEL_ERROR ")+e.what());RestoreJobsXp();
    }
}
