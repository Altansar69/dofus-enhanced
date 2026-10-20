#include "native-panel-test-fixture.hpp"

namespace fake {
struct Fixture {
    Module owner{std::filesystem::path{}};
    soul::Panel panel;
    soul::PanelView view{"Capacite requise : 150","Stock : 2","Equipee : aucune","Choisir une pierre",false,true};
    Obj root=nullptr,nativeChild=nullptr;
    Fixture() {
        panel.Hide();Check(handles.empty(),"Previous fixture leaked refs");
        module=&owner;owner.cached.reset(Make("Cache"));
        owner.ready=owner.servicesReady=true;owner.settings.soulReminder=true;owner.compositor.reset(Make("Compositor"));
        contextService=Make("ContextService");hides=nativePopulates=0;lastMenuRoot=nullptr;
        auto options=Make("Options"),lockOption=Make("BoolOption");Get(lockOption).enabled=false;
        Get(options).fields["lockUI"]=lockOption;Get(owner.cached.get()).fields["options"]=options;
        dependencies=Make("goc");input=Make("eqo");Get(dependencies).fields["cached"]=owner.cached.get();
        for(const auto* key:{"ydf","ydh","ydj","ydl"}) {
            auto event=Make("InputCustomEvent"),list=Make("List"),foreign=Make("Action");
            Get(foreign).fields["target"]=Make("NativeDragger");Get(list).children.push_back(foreign);
            Get(event).fields["m_input"]=list;Get(input).fields[key]=event;
        }
        snapCleanups=removedInputs=initFailures=0;
        bindSuccess=true;equips=saves=globalWrites=layoutSaves=0;globalDragging=false;capture=nullptr;
        root=Element("Root");Get(root).dimensions["width"]=1200;Get(root).dimensions["height"]=800;Get(root).fields["panel"]=Make("Panel");
        nativeRoot=Element("Document");auto layers=Element("gmy"),layerService=Make("gmo");
        Get(nativeRoot).fields["panel"]=Get(root).fields["panel"];
        Get(nativeRoot).fields["layersService"]=layerService;Get(layerService).fields["dzrv"]=layers;
        Add(nativeRoot,layers);Add(layers,root);Get(root).name="RootLow";
        nativeChild=Element("NativeUi");Add(root,nativeChild);
        panel.Install(&Equip,&Save,&HideModule);panel.Render(root,view);
    }
    ~Fixture(){panel.Hide();module=nullptr;}
    Obj Window(){return latestWindow;}
    Obj Dragger(){return Get(Window()).fields.at("m_movableManipulator");}
    void BeginDrag(float x,float y) {downHook(Dragger(),Make("MouseDown"),nullptr);Get(Window()).dimensions["left"]=x;Get(Window()).dimensions["top"]=y;}
};
struct MenuFixture {
    Obj button, manipulator, menu, event, root, item, view, callback;
    explicit MenuFixture(Fixture& f) {
        button=Get(Get(f.Window()).fields.at("header")).fields.at("rightButton");
        manipulator=Get(button).fields.at("manipulator");
        menu=Make("Menu");event=Make("PopulateEvent");root=Element("MenuRoot");view=Element("MenuView");
        Get(menu).fields["menuItems"]=Make("List");Get(menu).fields["dofusContextualMenuRoot"]=root;
        Get(event).fields["menu"]=menu;Get(root).fields["menuData"]=menu;
        populateHook(manipulator,event,nullptr);
        auto& rows=Get(Get(menu).fields.at("menuItems")).children;
        Check(rows.size()==3&&Get(rows[0]).type->name=="MenuTitle"&&Get(rows[1]).type->name=="MenuSeparator","Title and separator precede the hide option");
        item=rows[2];callback=Get(item).fields.at("m_callback");Get(view).fields["itemData"]=item;
        Add(nativeRoot,root);Add(root,view);lastMenuRoot=root;
    }
};
void HeaderMenuSelectionAndReopen() {
    Fixture f;MenuFixture menu(f);
    Check(hides==0&&f.owner.settings.soulReminder,"Opening options must not hide the module");
    populateHook(menu.manipulator,menu.event,nullptr);
    Check(Get(Get(menu.menu).fields.at("menuItems")).children.size()==3,"Duplicate event must not duplicate row");
    populateHook(Make("ForeignManipulator"),menu.event,nullptr);
    Check(nativePopulates==1,"Foreign menus must retain native callback");
    Check(!soul::HandlePanelMenuCallback(Make("ForeignCallback"),menu.view),"Foreign selection must continue native routing");
    Check(soul::HandlePanelMenuCallback(menu.callback,menu.view)&&hides==1&&!f.owner.settings.soulReminder&&!Panel(f.Window()),"Hide closes menu and owned panel");
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==1,"Stale queued callback cannot hide twice");
    Check(!Get(menu.button).fields["manipulator"],"Hide unregisters the native menu manipulator");
    f.owner.settings.soulReminder=true;f.panel.Render(f.root,f.view);
    MenuFixture reopened(f);Check(hides==1&&Panel(f.Window()),"Reactivation restores the same header menu");
    f.panel.Hide();Check(!Panel(reopened.root),"Hiding externally also closes its menu");
}
void HeaderMenuRejectsStaleAndCleanupFailure() {
    Fixture f;MenuFixture menu(f);
    Get(menu.item).enabled=false;soul::HandlePanelMenuCallback(menu.callback,menu.view);
    Check(hides==0,"Disabled row cannot hide module");Get(menu.item).enabled=true;
    auto compositor=f.owner.compositor.get();f.owner.compositor.reset(Make("Replacement"));
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==0,"Old session cannot change new settings");
    f.owner.compositor.reset(compositor);
    auto rows=Get(menu.menu).fields.at("menuItems");Get(rows).children.clear();
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==0,"Removed row cannot act");Get(rows).children={menu.item};
    failOwner=menu.root;failMethod="RemoveMenu";f.panel.Hide();
    Check(soul::PanelCleanupPending()&&Panel(menu.root),"Failed native menu close retains references for retry");
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==0,"Cleanup failure disarms callbacks");
    f.panel.Hide();Check(!soul::PanelCleanupPending()&&!Panel(menu.root),"Retry closes only owned native menu");
    f.panel.Render(f.root,f.view);MenuFixture recreated(f);
    failOwner=recreated.button;failMethod="RemoveManipulator";f.panel.Hide();
    Check(soul::PanelCleanupPending()&&Get(recreated.button).fields["manipulator"],"Failed manipulator removal retained");
    f.panel.Hide();Check(!soul::PanelCleanupPending()&&!Get(recreated.button).fields["manipulator"],"Cleanup retries exact manipulator");
}
bool OnlyModuleRefs() {
    return module&&handles.size()==2&&std::all_of(handles.begin(),handles.end(),[](const auto& entry) {
        return entry.second==module->cached.get()||entry.second==module->compositor.get();
    });
}
void CreationAndClicks() {
    Fixture f;
    Check(soul::GetUiRoot()==f.root,"Root API mismatch");
    Check(Get(f.Window()).name.empty()&&!Get(f.Window()).modal&&Get(f.Window()).movable,"Overlay must be unnamed nonmodal movable native window");
    Check(Get(f.root).children.size()==2,"One overlay only");
    f.panel.Render(f.root,f.view);Check(Get(f.root).children.size()==2,"Render duplicated overlay");
    auto content=Get(f.Window()).fields.at("content");
    Check(Get(content).children.size()==5,"Expected four lines and equip action");
    auto equipped=Get(Get(content).children[2]).children[1];Check(Get(equipped).dimensions["green"]<0.4f,"Missing stone must be red");
    auto other=Element("ListElementBasic");int before=nativeActions;actionHook(other,nullptr);Check(nativeActions==before+1,"Other native action swallowed");
    actionHook(latestAction,nullptr);actionHook(latestAction,nullptr);Check(equips==1,"Repeated queued click should not equip twice");
    f.view.equippedCorrect=true;f.view.equipEnabled=false;f.panel.Render(f.root,f.view);actionHook(latestAction,nullptr);Check(equips==1,"Disabled equip invoked callback");
    Check(Get(equipped).dimensions["green"]>0.8f,"Correct equipped stone warning persisted");
    f.view.equipEnabled=true;f.panel.Render(f.root,f.view);Get(f.root).enums["display"]=1;actionHook(latestAction,nullptr);Check(equips==1,"Hidden ancestor action accepted");
    Get(f.root).enums["display"]=0;Get(f.root).enabled=false;actionHook(latestAction,nullptr);Check(equips==1,"Disabled ancestor action accepted");
    f.panel.Hide();Check(Get(f.root).children==std::vector<Obj>{f.nativeChild},"Hide changed native siblings");
}
void EquippedItemRow() {
    Fixture f;auto content=Get(f.Window()).fields.at("content");
    auto row=Get(content).children[2];auto label=Get(row).children[1];
    auto item=Make("mc");f.view.equippedItem=item;f.view.equipped="Pierre moyenne";
    f.panel.Render(f.root,f.view);
    Check(itemBound==item&&Get(itemSlot).enums["display"]==0,"Equipped native instance must bind and show icon");
    Check(Get(row).enums["flexDirection"]==2&&Get(label).text=="Pierre moyenne","Item icon/text row layout");
    f.view.equipmentChecked=false;f.panel.Render(f.root,f.view);
    Check(Get(label).dimensions["red"]==Get(label).dimensions["green"],"Combat must not reuse placement warning color");
    bindSuccess=false;f.view.equippedItem=Make("mc");f.panel.Render(f.root,f.view);
    Check(Get(itemSlot).enums["display"]==1,"Failed binding must hide stale previous icon");
    bindSuccess=true;f.panel.Render(f.root,f.view);Check(itemBound==f.view.equippedItem&&Get(itemSlot).enums["display"]==0,"Binding recovery must show new item");
    f.view.equippedItem=nullptr;f.panel.Render(f.root,f.view);
    Check(!itemBound&&Get(itemSlot).enums["display"]==1,"Unequip must clear icon");
}
void DragCompletionAndClamping() {
    Fixture f;f.BeginDrag(9999,-50);
    f.panel.Render(f.root,f.view);Check(saves==0&&Get(f.Window()).dimensions["left"]==9999,"Render overwrote active native drag");
    auto event=Make("MouseUp");upHook(f.Dragger(),event,nullptr);
    Check(!capture&&!globalDragging&&!Get(f.Dragger()).active,"Native drag was not completed");
    Check(saves==1&&savedX==956&&savedY==0,"Position must clamp and save after drag only");
    Check(layoutSaves==0,"Native shared layout save must not run for unnamed overlay");
    f.panel.Render(f.root,f.view,savedX,savedY);Check(saves==1,"Stable position caused repeated saves");
    f.BeginDrag(50,60);Get(event).enabled=false;upHook(f.Dragger(),event,nullptr);
    Check(!capture&&!globalDragging,"Wrong mouse-button up left owned global drag flag");
}
void LayoutApplicationAndDragRevision() {
    Fixture f;
    f.panel.Render(f.root,f.view,24,180,10);
    f.BeginDrag(300,240);
    // A new native preset with the same coordinates still cancels this gesture.
    f.panel.Render(f.root,f.view,24,180,11);
    Check(!capture&&!globalDragging&&!Get(f.Dragger()).active&&saves==0,
        "Layout application must cancel the old drag without persisting it");
    Check(Get(f.Window()).dimensions["left"]==24&&Get(f.Window()).dimensions["top"]==180,
        "Same-coordinate layout must restore its position after interrupted drag");
    upHook(f.Dragger(),Make("MouseUp"),nullptr);
    Check(saves==0,"Stale mouse-up must not save into the new layout");
    f.BeginDrag(400,250);
    upHook(f.Dragger(),Make("MouseUp"),nullptr);
    Check(saves==1&&savedGeneration==11&&savedX==400&&savedY==250,
        "A completed gesture must save with the layout revision captured at mouse-down");
    f.panel.Render(f.root,f.view,400,250,11);
    Get(f.root).dimensions["width"]=500;
    f.panel.Render(f.root,f.view,400,250,11);
    Check(saves==1&&Get(f.Window()).dimensions["left"]==256,
        "Viewport clamping must not overwrite a saved native preset");
    f.panel.Render(f.root,f.view,40,60,12);
    Check(saves==1&&Get(f.Window()).dimensions["left"]==40&&Get(f.Window()).dimensions["top"]==60,
        "Applying another layout must restore coordinates without a save callback");
}
void CaptureStatusAndActionVisibility() {
    Fixture f;
    auto content=Get(f.Window()).fields.at("content");
    auto status=Get(Get(content).children[3]).children[1];
    f.view.equippedItem=Make("mc"); f.panel.Render(f.root,f.view);
    Check(itemBound==f.view.equippedItem,"Placement must bind actual native item before combat transition");
    // A stale placement view must still clear the binding and hide every
    // equipment row solely from the capture-only flag.
    f.view.showEquipment=false;
    f.view.captureSpell=Make("ht"); f.view.castEnabled=false;
    f.view.status="Capture absente"; f.view.statusTone=soul::StatusTone::Warning;
    f.panel.Render(f.root,f.view);
    Check(spellBound==f.view.captureSpell && !spellEnabled && Get(spellSlot).enums["display"]==0,
        "Missing capture must show the learned spell even while casting is disabled");
    Check(Get(status).dimensions["green"]<0.4f && Get(status).dimensions["red"]>0.9f,"Missing capture must be red");
    Check(Get(latestAction).enums["display"]==1 && !Get(latestAction).enabled,"Active combat must hide and disable equip even with stale enabled view");
    actionHook(latestAction,nullptr); Check(equips==0,"Hidden equipment callback must be suppressed");
    Check(Get(Get(content).children[0]).enums["display"]==1 && Get(Get(content).children[1]).enums["display"]==1 &&
        Get(Get(content).children[2]).enums["display"]==1 && !itemBound && Get(itemSlot).enums["display"]==1,
        "Capture-only state must hide stale preparation text, item and native hover binding");
    f.view.castEnabled=true;f.panel.Render(f.root,f.view);
    Check(spellEnabled && spellBound==f.view.captureSpell,"Cast eligibility enables the existing visible icon");
    f.view.status="Capture active"; f.view.statusTone=soul::StatusTone::Good; f.view.castEnabled=false;f.panel.Render(f.root,f.view);
    Check(spellBound==f.view.captureSpell && !spellEnabled && Get(spellSlot).enums["display"]==0,
        "Casting the spell changes status/eligibility without removing its icon");
    Check(Get(status).dimensions["green"]>0.8f && Get(status).dimensions["red"]<0.5f,"Actual active capture must be green");
    f.view.status="Inconnu"; f.view.statusTone=soul::StatusTone::Neutral; f.panel.Render(f.root,f.view);
    Check(Get(status).dimensions["green"]==Get(status).dimensions["red"],"Unknown must clear red/green indication");
    f.view.showEquipment=true; f.view.stock="En sac : 2"; f.panel.Render(f.root,f.view);
    Check(!spellBound && !spellEnabled && Get(spellSlot).enums["display"]==1,"Placement clears the stale spell binding");
    Check(Get(latestAction).enums["display"]==0 && Get(latestAction).enabled && Get(Get(content).children[1]).enums["display"]==0 &&
        Get(Get(content).children[0]).enums["display"]==0 && Get(Get(content).children[2]).enums["display"]==0 && itemBound==f.view.equippedItem,
        "New placement must restore previous native row visibility");
}
void HideDuringDragAndForeignCapture() {
    Fixture f;f.BeginDrag(40,70);auto header=capture;f.panel.Hide();
    Check(!capture&&!globalDragging&&globalWrites==2,"Mid-drag hide failed to release native capture/flag");
    Check(!Get(header).fields["parent"]||!Panel(header),"Hidden header remained attached");
    Check(OnlyModuleRefs(),"Hide leaked refs beyond the module cache and compositor");f.panel.Hide();Check(globalWrites==2,"Repeated hide changed global state");
    f.panel.Render(f.root,f.view);f.BeginDrag(50,80);
    auto foreign=Make("ElementDragger");Get(foreign).fields["header"]=Element("OtherNativeHeader");
    downHook(foreign,Make("MouseDown"),nullptr);Obj other=capture;int before=globalWrites;
    f.panel.Hide();Check(capture==other&&globalDragging&&globalWrites==before,"Hide cleared another native window's capture/drag state");
    capture=nullptr;globalDragging=false;
}
void LostCaptureStillClearsOnlyOwnedFlag() {
    Fixture f;f.BeginDrag(50,90);capture=nullptr;
    f.panel.Render(f.root,f.view);
    Check(!globalDragging&&!Get(f.Dragger()).active&&saves==1,"Lost capture left our global drag flag or discarded final position");
    f.BeginDrag(70,100);capture=Element("NonDraggingControl");Obj foreign=capture;
    f.panel.Hide();Check(!globalDragging&&capture==foreign,"Owned flag cleanup must preserve foreign non-drag capture");
    f.panel.Render(f.root,f.view);f.BeginDrag(60,110);flagHook(true,nullptr);capture=nullptr;int writes=globalWrites;
    f.panel.Hide();Check(globalDragging&&globalWrites==writes,"Unrelated direct native setter write must relinquish ownership");
    flagHook(false,nullptr);capture=nullptr;
}
void DetachedAndFailureCleanup() {
    Fixture f;auto old=f.Window();Detach(old);actionHook(latestAction,nullptr);Check(equips==0,"Detached action accepted");
    f.panel.Render(f.root,f.view);Check(f.Window()!=old&&Get(f.root).children.size()==2,"Detached window not replaced cleanly");
    failOwner=f.Window();failMethod="set_dragPosition";f.panel.Hide();Check(handles.size()>2,"Cleanup failure must retain references for retry");f.panel.Hide();Check(OnlyModuleRefs(),"Cleanup retry must release owned references");
    Check(Get(f.root).children.size()==1,"Cleanup exception prevented detach");
    f.panel.Render(f.root,f.view);old=f.Window();failOwner=old;failMethod="RemoveFromHierarchy";f.panel.Hide();
    Check(soul::PanelCleanupPending()&&Get(old).enums["display"]==1,"Failed detach keeps an invisible pending widget");
    f.panel.Render(f.root,f.view);
    Check(f.Window()!=old&&!soul::PanelCleanupPending()&&Get(f.root).children.size()==2,"Render must finish pending cleanup before rebuilding an attached widget");
    f.panel.Render(f.root,f.view);Detach(f.root);Get(f.root).fields["panel"]=nullptr;f.panel.Render(f.root,f.view);Check(OnlyModuleRefs(),"Unattached root must remove panel");
}
void NativeSubscriptionsAndPartialInit() {
    Fixture f;
    auto count=[](const char* key){return Get(Get(Get(input).fields.at(key)).fields.at("m_input")).children.size();};
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==5,"Native Init must subscribe all owned draggers and disabled resizers");
    f.panel.Hide();
    Check(removedInputs==16&&!soul::PanelCleanupPending(),"Hide must remove all owned keyboard subscriptions");
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==1,"Foreign native input callback must survive Hide");
    initFailures=1;bool failed=false;
    try{f.panel.Render(f.root,f.view);}catch(const std::exception&){failed=true;}
    Check(failed&&OnlyModuleRefs()&&!soul::PanelCleanupPending(),"Partial Init failure must release widget and subscribed callbacks");
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==1,"Partial Init cannot leak even its first event subscription");
    f.panel.Render(f.root,f.view);
    failOwner=Get(input).fields.at("ydf");failMethod="UnSubscribe";
    f.panel.Hide();
    Check(soul::PanelCleanupPending()&&handles.size()>2,"Failed unsubscribe retains roots and exposes cleanup pending to stop pump");
    Check(Get(f.root).children.size()==1,"Detach still proceeds after an unsubscribe failure");
    f.panel.Hide();
    Check(!soul::PanelCleanupPending()&&OnlyModuleRefs(),"Retry drains exact input delegates and releases owned refs");
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==1,"Cleanup retries must preserve unrelated subscriptions");
}
void ImmediateSnapAndNativeGroupMove() {
    Fixture f;f.panel.Render(f.root,f.view,24,180,10);
    f.BeginDrag(300,220);
    Get(f.Window()).delayedResolved={{"left",24.0f},{"top",180.0f}};
    upHook(f.Dragger(),Make("MouseUp"),nullptr);
    Check(saves==1&&savedX==300&&savedY==220,"Final snap position comes from inline coordinates before resolved layout catches up");
    Get(f.Window()).delayedResolved.clear();
    f.panel.Render(f.root,f.view,300,220,10);
    auto foreign=Make("ElementDraggerWithBounds"),other=Element("NativeWidget"),header=Element("HeaderWidget"),group=Make("List");
    Get(foreign).fields["header"]=header;Get(foreign).fields["window"]=other;
    Get(foreign).fields["m_snappedWindows"]=group;Get(group).children={other,f.Window()};Get(foreign).snapGroup=true;
    downHook(foreign,Make("MouseDown"),nullptr);
    Get(f.Window()).dimensions["left"]=480;Get(f.Window()).dimensions["top"]=260;
    f.panel.Render(f.root,f.view,300,220,10);
    Check(Get(f.Window()).dimensions["left"]==480&&saves==1,"Render must preserve a native neighbouring window's group movement");
    upHook(foreign,Make("MouseUp"),nullptr);
    Check(saves==2&&savedX==480&&savedY==260&&savedGeneration==10,"Group completion persists this widget after native snap");
    f.panel.Render(f.root,f.view,480,260,10);
    downHook(foreign,Make("MouseDown"),nullptr);
    f.panel.Render(f.root,f.view,40,60,11);
    Get(f.Window()).dimensions["left"]=700;Get(f.Window()).dimensions["top"]=500;
    upHook(foreign,Make("MouseUp"),nullptr);
    Check(saves==2&&Get(f.Window()).dimensions["left"]==40&&Get(f.Window()).dimensions["top"]==60,
        "A group drag crossing a layout change must restore the new layout and never save stale coordinates");
}
void CompactNativeLockAndCaptureLayout() {
    Fixture f;
    Check(Get(f.Window()).type->name=="MagneticDragWindow"&&Get(f.Window()).dimensions["width"]==244,
        "Placement uses the compact native magnetic widget");
    Check(Get(itemSlot).dimensions["width"]==80&&Get(itemSlot).dimensions["height"]==80&&
        Get(itemSlot).dimensions["minWidth"]==80&&Get(itemSlot).dimensions["maxWidth"]==80&&
        Get(itemSlot).dimensions["flexShrink"]==0,"Readable native item size must not shrink under a long item name");
    Check(Get(spellSlot).dimensions["width"]==80&&Get(spellSlot).dimensions["height"]==80&&
        Get(spellSlot).dimensions["minWidth"]==80&&Get(spellSlot).dimensions["maxWidth"]==80&&
        Get(spellSlot).dimensions["flexShrink"]==0,"Native capture spell case must remain80px beside wrapping status text");
    auto panelContent=Get(f.Window()).fields.at("content");
    for(auto index:{0,1}) {
        const auto& label=Get(Get(panelContent).children[index]);
        Check(label.dimensions.at("red")>=0.9f&&label.dimensions.at("green")>=0.9f&&label.dimensions.at("fontSize")==14,
            "Requirement and stock must explicitly use readable light text on the native dark background");
    }
    const auto& name=Get(Get(Get(panelContent).children[2]).children[1]);
    Check(name.dimensions.at("minWidth")==0&&name.dimensions.at("flexGrow")==1&&name.enums.at("whiteSpace")==0,
        "Item name must wrap in the remaining compact width");
    auto options=Get(f.owner.cached.get()).fields.at("options"),lockOption=Get(options).fields.at("lockUI");
    f.BeginDrag(50,90);Get(lockOption).enabled=true;f.panel.Render(f.root,f.view);
    Check(!Get(f.Window()).movable&&!capture&&!globalDragging,"Global UI lock cancels only the owned gesture and locks its native header");
    Get(lockOption).enabled=false;f.panel.Render(f.root,f.view);Check(Get(f.Window()).movable,"Native UI unlock restores header movement");
    f.view.showEquipment=false;f.view.status="Capture active";f.panel.Render(f.root,f.view);
    Check(Get(f.Window()).dimensions["width"]==220,"Active combat narrows the compact capture-only panel");
    f.view.showEquipment=true;f.view.status.clear();f.panel.Render(f.root,f.view);
    auto content=Get(f.Window()).fields.at("content");
    Check(Get(Get(content).children[3]).enums["display"]==1,"No empty redundant status row in placement");
}
}
namespace fake {
Obj auxiliaryOne = nullptr, auxiliaryTwo = nullptr;
int auxiliaryVisits = 0, auxiliaryHits = 0;
bool FirstAuxiliary(Obj owner) { ++auxiliaryVisits; if(owner != auxiliaryOne) return false; ++auxiliaryHits; return true; }
bool SecondAuxiliary(Obj owner) { if(owner != auxiliaryTwo) return false; ++auxiliaryHits; return true; }
void ComposedAuxiliaryActions() {
    Fixture f;
    auxiliaryOne=Make("NotebookAction"); auxiliaryTwo=Make("TreasureAction");
    soul::SetAuxiliaryPanelAction(FirstAuxiliary);
    soul::SetAuxiliaryPanelAction(SecondAuxiliary);
    soul::SetAuxiliaryPanelAction(FirstAuxiliary);
    soul::SetAuxiliaryPanelAction(nullptr);
    auxiliaryVisits=auxiliaryHits=0;
    const int before=nativeActions;
    actionHook(auxiliaryOne,nullptr); actionHook(auxiliaryTwo,nullptr);
    actionHook(Make("ForeignAction"),nullptr);
    Check(auxiliaryHits==2 && auxiliaryVisits==3 && nativeActions==before+1,
        "Auxiliary registration must compose once and preserve foreign native actions");
    auxiliaryOne=auxiliaryTwo=nullptr;
}
void SpellMatchesActionBarAndHudLayer() {
    Fixture f;
    auto grid=Element("SlotGrid"),slot=Element("Slot");Add(f.root,grid);Add(grid,slot);
    Get(slot).enums["slotType"]=4;Get(slot).enums["slotSize"]=1;
    Get(slot).dimensions["width"]=64;Get(slot).dimensions["height"]=64;
    f.view.showEquipment=false;f.view.captureSpell=Make("ht");
    f.panel.Render(f.root,f.view);
    Check(Get(spellSlot).dimensions["width"]==64&&Get(spellSlot).dimensions["height"]==64&&Get(spellSlot).enums["slotSize"]==1,
        "Capture slot must match native action bar size and scale");
    Check(soul::GetUiRoot()==f.root,"Soul panel stays in HUD layer below tooltips/windows");
    Get(f.root).name="RootWindow";Check(!soul::GetUiRoot(),"Missing HUD layer must never fall back to document overlay");
}
}
int main() {
    try {
        fake::Initialize();
        soul::Panel preinstall;preinstall.Hide();
        fake::SpellMatchesActionBarAndHudLayer();
        fake::ComposedAuxiliaryActions();
        for(auto test:{fake::HeaderMenuSelectionAndReopen,fake::HeaderMenuRejectsStaleAndCleanupFailure,fake::CreationAndClicks,fake::EquippedItemRow,fake::CaptureStatusAndActionVisibility,fake::DragCompletionAndClamping,fake::LayoutApplicationAndDragRevision,fake::HideDuringDragAndForeignCapture,fake::LostCaptureStillClearsOnlyOwnedFlag,fake::DetachedAndFailureCleanup,fake::NativeSubscriptionsAndPartialInit,fake::ImmediateSnapAndNativeGroupMove,fake::CompactNativeLockAndCaptureLayout}) {
            test();fake::Check(fake::handles.empty(),"Leaked 64-bit GC refs");
        }
        std::cout<<"PASS: soul panel native contracts, click isolation, clamped drag, mid-drag cleanup, foreign capture, detached UI and failures\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

namespace soul {
managed::Obj CreateSpellView(managed::Obj parent) { fake::spellSlot=fake::Element("SpellSlot");fake::Add(parent,fake::spellSlot);return fake::spellSlot; }
bool UpdateSpellView(managed::Obj spell,bool enabled) { fake::spellBound=spell;fake::spellEnabled=enabled;return true; }
void ReleaseSpellView() { if(fake::spellSlot)fake::Detach(fake::spellSlot);fake::spellSlot=fake::spellBound=nullptr;fake::spellEnabled=false; }
}
