/* Wintosh prefix-relative preferences subset; MIT, see LICENSE-WINTOSH-ORIGINAL-MIT.txt. */
#include "darling_windows_preferences.h"
#include <windows.h>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>

struct darling_windows_preferences { std::filesystem::path path; std::map<std::string,std::string> values; std::mutex mutex; };
namespace {
std::string Esc(const std::string& v){std::string o;for(char c:v){if(c=='&')o+="&amp;";else if(c=='<')o+="&lt;";else if(c=='>')o+="&gt;";else if(c=='\"')o+="&quot;";else if(c=='\'')o+="&apos;";else o+=c;}return o;}
std::string Unesc(std::string v){const std::pair<const char*,const char*> e[]={{"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&apos;","'"}};for(auto [a,b]:e)for(size_t p=0;(p=v.find(a,p))!=std::string::npos;){v.replace(p,std::strlen(a),b);p+=std::strlen(b);}return v;}
bool Valid(const char* s){if(!s||!*s)return false;for(unsigned char c:std::string(s))if(!(std::isalnum(c)||c=='.'||c=='-'||c=='_'))return false;return true;}
std::filesystem::path Prefix(){char b[32768]{};auto n=GetEnvironmentVariableA("WINTOSH_GUEST_PREFIX",b,sizeof b);return n&&n<sizeof b?std::filesystem::u8path(std::string(b,n)):std::filesystem::path();}
bool Load(darling_windows_preferences& s){std::ifstream f(s.path,std::ios::binary);if(!f)return true;std::string x((std::istreambuf_iterator<char>(f)),{});std::regex r(R"(<key>([^<]+)</key>\s*<(string|integer|true|false)>([^<]*)</(string|integer|true|false)>)");for(std::sregex_iterator i(x.begin(),x.end(),r),e;i!=e;++i)if((*i)[2].str()==(*i)[4].str())s.values[Unesc((*i)[1].str())]=(*i)[2].str()=="string"?std::string("s:")+Unesc((*i)[3].str()):(*i)[2].str()=="integer"?std::string("i:")+(*i)[3].str():std::string("b:")+((*i)[2].str()=="true"?"1":"0");return true;}
bool Save(darling_windows_preferences& s){std::error_code ec;std::filesystem::create_directories(s.path.parent_path(),ec);auto t=s.path.wstring()+L".tmp";std::ofstream f(std::filesystem::path(t),std::ios::binary|std::ios::trunc);if(!f)return false;f<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"><dict>";for(auto&[k,v]:s.values){f<<"<key>"<<Esc(k)<<"</key>";if(v.rfind("s:",0)==0)f<<"<string>"<<Esc(v.substr(2))<<"</string>";else if(v.rfind("i:",0)==0)f<<"<integer>"<<v.substr(2)<<"</integer>";else f<<(v=="b:1"?"<true></true>":"<false></false>");}f<<"</dict></plist>";f.close();std::filesystem::remove(s.path,ec);ec.clear();std::filesystem::rename(std::filesystem::path(t),s.path,ec);if(!ec)return true;ec.clear();std::filesystem::copy_file(std::filesystem::path(t),s.path,std::filesystem::copy_options::overwrite_existing,ec);std::filesystem::remove(std::filesystem::path(t),ec);return !ec;}
}
extern "C" darling_windows_preferences_ref darling_windows_preferences_open(const char* d){if(!Valid(d))return nullptr;auto p=Prefix();if(p.empty())return nullptr;auto*s=new darling_windows_preferences;s->path=p/L"Users"/L"Default"/L"Library"/L"Preferences"/(std::filesystem::u8path(std::string(d)+".plist"));if(!Load(*s)){delete s;return nullptr;}return s;}
extern "C" void darling_windows_preferences_close(darling_windows_preferences_ref s){if(s){darling_windows_preferences_synchronize(s);delete s;}}
extern "C" bool darling_windows_preferences_get_string(darling_windows_preferences_ref s,const char*k,char*o,size_t n){if(!s||!k||!o||!n)return false;std::lock_guard l(s->mutex);auto i=s->values.find(k);if(i==s->values.end()||i->second.rfind("s:",0)!=0||i->second.size()-2>=n)return false;memcpy(o,i->second.data()+2,i->second.size()-2);o[i->second.size()-2]=0;return true;}
extern "C" bool darling_windows_preferences_get_integer(darling_windows_preferences_ref s,const char*k,int64_t*v){if(!s||!k||!v)return false;std::lock_guard l(s->mutex);auto i=s->values.find(k);if(i==s->values.end()||i->second.rfind("i:",0)!=0)return false;try{*v=std::stoll(i->second.substr(2));return true;}catch(...){return false;}}
extern "C" bool darling_windows_preferences_get_boolean(darling_windows_preferences_ref s,const char*k,bool*v){if(!s||!k||!v)return false;std::lock_guard l(s->mutex);auto i=s->values.find(k);if(i==s->values.end()||i->second.rfind("b:",0)!=0)return false;*v=i->second=="b:1";return true;}
extern "C" bool darling_windows_preferences_set_string(darling_windows_preferences_ref s,const char*k,const char*v){if(!s||!Valid(k)||!v)return false;std::lock_guard l(s->mutex);s->values[k]=std::string("s:")+v;return Save(*s);}
extern "C" bool darling_windows_preferences_set_integer(darling_windows_preferences_ref s,const char*k,int64_t v){if(!s||!Valid(k))return false;std::lock_guard l(s->mutex);s->values[k]="i:"+std::to_string(v);return Save(*s);}
extern "C" bool darling_windows_preferences_set_boolean(darling_windows_preferences_ref s,const char*k,bool v){if(!s||!Valid(k))return false;std::lock_guard l(s->mutex);s->values[k]=v?"b:1":"b:0";return Save(*s);}
extern "C" bool darling_windows_preferences_remove(darling_windows_preferences_ref s,const char*k){if(!s||!k)return false;std::lock_guard l(s->mutex);s->values.erase(k);return Save(*s);}
extern "C" bool darling_windows_preferences_synchronize(darling_windows_preferences_ref s){if(!s)return false;std::lock_guard l(s->mutex);return Save(*s);}
