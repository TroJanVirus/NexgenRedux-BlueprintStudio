#pragma once
#include <cstddef>
#include "base/types.h"
#include "ui/design_geometry.h"
#include <string>

namespace nx
{
    // Project authoring metadata, not a promise that a runtime fits.
    struct BlueprintTarget
    {
        unsigned platform=0; // OG Xbox, Windows, Linux, macOS, Xbox 360, UWP
        unsigned ram=64;     // OG Xbox unified physical RAM in MiB
        unsigned resolution=0;
        static constexpr unsigned platformCount=6,resolutionCount=uiDesignResolutionCount;
        bool valid()const
        {return platform<platformCount&&(ram==64||ram==128||ram==256)&&resolution<resolutionCount;}
        bool operator==(const BlueprintTarget& other)const
        {return platform==other.platform&&ram==other.ram&&resolution==other.resolution;}
        bool xbox()const{return platform==0;}
        const char* platformName()const
        {constexpr const char* names[]={"Original Xbox","Windows","Linux","macOS","Xbox 360","UWP / Xbox One & Series"};return names[platform<platformCount?platform:0];}
        unsigned width()const {return uiDesignSize(resolution).width;}
        unsigned height()const {return uiDesignSize(resolution).height;}
        float pixelAspect()const{return uiDesignSize(resolution).pixelAspect;}
        const char* resolutionName()const
        {constexpr const char* names[]={"640 x 480","720 x 480 anamorphic","1280 x 720","1920 x 1080"};return names[resolution<resolutionCount?resolution:0];}
        // Illustrative allocation: two 32-bit colour surfaces and one
        // 32-bit depth surface; excludes pitch, tiling, MSAA and all other allocations.
        u64 surfaceBytes()const{return u64(width())*height()*12u;}
        bool highResolutionWarning()const{return xbox()&&resolution==3;}
        std::string label()const
        {return std::string(platformName())+(xbox()?" / "+std::to_string(ram)+" MiB":"")+" / "+resolutionName();}
        std::string warning()const
        {
            if(!xbox())return "Target selected; hardware compatibility unverified.";
            if(highResolutionWarning())return "1080i: high framebuffer cost; poor performance risk. Verify on hardware.";
            if(ram==256)return "Experimental 256 MiB mod; slower memory configuration may reduce performance.";
            if(ram==128)return "128 MiB upgrade; not compatible with stock RAM by assumption.";
            return "733 MHz CPU / 64 MiB unified RAM; real hardware validation required.";
        }
    };
}
