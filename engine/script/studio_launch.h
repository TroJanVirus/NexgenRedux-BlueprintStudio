#pragma once
#include <string>
namespace nx
{
    inline bool studioLaunchIdentity(const std::string& value)
    {
        if(value.size()!=32)return false;
        for(char ch:value)if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')))return false;
        return true;
    }
    // Narrow protocol grammar, never a shell or an arbitrary executable path.
    inline std::string studioLaunchPath(const std::string& remote)
    {
        if(remote.size()<10||remote.size()>210||remote[0]!='/'||remote[2]!='/'||
            (remote[1]!='E'&&remote[1]!='F'&&remote[1]!='G')||
            remote.compare(3,6,"Games/")!=0)return {};
        size_t start=9;
        while(start<remote.size())
        {
            const auto end=remote.find('/',start);
            const auto part=remote.substr(start,end==std::string::npos?end:end-start);
            if(part.empty()||part=="."||part==".."||part.size()>42||part.back()=='.'||part.back()==' ')return {};
            for(char ch:part)
                if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch==' '||ch=='.'))return {};
            if(end==std::string::npos)break;
            start=end+1;if(start==remote.size())return {};
        }
        std::string result(1,remote[1]);result+=':';result+=remote.substr(2);
        for(char& ch:result)if(ch=='/')ch='\\';
        return result+"\\default.xbe";
    }
}
