#include "tools/blueprint/native_assets.h"
#include "platform/platform.h"
#include "base/json_tree.h"
#include "base/json_value.h"
#include "assets/decode.h"
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <ctime>

namespace nx
{
    namespace
    {
        constexpr size_t kMaxFiles=4096,kMaxFile=64*1024*1024,kMaxTotal=256*1024*1024;
        bool folderId(const std::string& s)
        {return s=="Screen"||blueprintAssetName(s,true);}
        bool directoryRoot(const std::string& s)
        {return blueprintAssetRoot(s)||(s.rfind("bp-",0)==0&&s.size()<=32&&s.size()>3&&s.find_first_not_of("0123456789-",3)==std::string::npos);}
        bool ensure(Platform& p,const std::string& path)
        {return p.createOwnFolder(path.c_str())||p.fileTime(("own/"+path).c_str())!=0;}
        bool children(Platform& p,const std::string& path,bool folders,std::vector<std::string>& names)
        {
            struct Context {std::vector<std::string>& names;bool overflow=false;} ctx{names};
            p.list(path.c_str(),folders,[](const char* name,void* user)
            {
                auto& c=*static_cast<Context*>(user);
                if(c.names.size()>=kMaxFiles)c.overflow=true;
                else c.names.emplace_back(name);
            },&ctx);
            std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
            return !ctx.overflow;
        }
        bool collect(Platform& p,const std::string& base,const std::string& rel,unsigned depth,size_t& directories,std::vector<std::string>& result,std::string& error)
        {
            if(depth>4||rel.size()>80||++directories>1024){error="Asset folder count, nesting or path limit exceeded.";return false;}
            std::vector<std::string> files,folders;
            const auto path=base+(rel.empty()?"":"/"+rel);
            if(!children(p,path,false,files)||!children(p,path,true,folders)){error="Too many assets.";return false;}
            for(const auto& file:files)
            {
                if(!blueprintAssetName(file)||result.size()>=kMaxFiles){error="Invalid or excessive asset names in "+path;return false;}
                const auto relative=rel.empty()?file:rel+"/"+file;
                if(relative.size()>90){error="Asset path is too long: "+relative;return false;}
                result.push_back(relative);
            }
            for(const auto& folder:folders)
            {
                if(!blueprintAssetName(folder,true)){error="Invalid asset folder: "+folder;return false;}
                if(!collect(p,base,rel.empty()?folder:rel+"/"+folder,depth+1,directories,result,error))return false;
            }
            return true;
        }
        bool copy(Platform& p,const std::string& from,const std::string& to,size_t& total,std::string& error)
        {
            if(p.fileTime(to.c_str())){error="Asset already exists: "+to;return false;}
            FileData data;
            if(!p.readFile(from.c_str(),data,MemTag::General)||data.size>kMaxFile||data.size>kMaxTotal-total)
            {error="Cannot read asset or asset size limit exceeded: "+from;return false;}
            total+=data.size;
            if(!p.writeFile(to.c_str(),data.data,data.size)){error="Cannot write "+to;return false;}
            FileData check;
            if(!p.readFile(to.c_str(),check,MemTag::General)||check.size!=data.size||std::memcmp(check.data,data.data,data.size)!=0)
            {error="Asset copy verification failed: "+to;return false;}
            return true;
        }
        bool copyShared(Platform& p,const std::string& source,const std::string& destination,const std::vector<std::string>& files,size_t& total,std::string& error)
        {
            if(files.empty())return true;
            const auto dest=destination+"/_global",base="own/"+source+"/_global/";
            if(!ensure(p,dest)){error="Cannot create shared asset directory.";return false;}
            for(const auto& file:files)
            {
                for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                    if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create shared asset subfolder.";return false;}
                if(!copy(p,base+file,"own/"+dest+"/"+file,total,error))return false;
            }
            return true;
        }
    }
    bool blueprintCreateAssetRoot(Platform& p,std::string& root,std::string& error)
    {
        static unsigned sequence=0;
        for(unsigned i=0;i<100;++i)
        {
            const auto candidate="bpa-"+std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence);
            if(blueprintAssetRoot(candidate)&&p.createOwnFolder(candidate.c_str())){root=candidate;error.clear();return true;}
        }
        error="Cannot create project asset directory.";return false;
    }
    bool blueprintEnsureAssetFolder(Platform& p,const std::string& root,const std::string& screen,std::string& error)
    {
        if(!directoryRoot(root)||!folderId(screen)||!p.fileTime(("own/"+root).c_str()))
        {error="Invalid or missing project asset root.";return false;}
        if(!ensure(p,root+"/"+screen)||!ensure(p,root+"/"+screen+"/asset"))
        {error="Cannot create screen asset directory.";return false;}
        error.clear();return true;
    }
    bool blueprintListAssets(Platform& p,const std::string& root,const std::string& screen,std::vector<std::string>& files,std::string& error)
    {
        if(root.empty()){files.clear();error.clear();return true;}
        if(!blueprintAssetRoot(root)||!folderId(screen)||!p.fileTime(("own/"+root).c_str()))
        {error="Missing or invalid project asset root.";return false;}
        std::vector<std::string> result;size_t directories=0;
        if(!collect(p,"own/"+root+"/"+screen+"/asset","",0,directories,result,error))return false;
        std::sort(result.begin(),result.end());files.swap(result);error.clear();return true;
    }
    bool blueprintCopyAssets(Platform& p,const std::string& source,const std::string& destination,const std::string& screen,std::string& error)
    {
        std::vector<std::string> files;
        if(!blueprintListAssets(p,source,screen,files,error))return false;
        if(files.empty())return true;
        if(source==destination){error="Source and destination asset folders must differ.";return false;}
        if(!blueprintEnsureAssetFolder(p,destination,screen,error))return false;
        const auto dest=destination+"/"+screen+"/asset",base="own/"+source+"/"+screen+"/asset/";
        size_t total=0;
        for(const auto& file:files)
        {
            for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create asset subfolder.";return false;}
            if(!copy(p,base+file,"own/"+dest+"/"+file,total,error))return false;
        }
        error.clear();return true;
    }
    bool blueprintPrepareSceneEditor(Platform& p,const std::string& root,const std::string& screen,std::string& error)
    {
        if(!blueprintAssetRoot(root)||!blueprintEnsureAssetFolder(p,root,screen,error))return false;
        const auto base="own/"+root+"/"+screen+"/asset/";
        const auto manifest=base+"scenes.json";
        const auto readScene=[&](const std::string& file)
        {
            FileData data;JsonTree tree;
            if(!p.readFile((base+file).c_str(),data,MemTag::General)||data.size>1024*1024||
                !tree.parse(reinterpret_cast<const char*>(data.data),data.size)||!tree.root().isObject())
            {error="Scene JSON is missing, too large or invalid: "+file;return false;}
            return true;
        };
        if(p.fileTime(manifest.c_str()))
        {
            FileData data;JsonTree tree;
            if(!p.readFile(manifest.c_str(),data,MemTag::General)||data.size>1024*1024||
                !tree.parse(reinterpret_cast<const char*>(data.data),data.size)||!tree.root().isObject())
            {error="Invalid native scenes.json. Existing assets were not changed.";return false;}
            auto scenes=tree.root()["scenes"];
            if(!scenes.isArray()||!scenes.count()||scenes.count()>12){error="Invalid native scene list.";return false;}
            std::vector<std::string> seen;
            for(auto s=scenes.first();s.exists();s=s.next())
            {
                const std::string name=s["file"].text();
                const bool valid=name=="scene"||(name.size()>5&&name.size()<=11&&name.rfind("scene",0)==0&&name.find_first_not_of("0123456789",5)==std::string::npos);
                if(!valid||std::find(seen.begin(),seen.end(),name)!=seen.end()||!s["name"].isString())
                {error="Invalid or duplicate native scene entry.";return false;}
                seen.push_back(name);
                if(!readScene(name+".json"))return false;
            }
            error.clear();return true;
        }
        // Preserve an imported scene.json. A new screen starts empty, not with the showcase village.
        if(p.fileTime((base+"scene.json").c_str()))
        {
            if(!readScene("scene.json"))return false;
        }
        else
        {
            const std::string blank="{\"ground\":1,\"hour\":0,\"entities\":[]}";
            if(!p.writeFile((base+"scene.json").c_str(),blank.data(),blank.size())||!readScene("scene.json"))
            {error="Cannot create initial scene.";return false;}
        }
        const std::string list="{\"last\":0,\"scenes\":[{\"name\":\"Scene\",\"file\":\"scene\"}]}";
        if(!p.writeFile(manifest.c_str(),list.data(),list.size()))
        {error="Cannot create native scene list.";return false;}
        FileData check;
        if(!p.readFile(manifest.c_str(),check,MemTag::General)||check.size!=list.size()||std::memcmp(check.data,list.data(),list.size())!=0)
        {error="Could not verify native scene list.";return false;}
        error.clear();return true;
    }
    bool blueprintScenePreview(Platform& p,const std::string& root,const std::string& screen,std::string& json,unsigned& omitted,std::string& error)
    {
        json="[]";omitted=0;error.clear();
        if(!blueprintAssetRoot(root)||!folderId(screen)){error="Invalid scene preview path.";return false;}
        const auto base="own/"+root+"/"+screen+"/asset/";
        std::string name="scene";
        FileData manifest;
        if(p.readFile((base+"scenes.json").c_str(),manifest,MemTag::General))
        {
            JsonTree tree;
            if(manifest.size>1024*1024||!tree.parse(reinterpret_cast<const char*>(manifest.data),manifest.size))
            {error="Invalid scene preview manifest.";return false;}
            const auto scenes=tree.root()["scenes"],last=tree.root()["last"];
            const double index=last.isNumber()?last.exact():0;
            if(!scenes.isArray()||!scenes.count()||scenes.count()>12||!std::isfinite(index)||index<0||index>=scenes.count()||std::floor(index)!=index)
            {error="Invalid selected native scene.";return false;}
            name=scenes[static_cast<u32>(index)]["file"].text();
            if(name!="scene"&&!(name.size()>5&&name.size()<=11&&name.rfind("scene",0)==0&&name.find_first_not_of("0123456789",5)==std::string::npos))
            {error="Unsafe native scene filename.";return false;}
        }
        FileData file;
        if(!p.readFile((base+name+".json").c_str(),file,MemTag::General))return true;
        JsonTree tree;
        if(file.size>1024*1024||!tree.parse(reinterpret_cast<const char*>(file.data),file.size)||!tree.root().isObject())
        {error="Invalid native scene preview.";return false;}
        const auto entities=tree.root()["entities"];
        if(!entities.exists())return true;
        if(!entities.isArray()){error="Native scene entities are invalid.";return false;}
        JsonValue out;out.setArray();
        for(u32 i=0;i<entities.count();++i)
        {
            const auto entity=entities[i];
            if(i>=512||!entity.isObject()||(!entity["model"].isString()&&!entity["shape"].isString()&&!entity["sprite"].isString())){++omitted;continue;}
            // Explicit allowlist: no behaviour, cameras, sounds, physics, scripts,
            // triggers, emitters or arbitrary asset-loading callbacks.
            JsonValue safe;
            for(auto field=entity.first();field.exists();field=field.next())
            {
                const std::string key=field.key();
                if(key!="place"&&key!="angles"&&key!="size"&&key!="model"&&key!="skin"&&key!="sprite"&&key!="shape"&&key!="color"&&key!="shine"&&key!="glowing"&&key!="twoSided"&&key!="shadow"&&key!="views"&&key!="frames")continue;
                if(auto* value=safe.put(key))value->read(field);
            }
            if(auto* entry=out.add())entry->setCopy(safe);
        }
        json=out.write(true);return true;
    }
    std::string blueprintAssetImportPath(const std::string& file)
    {
        if(!blueprintAssetName(file))return "";
        const auto ext=file.substr(file.find_last_of('.')==std::string::npos?file.size():file.find_last_of('.'));
        for(const char* type:{".obj",".glb",".gltf",".stl",".fbx",".dae"})
            if(ext==type)return "model/"+file;
        for(const char* type:{".png",".jpg",".jpeg",".tga",".bmp",".dds"})
            if(ext==type)return "picture/"+file;
        for(const char* type:{".wav",".ogg",".mp3"})
            if(ext==type)return "sound/"+file;
        return file;
    }
    bool blueprintImportProjectFolderName(const std::string& folder)
    {
        if(!blueprintAssetName(folder,true))return false;
        if(folder.size()>8&&folder.rfind("project-",0)==0)return true;
        if(folder.rfind("bp-",0)!=0||folder.size()>32)return false;
        const auto stamp=folder.substr(3);
        const auto separator=stamp.find('-');
        return separator!=std::string::npos&&separator>0&&separator+1<stamp.size()&&
            stamp.substr(0,separator).find_first_not_of("0123456789")==std::string::npos&&
            stamp.substr(separator+1).find_first_not_of("0123456789")==std::string::npos;
    }
    bool blueprintCopySharedAssets(Platform& p,const std::string& source,const std::string& destination,std::string& error)
    {
        if(source.empty()){error.clear();return true;}
        if(!blueprintAssetRoot(source)||!directoryRoot(destination)||source==destination||
            !p.fileTime(("own/"+source).c_str())||!p.fileTime(("own/"+destination).c_str()))
        {error="Invalid shared asset copy roots.";return false;}
        const auto base="own/"+source+"/_global";
        if(!p.fileTime(base.c_str())){error.clear();return true;}
        std::vector<std::string> files;size_t directories=0,total=0;
        if(!collect(p,base,"",0,directories,files,error)||!copyShared(p,source,destination,files,total,error))return false;
        error.clear();return true;
    }
    bool blueprintImportFolderAssets(Platform& p,const std::string& folder,const std::vector<std::string>& screens,std::string& root,std::string& error)
    {
        if(!blueprintImportProjectFolderName(folder)||screens.empty()||screens.size()>10)
        {error="Invalid native project folder.";return false;}
        std::vector<std::vector<std::string>> collected(screens.size());
        size_t directories=0,totalFiles=0;
        for(size_t i=0;i<screens.size();++i)
        {
            if(!folderId(screens[i])){error="Invalid screen asset folder.";return false;}
            const auto base="own/"+folder+"/"+screens[i]+"/asset";
            if(!p.fileTime(base.c_str()))continue;
            if(!collect(p,base,"",0,directories,collected[i],error))return false;
            totalFiles+=collected[i].size();
            if(totalFiles>kMaxFiles){error="Project asset count limit exceeded.";return false;}
        }
        std::vector<std::string> shared;
        const auto sharedBase="own/"+folder+"/_global";
        if(p.fileTime(sharedBase.c_str())&&!collect(p,sharedBase,"",0,directories,shared,error))return false;
        if(shared.size()>kMaxFiles-totalFiles){error="Project asset count limit exceeded.";return false;}
        std::string fresh;
        if(!blueprintCreateAssetRoot(p,fresh,error))return false;
        size_t total=0;
        if(!copyShared(p,folder,fresh,shared,total,error))return false;
        for(size_t i=0;i<screens.size();++i)
        {
            if(!blueprintEnsureAssetFolder(p,fresh,screens[i],error))return false;
            const auto dest=fresh+"/"+screens[i]+"/asset",base="own/"+folder+"/"+screens[i]+"/asset/";
            for(const auto& file:collected[i])
            {
                for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                    if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create imported asset subfolder.";return false;}
                if(!copy(p,base+file,"own/"+dest+"/"+file,total,error))return false;
            }
        }
        root=std::move(fresh);error.clear();return true;
    }

    bool blueprintImportSourceAssets(Platform& p,const std::string& sourceFile,std::string& root,std::string& error)
    {
        if(!sourceFile.ends_with(".as")||sourceFile.find_first_of("/\\:")!=std::string::npos)
        {error="Invalid standalone source filename.";return false;}
        const auto stem=sourceFile.substr(0,sourceFile.size()-3);
        if(!blueprintAssetName(stem,true)){root.clear();error.clear();return true;}
        const auto base="own/"+stem+"/asset";
        if(!p.fileTime(base.c_str())){root.clear();error.clear();return true;}
        std::vector<std::string> files;size_t directories=0;
        if(!collect(p,base,"",0,directories,files,error))return false;
        if(files.empty()){root.clear();error.clear();return true;}
        std::string fresh;
        if(!blueprintCreateAssetRoot(p,fresh,error)||!blueprintEnsureAssetFolder(p,fresh,"Screen",error))return false;
        const auto dest=fresh+"/Screen/asset";
        size_t total=0;
        for(const auto& file:files)
        {
            for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create imported asset folder.";return false;}
            if(!copy(p,base+"/"+file,"own/"+dest+"/"+file,total,error))return false;
        }
        root=std::move(fresh);error.clear();return true;
    }
    bool blueprintImportAsset(Platform& p,const std::string& root,const std::string& screen,const std::string& file,std::string& error)
    {
        if(!blueprintAssetName(file)){error="Use a lowercase portable asset filename (letters, digits, _, -, .).";return false;}
        if(!blueprintAssetRoot(root)){error="Invalid project asset root.";return false;}
        if(!blueprintEnsureAssetFolder(p,root,screen,error))return false;
        const auto relative=blueprintAssetImportPath(file),dest=root+"/"+screen+"/asset/";
        const auto slash=relative.find('/');
        if(slash!=std::string::npos&&!ensure(p,dest+relative.substr(0,slash)))
        {error="Cannot create native asset type folder.";return false;}
        size_t total=0;
        return copy(p,"own/"+file,"own/"+dest+relative,total,error);
    }

    namespace
    {
        // Standard stored ZIP entries. No encryption, data descriptors, ZIP64,
        // compressed data, links or directory records are accepted by this package reader.
        constexpr size_t packageLimit=kMaxTotal+4*1024*1024;
        struct PackageEntry {std::string name;size_t offset=0,size=0;u32 crc=0;};
        void put16(std::string& s,u32 v){s+=static_cast<char>(v&255);s+=static_cast<char>((v>>8)&255);}
        void put32(std::string& s,u32 v){put16(s,v);put16(s,v>>16);}
        u32 get16(const u8* p){return static_cast<u32>(p[0])|(static_cast<u32>(p[1])<<8);}
        u32 get32(const u8* p){return get16(p)|(get16(p+2)<<16);}
        bool range(size_t at,size_t count,size_t limit){return at<=limit&&count<=limit-at;}
        bool relativeAsset(const std::string& path)
        {
            if(path.empty()||path.size()>90)return false;
            size_t start=0;unsigned depth=0;
            for(;;)
            {
                const auto slash=path.find('/',start);
                if(!blueprintAssetName(path.substr(start,slash==std::string::npos?slash:slash-start),slash!=std::string::npos))return false;
                if(slash==std::string::npos)return true;
                if(++depth>4)return false;
                start=slash+1;
            }
        }
        bool packageAsset(const std::string& name,const std::vector<std::string>& screens)
        {
            if(name.rfind("assets/_global/",0)==0)return relativeAsset(name.substr(15));
            for(const auto& screen:screens)
            {
                const auto prefix="assets/"+screen+"/asset/";
                if(name.rfind(prefix,0)==0)return relativeAsset(name.substr(prefix.size()));
            }
            return false;
        }
        bool appendPackage(std::string& zip,std::vector<PackageEntry>& entries,const std::string& name,const void* bytes,size_t size,std::string& error)
        {
            if(size>kMaxFile||entries.size()>=kMaxFiles+1||!range(zip.size(),size+30+name.size(),packageLimit))
            {error="Project package size or file limit exceeded.";return false;}
            const auto crc=crc32Of(static_cast<const u8*>(bytes),size);
            entries.push_back({name,zip.size(),size,crc});
            put32(zip,0x04034b50);put16(zip,20);put16(zip,0);put16(zip,0);put16(zip,0);put16(zip,33);
            put32(zip,crc);put32(zip,static_cast<u32>(size));put32(zip,static_cast<u32>(size));
            put16(zip,static_cast<u32>(name.size()));put16(zip,0);zip+=name;
            if(size)zip.append(static_cast<const char*>(bytes),size);
            return true;
        }
        bool finishPackage(std::string& zip,const std::vector<PackageEntry>& entries)
        {
            const size_t central=zip.size();
            for(const auto& e:entries)
            {
                if(!range(zip.size(),46+e.name.size(),packageLimit))return false;
                put32(zip,0x02014b50);put16(zip,20);put16(zip,20);put16(zip,0);put16(zip,0);put16(zip,0);put16(zip,33);
                put32(zip,e.crc);put32(zip,static_cast<u32>(e.size));put32(zip,static_cast<u32>(e.size));
                put16(zip,static_cast<u32>(e.name.size()));put16(zip,0);put16(zip,0);put16(zip,0);put16(zip,0);put32(zip,0);
                put32(zip,static_cast<u32>(e.offset));zip+=e.name;
            }
            if(!range(zip.size(),22,packageLimit))return false;
            const auto directorySize=static_cast<u32>(zip.size()-central);
            put32(zip,0x06054b50);put16(zip,0);put16(zip,0);
            put16(zip,static_cast<u32>(entries.size()));put16(zip,static_cast<u32>(entries.size()));
            put32(zip,directorySize);put32(zip,static_cast<u32>(central));put16(zip,0);return true;
        }
        bool readPackage(const FileData& data,std::vector<PackageEntry>& entries,std::string& error)
        {
            const auto fail=[&](){error="Invalid, unsupported or damaged project ZIP (stored entries only).";return false;};
            const auto* bytes=static_cast<const u8*>(data.data);const size_t n=data.size;
            if(n<22||n>packageLimit)return fail();
            const size_t end=n-22;const auto* footer=bytes+end;
            if(get32(footer)!=0x06054b50||get16(footer+4)||get16(footer+6)||get16(footer+20))return fail();
            const size_t count=get16(footer+10),central=get32(footer+16);
            if(count==0||count>kMaxFiles+1||get16(footer+8)!=count||!range(central,get32(footer+12),end)||central+get32(footer+12)!=end)return fail();
            size_t at=central,nextLocal=0,total=0;std::unordered_set<std::string> names;
            for(size_t i=0;i<count;++i)
            {
                if(!range(at,46,end))return fail();
                const auto* e=bytes+at;
                const size_t length=get16(e+28),size=get32(e+24),local=get32(e+42);
                const u32 crc=get32(e+16);
                if(get32(e)!=0x02014b50||get16(e+8)||get16(e+10)||get16(e+30)||get16(e+32)||get16(e+34)||
                    get32(e+38)||get32(e+20)!=size||size>kMaxFile||length==0||length>128||!range(at+46,length,end)||local!=nextLocal||
                    !range(local,30,central)||!range(local+30,length,central))return fail();
                const auto* h=bytes+local;
                if(get32(h)!=0x04034b50||get16(h+6)||get16(h+8)||get16(h+28)||get16(h+26)!=length||
                    get32(h+14)!=crc||get32(h+18)!=size||get32(h+22)!=size||std::memcmp(h+30,e+46,length)!=0)return fail();
                const size_t payload=local+30+length;
                if(!range(payload,size,central)||size>kMaxTotal-total||crc32Of(bytes+payload,size)!=crc)return fail();
                std::string name(reinterpret_cast<const char*>(e+46),length);
                if(!names.insert(name).second)return fail();
                entries.push_back({std::move(name),payload,size,crc});
                total+=size;nextLocal=payload+size;at+=46+length;
            }
            if(at!=end||nextLocal!=central)return fail();
            return true;
        }
    }
    bool blueprintPackageName(const std::string& file)
    {
        if(!file.ends_with(".bp.zip")||file.size()>100||file.size()<=7)return false;
        const auto stem=file.substr(0,file.size()-7);
        std::string ignored;
        return blueprintManagedName(stem,ignored);
    }
    bool blueprintExportPackage(Platform& p,const BlueprintDocument& doc,const std::string& file,std::string& error)
    {
        const auto target="own/"+file;
        if(!blueprintPackageName(file)||p.fileTime(target.c_str())){error="Invalid package filename or package already exists.";return false;}
        std::string graph;
        if(!doc.serialize(graph,error))return false;
        BlueprintDocument portable;
        if(!portable.load(graph.data(),graph.size(),error))return false;
        portable.assetRoot.clear();
        if(!portable.serialize(graph,error))return false;
        std::string zip;std::vector<PackageEntry> entries;
        if(!appendPackage(zip,entries,"project.bp.json",graph.data(),graph.size(),error))return false;
        size_t total=graph.size(),directories=0;
        if(!doc.assetRoot.empty())
        {
            if(!blueprintAssetRoot(doc.assetRoot)||!p.fileTime(("own/"+doc.assetRoot).c_str()))
            {error="Project asset directory is missing.";return false;}
            std::vector<std::string> scopes={"_global"};
            for(const auto& screen:doc.screens())scopes.push_back(screen+"/asset");
            for(const auto& scope:scopes)
            {
                const auto base="own/"+doc.assetRoot+"/"+scope;
                if(!p.fileTime(base.c_str()))continue;
                std::vector<std::string> files;
                if(!collect(p,base,"",0,directories,files,error))return false;
                for(const auto& name:files)
                {
                    FileData data;
                    if(!p.readFile((base+"/"+name).c_str(),data,MemTag::General)||data.size>kMaxFile||data.size>kMaxTotal-total)
                    {error="Cannot read package asset or size limit exceeded: "+name;return false;}
                    total+=data.size;
                    if(!appendPackage(zip,entries,"assets/"+scope+"/"+name,data.data,data.size,error))return false;
                }
            }
        }
        if(!finishPackage(zip,entries)){error="Project ZIP exceeds package limit.";return false;}
        if(!p.writeFile(target.c_str(),zip.data(),zip.size())){error="Cannot write project package.";return false;}
        FileData check;
        if(!p.readFile(target.c_str(),check,MemTag::General)||check.size!=zip.size()||std::memcmp(check.data,zip.data(),zip.size())!=0)
        {error="Package write verification failed.";return false;}
        error.clear();return true;
    }
    bool blueprintImportPackage(Platform& p,const std::string& file,BlueprintDocument& document,std::string& error)
    {
        if(!blueprintPackageName(file)){error="Choose a .bp.zip project package.";return false;}
        FileData data;
        if(!p.readFile(("own/"+file).c_str(),data,MemTag::General)){error="Cannot read project package.";return false;}
        std::vector<PackageEntry> entries;
        if(!readPackage(data,entries,error))return false;
        const auto graph=std::find_if(entries.begin(),entries.end(),[](const PackageEntry& e){return e.name=="project.bp.json";});
        if(graph==entries.end()){error="Package has no project.bp.json.";return false;}
        const auto* bytes=reinterpret_cast<const char*>(data.data);
        BlueprintDocument candidate;
        if(!candidate.load(bytes+graph->offset,graph->size,error))return false;
        if(!candidate.assetRoot.empty()){error="Package graph must not refer to an external asset root.";return false;}
        const auto screens=candidate.screens();
        // Validate every member before creating a directory or writing files.
        for(const auto& e:entries)if(e.name!="project.bp.json"&&!packageAsset(e.name,screens))
        {error="Invalid package asset path: "+e.name;return false;}
        if(entries.size()>1)
        {
            std::string fresh;
            if(!blueprintCreateAssetRoot(p,fresh,error))return false;
            for(const auto& screen:screens)if(!blueprintEnsureAssetFolder(p,fresh,screen,error))return false;
            for(const auto& e:entries)
            {
                if(e.name=="project.bp.json")continue;
                const auto rel=e.name.substr(7),dest=fresh+"/"+rel;
                for(size_t at=rel.find('/');at!=std::string::npos;at=rel.find('/',at+1))
                    if(!ensure(p,fresh+"/"+rel.substr(0,at))){error="Cannot create package folder.";return false;}
                const auto target="own/"+dest;
                if(p.fileTime(target.c_str())||!p.writeFile(target.c_str(),bytes+e.offset,e.size))
                {error="Cannot restore package asset: "+rel;return false;}
                FileData check;
                if(!p.readFile(target.c_str(),check,MemTag::General)||check.size!=e.size||(e.size&&std::memcmp(check.data,bytes+e.offset,e.size)!=0))
                {error="Package asset verification failed: "+rel;return false;}
            }
            candidate.assetRoot=std::move(fresh);
        }
        candidate.markRecovered();document=std::move(candidate);error.clear();return true;
    }
}
