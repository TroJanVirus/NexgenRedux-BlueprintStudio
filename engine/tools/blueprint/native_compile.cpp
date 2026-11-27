#include "tools/blueprint/native_compile.h"
#include "base/json_value.h"
#include <algorithm>
#include <utility>

namespace nx
{
    namespace
    {
        using Graph=BlueprintDocument::Graph;
        using Link=BlueprintDocument::Link;
        std::string quote(const std::string& value)
        {
            std::string result="\"";
            for(char c:value)
            {
                switch(c){case '\\':result+="\\\\";break;case '"':result+="\\\"";break;
                    case '\n':result+="\\n";break;case '\r':result+="\\r";break;case '\t':result+="\\t";break;
                    default:result+=c;break;}
            }
            return result+"\"";
        }
        struct ByteSpan { size_t begin,end; BlueprintLocation location; };
        struct Emitter
        {
            const BlueprintDocument& document;
            const Graph& graph;
            std::string& source;
            std::string& error;
            std::vector<bool> emitted,available;
            size_t graphIndex;
            BlueprintLocation* location;
            std::vector<ByteSpan>& spans;
            size_t budget=8192;
            void mark(size_t start,size_t node)
            {
                if(source.size()>start)spans.push_back({start,source.size(),{graphIndex,node}});
            }
            bool fail(size_t node,const std::string& why)
            {
                if(location)*location={graphIndex,node};
                error=graph.name+" / "+(node<graph.nodes.size()?graph.nodes[node].id:"?")+": "+why;
                return false;
            }
            const Link* incoming(size_t node,const std::string& input)const
            {
                const auto found=std::find_if(graph.links.begin(),graph.links.end(),[&](const Link& l){return l.to==node&&l.input==input;});
                return found==graph.links.end()?nullptr:&*found;
            }
            bool literalValue(size_t node,const std::string& type,const std::string& value,std::string& result)
            {
                if(type=="string")
                {
                    for(unsigned char c:value)if(c<32&&c!='\n'&&c!='\r'&&c!='\t')return fail(node,"Unsupported control character in string.");
                    result=quote(value);
                }
                else if(type=="bool")result=value;
                else if(type=="int"||type=="uint")
                {
                    const bool negative=!value.empty()&&value.front()=='-';
                    size_t at=negative?1:0;
                    while(at+1<value.size()&&value[at]=='0')++at;
                    const auto digits=value.substr(at);
                    // AngelScript may infer signed literals differently at the 32-bit boundary.
                    result=type+"("+(negative&&digits!="0"?"-":"")+digits+")";
                }
                else result=type+"("+value+")";
                return true;
            }
            bool value(size_t node,const std::string& pin,std::string& result,unsigned depth)
            {
                const auto* wire=incoming(node,pin);
                if(!wire)
                {
                    const auto& n=graph.nodes[node];
                    BlueprintNodeSpec spec;
                    if(document.nodeSpec(graph,n,spec))
                    {
                        for(const auto& v:n.values)if(v.pin==pin)
                            for(const auto& p:spec.inputs)if(p.name==pin)return literalValue(node,p.type,v.value,result);
                        for(const auto& d:spec.defaults)if(d.name==pin){result=d.type;return true;}
                    }
                    return fail(node,"Connect input "+pin+".");
                }
                if(!expression(wire->from,wire->output,result,depth+1))return false;
                if(result.size()>65536)return fail(node,"Expression too large.");
                return true;
            }
            bool api(size_t node,const BlueprintNodeSpec& spec,std::string& result,unsigned depth)
            {
                std::string target,args;
                if(spec.member)
                {
                    std::string v;if(!value(node,"target",v,depth))return false;
                    target="("+v+").";
                }
                for(const auto& arg:spec.arguments)
                {
                    std::string v;
                    if(arg.name=="out")v="bp_out_"+std::to_string(node)+"_"+arg.type;
                    else if(!value(node,arg.type,v,depth))return false;
                    if(!args.empty())args+=", ";
                    args+=v;
                    if(args.size()>65536)return fail(node,"Expression too large.");
                }
                const auto sep=spec.call.rfind("::");
                const auto name=spec.member&&sep!=std::string::npos?spec.call.substr(sep+2):spec.call;
                if(spec.property=="get"||spec.property=="set")
                {
                    const auto offset=name.rfind("::");const auto start=offset==std::string::npos?0:offset+2;
                    if(name.size()<start+5)return fail(node,"Malformed property metadata.");
                    result=target+name.substr(0,start)+name.substr(start+4);
                    if(spec.property=="set")
                    {
                        result=(spec.inputs.back().type.ends_with("@")?"@":"")+result+" = "+args;
                    }
                }
                else result=target+name+"("+args+")";
                return true;
            }
            bool expression(size_t node,const std::string& pin,std::string& result,unsigned depth)
            {
                if(node>=graph.nodes.size()||depth>64||budget==0)return fail(node,"Expression depth or complexity limit.");
                --budget;
                const auto& n=graph.nodes[node];
                const auto* spec=blueprintSpec(n.kind);
                if(!spec)return fail(node,"Unknown node.");
                if(n.kind=="entry"&&graph.kind=="function"&&pin.rfind("arg_",0)==0)
                {
                    for(const auto& p:graph.parameters)if(pin=="arg_"+p.name){result="bp_arg_"+p.name;return true;}
                }
                if(n.kind.rfind("capture:",0)==0&&pin=="result")
                {
                    if(!available[node])return fail(node,"Captured value used before execution or outside its branch.");
                    result="bp_local_"+n.ref+"_"+std::to_string(node);return true;
                }
                if(n.kind=="call:function"&&pin=="result")
                {
                    if(!available[node])return fail(node,"Function result used before execution or outside its branch.");
                    result="bp_value_"+std::to_string(node);return true;
                }
                if(n.kind.rfind("literal:",0)==0&&pin=="value")
                {
                    return literalValue(node,n.kind.substr(8),n.ref,result);
                }
                if(!spec->operation.empty()&&pin=="result")
                {
                    std::string a,b;
                    if(!value(node,"a",a,depth))return false;
                    if(spec->operation=="cast"){result=spec->result+"("+a+")";return true;}
                    if(spec->inputs.size()==1){result="("+spec->operation+a+")";return true;}
                    if(!value(node,"b",b,depth))return false;
                    result="("+a+" "+spec->operation+" "+b+")";return true;
                }
                if(n.kind=="event:delta"&&graph.name=="update"&&pin=="value"){result="dt";return true;}
                if(n.kind=="event:canvas"&&graph.name=="draw"&&pin=="value"){result="canvas";return true;}
                if(n.kind=="event:entity"&&pin=="value"){result="entity";return true;}
                if(n.kind=="event:other"&&pin=="value"){result="other";return true;}
                if(n.kind=="event:levelName"&&pin=="value"){result="bp_event_name";return true;}
                if(n.kind=="event:levelWho"&&pin=="value"){result="bp_event_who";return true;}
                if(n.kind=="event:levelWhat"&&pin=="value"){result="bp_event_what";return true;}
                if((n.kind=="level:attach"||n.kind=="attach:behaviour")&&pin=="result")
                {
                    if(!available[node])return fail(node,"Attachment result used before execution.");
                    result="bp_value_"+std::to_string(node);return true;
                }
                if(n.kind.rfind("make:",0)==0&&pin=="result")
                {
                    result=n.kind.substr(5)+"(";bool first=true;
                    for(const auto& p:spec->inputs)
                    {
                        std::string v;if(!value(node,p.name,v,depth))return false;
                        if(!first)result+=", ";
                        first=false;result+=v;
                    }
                    result+=")";return true;
                }
                if(n.kind.rfind("array:",0)==0&&pin=="result")
                {
                    std::string target;if(!value(node,"target",target,depth))return false;
                    if(n.kind.rfind("array:length:",0)==0){result="("+target+").length()";return true;}
                    if(n.kind.rfind("array:get:",0)==0){std::string index;if(!value(node,"index",index,depth))return false;result="("+target+")["+index+"]";return true;}
                }
                if(n.kind.rfind("global:get:",0)==0&&pin=="value"){result="bp_global_"+n.ref;return true;}
                if(n.kind.rfind("var:get:",0)==0&&pin=="value"){result="bp_var_"+n.ref;return true;}
                if(n.kind=="repeat"&&pin=="index")
                {
                    if(!available[node])return fail(node,"Loop index is only available in its body.");
                    result="bp_index_"+std::to_string(node);return true;
                }
                if(!spec->call.empty()&&spec->pure&&pin=="result")
                    return api(node,*spec,result,depth);
                if(!spec->call.empty()&&!spec->pure&&pin=="result"&&spec->result!="void")
                {
                    if(!available[node])return fail(node,"Action result used before execution or outside its branch.");
                    result="bp_value_"+std::to_string(node);return true;
                }
                if(spec->hasOut&&pin!="then")
                {
                    if(!available[node])return fail(node,"Action output used before execution or outside its branch.");
                    for(const auto& p:spec->outputs)if(p.name==pin){result="bp_out_"+std::to_string(node)+"_"+pin;return true;}
                }
                return fail(node,"This output is not supported by the native compiler yet.");
            }
            bool returns(size_t node,const std::string& pin,unsigned depth) const
            {
                if(depth>128)return false;
                const auto found=std::find_if(graph.links.begin(),graph.links.end(),[&](const Link& l){return l.from==node&&l.output==pin;});
                if(found==graph.links.end())return false;
                const auto to=found->to;const auto& kind=graph.nodes[to].kind;
                if(kind=="return:function")return true;
                if(kind=="branch"&&returns(to,"true",depth+1)&&returns(to,"false",depth+1))return true;
                if(kind=="sequence"&&(returns(to,"first",depth+1)||returns(to,"second",depth+1)))return true;
                return returns(to,"then",depth+1);
            }
            bool flow(size_t node,const std::string& pin,unsigned indent,unsigned depth)
            {
                const auto found=std::find_if(graph.links.begin(),graph.links.end(),[&](const Link& l){return l.from==node&&l.output==pin;});
                if(found==graph.links.end())return true;
                const auto to=found->to;
                if(to>=graph.nodes.size()||depth>128||budget==0||emitted[to])return fail(to,"Execution cycle or complexity limit.");
                --budget;emitted[to]=true;
                const auto& n=graph.nodes[to];const auto* spec=blueprintSpec(n.kind);
                const std::string pad(indent*4,' ');
                const size_t start=source.size();
                if(n.kind=="branch")
                {
                    std::string condition;
                    if(!value(to,"condition",condition,0))return false;
                    const auto outer=available;
                    source+=pad+"if ("+condition+")\n"+pad+"{\n";mark(start,to);
                    if(!flow(to,"true",indent+1,depth+1))return false;
                    available=outer;
                    source+=pad+"}\n"+pad+"else\n"+pad+"{\n";
                    if(!flow(to,"false",indent+1,depth+1))return false;
                    available=outer;
                    source+=pad+"}\n";
                    if(returns(to,"true",0)&&returns(to,"false",0))
                    {
                        if(std::any_of(graph.links.begin(),graph.links.end(),[&](const Link& l){return l.from==to&&l.output=="then";}))
                            return fail(to,"Branch continuation is unreachable: both paths return.");
                        return true;
                    }
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind.rfind("local:set:",0)==0)
                {
                    const auto* local=incoming(to,"local");
                    if(!local||local->output!="local"||graph.nodes[local->from].kind!="capture:"+n.kind.substr(10))
                        return fail(to,"Connect a matching Capture local reference.");
                    if(!available[local->from])return fail(to,"Local assignment before declaration or outside its scope.");
                    std::string v;if(!value(to,"value",v,0))return false;
                    const auto& declaration=graph.nodes[local->from];
                    source+=pad+(n.kind.ends_with("@")?"@":"")+"bp_local_"+declaration.ref+"_"+std::to_string(local->from)+" = "+v+";\n";
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind.rfind("capture:",0)==0)
                {
                    std::string v;if(!value(to,"value",v,0))return false;
                    source+=pad+n.kind.substr(8)+" bp_local_"+n.ref+"_"+std::to_string(to)+" = "+v+";\n";
                    available[to]=true;mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind.rfind("array:",0)==0)
                {
                    std::string target,index,v;if(!value(to,"target",target,0))return false;
                    const auto member="("+target+")";std::string statement;
                    if(n.kind.rfind("array:set:",0)==0){if(!value(to,"index",index,0)||!value(to,"value",v,0))return false;statement=member+"["+index+"] = "+v;}
                    else if(n.kind.rfind("array:append:",0)==0){if(!value(to,"value",v,0))return false;statement=member+".insertLast("+v+")";}
                    else if(n.kind.rfind("array:remove:",0)==0){if(!value(to,"index",index,0))return false;statement=member+".removeAt("+index+")";}
                    else if(n.kind.rfind("array:resize:",0)==0){if(!value(to,"size",v,0))return false;statement=member+".resize("+v+")";}
                    else if(n.kind.rfind("array:clear:",0)==0)statement=member+".resize(0)";
                    else return fail(to,"Unsupported array action.");
                    source+=pad+statement+";\n";mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind.rfind("global:set:",0)==0)
                {
                    std::string v;if(!value(to,"value",v,0))return false;
                    source+=pad+"bp_global_"+n.ref+" = "+v+";\n";mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind.rfind("var:set:",0)==0)
                {
                    std::string v;if(!value(to,"value",v,0))return false;
                    source+=pad+(n.kind.ends_with("@")?"@":"")+"bp_var_"+n.ref+" = "+v+";\n";mark(start,to);
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="attach:direct")
                {
                    std::string entity;if(!value(to,"target",entity,0))return false;
                    source+=pad+"("+entity+").add(BP_Behaviour_"+n.ref+"());\n";
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="level:attach"||n.kind=="attach:behaviour")
                {
                    std::string entity;
                    if(n.kind=="level:attach")
                    {
                        std::string name;if(!value(to,"name",name,0))return false;
                        entity="bp_level.find("+name+")";
                    }
                    else if(!value(to,"target",entity,0))return false;
                    const auto suffix=std::to_string(to);
                    // Evaluate the entity expression once, then guard both null and destroyed entities.
                    source+=pad+"Entity@ bp_attach_"+suffix+" = "+entity+";\n";
                    source+=pad+"bool bp_value_"+suffix+" = bp_attach_"+suffix+" !is null && bp_attach_"+suffix+".alive;\n";
                    source+=pad+"if (bp_value_"+suffix+") bp_attach_"+suffix+".add(BP_Behaviour_"+n.ref+"());\n";
                    available[to]=true;mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="call:function")
                {
                    const auto* target=document.function(graph.owner,n.ref);
                    if(!target)return fail(to,"Missing function.");
                    if(target->inlineMacro)
                    {
                        if(depth>96||source.size()>1024*1024)return fail(to,"Inline macro expansion limit.");
                        source+=pad+"{ // inline macro "+n.ref+"\n";
                        // Evaluate each parameter once, before shadowing caller argument names.
                        for(size_t i=0;i<target->parameters.size();++i)
                        {
                            const auto& p=target->parameters[i];std::string v;if(!value(to,"arg_"+p.name,v,0))return false;
                            source+=pad+"    "+p.type+" bp_macro_arg_"+std::to_string(i)+" = "+v+";\n";
                        }
                        for(size_t i=0;i<target->parameters.size();++i)
                        {
                            const auto& p=target->parameters[i];source+=pad+"    "+p.type+" bp_arg_"+p.name+" = bp_macro_arg_"+std::to_string(i)+";\n";
                        }
                        mark(start,to);
                        const auto entry=std::find_if(target->nodes.begin(),target->nodes.end(),[](const BlueprintDocument::Node& item){return item.kind=="entry";});
                        if(entry==target->nodes.end())return fail(to,"Missing macro entry.");
                        Emitter inner{document,*target,source,error,std::vector<bool>(target->nodes.size(),false),std::vector<bool>(target->nodes.size(),false),static_cast<size_t>(target-document.graphs.data()),location,spans,budget};
                        if(!inner.flow(static_cast<size_t>(entry-target->nodes.begin()),"then",indent+1,depth+1))return false;
                        budget=inner.budget;source+=pad+"}\n";
                        if(source.size()>1024*1024)return fail(to,"Inline macro generated source exceeds limit.");
                        return flow(to,"then",indent,depth+1);
                    }
                    std::string args;
                    for(const auto& p:target->parameters)
                    {
                        std::string v;if(!value(to,"arg_"+p.name,v,0))return false;
                        if(!args.empty())args+=", ";
                        args+=v;
                    }
                    const auto call="bp_fn_"+n.ref+"("+args+")";
                    if(target->returnType=="void")source+=pad+call+";\n";
                    else
                    {
                        source+=pad+target->returnType+" bp_value_"+std::to_string(to)+" = "+call+";\n";available[to]=true;
                    }
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="return:function")
                {
                    std::string v;
                    if(graph.returnType!="void"&&!value(to,"value",v,0))return false;
                    source+=pad+(v.empty()?"return;":"return "+v+";")+"\n";mark(start,to);return true;
                }
                if(n.kind=="sequence")
                {
                    // First and second execute in order, in separate scopes.
                    const auto outer=available;
                    bool terminated=false;
                    for(const char* output:{"first","second","then"})
                    {
                        if(terminated)
                        {
                            if(std::any_of(graph.links.begin(),graph.links.end(),[&](const Link& l){return l.from==to&&l.output==output;}))
                                return fail(to,"Sequence continuation is unreachable after return.");
                            continue;
                        }
                        if(std::string(output)=="then")return flow(to,output,indent,depth+1);
                        source+=pad+"{\n";
                        if(!flow(to,output,indent+1,depth+1))return false;
                        source+=pad+"}\n";available=outer;terminated=returns(to,output,0);
                    }
                    return true;
                }
                if(n.kind=="repeat")
                {
                    std::string count;if(!value(to,"count",count,0))return false;
                    const auto suffix=std::to_string(to),index="bp_index_"+suffix,limit="bp_limit_"+suffix;
                    const auto outer=available;
                    source+=pad+"{\n"+pad+"    int "+limit+" = "+count+";\n"+
                        pad+"    if ("+limit+" > 10000) "+limit+" = 10000;\n"+
                        pad+"    for (int "+index+" = 0; "+index+" < "+limit+"; ++"+index+")\n"+pad+"    {\n";
                    mark(start,to);available[to]=true;
                    if(!flow(to,"body",indent+2,depth+1))return false;
                    available=outer;source+=pad+"    }\n"+pad+"}\n";
                    return flow(to,"then",indent,depth+1);
                }
                if(!spec||spec->call.empty()||spec->pure||spec->result.empty())
                    return fail(to,"This execution node is not supported by the native compiler yet.");
                std::string statement;
                if(!api(to,*spec,statement,0))return false;
                if(spec->hasOut)for(const auto& p:spec->outputs)if(p.name!="then"&&p.name!="result")
                    source+=pad+p.type+" bp_out_"+std::to_string(to)+"_"+p.name+";\n";
                if(spec->result!="void")
                {
                    source+=pad+spec->result+" bp_value_"+std::to_string(to)+" = "+statement+";\n";
                    available[to]=true;
                }
                else source+=pad+statement+";\n";
                if(spec->hasOut)available[to]=true;
                mark(start,to);
                if(source.size()>1024*1024)return fail(to,"Generated source exceeds the size limit.");
                return flow(to,"then",indent,depth+1);
            }
        };
    }
    bool compileBlueprintOwner(const BlueprintDocument& document,const std::string& owner,std::string& source,std::string& error,BlueprintLocation* location,BlueprintSourceMap* map)
    {
        if(location)*location={};
        std::vector<ByteSpan> spans;
        // Validate the public graph model before reading any endpoints.
        std::string checked;
        if(!document.serialize(checked,error))return false;
        std::string result;
        std::vector<BlueprintPin> vars;
        const auto owners=document.screens();
        if(std::find(owners.begin(),owners.end(),owner)==owners.end()){error="Unknown screen "+owner;return false;}
        // Generate shared native behaviour classes in each screen module, before the Screen.
        bool usesLevel=false,touch=false,agent=false;
        for(const auto& graph:document.graphs)if(graph.owner==owner)
        {
            touch=touch||graph.name=="onLevelTouch";agent=agent||graph.name=="onLevelAgent";
            for(const auto& node:graph.nodes)if(node.kind.rfind("level:",0)==0)usesLevel=true;
        }
        usesLevel=usesLevel||touch||agent;
        // The Level helper calls delegates while iterating its own arrays. Do not permit rebuilding,
        // clearing or recursively updating it on that stack, including via reusable functions.
        std::vector<size_t> work;std::vector<bool> checkedGraphs(document.graphs.size(),false);
        for(size_t i=0;i<document.graphs.size();++i)
            if(document.graphs[i].owner==owner&&(document.graphs[i].name=="onLevelTouch"||document.graphs[i].name=="onLevelAgent"))work.push_back(i);
        while(!work.empty())
        {
            const auto i=work.back();work.pop_back();if(checkedGraphs[i])continue;checkedGraphs[i]=true;
            const auto& graph=document.graphs[i];
            for(size_t n=0;n<graph.nodes.size();++n)
            {
                const auto& node=graph.nodes[n];
                if(node.kind=="level:load"||node.kind=="level:clear"||node.kind=="level:update")
                {
                    if(location)*location={i,n};
                    error=graph.name+": Level callbacks cannot load, clear or update the level recursively. Set a variable and handle it after Update native level.";
                    return false;
                }
                if(node.kind=="call:function")
                    for(size_t j=0;j<document.graphs.size();++j)
                        if(document.graphs[j].owner==owner&&document.graphs[j].kind=="function"&&document.graphs[j].name==node.ref)work.push_back(j);
            }
        }
        if(agent)
        {
            std::fill(checkedGraphs.begin(),checkedGraphs.end(),false);
            for(size_t i=0;i<document.graphs.size();++i)
                if(document.graphs[i].owner==owner&&document.graphs[i].name=="onLevelAgent")work.push_back(i);
            while(!work.empty())
            {
                const auto i=work.back();work.pop_back();
                if(checkedGraphs[i])continue;
                checkedGraphs[i]=true;
                const auto& graph=document.graphs[i];
                for(size_t n=0;n<graph.nodes.size();++n)
                {
                    const auto& node=graph.nodes[n];
                    if(node.kind=="level:show"||node.kind=="level:hide")
                    {
                        if(location)*location={i,n};
                        error=graph.name+": Agent callbacks cannot change Level visibility during Crowd.update. Request it after Update native level.";
                        return false;
                    }
                    if(node.kind=="call:function")
                        for(size_t j=0;j<document.graphs.size();++j)
                            if(document.graphs[j].owner==owner&&document.graphs[j].kind=="function"&&document.graphs[j].name==node.ref)work.push_back(j);
                }
            }
        }
        if(usesLevel)result+="#include \"global:scene.as\"\n\n";
        for(const auto& f:document.moduleGlobals(owner))
        {
            result+=f.type+" bp_global_"+f.name;
            if(f.initialized)
            {
                std::string literal;
                Emitter e{document,document.graphs.front(),result,error,{}, {},0,location,spans};
                if(!e.literalValue(0,f.type,f.value,literal))return false;
                result+=" = "+literal;
            }
            result+=";\n";
        }
        std::vector<std::string> classes;
        for(const auto& name:document.behaviours())
        {
            const auto classOwner="behaviour:"+name;
            const auto scope=document.moduleForOwner(classOwner);
            if(scope.empty()||scope==owner)classes.push_back(classOwner);
        }
        classes.push_back(owner);
        for(const auto& classOwner:classes)
        {
        const bool behaviour=classOwner!=owner;
        result+=behaviour?"class BP_Behaviour_"+classOwner.substr(10)+" : Behaviour\n{\n":"class BP_NativeScreen : Screen\n{\n";
        if(!behaviour&&usesLevel)result+="    Level bp_level;\n";
        if(!document.variables(vars,error,classOwner))return false;
        for(const auto& v:vars)
        {
            result+="    "+v.type+" bp_var_"+v.name;
            for(const auto& g:document.graphs)if(g.owner==classOwner)for(const auto& f:g.fields)
                if(f.name==v.name&&f.initialized)
                {
                    std::string literal;
                    // Reuse the node literal emitter for precisely the same type and escaping rules.
                    Emitter e{document,g,result,error,{}, {},0,location,spans};
                    if(!e.literalValue(0,f.type,f.value,literal))return false;
                    result+=" = "+literal;
                }
            result+=";\n";
        }
        for(size_t graphIndex=0;graphIndex<document.graphs.size();++graphIndex)
        {
            const auto& graph=document.graphs[graphIndex];
            if(graph.owner!=classOwner)continue;
            if(graph.inlineMacro)
            {
                // Validate even unused macro bodies, without emitting a callable method.
                std::string scratch;std::vector<ByteSpan> ignored;
                const auto entry=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const BlueprintDocument::Node& item){return item.kind=="entry";});
                if(entry==graph.nodes.end()){error="Missing macro entry.";return false;}
                Emitter validation{document,graph,scratch,error,std::vector<bool>(graph.nodes.size(),false),std::vector<bool>(graph.nodes.size(),false),graphIndex,location,ignored};
                if(!validation.flow(static_cast<size_t>(entry-graph.nodes.begin()),"then",2,0))return false;
                continue;
            }
            const auto entry=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const BlueprintDocument::Node& n){return n.kind=="entry";});
            if(entry==graph.nodes.end()){error="Missing event entry.";return false;}
            const auto index=static_cast<size_t>(entry-graph.nodes.begin());
            std::string signature=graph.kind=="function"?"":graph.name=="update"?"float dt":graph.name=="draw"?"Canvas@ canvas":
                (graph.name=="onHit"||graph.name=="onLeave")?"Entity@ other":
                graph.name=="onLevelTouch"?"const string &in bp_event_name, Entity@ bp_event_who":
                graph.name=="onLevelAgent"?"const string &in bp_event_name, const string &in bp_event_what":"";
            if(graph.kind=="function")for(const auto& p:graph.parameters)
            {
                if(!signature.empty())signature+=", ";
                signature+=p.type+" bp_arg_"+p.name;
            }
            const size_t start=result.size();
            result+="    "+(graph.kind=="function"?graph.returnType:"void")+" "+(graph.kind=="function"?"bp_fn_"+graph.name:graph.name)+"("+signature+")\n    {\n";
            if(!behaviour&&graph.name=="onEnter")
            {
                if(touch)result+="        @bp_level.onTouch = LevelTouch(this.onLevelTouch);\n";
                if(agent)result+="        @bp_level.onAgent = LevelAgent(this.onLevelAgent);\n";
            }
            // Break the Level -> delegate -> Screen reference cycle on exit.
            if(!behaviour&&graph.name=="onExit")
            {
                if(touch)result+="        @bp_level.onTouch = null;\n";
                if(agent)result+="        @bp_level.onAgent = null;\n";
            }
            spans.push_back({start,result.size(),{graphIndex,index}});
            Emitter e{document,graph,result,error,std::vector<bool>(graph.nodes.size(),false),std::vector<bool>(graph.nodes.size(),false),graphIndex,location,spans};
            if(graph.returnType!="void"&&!e.returns(index,"then",0))return e.fail(index,"Not every execution path returns a value.");
            if(!e.flow(index,"then",2,0))return false;
            result+="    }\n";
            if(result.size()>1024*1024){error="Generated source exceeds the size limit.";return false;}
        }
        result+="}\n";
        }
        if(map)
        {
            BlueprintSourceMap lines;lines.reserve(spans.size());size_t cursor=0,line=1;
            for(const auto& span:spans)
            {
                while(cursor<span.begin){if(result[cursor]=='\n')++line;++cursor;}
                const size_t first=line;
                while(cursor<span.end){if(result[cursor]=='\n')++line;++cursor;}
                lines.push_back({first,line-1,span.location});
            }
            map->swap(lines);
        }
        source.swap(result);error.clear();return true;
    }
    bool compileBlueprint(const BlueprintDocument& document,std::string& source,std::string& error,BlueprintLocation* location,BlueprintSourceMap* map)
    {
        const auto owners=document.screens();
        if(owners.size()!=1){error="Choose a screen or compile the complete project.";if(location)*location={};return false;}
        return compileBlueprintOwner(document,owners.front(),source,error,location,map);
    }
    BlueprintLocation blueprintSourceLocation(const BlueprintSourceMap& map,size_t line)
    {
        const auto found=std::lower_bound(map.begin(),map.end(),line,[](const BlueprintSourceSpan& span,size_t target){return span.lastLine<target;});
        return found!=map.end()&&line>=found->firstLine?found->location:BlueprintLocation{};
    }
    size_t blueprintErrorLine(const std::string& error,const std::string& section)
    {
        const auto at=error.find(section+":");
        if(at==std::string::npos)return 0;
        size_t i=at+section.size()+1,line=0,start=i;
        while(i<error.size()&&error[i]>='0'&&error[i]<='9')
        {
            if(line>1000000)return 0;
            line=line*10+static_cast<size_t>(error[i++]-'0');
        }
        return i>start&&i<error.size()&&error[i]==':'?line:0;
    }
    static bool manifestFor(const std::string& name,std::string& manifest,std::string& error)
    {
        JsonValue info;
        auto* title=info.put("title");if(!title){error="Cannot allocate screen manifest.";return false;}
        title->setText(name);
        auto* category=info.put("category");if(!category){error="Cannot allocate screen manifest.";return false;}
        category->setText("Blueprints");
        auto* description=info.put("description");if(!description){error="Cannot allocate screen manifest.";return false;}
        description->setText("Generated by native Blueprint Studio");
        auto json=info.write(false);manifest.swap(json);error.clear();return true;
    }
    static bool manifestForOwner(const BlueprintDocument& document,const std::string& owner,const std::string& fallback,std::string& manifest,std::string& error)
    {
        if(const auto* original=document.screenManifest(owner)){manifest=*original;error.clear();return true;}
        return manifestFor(fallback,manifest,error);
    }
    bool compileBlueprintScreen(const BlueprintDocument& document,std::string& source,std::string& manifest,std::string& error,BlueprintLocation* location,BlueprintSourceMap* map)
    {
        std::string code,json;BlueprintSourceMap lines;
        if(!compileBlueprint(document,code,error,location,map?&lines:nullptr)||!manifestForOwner(document,document.screens().front(),document.name,json,error))return false;
        source.swap(code);manifest.swap(json);if(map)map->swap(lines);return true;
    }
    bool compileBlueprintProject(const BlueprintDocument& document,std::vector<BlueprintScreenOutput>& output,std::string& error,BlueprintLocation* location)
    {
        std::vector<BlueprintScreenOutput> result;
        const auto owners=document.screens();
        // Validate structure before inspecting graph endpoints or compiling individual screens.
        std::string checked;if(location)*location={};
        if(!document.serialize(checked,error))return false;
        for(size_t g=0;g<document.graphs.size();++g)
        {
            const auto& graph=document.graphs[g];
            for(size_t n=0;n<graph.nodes.size();++n)
            {
                const auto& node=graph.nodes[n];
                if(node.kind!="api:Screens:Screens::push:string"&&node.kind!="api:Screens:Screens::replace:string")continue;
                const auto wire=std::find_if(graph.links.begin(),graph.links.end(),[&](const BlueprintDocument::Link& l){return l.to==n&&l.input=="id";});
                const std::string* target=nullptr;
                if(wire!=graph.links.end())
                {
                    const auto& from=graph.nodes[wire->from];
                    if(from.kind=="literal:string")target=&from.ref;
                }
                else for(const auto& v:node.values)if(v.pin=="id")target=&v.value;
                if(target&&std::find(owners.begin(),owners.end(),*target)==owners.end())
                {
                    if(location)*location={g,n};
                    error=graph.owner+"/"+graph.name+": Unknown project screen "+*target;return false;
                }
            }
        }
        for(const auto& owner:owners)
        {
            BlueprintScreenOutput screen;screen.id=owner;
            if(!compileBlueprintOwner(document,owner,screen.source,error,location,&screen.map)||
                !manifestForOwner(document,owner,owner=="Screen"?document.name:document.name+" - "+owner,screen.manifest,error))return false;
            result.push_back(std::move(screen));
        }
        if(result.empty()){error="No screens.";return false;}
        output.swap(result);return true;
    }
}
