#pragma once
#include "tools/blueprint/native_document.h"
#include <functional>

namespace nx
{
    // Read a portable asset: or global: include from the caller-defined import context.
    using BlueprintIncludeReader=std::function<bool(const std::string&,std::string&)>;
    bool resolveBlueprintIncludes(const std::string& source,const std::string& label,const BlueprintIncludeReader& read,std::string& expanded,std::string& error);
    // Conservative source conversion. The output document is unchanged on any failure.
    // The caller should validate original and generated code using the native engine.
    bool importBlueprintScreen(const std::string& source,BlueprintDocument& document,std::string& error);
}
