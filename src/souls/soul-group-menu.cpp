#include "souls/soul-group-menu.hpp"
#include "souls/soul-policy.hpp"
#include "souls/soul-monster.hpp"
#include "souls/soul-inventory.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <memory>

namespace {
using namespace managed;
constexpr const char* MenuSpace = "Core.UILogic.Components.ContextMenu";
constexpr const char* MakerSpace = "Core.UILogic.Components.ContextMenu.ContextMenuFactory";
struct Menu { Ref menu, maker, entity, group, roleplay, statics, cached, context, collection, item, callback; int required = 0; bool armed = false, attached = false; uint64_t created = 0; };
std::unique_ptr<Menu> owned;
Method append{}, entityId{}, store{}, variant{}, entities{}, findEntity{}, rpService{}, staticService{};
soul::MonsterCaptureData captureData;
bool installed = false;
using CreateFn = Obj(*)(Obj, Method);
CreateFn originalCreate = nullptr;
bool Available() { return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested && !module->InCombat(); }
int Count(Obj list) {
    if (!list) return 0;
    const int n = api.Value<int>(api.Call(list, "get_Count"));
    if (n < 0 || n > 1024) throw std::runtime_error("Soul group list bounds");
    return n;
}
int Index(const Menu& menu) {
    Ref rows(api.Call(menu.menu.get(), "get_menuItems"));
    for (int i=0,n=Count(rows.get());i<n;++i) if (api.Call(rows.get(), "get_Item", {&i}) == menu.item.get()) return i;
    return -1;
}
Obj Root(const Menu& menu) {
    Obj root = api.Field(menu.menu.get(), "dofusContextualMenuRoot");
    return root && api.Call(root, "get_menuData") == menu.menu.get() ? root : nullptr;
}
bool Fresh(const Menu& menu) {
    if (!Available() || api.Field(menu.maker.get(),"m_data")!=menu.entity.get() || module->cached.get()!=menu.cached.get() || module->FindService("fns")!=menu.roleplay.get() ||
        module->FindService("fsr")!=menu.statics.get()) return false;
    Ref context(api.Field(menu.roleplay.get(), "dqst"));
    if (!context.get() || context.get()!=menu.context.get() || std::string(api.class_get_name(api.object_get_class(context.get()))) != "epk" ||
        api.Invoke(rpService, context.get())!=menu.roleplay.get() || api.Invoke(staticService, context.get())!=menu.statics.get()) return false;
    Ref collection(api.Invoke(entities, context.get()));
    int64_t id = api.Value<int64_t>(api.Invoke(entityId, menu.entity.get()));
    Obj found = nullptr;
    if (!collection.get() || collection.get()!=menu.collection.get() || !api.Value<bool>(api.Invoke(findEntity, collection.get(), {&id,&found})) || found!=menu.entity.get()) return false;
    Ref groups(api.Invoke(store,menu.statics.get()));
    found = nullptr;
    return groups.get() && api.Value<bool>(api.Invoke(api.FindVerified(api.object_get_class(groups.get()),"baqa",2,0x3BFB210),groups.get(),{&id,&found})) && found==menu.group.get();
}
int Highest(const Menu& menu) {
    if (!Fresh(menu)) return 0;
    Ref variants(api.Field(menu.group.get(), "dqys"));
    struct Adjusted { int id, level; bool used = false; };
    std::vector<Adjusted> adjusted;
    if (Count(variants.get())>0) {
        Ref copy(api.Invoke(variant,menu.roleplay.get(),{menu.group.get()}));
        if (!copy.get()) return 0;
        for (int i=0,n=Count(copy.get());i<n;++i) {
            Ref monster(api.Call(copy.get(),"get_Item",{&i}));
            if (!monster.get()) return 0;
            adjusted.push_back({*static_cast<int*>(api.FieldAddress(monster.get(),"dcgq")),
                *static_cast<int*>(api.FieldAddress(monster.get(),"dcgs")),false});
        }
    }
    int highest=0; bool capturable=false, metadataMissing=false;
    auto add=[&](Obj monster) {
        if (!monster) throw std::runtime_error("Incomplete monster group");
        int value=*static_cast<int*>(api.FieldAddress(monster,"dcgs"));
        const int id=*static_cast<int*>(api.FieldAddress(monster,"dcgq"));
        Ref data(captureData.Find(id));
        if (!data.get() || std::string(api.class_get_name(api.object_get_class(data.get()))) != "MonsterData") metadataMissing=true;
        else if (captureData.Eligible(data.get())) capturable=true;
        for (auto& replacement:adjusted) if (!replacement.used && replacement.id==id) {
            value=replacement.level; replacement.used=true; break;
        }
        if (value<=0 || value>10000) throw std::runtime_error("Invalid monster level");
        highest=std::max(highest,value);
    };
    add(api.Field(menu.group.get(),"dqyq"));
    Ref monsters(api.Field(menu.group.get(),"dqyr"));
    for (int i=0,n=Count(monsters.get());i<n;++i) add(api.Call(monsters.get(),"get_Item",{&i}));
    return capturable && !metadataMissing ? highest : 0;
}
Obj CreateHook(Obj maker, Method method) {
    Obj result=originalCreate(maker,method);
    if (!Available() || !result) return result;
    try {
        RestoreSoulGroupMenu();
        auto next=std::make_unique<Menu>();
        next->menu.reset(result); next->maker.reset(maker); next->cached.reset(module->cached.get());
        next->entity.reset(api.Field(maker,"m_data"));
        next->roleplay.reset(api.Field(maker,"m_rolePlayEntitiesService"));
        next->statics.reset(api.Field(maker,"m_staticInfoService"));
        if (!next->entity.get() || !next->statics.get() || !next->roleplay.get()) return result;
        next->context.reset(api.Field(next->roleplay.get(),"dqst"));
        if (!next->context.get() || std::string(api.class_get_name(api.object_get_class(next->context.get())))!="epk") return result;
        next->collection.reset(api.Invoke(entities,next->context.get()));
        int64_t id=api.Value<int64_t>(api.Invoke(entityId,next->entity.get()));
        Ref groups(api.Invoke(store,next->statics.get()));
        Obj group=nullptr;
        if (groups.get() && api.Value<bool>(api.Invoke(api.FindVerified(api.object_get_class(groups.get()),"baqa",2,0x3BFB210),groups.get(),{&id,&group}))) next->group.reset(group);
        const int highest=next->group.get() ? Highest(*next) : 0;
        if (highest == 0) return result;
        const auto* required=soul::RequiredStone(highest);
        std::string label="Pierre d’âme : groupe spécial ou indisponible";
        bool disabled=true; int icon=94;
        if (required) {
            next->required=required->genericId;
            Ref inventory(module->FindService("fmo"));
            const auto contents=inventory.get()?soul::ReadInventory(inventory.get()):soul::InventorySnapshot{};
            const auto selected=soul::SelectStone(*required,contents);
            const auto power=std::to_string(required->power);
            if (!contents.complete) label="Pierre d’âme : inventaire indisponible";
            else if (selected.equipped) { label="Pierre d’âme "+power+" déjà équipée"; icon=91; }
            else if (!selected.bagCount) label="Aucune pierre d’âme "+power+" en sac";
            else if (!selected.uid) label=contents.equipmentPending ? "Pierre d’âme : équipement en cours…" :
                "Pierre d’âme "+power+" : conditions non remplies";
            else { label="Équiper la pierre d’âme "+power+" (×"+std::to_string(selected.bagCount)+")"; icon=128; disabled=false; }
        }
        Ref asset(api.object_new(api.class_from_type(api.method_get_param(append,7))));
        Ref color(api.object_new(api.class_from_type(api.method_get_param(append,10))));
        Ref text(api.string_new(label.c_str())), empty(api.string_new(""));
        bool close=true; unsigned delay=1000; int style=1;
        Ref rows(api.Call(result,"get_menuItems")); int index=Count(rows.get());
        // Publish ownership before mutation, so partial native append failures can be removed.
        owned=std::move(next); owned->created=GetTickCount64();
        api.Invoke(append,result,{text.get(),nullptr,&disabled,&close,empty.get(),&delay,empty.get(),
            api.object_unbox(asset.get()),&style,&icon,api.object_unbox(color.get()),empty.get(),empty.get()});
        owned->item.reset(api.Call(rows.get(),"get_Item",{&index}));
        owned->callback.reset(api.Call(owned->item.get(),"get_m_callback"));
        owned->armed=!disabled;
    } catch (const std::exception& e) { Log(std::string("SOUL_GROUP_MENU_ERROR ")+e.what()); RestoreSoulGroupMenu(); }
    return result;
}
}
bool HandleSoulGroupMenuCallback(managed::Obj callback, managed::Obj view) {
    if (!owned || owned->callback.get()!=callback) return false;
    try {
        if (!owned->armed || !view || !api.Call(view,"get_panel") ||
            api.Call(view,"get_itemData")!=owned->item.get() || Index(*owned)<0 ||
            api.Value<bool>(api.Call(owned->item.get(),"get_disabled")) || !Fresh(*owned)) return true;
        const auto* required=soul::RequiredStone(Highest(*owned));
        if (!required || required->genericId!=owned->required) return true;
        Ref inventory(module->FindService("fmo")); if (!inventory.get()) return true;
        const auto selected=soul::SelectStone(*required,soul::ReadInventory(inventory.get()));
        if (selected.equipped || !selected.uid || !Fresh(*owned)) return true;
        owned->armed=false;
        soul::EquipExactStone(inventory.get(),selected.uid,required->genericId);
    } catch (const std::exception& e) { Log(std::string("SOUL_GROUP_EQUIP_ERROR ")+e.what()); }
    return true;
}
void RestoreSoulGroupMenu() {
    auto old=std::move(owned); if (!old) return; old->armed=false;
    try {
        if (old->item.get()) { int index=Index(*old); if (index>=0) api.Call(old->menu.get(),"RemoveItemAt",{&index}); }
        Ref root(Root(*old)); if (root.get() && api.Call(root.get(),"get_panel")) { bool delayed=false; api.Call(root.get(),"RemoveMenu",{&delayed}); }
    } catch (const std::exception& e) { Log(std::string("SOUL_GROUP_RESTORE_ERROR ")+e.what()); }
}
void TickSoulGroupMenu() {
    if (!owned) return;
    try {
        if (!Available() || (owned->group.get() && !Fresh(*owned))) { RestoreSoulGroupMenu(); return; }
        Ref root(Root(*owned)); const bool attached=root.get() && api.Call(root.get(),"get_panel");
        if (attached) owned->attached=true;
        if (!attached && (owned->attached || GetTickCount64()-owned->created>5000)) RestoreSoulGroupMenu();
    } catch (const std::exception& e) { Log(std::string("SOUL_GROUP_TICK_ERROR ")+e.what()); RestoreSoulGroupMenu(); }
}
void InstallSoulGroupMenu() {
    captureData.Install();
    const auto verify=[](const char* ns,const char* type,const char* name,int argc,uintptr_t rva) {
        return api.FindVerified(api.Class("Core.dll",ns,type),name,argc,rva);
    };
    append=verify(MenuSpace,"DofusContextualMenu","AppendDefaultItem",13,0x16D6C60);
    entityId=verify("Core.Services.Entities.EntityInformationServices","EntityInfo","bajh",0,0x1951440);
    store=verify("","ewa","bapo",0,0x73DC70);
    variant=verify("","evh","bafy",1,0x1938C10);
    entities=verify("","epk","xmg",0,0x68D2D0);
    rpService=verify("","epk","xmp",0,0x9566A0);
    staticService=verify("","epk","xmi",0,0x73DC70);
    findEntity=verify("","evv","bamm",2,0x1957A70);
    auto create=verify(MakerSpace,"MonsterGroupMenuMaker","CreateMenu",0,0x16E72F0);
    AddHook(create,0x16E72F0,reinterpret_cast<void*>(&CreateHook),reinterpret_cast<void**>(&originalCreate));
    installed=true;
}
