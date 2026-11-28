#pragma once
#include "base/types.h"
#include <string>

namespace nx
{
    // Non-cryptographic source identity for matching debugger line mappings,
    // not authentication. Hash the bytes actually handed to AngelScript.
    inline std::string scriptFingerprint(const char* bytes,size_t size)
    {
        u64 hash=14695981039346656037ull;
        for(size_t i=0;i<size;++i){hash^=static_cast<u8>(bytes[i]);hash*=1099511628211ull;}
        constexpr char hex[]="0123456789abcdef";
        std::string result(16,'0');
        for(unsigned i=0;i<16;++i){result[15-i]=hex[hash&15u];hash>>=4;}
        return result;
    }
}
