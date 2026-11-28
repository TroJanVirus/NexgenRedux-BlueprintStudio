#pragma once
#include "script/breakpoint_condition.h"
#include <angelscript.h>
#include <cstring>
namespace nx
{
    // Read VM storage only, in lexical lookup order. Never call getters or execute script.
    inline bool readDebuggerValue(asIScriptContext* context,const std::string& name,int& type,void*& address)
    {
        address=nullptr;
        if(!context||!context->GetCallstackSize()||name.empty()||name[0]=='!'||!validBreakpointCondition(name))return false;
        for(int i=context->GetVarCount(0)-1;i>=0;--i)
        {
            const char* label=nullptr;int candidate=0;
            if(!context->IsVarInScope(i,0)||context->GetVar(i,0,&label,&candidate)<0||!label||std::strcmp(label,name.c_str())!=0)continue;
            type=candidate;address=context->GetAddressOfVar(i,0);return true;
        }
        if((context->GetThisTypeId(0)&asTYPEID_SCRIPTOBJECT)!=0)
            if(auto* self=static_cast<asIScriptObject*>(context->GetThisPointer(0)))
                for(asUINT i=0;i<self->GetPropertyCount();++i)
                {
                    const char* label=self->GetPropertyName(i);
                    if(!label||std::strcmp(label,name.c_str())!=0)continue;
                    type=self->GetPropertyTypeId(i);address=self->GetAddressOfProperty(i);return true;
                }
        auto* function=context->GetFunction(0);auto* module=function?function->GetModule():nullptr;
        for(asUINT i=0;module&&i<module->GetGlobalVarCount();++i)
        {
            const char* label=nullptr;int candidate=0;
            module->GetGlobalVar(i,&label,nullptr,&candidate);
            if(!label||std::strcmp(label,name.c_str())!=0)continue;
            type=candidate;address=module->GetAddressOfGlobalVar(i);return true;
        }
        return false;
    }
    inline bool breakpointConditionMatches(asIScriptContext* context,const std::string& condition)
    {
        if(condition.empty())return true;
        if(!validBreakpointCondition(condition))return false;
        const bool negate=condition[0]=='!';
        int type=0;void* address=nullptr;
        return readDebuggerValue(context,negate?condition.substr(1):condition,type,address)&&
            type==asTYPEID_BOOL&&address&&(*static_cast<const bool*>(address)!=negate);
    }
}
