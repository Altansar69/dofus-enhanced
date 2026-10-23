#pragma once
#include "core/runtime.hpp"

namespace soul {
// Since 3.5, absence of SoulCaptureForbidden does not authorize an ordinary
// monster capture. Both combat and the world-menu action use this same gate.
class MonsterCaptureData {
    managed::Klass type{};
    managed::Method find{}, forbidden{}, boss{}, arch{};
public:
    void Install() {
        using namespace managed;
        type = api.Class("Ankama.Dofus.Core.DataCenter.dll","Core.DataCenter.Metadata.Monster","MonsterData");
        const auto verify = [&](const char* name,int argc,uintptr_t rva) {
            auto method = api.Find(type,name,argc); api.Native(method,rva); return method;
        };
        find = verify("GetById",1,0x7E76B0);
        forbidden = verify("get_soulCaptureForbidden",0,0x7C4310);
        boss = verify("get_isBoss",0,0x7C43C0);
        arch = verify("get_isMiniBoss",0,0x7C43B0);
    }
    managed::Obj Find(int64_t id) const {
        return id > 0 ? managed::api.Invoke(find,nullptr,{&id}) : nullptr;
    }
    bool Eligible(managed::Obj monster) const {
        using namespace managed;
        if (!monster || api.object_get_class(monster) != type ||
            api.Value<bool>(api.Invoke(forbidden,monster))) return false;
        if (!api.Value<bool>(api.Invoke(boss,monster)) && !api.Value<bool>(api.Invoke(arch,monster))) return false;
        // Some boss variants have no capturable soul. Ordinary quest souls use
        // automatic quest drops and must not authorize this stone/spell helper.
        Ref souls(api.Field(monster,"souls"));
        if (!souls.get()) return false;
        const int count = api.Value<int>(api.Call(souls.get(),"get_Count"));
        return count > 0 && count <= 32;
    }
};
}
