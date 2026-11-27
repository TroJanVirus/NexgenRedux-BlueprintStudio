#pragma once
#include "tools/blueprint/native_document.h"

namespace nx
{
    // Conservative source conversion. The output document is unchanged on any failure.
    // The caller should validate original and generated code using the native engine.
    bool importBlueprintScreen(const std::string& source,BlueprintDocument& document,std::string& error);
}
