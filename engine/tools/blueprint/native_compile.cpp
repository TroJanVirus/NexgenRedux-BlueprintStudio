#include "tools/blueprint/native_compile.h"
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
        struct Emitter
        {
            const Graph& graph;
            std::string& source;
            std::string& error;
            std::vector<bool> emitted,available;
            size_t budget=8192;
            bool fail(size_t node,const std::string& why)
            {
                error=graph.name+" / "+(node<graph.nodes.size()?graph.nodes[node].id:"?")+": "+why;
                return false;
            }
            const Link* incoming(size_t node,const std::string& input)const
            {
                const auto found=std::find_if(graph.links.begin(),graph.links.end(),[&](const Link& l){return l.to==node&&l.input==input;});
                return found==graph.links.end()?nullptr:&*found;
            }
            bool value(size_t node,const std::string& pin,std::string& result,unsigned depth)
            {
                const auto* wire=incoming(node,pin);
                if(!wire)return fail(node,"Connect input "+pin+".");
                return expression(wire->from,wire->output,result,depth+1);
            }
            bool api(size_t node,const BlueprintNodeSpec& spec,std::string& result,unsigned depth)
            {
                if(spec.hasOut)return fail(node,"Output-reference API calls are not implemented by the native compiler yet.");
                std::string target,args;
                for(const auto& p:spec.inputs)
                {
                    if(p.type=="exec")continue;
                    std::string v;
                    if(!value(node,p.name,v,depth))return false;
                    if(spec.member&&p.name=="target")target="("+v+").";
                    else{if(!args.empty())args+=", ";args+=v;}
                    if(args.size()>65536)return fail(node,"Expression too large.");
                }
                const auto sep=spec.call.rfind("::");
                const auto name=spec.member&&sep!=std::string::npos?spec.call.substr(sep+2):spec.call;
                if(spec.property=="get"||spec.property=="set")
                {
                    if(name.size()<5)return fail(node,"Malformed property metadata.");
                    result=target+name.substr(4);
                    if(spec.property=="set")
                    {
                        if(!spec.inputs.empty()&&spec.inputs.back().type.ends_with("@"))
                            return fail(node,"Handle property assignment is not implemented yet.");
                        result+=" = "+args;
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
                if(n.kind.rfind("literal:",0)==0&&pin=="value")
                {
                    const auto type=n.kind.substr(8);
                    if(type=="string")
                    {
                        for(unsigned char c:n.ref)if(c<32&&c!='\n'&&c!='\r'&&c!='\t')return fail(node,"Unsupported control character in string.");
                        result=quote(n.ref);
                    }
                    else if(type=="bool")result=n.ref;
                    else result=type+"("+n.ref+")";
                    return true;
                }
                if((n.kind=="add:float"||n.kind=="less:float")&&pin=="result")
                {
                    std::string a,b;
                    if(!value(node,"a",a,depth)||!value(node,"b",b,depth))return false;
                    result="("+a+(n.kind=="add:float"?" + ":" < ")+b+")";return true;
                }
                if(n.kind=="event:delta"&&graph.name=="update"&&pin=="value"){result="dt";return true;}
                if(n.kind=="event:canvas"&&graph.name=="draw"&&pin=="value"){result="canvas";return true;}
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
                if(!spec->call.empty()&&spec->pure&&pin=="result")
                    return api(node,*spec,result,depth);
                if(!spec->call.empty()&&!spec->pure&&pin=="result"&&spec->result!="void")
                {
                    if(!available[node])return fail(node,"Action result used before execution or outside its branch.");
                    result="bp_value_"+std::to_string(node);return true;
                }
                return fail(node,"This output is not supported by the native compiler yet.");
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
                if(n.kind=="branch")
                {
                    std::string condition;
                    if(!value(to,"condition",condition,0))return false;
                    const auto outer=available;
                    source+=pad+"if ("+condition+")\n"+pad+"{\n";
                    if(!flow(to,"true",indent+1,depth+1))return false;
                    available=outer;
                    source+=pad+"}\n"+pad+"else\n"+pad+"{\n";
                    if(!flow(to,"false",indent+1,depth+1))return false;
                    available=outer;
                    source+=pad+"}\n";return true;
                }
                if(!spec||spec->call.empty()||spec->pure||spec->result.empty())
                    return fail(to,"This execution node is not supported by the native compiler yet.");
                std::string statement;
                if(!api(to,*spec,statement,0))return false;
                if(spec->result!="void")
                {
                    source+=pad+spec->result+" bp_value_"+std::to_string(to)+" = "+statement+";\n";
                    available[to]=true;
                }
                else source+=pad+statement+";\n";
                if(source.size()>1024*1024)return fail(to,"Generated source exceeds the size limit.");
                return flow(to,"then",indent,depth+1);
            }
        };
    }
    bool compileBlueprint(const BlueprintDocument& document,std::string& source,std::string& error)
    {
        // Validate the public graph model before reading any endpoints.
        std::string checked;
        if(!document.serialize(checked,error))return false;
        std::string result="class BP_NativeScreen : Screen\n{\n";
        for(const auto& graph:document.graphs)
        {
            const auto entry=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const BlueprintDocument::Node& n){return n.kind=="entry";});
            if(entry==graph.nodes.end()){error="Missing event entry.";return false;}
            const auto index=static_cast<size_t>(entry-graph.nodes.begin());
            const auto signature=graph.name=="update"?"float dt":graph.name=="draw"?"Canvas@ canvas":"";
            result+="    void "+graph.name+"("+signature+")\n    {\n";
            Emitter e{graph,result,error,std::vector<bool>(graph.nodes.size(),false),std::vector<bool>(graph.nodes.size(),false)};
            if(!e.flow(index,"then",2,0))return false;
            result+="    }\n";
        }
        result+="}\n";source.swap(result);error.clear();return true;
    }
}
