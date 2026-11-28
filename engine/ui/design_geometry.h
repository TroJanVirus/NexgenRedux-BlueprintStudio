#pragma once
namespace nx
{
    struct UiDesignSize { unsigned width,height; float pixelAspect; };
    inline constexpr unsigned uiDesignResolutionCount=4;
    inline constexpr UiDesignSize uiDesignSizes[]={
        {640,480,1.f},{720,480,32.f/27.f},{1280,720,1.f},{1920,1080,1.f}
    };
    inline constexpr UiDesignSize uiDesignSize(unsigned resolution)
    {return uiDesignSizes[resolution<uiDesignResolutionCount?resolution:0];}
    struct UiDesignRect
    {
        float x=0,y=0,w=0,h=0,scaleX=1,scaleY=1;
        float localX(float at) const {return (at-x)/scaleX;}
        float localY(float at) const {return (at-y)/scaleY;}
    };
    inline UiDesignRect fitUiDesign(float x,float y,float w,float h,UiDesignSize size,float outputPixelAspect=1.f)
    {
        const float ratio=size.pixelAspect/outputPixelAspect;
        const float sx=w/(size.width*ratio),sy=h/size.height;
        const float scale=sx<sy?sx:sy;
        const float width=size.width*ratio*scale,height=size.height*scale;
        return {x+(w-width)*0.5f,y+(h-height)*0.5f,width,height,scale*ratio,scale};
    }
    // Centered display-space footprints, not a simulation of responsive reflow.
    inline UiDesignRect uiDesignGuide(float centerX,float centerY,float displayScale,unsigned resolution)
    {
        const auto size=uiDesignSize(resolution);
        const float w=size.width*size.pixelAspect*displayScale,h=size.height*displayScale;
        return {centerX-w*0.5f,centerY-h*0.5f,w,h,displayScale*size.pixelAspect,displayScale};
    }
}
