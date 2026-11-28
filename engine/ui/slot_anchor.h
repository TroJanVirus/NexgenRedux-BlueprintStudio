#pragma once
#include <cstring>
namespace nx
{
    inline constexpr const char* uiSlotAnchors[]={"top-left","top-center","top-right","middle-left","center","middle-right","bottom-left","bottom-center","bottom-right","stretch-x","stretch-y","stretch-both"};
    inline int uiSlotAnchorIndex(const char* name)
    {
        for(int i=0;i<12;++i)if(std::strcmp(name,uiSlotAnchors[i])==0)return i;
        return -1;
    }
    inline const char* uiSlotAnchorName(int anchor)
    {return anchor>=0&&anchor<12?uiSlotAnchors[anchor]:"";}
}
