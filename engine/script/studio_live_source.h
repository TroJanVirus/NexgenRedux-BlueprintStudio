#pragma once
#include "script/source_fingerprint.h"
#include <string>
namespace nx
{
    // One bounded, sequential upload. Hex keeps the wire ASCII and allows
    // arbitrary UTF-8 source bytes without JSON or line-ending ambiguity.
    struct StudioLiveSource
    {
        static constexpr size_t limit=128*1024,chunkLimit=2048;
        std::string screen,expected,fingerprint,source;
        size_t total=0;
        bool ready=false;
        void clear(){screen.clear();expected.clear();fingerprint.clear();std::string().swap(source);total=0;ready=false;}
        static bool validHash(const std::string& hash)
        {
            if(hash.size()!=16)return false;
            for(char c:hash)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
            return true;
        }
        bool begin(const std::string& id,const std::string& oldHash,const std::string& newHash,size_t size)
        {
            clear();
            if(id.empty()||id.size()>42||id=="."||id==".."||size==0||size>limit||
                !validHash(oldHash)||!validHash(newHash))return false;
            for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
            screen=id;expected=oldHash;fingerprint=newHash;total=size;return true;
        }
        static int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
        bool append(size_t offset,const std::string& hex)
        {
            if(!total||ready||offset!=source.size()||hex.empty()||hex.size()%2||hex.size()>chunkLimit*2||hex.size()/2>total-source.size()){clear();return false;}
            for(size_t i=0;i<hex.size();i+=2)
            {
                const int a=digit(hex[i]),b=digit(hex[i+1]);
                if(a<0||b<0||!(a||b)){clear();return false;}
                source+=static_cast<char>((a<<4)|b);
            }
            return true;
        }
        bool commit()
        {
            if(!total||ready||source.size()!=total||scriptFingerprint(source.data(),source.size())!=fingerprint){clear();return false;}
            ready=true;return true;
        }
    };
}
