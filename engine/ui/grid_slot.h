#pragma once
#include "base/types.h"

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
        u64 occupied[256]={};
        unsigned next=0;
        void reserve(const GridSlot& s)
        {
            if(!s.valid())return;
            const u64 bits=s.columnSpan==64?~u64(0):
                ((u64(1)<<s.columnSpan)-1)<<s.column;
            for(int row=s.row;row<s.row+s.rowSpan;++row)occupied[row]|=bits;
        }
        GridSlot nextFree(unsigned columns)
        {
            if(columns<1||columns>64)return {};
            while(next<256*columns)
            {
                const unsigned at=next++,row=at/columns,col=at%columns;
                if(!(occupied[row]&(u64(1)<<col)))
                {
                    GridSlot s{static_cast<int>(row),static_cast<int>(col),1,1};
                    reserve(s);return s;
                }
            }
            return {};
        }
    };
}
