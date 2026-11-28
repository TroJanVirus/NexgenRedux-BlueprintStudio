#pragma once
#include "base/types.h"
#include <string>
#include <vector>
namespace nx
{
    constexpr u32 kStudioPreviewWidth=128,kStudioPreviewHeight=96;
    inline u16 studioRgb565(u8 r,u8 g,u8 b){return static_cast<u16>(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
    inline bool studioPreviewSize(u32 w,u32 h){return w&&h&&w<=kStudioPreviewWidth&&h<=kStudioPreviewHeight;}
    inline std::string studioPreviewEncode(const u16* pixels,u32 w,u32 h)
    {
        if(!pixels||!studioPreviewSize(w,h))return {};
        const char* digits="0123456789abcdef";std::string text;text.reserve(static_cast<size_t>(w)*h*4);
        for(size_t i=0;i<static_cast<size_t>(w)*h;++i)
            for(int shift=12;shift>=0;shift-=4)text+=digits[(pixels[i]>>shift)&15];
        return text;
    }
    inline bool studioPreviewDecode(const std::string& text,u32 w,u32 h,std::vector<u8>& rgba)
    {
        if(!studioPreviewSize(w,h)||text.size()!=static_cast<size_t>(w)*h*4)return false;
        std::vector<u8> result(text.size());
        for(size_t i=0;i<text.size();i+=4)
        {
            unsigned value=0;
            for(size_t j=0;j<4;++j)
            {
                const char c=text[i+j];const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
                if(n<0)return false;
                value=(value<<4)|static_cast<unsigned>(n);
            }
            const unsigned r=(value>>11)&31,g=(value>>5)&63,b=value&31;
            result[i]=static_cast<u8>((r<<3)|(r>>2));result[i+1]=static_cast<u8>((g<<2)|(g>>4));result[i+2]=static_cast<u8>((b<<3)|(b>>2));result[i+3]=255;
        }
        rgba.swap(result);return true;
    }
}
