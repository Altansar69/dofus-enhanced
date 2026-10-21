#include "notebook-data.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Fn> void Reject(Fn action, const char* message) {
    bool failed=false;try {action();} catch(const std::exception&) {failed=true;}Check(failed,message);
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("DofusNotebookTests-"+std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::create_directory(root);
        const auto path=notebook::DataPath(root/"settings.ini");
        Check(notebook::Read(path).entries.empty(),"Missing data is empty");
        notebook::Entry position;position.title="Zaap forêt";position.x=-5;position.y=7;position.world=1;position.map=123;
        auto first=notebook::Save(path,position);
        notebook::Entry note;note.kind=notebook::Kind::Note;note.title="Stratégie";note.body="Étourdi\nDeuxième ligne <b>littérale</b>";
        auto second=notebook::Save(path,note);
        notebook::Entry message;message.kind=notebook::Kind::Message;message.title="Recrutement";message.body="Bonjour !";message.channel=64;
        auto third=notebook::Save(path,message);
        Check(first.id&&second.id!=first.id&&third.id!=second.id,"IDs are unique");
        const auto data=notebook::Read(path);
        Check(data.entries.size()==3&&data.entries[1]==second,"UTF8 multiline survives reload");
        Check(data.entries[2].channel==64,"The chat channel persists with the message");
        {auto bad=message;bad.channel=3;Reject([&]{notebook::Save(path,bad);},"Unknown chat channel rejected");}
        {auto bad=note;bad.channel=4;Reject([&]{notebook::Save(path,bad);},"Only messages carry a channel");}
        Check(notebook::ValidChannel(0)&&notebook::ValidChannel(64)&&!notebook::ValidChannel(5)&&notebook::NextChannel(0)==1&&notebook::NextChannel(64)==0&&
            std::string(notebook::ChannelName(4))=="Guilde"&&std::string(notebook::ChannelName(0))=="actuel","Channel cycle and names");
        {const auto legacy=root/"legacy.json";{std::ofstream file(legacy,std::ios::binary);file<<R"({"schema":1,"nextId":2,"entries":[{"id":1,"version":1,"kind":"message","title":"t","body":"b","x":0,"y":0,"world":1,"map":0}]})";}
            Check(notebook::Read(legacy).entries.at(0).channel==0,"Entries saved before 1.4.0 load on the current channel");}
        auto stale=first;first.title="Nouveau titre";first=notebook::Save(path,first);
        stale.title="Écriture périmée";Reject([&]{notebook::Save(path,stale);},"Stale edit must conflict");
        Reject([&]{notebook::Delete(path,stale.id,stale.version);},"Stale delete must conflict");
        Check(notebook::Read(path).entries.size()==3&&notebook::Read(path).entries[1]==second,"Unrelated records preserved");
        auto lockPath=path;lockPath+=L".lock";
        const auto lock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(lock!=INVALID_HANDLE_VALUE,"Fixture lock");
        Reject([&]{notebook::Save(path,note);},"Concurrent writer lock must reject");
        CloseHandle(lock);
        const auto before=Bytes(path);
        Check(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Fixture readonly");
        Reject([&]{notebook::Save(path,note);},"Atomic replacement failure must reject");
        SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
        Check(Bytes(path)==before,"Failed write preserves exact original");
        for(const auto& file:std::filesystem::directory_iterator(root))
            Check(file.path().filename().wstring().find(L".tmp-")==std::wstring::npos,"Temporary file cleanup");
        notebook::Delete(path,second.id,second.version);
        Check(notebook::Read(path).entries.size()==2,"Delete persists");
        message.body="première\nseconde";Reject([&]{notebook::Save(path,message);},"Messages cannot contain command/newline controls");
        note.body=std::string(17000,'x');Reject([&]{notebook::Save(path,note);},"Notes bounded");
        position.title=" ";Reject([&]{notebook::Save(path,position);},"Blank title rejected");
        position.title="test";position.x=1001;Reject([&]{notebook::Save(path,position);},"Coordinates bounded");
        int coordinate=0;Check(notebook::Coordinate("-25",coordinate)&&coordinate==-25,"Signed coordinates");
        Check(!notebook::Coordinate("3foo",coordinate)&&!notebook::Coordinate("1001",coordinate),"Invalid coordinate input");
        {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<"{private secret malformed";}
        const auto corrupt=Bytes(path);
        Reject([&]{notebook::Read(path);},"Corrupt data must report error");
        position.x=0;Reject([&]{notebook::Save(path,position);},"Corrupt document must not be overwritten");
        Check(Bytes(path)==corrupt,"Corrupt file unchanged");
        try {notebook::Read(path);}catch(const std::exception& e){Check(std::string(e.what()).find("secret")==std::string::npos,"Diagnostic must not reveal private content");}
        std::filesystem::remove_all(root);
        std::cout<<"PASS: notebook persistence, UTF8, concurrent conflicts, locks, atomic failures and corruption preservation\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';std::error_code ignored;std::filesystem::remove_all(root,ignored);return 1;}
}
