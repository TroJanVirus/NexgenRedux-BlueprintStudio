#pragma once
#include "script/breakpoint_condition.h"
#include <angelscript.h>
namespace nx
{
    // Read VM storage only. No getter, assignment, temporary context or script evaluation.
    inline bool breakpointConditionMatches(asIScriptContext* context,const std::string& condition)
    {
        if(condition.empty())return true;
        if(!context||!context->GetCallstackSize()||!validBreakpointCondition(condition))return false;
        const bool negate=condition[0]=='!';
        const char* name=condition.c_str()+(negate?1:0);
        for(int i=context->GetVarCount(0)-1;i>=0;--i)
        {
            const char* label=nullptr;int type=0;
            if(!context->IsVarInScope(i,0)||context->GetVar(i,0,&label,&type)<0||!label||std::string(label)!=name)continue;
            const auto* value=context->GetAddressOfVar(i,0);
            return type==asTYPEID_BOOL&&value&&(*static_cast<const bool*>(value)!=negate);
        }
        auto* function=context->GetFunction(0);auto* module=function?function->GetModule():nullptr;
        for(asUINT i=0;module&&i<module->GetGlobalVarCount();++i)
        {
            const char* label=nullptr;int type=0;
            module->GetGlobalVar(i,&label,nullptr,&type);
            if(!label||std::string(label)!=name)continue;
            const auto* value=module->GetAddressOfGlobalVar(i);
            return type==asTYPEID_BOOL&&value&&(*static_cast<const bool*>(value)!=negate);
        }
        return false;
    }
}
