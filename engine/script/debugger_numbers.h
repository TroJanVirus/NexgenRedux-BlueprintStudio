#pragma once
#include "base/json_value.h"

namespace nx
{
    // Validate before narrowing network-supplied JSON numbers. On the Xbox
    // and desktop targets, out-of-range floating-to-integer casts are unsafe.
    inline bool debuggerUnsigned(const JsonValue* item,u32& out)
    {
        if(!item||item->type()!=JsonValue::Type::Number)return false;
        const double value=item->number();
        if(!(value>=0&&value<=4294967295.0))return false;
        const u32 converted=static_cast<u32>(value);
        if(value!=static_cast<double>(converted))return false;
        out=converted;return true;
    }
}
