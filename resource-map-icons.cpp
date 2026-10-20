#include "resource-map-icons.hpp"

namespace resourceMap {
namespace {
using namespace managed;
Klass addresses{}, entry{}, entryType{};
Method initialize{}, construct{}, load{};
void* itemTypeField{};
}
void InstallIcons() {
    if (load) return;
    addresses = api.Class("Core.dll", "", "rz");
    constexpr const char* assembly = "Ankama.AddressableUtility.Runtime.dll";
    constexpr const char* space = "Ankama.AddressableUtilities.Runtime";
    entry = api.Class(assembly, space, "AddressableEntry");
    entryType = api.Class(assembly, space, "AddressableEntryType");
    itemTypeField = api.class_get_field_from_name(addresses, "deuc");
    // mc.mlu constructs the normal item image using rz's entry type at 0x60.
    if (!itemTypeField || api.field_get_offset(itemTypeField) != 0x60)
        throw std::runtime_error("Resource item icon catalog layout mismatch");
    initialize = api.FindVerified(addresses, "qbk", 0, 0x1E2A0D0);
    construct = api.FindVerified(entry, ".ctor", 2, 0x68D5C0);
    load = api.FindVerified(api.Class("Core.dll", "", "pq"), "paw", 7, 0x2E91370);
}
managed::Obj IconAddress(void* typeField,int iconId) {
    if (!load || iconId <= 0) throw std::runtime_error("Invalid resource icon");
    // This native getter initializes rz. Read the complete boxed value type;
    // StaticField's pointer-sized destination is unsuitable for this struct.
    Ref initialized(api.Invoke(initialize, nullptr));
    Ref type(api.field_get_value_object(typeField, nullptr));
    if (!type.get() || api.object_get_class(type.get()) != entryType)
        throw std::runtime_error("Resource item icon type unavailable");
    Ref result(api.object_new(entry));
    api.Invoke(construct, api.object_unbox(result.get()), {api.object_unbox(type.get()), &iconId});
    return result.get();
}
managed::Obj ItemIconAddress(int iconId) {return IconAddress(itemTypeField,iconId);}
void LoadItemIcon(managed::Obj element, int iconId) {
    Ref address(ItemIconAddress(iconId));
    bool no = false, yes = true;
    // Same null callbacks/token and quality flags as CheckboxCustom's
    // CreateIconFromAddress. The managed cache owns any pending completion;
    // it can finish on a detached element without calling back into this DLL.
    api.Invoke(load, nullptr, {element, api.object_unbox(address.get()), nullptr, nullptr, nullptr, &no, &yes});
}
void LoadJobIcon(managed::Obj element,int jobId) {
    const int icon = RepresentativeResourceIcon(Catalog(), jobId);
    if (!icon) throw std::runtime_error("Unknown harvest job resource icon");
    LoadItemIcon(element, icon);
}
}
