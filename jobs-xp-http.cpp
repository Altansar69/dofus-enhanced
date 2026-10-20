#include "jobs-xp-data.hpp"
#include <windows.h>
#include <winhttp.h>
#include <memory>
#include <stdexcept>

namespace jobsxp {
namespace {
struct Close { void operator()(void* h) const {if(h)WinHttpCloseHandle(h);} };
using Internet=std::unique_ptr<void,Close>;
void Require(bool okay) {if(!okay)throw std::runtime_error("DofusDB est injoignable. Clique sur Actualiser pour réessayer.");}
}
Page Fetch(const Query& query,const std::atomic_bool& cancelled) {
    const auto path=RequestPath(query); const auto deadline=GetTickCount64()+20000;
    auto active=[&]{if(cancelled.load()||GetTickCount64()>deadline)throw std::runtime_error("Recherche interrompue.");};
    active();
    Internet session(WinHttpOpen(L"DofusMiniatures (jobs XP planner)",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    Require(!!session);Require(WinHttpSetTimeouts(session.get(),4000,4000,4000,4000));
    Internet connection(WinHttpConnect(session.get(),L"api.dofusdb.fr",INTERNET_DEFAULT_HTTPS_PORT,0));Require(!!connection);
    Internet request(WinHttpOpenRequest(connection.get(),L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));Require(!!request);
    DWORD disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_AUTHENTICATION;
    Require(WinHttpSetOption(request.get(),WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)));
    active();Require(WinHttpSendRequest(request.get(),L"Accept: application/json\r\n",DWORD(-1),nullptr,0,0,0));
    Require(WinHttpReceiveResponse(request.get(),nullptr));
    DWORD status=0,size=sizeof(status);
    Require(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX));
    if(status==429)throw std::runtime_error("DofusDB demande de patienter. Réessaie dans un instant.");
    Require(status==200);
    std::string body;char buffer[16384];DWORD read=0;
    do {
        active();Require(WinHttpReadData(request.get(),buffer,sizeof(buffer),&read));
        if(body.size()+read>4*1024*1024)throw std::runtime_error("Réponse DofusDB trop volumineuse.");
        body.append(buffer,read);
    }while(read);
    active();return ParsePage(body,query);
}
}
