#include "tools/blueprint/native_assets.h"
#include "platform/platform.h"
#include "base/json_tree.h"
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
    bool blueprintImportFolderAssets(Platform& p,const std::string& folder,const std::vector<std::string>& screens,std::string& root,std::string& error)
    {
        if(!blueprintAssetName(folder,true)||folder.rfind("project-",0)!=0||screens.empty()||screens.size()>10)
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
        std::string fresh;
        if(!blueprintCreateAssetRoot(p,fresh,error))return false;
        size_t total=0;
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
}
