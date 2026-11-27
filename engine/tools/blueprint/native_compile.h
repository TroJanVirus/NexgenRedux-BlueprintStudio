#pragma once
#include "tools/blueprint/native_document.h"
namespace nx
{
    // Generates plain AngelScript. Errors name the graph and node; output is unchanged on failure.
    bool compileBlueprint(const BlueprintDocument& document,std::string& source,std::string& error);
    // A normal Redux screen, independent of Studio and its source format.
    bool compileBlueprintScreen(const BlueprintDocument& document,std::string& source,std::string& manifest,std::string& error);
}
