#pragma once
#include <cmath>
#include <cstring>

namespace nx
{
    enum class UiScaleMode { None, Fit, DownOnly };
    inline UiScaleMode uiScaleMode(const char* name)
    {
        if(std::strcmp(name,"fit")==0)return UiScaleMode::Fit;
        if(std::strcmp(name,"down-only")==0)return UiScaleMode::DownOnly;
        return UiScaleMode::None;
    }
    inline float fitUiChild(float width,float height,float childWidth,float childHeight,UiScaleMode mode)
    {
        if(!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(childWidth)||!std::isfinite(childHeight)||
            width<=0||height<=0||childWidth<=0||childHeight<=0)return 0;
        const float x=width/childWidth,y=height/childHeight;
        float factor=x<y?x:y;
        if(mode==UiScaleMode::DownOnly&&factor>1)factor=1;
        return std::isfinite(factor)&&factor>=0.000001f?factor:0;
    }
}
