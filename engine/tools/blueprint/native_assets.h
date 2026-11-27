#pragma once
#include "tools/blueprint/native_document.h"
namespace nx
{
    class Platform;
    bool blueprintCreateAssetRoot(Platform& platform,std::string& root,std::string& error);
    bool blueprintEnsureAssetFolder(Platform& platform,const std::string& root,const std::string& screen,std::string& error);
    bool blueprintListAssets(Platform& platform,const std::string& root,const std::string& screen,std::vector<std::string>& files,std::string& error);
    bool blueprintCopyAssets(Platform& platform,const std::string& source,const std::string& destination,const std::string& screen,std::string& error);
    bool blueprintImportAsset(Platform& platform,const std::string& root,const std::string& screen,const std::string& file,std::string& error);
}
