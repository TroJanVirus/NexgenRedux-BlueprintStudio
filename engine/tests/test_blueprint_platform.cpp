// Real native filesystem staging (no GUI, no simulated TestPlatform).
#if defined(_WIN32)
#include "platform/windows/platform_windows.h"
using NativePlatform = nx::PlatformWindows;
#else
#include "platform/posix/platform_posix.h"
using NativePlatform = nx::PlatformPosix;
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;
namespace
{
    class FilePlatform final : public NativePlatform
    {
    public:
        bool init(const fs::path& root)
        {return setFolders(root.c_str(),root.c_str());}
        void drop(const fs::path& file){keepOwn(file.c_str());}
        bool openWindow(const nx::WindowDesc&) override {return false;}
        void* nativeWindow() override {return nullptr;}
        bool pumpEvents() override {return false;}
        const nx::InputState& input() const override {return m_state;}
        nx::u64 totalMemory() override {return 0;}
        nx::u64 freeMemory() override {return 0;}
        const char* name() const override {return "Native filesystem test";}
    private:
        nx::InputState m_state;
    };
    struct Temp
    {
        fs::path path;
        ~Temp(){if(!path.empty()){std::error_code ec;fs::remove_all(path,ec);}}
    };
    bool write(const fs::path& path,const std::string& content)
    {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out.write(content.data(),static_cast<std::streamsize>(content.size()));
        out.close();return !out.fail();
    }
    bool bytes(FilePlatform& p,const std::string& file,const std::string& expected)
    {
        nx::FileData data;
        return p.readFile(("own/"+file).c_str(),data,nx::MemTag::General)&&
            std::string(reinterpret_cast<const char*>(data.data),data.size)==expected;
    }
    bool dropped(FilePlatform& p,const fs::path& source,const std::string& expected)
    {
        p.drop(source);std::string name;
        return p.takeDropped(name)&&name==expected&&!p.takeDropped(name);
    }
}
#define CHECK(condition) do { if(!(condition)){std::cerr<<"Failed at line "<<__LINE__<<": "<<#condition<<"\n";return 1;} } while(false)
int main()
{
    std::error_code ec;
    const auto base=fs::temp_directory_path(ec);
    CHECK(!ec);
    Temp temp;
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned i=0;i<100;++i)
    {
        auto candidate=base/("nx-blueprint-files-"+std::to_string(stamp)+"-"+std::to_string(i));
        if(fs::create_directory(candidate,ec)){temp.path=std::move(candidate);break;}
        CHECK(!ec);
    }
    CHECK(!temp.path.empty());
    const auto incoming=temp.path/"incoming";
    CHECK(fs::create_directory(incoming,ec)&&!ec);
    FilePlatform p;
    CHECK(p.init(temp.path));
    const std::string old("original\0bytes",14), fresh("new\0bytes",9);
    // Reserved files are protected even before a workspace takes the lease.
    for(const std::string reserved:{"blueprint-recovery.bp.json","blueprint-project-test.bp.json"})
    {
        CHECK(p.writeFile(("own/"+reserved).c_str(),old.data(),old.size()));
        CHECK(write(incoming/reserved,fresh));
        CHECK(dropped(p,incoming/reserved,"import-"+reserved));
        CHECK(bytes(p,reserved,old)&&bytes(p,"import-"+reserved,fresh));
    }
    CHECK(p.acquireBlueprintLease());
    CHECK(!p.acquireBlueprintLease());
    for(const std::string file:{"example.as","texture.png","example.bp.json"})
    {
        CHECK(p.writeFile(("own/"+file).c_str(),old.data(),old.size()));
        CHECK(write(incoming/file,fresh));
        const auto staged=nx::blueprintDropName(file,1);
        CHECK(dropped(p,incoming/file,staged));
        CHECK(bytes(p,file,old)&&bytes(p,staged,fresh));
        CHECK(dropped(p,incoming/file,staged)); // identical second import
        CHECK(!p.fileTime(("own/"+nx::blueprintDropName(file,2)).c_str()));
        CHECK(write(incoming/file,"third"));
        CHECK(dropped(p,incoming/file,nx::blueprintDropName(file,2)));
        CHECK(bytes(p,file,old)&&bytes(p,staged,fresh));
    }
    // Near the native input name limit: numbered name must not truncate.
    const std::string longName=std::string(52,'a')+".bp.json";
    CHECK(p.writeFile(("own/"+longName).c_str(),old.data(),old.size()));
    CHECK(write(incoming/longName,fresh));
    CHECK(dropped(p,incoming/longName,nx::blueprintDropName(longName,1)));
    CHECK(bytes(p,longName,old)&&bytes(p,nx::blueprintDropName(longName,1),fresh));
    p.releaseBlueprintLease();
    CHECK(p.acquireBlueprintLease());
    p.releaseBlueprintLease();
    std::cout<<"Native drop collision and lease filesystem checks passed.\n";
    return 0;
}
