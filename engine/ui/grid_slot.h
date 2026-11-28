#pragma once
#include <cstdint>

namespace nx
{
    // Explicit grids are bounded to a 64-column, 256-row address space.
    // Ordinary auto-flow grids retain their original behaviour.
    struct GridSlot
    {
        int row=-1,column=-1,rowSpan=1,columnSpan=1;
        bool explicitSlot()const{return row>=0&&column>=0;}
        bool valid()const
        {
            return row>=0&&row<256&&column>=0&&column<64&&
                rowSpan>=1&&rowSpan<=256-row&&columnSpan>=1&&columnSpan<=64-column;
        }
    };

    struct GridOccupancy
    {
        std::uint64_t occupied[256]={};
        unsigned next=0;
        void reserve(const GridSlot& s)
        {
            if(!s.valid())return;
            const std::uint64_t bits=s.columnSpan==64?~std::uint64_t(0):
                ((std::uint64_t(1)<<s.columnSpan)-1)<<s.column;
            for(int row=s.row;row<s.row+s.rowSpan;++row)occupied[row]|=bits;
        }
        GridSlot nextFree(unsigned columns)
        {
            if(columns<1||columns>64)return {};
            while(next<256*columns)
            {
                const unsigned at=next++,row=at/columns,col=at%columns;
                if(!(occupied[row]&(std::uint64_t(1)<<col)))
                {
                    GridSlot s{static_cast<int>(row),static_cast<int>(col),1,1};
                    reserve(s);return s;
                }
            }
            return {};
        }
    };
}
