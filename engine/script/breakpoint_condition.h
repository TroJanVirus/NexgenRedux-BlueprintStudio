#pragma once
#include <string>
namespace nx
{
    // One boolean name, optional negation; empty means unconditional.
    inline bool validBreakpointCondition(const std::string& condition)
    {
        if(condition.empty())return true;
        if(condition.size()>96)return false;
        size_t i=condition[0]=='!'?1u:0u;
        if(i==condition.size())return false;
        const auto alpha=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
        if(!alpha(condition[i++]))return false;
        for(;i<condition.size();++i)if(!alpha(condition[i])&&!(condition[i]>='0'&&condition[i]<='9'))return false;
        return true;
    }
}
