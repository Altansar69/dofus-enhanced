#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
namespace fake { ULONGLONG now=100; ULONGLONG FakeTick(){return now;} }
#define GetTickCount64 fake::FakeTick
#include "maps/map-transitions.cpp"
#undef GetTickCount64

namespace fake {
bool combat=false, lookupFails=false, nativeFails=false;
int calls=0, hooks=0;
float received=0;
managed::Obj receivedSprite=nullptr;
const Color4* receivedColor=nullptr;
managed::Method receivedMethod=nullptr;
std::vector<std::string> logs;
ColorFn installedHook=nullptr;
int spriteStorage, tweenStorage, methodStorage;
Color4 color{0.2f,0.3f,0.4f,1.0f};
void Check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
managed::Obj Original(managed::Obj sprite,const Color4* value,float duration,managed::Method method) {
    ++calls;received=duration;receivedSprite=sprite;receivedColor=value;receivedMethod=method;
    if(nativeFails)throw std::runtime_error("native tween creation failure");
    return &tweenStorage;
}
void Run(float duration,uintptr_t callsite,float expected) {
    const auto before=calls;
    auto result=DispatchColor(&spriteStorage,&color,duration,&methodStorage,managed::gameBase+callsite+5);
    Check(calls==before+1&&result==&tweenStorage,"Always return the one native tween without completing callbacks");
    Check(receivedSprite==&spriteStorage&&receivedColor==&color&&receivedMethod==&methodStorage,"Preserve native pointer arguments and Color ABI");
    Check(std::isnan(expected)?std::isnan(received):received==expected,"Wrong scoped transition duration");
}
}
namespace managed {
Api api{};uintptr_t gameBase=0x180000000;
Klass Api::Class(const char* image,const char* space,const char* name)const {
    fake::Check(!std::strcmp(image,"DOTween.Modules.dll")&&!std::strcmp(space,"DG.Tweening")&&!std::strcmp(name,"DOTweenModuleSprite"),"Use exact native tween module");return reinterpret_cast<Klass>(1);
}
Method Api::FindVerified(Klass klass,const char* name,int count,uintptr_t rva)const {
    fake::Check(klass==reinterpret_cast<Klass>(1)&&!std::strcmp(name,"DOColor")&&count==3&&rva==0x30AAEC0,"Verify native three-parameter sprite tween overload");return reinterpret_cast<Method>(2);
}
void* Api::NativeAt(uintptr_t rva)const {
    fake::Check(rva==FadeOutCall||rva==FadeInCall,"Verify both exact native callsites");return reinterpret_cast<void*>(gameBase+rva);
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path) {}
bool Module::InCombat()const {if(fake::lookupFails)throw std::runtime_error("fight unavailable");return fake::combat;}
void Log(const std::string& text) {fake::logs.push_back(text);}
void AddHook(managed::Method method,uintptr_t rva,void* replacement,void** original) {
    fake::Check(method==reinterpret_cast<managed::Method>(2)&&rva==0x30AAEC0,"Only the guarded tween function is hooked");
    fake::installedHook=reinterpret_cast<ColorFn>(replacement);*original=reinterpret_cast<void*>(&fake::Original);++fake::hooks;
}
int main() {
    using namespace fake;
    try {
        InstallFastMapTransitions();Check(hooks==1,"One hook only");
        Run(0.5f,FadeOutCall,0.5f); // No session.
        Module owner(std::filesystem::path{});module=&owner;
        owner.ready=true;owner.servicesReady=true;
        owner.settings.fastMapTransitions=false;
        Run(0.5f,FadeOutCall,0.5f); // Explicit opt-out is respected.
        owner.settings.fastMapTransitions=true;
        Run(0.5f,FadeOutCall,0.01f);Run(0.4f,FadeInCall,0.01f);
        Run(0.5f,FadeInCall+1,0.5f);Run(0.5f,0x123456,0.5f);
        installedHook(&spriteStorage,&color,0.5f,&methodStorage);Check(received==0.5f,"Other native sprite tweens remain unchanged");
        for(float duration:{0.0f,-1.0f,0.005f,0.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})Run(duration,FadeOutCall,duration);
        combat=true;Run(0.5f,FadeOutCall,0.5f);combat=false;
        owner.ready=false;Run(0.5f,FadeOutCall,0.5f);owner.ready=true;
        owner.servicesReady=false;Run(0.5f,FadeOutCall,0.5f);owner.servicesReady=true;
        owner.stopped=true;Run(0.5f,FadeOutCall,0.5f);owner.stopped=false;
        owner.stopRequested=true;Run(0.5f,FadeOutCall,0.5f);owner.stopRequested=false;
        lookupFails=true;Run(0.5f,FadeOutCall,0.5f);lookupFails=false;
        nativeFails=true;const auto before=calls;bool threw=false;
        try{Run(0.5f,FadeOutCall,0.01f);}catch(const std::exception&){threw=true;}
        Check(threw&&calls==before+1,"Native failure must propagate without repeating the native tween");nativeFails=false;
        owner.settings.fastMapTransitions=false;Run(0.5f,FadeOutCall,0.5f);
        Run(0.2f,FadeOutCall,0.2f);
        Check(MapTransitionLoading(),"Observe native loading even when fast fades are off");
        now+=500;Run(0.2f,FadeInCall,0.2f);
        Check(!MapTransitionLoading() && logs.back().find("fadeWindowMs=500")!=std::string::npos,"Native fade-in ends and times the matching window");
        Run(0.2f,FadeOutCall,0.2f);now+=4999;Check(MapTransitionLoading(),"Loading window stays bounded but active");
        ++now;Check(!MapTransitionLoading(),"Missing fade-in must not hide glow indefinitely");
        Run(0.2f,FadeOutCall,0.2f);combat=true;Check(!MapTransitionLoading(),"Combat releases observer");combat=false;
        Run(0.2f,FadeOutCall,0.2f);owner.stopRequested=true;Check(!MapTransitionLoading(),"Stop releases observer");owner.stopRequested=false;
        nativeFails=true;try{Run(0.2f,FadeOutCall,0.2f);}catch(const std::exception&){}
        Check(!MapTransitionLoading(),"Failed tween creation must not begin loading observation");nativeFails=false;
        module=nullptr;
        std::cout<<"PASS: native fade callsite isolation, opt-in/context gates, ABI and tween lifecycle preserved\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
