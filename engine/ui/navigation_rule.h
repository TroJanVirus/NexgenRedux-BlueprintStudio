#pragma once
#include <string>
namespace nx
{
    inline constexpr const char* uiNavigationProperties[]={"navUp","navDown","navLeft","navRight"};
    inline constexpr const char* uiNavigationDirections[]={"up","down","left","right"};
    inline int uiNavigationIndex(const std::string& key)
    {
        for(int i=0;i<4;++i)if(key==uiNavigationProperties[i]||key==uiNavigationDirections[i])return i;
        return -1;
    }
    inline bool uiNavigationTarget(const std::string& value)
    {
        if(value.size()>128)return false;
        for(unsigned char c:value)if(c<32||c==127)return false;
        return true;
    }
}
