#pragma once
#include "tools/blueprint/native_document.h"
namespace nx
{
    struct BlueprintLocation { size_t graph=static_cast<size_t>(-1),node=static_cast<size_t>(-1); };
    struct BlueprintSourceSpan { size_t firstLine,lastLine; BlueprintLocation location; };
    using BlueprintSourceMap=std::vector<BlueprintSourceSpan>;
    // Map only executable statements and method headers, not unrelated closing braces.
    BlueprintLocation blueprintSourceLocation(const BlueprintSourceMap& map,size_t line);
    size_t blueprintErrorLine(const std::string& error,const std::string& section);
    // Generates plain AngelScript. Source and map are unchanged on failure.
    bool compileBlueprint(const BlueprintDocument& document,std::string& source,std::string& error,
        BlueprintLocation* location=nullptr,BlueprintSourceMap* map=nullptr);
    // A normal Redux screen, independent of Studio and its source format.
    bool compileBlueprintScreen(const BlueprintDocument& document,std::string& source,std::string& manifest,std::string& error,
        BlueprintLocation* location=nullptr,BlueprintSourceMap* map=nullptr);
}
