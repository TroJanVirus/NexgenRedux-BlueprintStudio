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
            bool literalValue(size_t node,const std::string& type,const std::string& value,std::string& result)
            {
                if(type=="string")
                {
                    for(unsigned char c:value)if(c<32&&c!='\n'&&c!='\r'&&c!='\t')return fail(node,"Unsupported control character in string.");
                    result=quote(value);
                }
                else if(type=="bool")result=value;
                else result=type+"("+value+")";
                return true;
            }
            bool value(size_t node,const std::string& pin,std::string& result,unsigned depth)
            {
                const auto* wire=incoming(node,pin);
                if(!wire)
                {
                    const auto& n=graph.nodes[node];
                    if(const auto* spec=blueprintSpec(n.kind))
                    {
                        for(const auto& v:n.values)if(v.pin==pin)
                            for(const auto& p:spec->inputs)if(p.name==pin)return literalValue(node,p.type,v.value,result);
                        for(const auto& d:spec->defaults)if(d.name==pin){result=d.type;return true;}
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
                if(n.kind.rfind("literal:",0)==0&&pin=="value")
                {
                    return literalValue(node,n.kind.substr(8),n.ref,result);
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
                if(n.kind.rfind("var:set:",0)==0)
                {
                    std::string v;if(!value(to,"value",v,0))return false;
                    source+=pad+(n.kind.ends_with("@")?"@":"")+"bp_var_"+n.ref+" = "+v+";\n";
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="call:function")
                {
                    source+=pad+"bp_fn_"+n.ref+"();\n";
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="sequence")
                {
                    // First and second execute in order, in separate scopes.
                    const auto outer=available;
                    for(const char* output:{"first","second"})
                    {
                        source+=pad+"{\n";
                        if(!flow(to,output,indent+1,depth+1))return false;
                        source+=pad+"}\n";available=outer;
                    }
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="repeat")
                {
                    std::string count;if(!value(to,"count",count,0))return false;
                    const auto suffix=std::to_string(to),index="bp_index_"+suffix,limit="bp_limit_"+suffix;
                    const auto outer=available;
                    source+=pad+"{\n"+pad+"    int "+limit+" = "+count+";\n"+
                        pad+"    if ("+limit+" > 10000) "+limit+" = 10000;\n"+
                        pad+"    for (int "+index+" = 0; "+index+" < "+limit+"; ++"+index+")\n"+pad+"    {\n";
                    available[to]=true;
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
        std::vector<BlueprintPin> vars;
        if(!document.variables(vars,error))return false;
        for(const auto& v:vars)result+="    "+v.type+" bp_var_"+v.name+";\n";
        for(const auto& graph:document.graphs)
        {
            const auto entry=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const BlueprintDocument::Node& n){return n.kind=="entry";});
            if(entry==graph.nodes.end()){error="Missing event entry.";return false;}
            const auto index=static_cast<size_t>(entry-graph.nodes.begin());
            const auto signature=graph.name=="update"?"float dt":graph.name=="draw"?"Canvas@ canvas":"";
            result+="    void "+(graph.kind=="function"?"bp_fn_"+graph.name:graph.name)+"("+signature+")\n    {\n";
            Emitter e{graph,result,error,std::vector<bool>(graph.nodes.size(),false),std::vector<bool>(graph.nodes.size(),false)};
            if(!e.flow(index,"then",2,0))return false;
            result+="    }\n";
            if(result.size()>1024*1024){error="Generated source exceeds the size limit.";return false;}
        }
        result+="}\n";source.swap(result);error.clear();return true;
    }
    bool compileBlueprintScreen(const BlueprintDocument& document,std::string& source,std::string& manifest,std::string& error)
    {
        std::string code;
        if(!compileBlueprint(document,code,error))return false;
        JsonValue info;
        auto* title=info.put("title");if(!title){error="Cannot allocate screen manifest.";return false;}
        title->setText(document.name);
        auto* category=info.put("category");if(!category){error="Cannot allocate screen manifest.";return false;}
        category->setText("Blueprints");
        auto* description=info.put("description");if(!description){error="Cannot allocate screen manifest.";return false;}
        description->setText("Generated by native Blueprint Studio");
        auto json=info.write(false);
        source.swap(code);manifest.swap(json);error.clear();return true;
    }
}
