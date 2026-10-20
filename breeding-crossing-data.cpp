#include "breeding-crossing-data.hpp"
#include <windows.h>
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <cwctype>
#include <atomic>

namespace breedingCrossing {
const std::array<const char*,11>& Colors() {
    static const std::array<const char*,11> colors={
        "Dorée","Amande","Rousse","Indigo","Ébène","Pourpre","Orchidée","Ivoire","Turquoise","Émeraude","Prune"};
    return colors;
}
const std::vector<Crossing>& Crossings() {
    static const auto values=[] {
        constexpr int generations[]={1,1,1,3,3,5,5,7,7,9,9};
        std::vector<Crossing> result;
        for(int i=0;i<11;++i) for(int j=i;j<11;++j) {
            const int generation=i==j?generations[i]:generations[j]+1;
            // DofusDB documents durations for pure-color rows only.
            result.push_back({i,j,generation,i==j?48+12*(generation-1):0,
                std::string(Colors()[i])+(i==j?"":" et "+std::string(Colors()[j]))});
        }
        return result;
    }();
    return values;
}
Selection Parse(const std::string& text) {
    constexpr const char* prefix="DofusMiniatures.Crossings.1\n";
    const std::string header(prefix);
    if(text.size()!=header.size()+67 || text.compare(0,header.size(),header)!=0 || text.back()!='\n')
        throw std::runtime_error("Fichier de croisements invalide.");
    Selection result{};
    for(size_t i=0;i<result.size();++i) {
        const char c=text[header.size()+i];
        if(c!='0'&&c!='1') throw std::runtime_error("Valeur de croisement invalide.");
        result[i]=c=='1';
    }
    return result;
}
std::string Serialize(const Selection& value) {
    std::string result="DofusMiniatures.Crossings.1\n";
    for(bool checked:value) result+=checked?'1':'0';
    return result+'\n';
}
namespace {
void Plain(const std::filesystem::path& path,bool mayBeAbsent) {
    auto absolute=std::filesystem::absolute(path).lexically_normal();
    for(auto current=absolute;!current.empty();current=current.parent_path()) {
        const auto attrs=GetFileAttributesW(current.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES) {
            if(current==absolute&&mayBeAbsent&&GetLastError()==ERROR_FILE_NOT_FOUND) continue;
            throw std::runtime_error("Chemin de croisements inaccessible.");
        }
        if(attrs&FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Chemin de croisements indirect.");
        if(current==current.parent_path()) break;
    }
}
struct Lock {
    HANDLE handle=nullptr;
    explicit Lock(const std::filesystem::path& path) {
        uint64_t hash=14695981039346656037ull;
        for(wchar_t c:std::filesystem::absolute(path).lexically_normal().wstring()) {
            hash^=static_cast<uint16_t>(std::towlower(c));hash*=1099511628211ull;
        }
        const auto name=L"Local\\DofusMiniaturesCrossings-"+std::to_wstring(hash);
        handle=CreateMutexW(nullptr,FALSE,name.c_str());
        if(!handle)throw std::runtime_error("Verrou des croisements indisponible.");
        auto wait=WaitForSingleObject(handle,0);
        if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("Les croisements sont occupés.");}
    }
    ~Lock(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}
};
}
Selection Read(const std::filesystem::path& path) {
    Plain(path,true);
    if(!std::filesystem::exists(path))return {};
    if(std::filesystem::file_size(path)>256)throw std::runtime_error("Fichier de croisements trop grand.");
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Lecture des croisements impossible.");
    std::string text((std::istreambuf_iterator<char>(input)),{});
    if(input.bad())throw std::runtime_error("Lecture des croisements interrompue.");
    return Parse(text);
}
Selection Set(const std::filesystem::path& path,size_t index,bool checked) {
    if(index>=66)throw std::runtime_error("Croisement hors limites.");
    Lock lock(path);
    auto selection=Read(path);selection[index]=checked;
    auto temporary=path;
    static std::atomic_uint64_t sequence{0};
    temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
    Plain(temporary,true);
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Création des croisements impossible.");
    const auto text=Serialize(selection);DWORD count=0;
    const bool written=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)&&count==text.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!written){DeleteFileW(temporary.c_str());throw std::runtime_error("Sauvegarde des croisements impossible.");}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temporary.c_str());throw std::runtime_error("Remplacement des croisements impossible.");
    }
    return selection;
}
std::filesystem::path StoragePath(const std::filesystem::path& settingsPath) {
    auto result=settingsPath;result+=L".crossings";return result;
}
Progress Completion(const Selection& selection,int generation) {
    if(generation<0||generation>10)throw std::runtime_error("Génération hors limites.");
    Progress result;
    const auto& list=Crossings();
    for(size_t i=0;i<list.size();++i)if(generation==0||list[i].generation==generation){
        ++result.total;if(selection[i])++result.completed;
    }
    return result;
}
}
