#pragma once
#include "base/types.h"
#include <string>
#include <stdlib.h>
namespace nx
{
    // Reject trailing junk, NaN/infinity, extreme coordinates and wrong arity.
    inline bool studioActorNumbers(const std::string& value,unsigned count,float bound,bool positive=false)
    {
        if(value.empty()||value.size()>96||!count||count>4)return false;
        const char* p=value.c_str();
        for(unsigned i=0;i<count;++i)
        {
            while(*p==' '||*p=='\t')++p;
            if(i&&*p==','){++p;while(*p==' '||*p=='\t')++p;}
            char* end=nullptr;const float v=strtof(p,&end);
            if(end==p||!(v>=-bound&&v<=bound)||(positive&&v<=0))return false;
            p=end;
            if(i+1<count&&*p!=','&&*p!=' '&&*p!='\t')return false;
        }
        while(*p==' '||*p=='\t')++p;
        return !*p;
    }
    inline bool studioActorValue(const std::string& property,const std::string& value,bool space)
    {
        if(property=="shown")return value=="true"||value=="false";
        if(property=="place"&&space)return studioActorNumbers(value,3,100000);
        if(property=="angles"&&space)return studioActorNumbers(value,3,36000);
        if(property=="size"&&space)return studioActorNumbers(value,3,1000,true);
        if(property=="position"&&!space)return studioActorNumbers(value,2,100000);
        if(property=="rotation"&&!space)return studioActorNumbers(value,1,36000);
        if(property=="scale"&&!space)return studioActorNumbers(value,2,1000,true);
        if(property=="velocity")return studioActorNumbers(value,space?3:2,10000);
        if(property=="mass"&&space)return studioActorNumbers(value,1,100000,true);
        if(property=="color"&&space)return studioActorNumbers(value,4,1)&&value.find('-')==std::string::npos;
        return false;
    }
}
