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
        bool deferredTime(const std::string& kind)
        {return kind=="time:delay"||kind=="time:retrigger-delay"||kind=="time:timer";}
        std::string deferredKey(size_t graph,size_t node)
        {return "bp_deferred_"+std::to_string(graph)+"_"+std::to_string(node);}
        std::string deferredTick(const std::string& owner)
        {return owner.rfind("behaviour:",0)==0?"bp_tick_deferred_"+owner.substr(10):"bp_tick_deferred";}
        bool hasDeferred(const BlueprintDocument& document,const std::string& owner)
        {
            for(const auto& g:document.graphs)if(g.owner==owner)
                for(const auto& n:g.nodes)if(deferredTime(n.kind))return true;
            return false;
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
            bool resuming=false;
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
                else if(!blueprintInputComponentLabels(type).empty())
                {
                    std::vector<std::string> components;
                    if(!blueprintInputComponents(type,value,components))return fail(node,"Invalid component value.");
                    result=type+"(";
                    for(size_t i=0;i<components.size();++i){if(i)result+=", ";result+="float("+components[i]+")";}
                    result+=")";
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
                    if(resuming)return fail(node,"Deferred continuations cannot read original function parameters; store them in instance fields first.");
                    for(const auto& p:graph.parameters)if(pin=="arg_"+p.name){result="bp_arg_"+p.name;return true;}
                }
                if(n.kind.rfind("capture:",0)==0&&pin=="result")
                {
                    if(!available[node])return fail(node,"Captured value used before execution or outside its branch.");
                    result="bp_local_"+n.ref+"_"+std::to_string(node);return true;
                }
                if((n.kind=="call:function"||n.kind=="call:library"||n.kind=="call:parent")&&pin=="result")
                {
                    if(!available[node])return fail(node,"Function result used before execution or outside its branch.");
                    result="bp_value_"+std::to_string(node);return true;
                }
                if(n.kind.rfind("literal:",0)==0&&pin=="value")
                {
                    return literalValue(node,n.kind.substr(8),n.ref,result);
                }
                if(n.kind=="time:timeline"||n.kind=="camera:orbit-timed")
                {
                    if(!available[node])return fail(node,"Timeline output must be used after its execution in Update.");
                    if(pin=="found"&&n.kind=="camera:orbit-timed"){result="bp_orbit_found_"+std::to_string(node);return true;}
                    if(pin!="progress"&&pin!="elapsed"&&pin!="finished")return fail(node,"Unknown Timeline output.");
                    result="bp_timeline_"+pin+"_"+std::to_string(node);return true;
                }
                if(n.kind=="math:orbit-position"&&pin=="result")
                {
                    std::string center,radius,height,turns;
                    if(!value(node,"center",center,depth)||!value(node,"radius",radius,depth)||!value(node,"height",height,depth)||!value(node,"turns",turns,depth))return false;
                    const auto phase="(("+turns+") == floor("+turns+") ? float(0) : ("+turns+") * float(6.283185307179586))";
                    result="(("+center+") + Vec3(sin("+phase+") * ("+radius+"), "+height+", cos("+phase+") * ("+radius+")))";return true;
                }
                if(n.kind.rfind("data:",0)==0)
                {
                    const auto* decl=document.dataType(n.ref);if(!decl)return fail(node,"Unknown custom type.");
                    const auto type="BP_Data_"+decl->name;
                    if(n.kind=="data:enum-value")
                    {
                        if(incoming(node,"member"))return fail(node,"Enum member must be a constant name.");
                        const auto at=std::find_if(n.values.begin(),n.values.end(),[](const BlueprintDocument::InputValue& v){return v.pin=="member";});
                        if(at==n.values.end()||std::none_of(decl->parameters.begin(),decl->parameters.end(),[&](const BlueprintPin& p){return p.name==at->value;}))
                            return fail(node,"Enter a declared enum member name.");
                        result=type+"::bp_"+at->value;return true;
                    }
                    if(n.kind=="data:enum-equal")
                    {
                        std::string a,b;if(!value(node,"a",a,depth)||!value(node,"b",b,depth))return false;
                        result="("+a+" == "+b+")";return true;
                    }
                    if(n.kind=="data:enum-index")
                    {
                        std::string v;if(!value(node,"value",v,depth))return false;result="int("+v+")";return true;
                    }
                    if(n.kind=="data:make-struct")
                    {
                        result=type+"(";for(size_t i=0;i<decl->parameters.size();++i)
                        {std::string v;if(!value(node,decl->parameters[i].name,v,depth))return false;if(i)result+=", ";result+=v;}
                        result+=")";return true;
                    }
                    if(n.kind=="data:break-struct")
                    {
                        std::string v;if(!value(node,"value",v,depth))return false;
                        result="("+v+").bp_"+pin;return true;
                    }
                }
                if(n.kind=="actor:is-class"&&pin=="result")
                {
                    std::string actor;if(!value(node,"actor",actor,depth))return false;
                    result="bp_is_class_"+n.ref+"("+actor+")";return true;
                }
                if(n.kind=="actor:class-entity"&&pin=="result")
                {
                    std::string target;if(!value(node,"target",target,depth))return false;
                    result="bp_class_entity_"+n.ref+"("+target+")";return true;
                }
                if(n.kind.rfind("typed:",0)==0&&pin=="value"&&n.kind.ends_with(":get"))
                {result=std::string(n.kind.rfind("typed:global:",0)==0?"bp_global_":"bp_var_")+n.ref;return true;}
                if(n.kind=="actor:cast-class"&&(pin=="result"||pin=="typed"))
                {
                    if(!available[node])return fail(node,"Cast result is only available in the success branch or after the cast.");
                    result=std::string(pin=="typed"?"bp_typed_":"bp_value_")+std::to_string(node);return true;
                }
                if((n.kind=="spawn:box"||n.kind=="spawn:plane"||n.kind=="actor:spawn-asset")&&pin=="result")
                {
                    if(!available[node])return fail(node,"Spawn result used before execution or outside its branch.");
                    result="bp_value_"+std::to_string(node);return true;
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
                if((n.kind=="null:entity"||n.kind=="null:playlist"||n.kind=="null:websocket"||n.kind=="null:widget")&&pin=="value"){result="null";return true;}
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
                if(n.kind.rfind("flow:foreach:",0)==0&&(pin=="index"||pin=="element"))
                {
                    if(!available[node])return fail(node,"For Each values are only available in the loop body.");
                    result=std::string(pin=="index"?"bp_index_":"bp_element_")+std::to_string(node);return true;
                }
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
                if(kind=="flow:controlled-gate"||deferredTime(kind))return false;
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
                if(to>=graph.nodes.size()||depth>128||budget==0)return fail(to,"Execution cycle or complexity limit.");
                const bool gateCommand=graph.nodes[to].kind=="flow:controlled-gate"&&found->input!="in";
                const bool cancelTime=deferredTime(graph.nodes[to].kind)&&found->input=="cancel";
                if(!gateCommand&&!cancelTime&&emitted[to])return fail(to,"Execution cycle or complexity limit.");
                --budget;if(!gateCommand&&!cancelTime)emitted[to]=true;
                const auto& n=graph.nodes[to];const auto* spec=blueprintSpec(n.kind);
                const std::string pad(indent*4,' ');
                const size_t start=source.size();
                if(n.kind=="time:cancel-all")
                {
                    if(graph.inlineMacro)return fail(to,"Timer cancellation is not supported in inline macros.");
                    for(auto owner=graph.owner;!owner.empty();owner=document.parentBehaviour(owner))
                        for(size_t gi=0;gi<document.graphs.size();++gi)
                        {
                            const auto& g=document.graphs[gi];if(g.owner!=owner)continue;
                            for(size_t ni=0;ni<g.nodes.size();++ni)if(deferredTime(g.nodes[ni].kind))
                                source+=pad+deferredKey(gi,ni)+"_active = false;\n";
                        }
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(deferredTime(n.kind))
                {
                    const auto key=deferredKey(graphIndex,to);
                    if(cancelTime){source+=pad+key+"_active = false;\n";mark(start,to);return true;}
                    const bool actor=graph.owner.rfind("behaviour:",0)==0;
                    if(graph.inlineMacro||graph.returnType!="void"||graph.name=="Construction"||
                        (graph.kind=="event"&&graph.name!="update"&&graph.name!=(actor?"start":"onEnter")))
                        return fail(to,"Deferred time requires Start, On Enter, Update or a callable void graph.");
                    std::string seconds,loop="false";
                    if(!value(to,"seconds",seconds,0)||(n.kind=="time:timer"&&!value(to,"loop",loop,0)))return false;
                    if(n.kind=="time:delay")source+=pad+"if (!"+key+"_active)\n"+pad+"{\n";
                    const auto p=n.kind=="time:delay"?pad+"    ":pad;
                    source+=p+key+"_period = max(float(0.000001), "+seconds+");\n";
                    source+=p+key+"_remaining = "+key+"_period;\n";
                    source+=p+key+"_loop = "+loop+";\n"+p+key+"_active = true;\n";
                    if(n.kind=="time:delay")source+=pad+"}\n";
                    mark(start,to);
                    return n.kind=="time:timer"?flow(to,"then",indent,depth+1):true;
                }
                if(n.kind=="time:timeline"||n.kind=="camera:orbit-timed")
                {
                    std::string delta,duration,loop,reset;
                    if(!value(to,"dt",delta,0)||!value(to,"duration",duration,0)||!value(to,"loop",loop,0)||!value(to,"reset",reset,0))return false;
                    const auto suffix=std::to_string(to),state="bp_timeline_state_"+std::to_string(graphIndex)+"_"+suffix;
                    const auto period="bp_timeline_period_"+suffix,cycle="bp_timeline_loop_"+suffix,elapsed="bp_timeline_elapsed_"+suffix,progress="bp_timeline_progress_"+suffix,finished="bp_timeline_finished_"+suffix;
                    source+=pad+"float "+period+" = max(float(0), "+duration+");\n"+pad+"bool "+cycle+" = "+loop+";\n";
                    source+=pad+"if ("+reset+") "+state+" = 0;\n";
                    source+=pad+"float "+elapsed+" = min("+state+", "+period+");\n";
                    source+=pad+"if ("+cycle+" && "+period+" > 0) "+elapsed+" = "+state+" - floor("+state+" / "+period+") * "+period+";\n";
                    source+=pad+"float "+progress+" = "+period+" > 0 ? clamp("+elapsed+" / "+period+", float(0), float(1)) : float(1);\n";
                    source+=pad+"bool "+finished+" = "+period+" <= 0 || (!"+cycle+" && "+elapsed+" >= "+period+");\n";
                    source+=pad+state+" = "+elapsed+" + max(float(0), "+delta+");\n";
                    source+=pad+"if (!"+cycle+" || "+period+" <= 0) "+state+" = min("+state+", "+period+");\n";
                    if(n.kind=="camera:orbit-timed")
                    {
                        std::string entityName,radius,height,turns,angle;
                        if(!value(to,"name",entityName,0)||!value(to,"radius",radius,0)||!value(to,"height",height,0)||!value(to,"turns",turns,0)||!value(to,"angle",angle,0))return false;
                        const auto target="bp_orbit_target_"+suffix,foundTarget="bp_orbit_found_"+suffix,center="bp_orbit_center_"+suffix,phase="bp_orbit_phase_"+suffix,distance="bp_orbit_radius_"+suffix,rotation="bp_orbit_turns_"+suffix;
                        source+=pad+"Entity@ "+target+" = Scene::find("+entityName+");\n"+pad+"bool "+foundTarget+" = "+target+" !is null && "+target+".alive;\n";
                        source+=pad+"if ("+foundTarget+")\n"+pad+"{\n"+pad+"    Vec3 "+center+" = "+target+".place;\n";
                        source+=pad+"    float "+rotation+" = "+progress+" * ("+turns+");\n"+pad+"    float "+phase+" = ("+rotation+" - floor("+rotation+")) * float(6.283185307179586);\n";
                        source+=pad+"    float "+distance+" = max(float(0.001), abs("+radius+"));\n";
                        source+=pad+"    Space::camera("+center+" + Vec3(sin("+phase+") * "+distance+", "+height+", cos("+phase+") * "+distance+"), "+center+", "+angle+");\n"+pad+"}\n";
                    }
                    available[to]=true;mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="ui:load-asset")
                {
                    if(incoming(to,"id"))return fail(to,"A linked widget ID must be a constant asset reference.");
                    const auto it=std::find_if(n.values.begin(),n.values.end(),[](const BlueprintDocument::InputValue& v){return v.pin=="id";});
                    std::string page,json,why;
                    if(it==n.values.end()||!document.visualWidgetForOwner(it->value,graph.owner,page,why))
                        return fail(to,why.empty()?"Select a linked widget page.":why);
                    std::string theme;
                    if(!document.visualWidgetTheme(it->value,theme,why))return fail(to,why);
                    if(!theme.empty())
                    {
                        if(!literalValue(to,"string",theme,json))return false;
                        source+=pad+"Ui::loadThemeSource("+json+");\n";
                    }
                    if(!literalValue(to,"string",page,json))return false;
                    source+=pad+"Ui::loadPage("+json+");\n";
                    if(source.size()>1024*1024)return fail(to,"Generated source exceeds the size limit.");
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="actor:spawn-asset")
                {
                    for(const char* overridePin:{"id","instanceName","rotationOverride","sizeOverride"})
                        if(incoming(to,overridePin))return fail(to,"Linked actor IDs and instance overrides must be constant authoring values.");
                    const auto it=std::find_if(n.values.begin(),n.values.end(),[](const BlueprintDocument::InputValue& v){return v.pin=="id";});
                    std::string json,why,sourceJson,place;
                    if(it==n.values.end()||!document.visualActorInstance(it->value,graph.owner,n.values,json,why))
                        return fail(to,why.empty()?"Select a linked actor asset.":why);
                    if(!literalValue(to,"string",json,sourceJson)||!value(to,"place",place,0))return false;
                    const auto actor="bp_value_"+std::to_string(to);
                    source+=pad+"Entity@ "+actor+" = Space::makeActor(Json("+sourceJson+"), "+place+");\n";
                    const auto owner=document.actorBehaviour(it->value);
                    const auto fields=document.actorSpawnFields(it->value);
                    const auto* construction=document.function(owner,"Construction");
                    if(!owner.empty()&&(!fields.empty()||construction))
                    {
                        const auto instance="bp_actor_behaviour_"+std::to_string(to),type="BP_Behaviour_"+owner.substr(10);
                        source+=pad+"if (Space::validEntity("+actor+"))\n"+pad+"{\n";
                        source+=pad+"    "+type+"@ "+instance+" = cast<"+type+">("+actor+".behaviour);\n";
                        source+=pad+"    if ("+instance+" !is null)\n"+pad+"    {\n";
                        for(const auto& field:fields)
                        {
                            const auto spawnPin="spawn_"+field.name;
                            if(!incoming(to,spawnPin)&&std::none_of(n.values.begin(),n.values.end(),[&](const BlueprintDocument::InputValue& v){return v.pin==spawnPin;}))continue;
                            std::string expr;if(!value(to,spawnPin,expr,0))return false;
                            source+=pad+"        "+instance+".bp_var_"+field.name+" = "+expr+";\n";
                        }
                        if(construction)source+=pad+"        "+instance+".bp_fn_Construction();\n";
                        source+=pad+"    }\n"+pad+"}\n";
                    }
                    available[to]=true;mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="render:space")
                {source+=pad+"Space::draw(canvas);\n";mark(start,to);return flow(to,"then",indent,depth+1);}
                if(n.kind=="spawn:box"||n.kind=="spawn:plane")
                {
                    std::string place,size,color,entityName;
                    if(!value(to,"place",place,0)||!value(to,"size",size,0)||!value(to,"color",color,0)||!value(to,"name",entityName,0))return false;
                    const auto result="bp_value_"+std::to_string(to);
                    source+=pad+"Entity@ "+result+" = Space::spawn("+place+");\n"+pad+result+".name = "+entityName+";\n"+pad+result+".size = "+size+";\n"+pad+result+".shape("+(n.kind=="spawn:box"?"SolidBox":"SolidPlane")+", "+color+");\n";
                    available[to]=true;mark(start,to);return flow(to,"then",indent,depth+1);
                }
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
                if(n.kind.rfind("typed:",0)==0&&n.kind.ends_with(":set"))
                {
                    BlueprintNodeSpec resolved;if(!document.nodeSpec(graph,n,resolved))return fail(to,"Invalid typed field.");
                    std::string v;if(!value(to,"value",v,0))return false;
                    source+=pad+(resolved.inputs.back().type.ends_with("@")?"@":"")+std::string(n.kind.rfind("typed:global:",0)==0?"bp_global_":"bp_var_")+n.ref+" = "+v+";\n";
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind.rfind("global:set:",0)==0)
                {
                    std::string v;if(!value(to,"value",v,0))return false;
                    source+=pad+(n.kind.ends_with("@")?"@":"")+"bp_global_"+n.ref+" = "+v+";\n";mark(start,to);return flow(to,"then",indent,depth+1);
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
                if(n.kind=="actor:cast-class")
                {
                    std::string actor;if(!value(to,"actor",actor,0))return false;
                    const auto v="bp_value_"+std::to_string(to);
                    source+=pad+"Entity@ "+v+" = "+actor+";\n";
                    const auto typed="bp_typed_"+std::to_string(to);
                    source+=pad+"BP_Behaviour_"+n.ref+"@ "+typed+" = null;\n";
                    source+=pad+"if (Space::validEntity("+v+")) @"+typed+" = cast<BP_Behaviour_"+n.ref+">("+v+".behaviour);\n";
                    source+=pad+"if ("+typed+" !is null)\n"+pad+"{\n";mark(start,to);
                    const auto outer=available;available[to]=true;
                    if(!flow(to,"success",indent+1,depth+1))return false;
                    available=outer;source+=pad+"}\n"+pad+"else\n"+pad+"{\n"+pad+"    @"+v+" = null;\n";
                    if(!flow(to,"failure",indent+1,depth+1))return false;
                    available=outer;available[to]=true;source+=pad+"}\n";
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="async:wait"||n.kind=="async:next")
                {
                    if(graph.dataRole!="coroutine")return fail(to,"Wait nodes require a coroutine.");
                    if(n.kind=="async:next")source+=pad+"nextUpdate();\n";
                    else {std::string seconds;if(!value(to,"seconds",seconds,0))return false;source+=pad+"wait(max(float(0), "+seconds+"));\n";}
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="async:start")
                {
                    const auto* target=document.function(graph.owner,n.ref);
                    if(!target||target->dataRole!="coroutine")return fail(to,"Select a coroutine graph.");
                    source+=pad+"::start(Coroutine(this.bp_fn_"+n.ref+"));\n";
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="dispatch:bind-listener"||n.kind=="dispatch:unbind-listener")
                {
                    if(graph.inlineMacro||incoming(to,"handler"))return fail(to,"Select a constant listener name in a callable graph.");
                    const auto it=std::find_if(n.values.begin(),n.values.end(),[](const BlueprintDocument::InputValue& v){return v.pin=="handler";});
                    const auto listeners=document.dispatcherListeners(graph.owner,n.ref);
                    if(it==n.values.end()||std::none_of(listeners.begin(),listeners.end(),[&](const Graph* g){return g->name==it->value;}))
                        return fail(to,"Select a local void handler with the same parameter types as the dispatcher.");
                    source+=pad+"bp_listener_"+std::to_string(n.ref.size())+"_"+n.ref+"_"+it->value+(n.kind=="dispatch:bind-listener"?" = true;\n":" = false;\n");
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="dispatch:bind"||n.kind=="dispatch:unbind")
                {
                    if(graph.inlineMacro)return fail(to,"Dispatcher binding requires a callable graph.");
                    source+=pad+"bp_bound_"+n.ref+(n.kind=="dispatch:bind"?" = true;\n":" = false;\n");
                    mark(start,to);return flow(to,"then",indent,depth+1);
                }
                if(blueprintInvokesFunction(n.kind)||n.kind=="call:parent")
                {
                    const auto* target=n.kind=="call:library"?document.libraryFunction(n.ref):n.kind=="call:parent"?document.function(document.parentBehaviour(graph.owner),graph.name):document.function(graph.owner,n.ref);
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
                    const auto call=(n.kind=="call:parent"?"BP_Behaviour_"+target->owner.substr(10)+"::":"")+(target->sharedLibrary?"bp_lib_":"bp_fn_")+target->name+"("+args+")";
                    if(n.kind=="dispatch:broadcast")
                    {
                        if(graph.inlineMacro)return fail(to,"Dispatcher broadcast requires a callable graph.");
                        if(document.dispatcherListeners(target->owner,n.ref).empty())source+=pad+"if (bp_bound_"+n.ref+") "+call+";\n";
                        else source+=pad+"bp_broadcast_"+n.ref+"("+args+");\n";
                    }
                    else if(target->returnType=="void")source+=pad+call+";\n";
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
                if(n.kind=="flow:controlled-gate")
                {
                    if(graph.inlineMacro)return fail(to,"Stateful gates are not supported in inline macros.");
                    const auto state="bp_flow_state_"+std::to_string(graphIndex)+"_"+std::to_string(to);
                    if(gateCommand)
                    {
                        if(found->input=="open")source+=pad+state+" = 1;\n";
                        else if(found->input=="close")source+=pad+state+" = 0;\n";
                        else if(found->input=="toggle")source+=pad+state+" = 1 - "+state+";\n";
                        else return fail(to,"Unknown gate command.");
                        mark(start,to);return true;
                    }
                    const auto outer=available;
                    source+=pad+"if ("+state+" != 0)\n"+pad+"{\n";mark(start,to);
                    if(!flow(to,"body",indent+1,depth+1))return false;
                    available=outer;source+=pad+"}\n";return true;
                }
                if(n.kind=="flow:flip-flop"||n.kind=="flow:multi-gate"||n.kind=="flow:switch-int")
                {
                    const bool flip=n.kind=="flow:flip-flop",multi=n.kind=="flow:multi-gate";
                    const auto suffix=std::to_string(to),state="bp_flow_state_"+std::to_string(graphIndex)+"_"+suffix,choice="bp_choice_"+suffix;
                    if(flip||multi)
                    {
                        if(graph.inlineMacro)return fail(to,"Stateful flow nodes are not supported in inline macros.");
                        std::string reset,loop;
                        if(!value(to,"reset",reset,0)||(multi&&!value(to,"loop",loop,0)))return false;
                        source+=pad+"if ("+reset+") "+state+" = 0;\n";
                        if(multi)source+=pad+"if ("+state+" >= 4 && ("+loop+")) "+state+" = 0;\n";
                        source+=pad+"int "+choice+" = "+state+";\n";
                        source+=pad+(flip?state+" = 1 - "+state+";\n":"if ("+state+" < 4) ++"+state+";\n");
                    }
                    else
                    {
                        std::string v;if(!value(to,"value",v,0))return false;
                        source+=pad+"int "+choice+" = "+v+";\n";
                    }
                    const auto outer=available;
                    const std::vector<std::string> ports=flip?std::vector<std::string>{"a","b"}:std::vector<std::string>{"first","second","third","fourth"};
                    for(size_t i=0;i<ports.size();++i)
                    {
                        std::string key=std::to_string(i);
                        if(!flip&&!multi&&!value(to,"case"+key,key,0))return false;
                        source+=pad+(i?"else if (":"if (")+choice+" == ("+key+"))\n"+pad+"{\n";
                        if(i==0)mark(start,to);
                        if(!flow(to,ports[i],indent+1,depth+1))return false;
                        available=outer;source+=pad+"}\n";
                    }
                    if(!flip&&!multi)
                    {
                        source+=pad+"else\n"+pad+"{\n";
                        if(!flow(to,"default",indent+1,depth+1))return false;
                        available=outer;source+=pad+"}\n";
                    }
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind.rfind("flow:foreach:",0)==0)
                {
                    std::string target;if(!value(to,"target",target,0))return false;
                    const auto element=n.kind.substr(13),suffix=std::to_string(to),array="bp_each_array_"+suffix,index="bp_index_"+suffix,limit="bp_each_limit_"+suffix;
                    const auto outer=available;
                    source+=pad+"{\n"+pad+"    array<"+element+">@ "+array+" = "+target+";\n";
                    source+=pad+"    if ("+array+" !is null)\n"+pad+"    {\n";
                    source+=pad+"        uint "+limit+" = "+array+".length();\n";
                    source+=pad+"        if ("+limit+" > 10000) "+limit+" = 10000;\n";
                    source+=pad+"        for (uint "+index+" = 0; "+index+" < "+limit+" && "+index+" < "+array+".length(); ++"+index+")\n"+pad+"        {\n";
                    source+=pad+"            "+element+" bp_element_"+suffix+" = "+array+"["+index+"];\n";
                    mark(start,to);available[to]=true;
                    if(!flow(to,"body",indent+3,depth+1))return false;
                    available=outer;source+=pad+"        }\n"+pad+"    }\n"+pad+"}\n";
                    return flow(to,"then",indent,depth+1);
                }
                if(n.kind=="flow:do-once"||n.kind=="flow:do-n"||n.kind=="flow:gate")
                {
                    const auto outer=available;
                    if(n.kind=="flow:gate")
                    {
                        std::string open;if(!value(to,"open",open,0))return false;
                        source+=pad+"if ("+open+")\n";
                    }
                    else
                    {
                        if(graph.inlineMacro)return fail(to,"Stateful flow nodes belong in events or functions, not inline macros.");
                        std::string reset,count="1";
                        if(!value(to,"reset",reset,0)||(n.kind=="flow:do-n"&&!value(to,"count",count,0)))return false;
                        const auto state="bp_flow_state_"+std::to_string(graphIndex)+"_"+std::to_string(to);
                        source+=pad+"if ("+reset+") "+state+" = 0;\n";
                        source+=pad+"if ("+state+" < 10000 && "+state+" < ("+count+"))\n";
                    }
                    source+=pad+"{\n";
                    if(n.kind!="flow:gate")
                        source+=pad+"    ++bp_flow_state_"+std::to_string(graphIndex)+"_"+std::to_string(to)+";\n";
                    mark(start,to);
                    if(!flow(to,"body",indent+1,depth+1))return false;
                    available=outer;source+=pad+"}\n";
                    return flow(to,"then",indent,depth+1);
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
                if(blueprintInvokesFunction(node.kind))
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
                    if(blueprintInvokesFunction(node.kind))
                        for(size_t j=0;j<document.graphs.size();++j)
                            if(document.graphs[j].owner==owner&&document.graphs[j].kind=="function"&&document.graphs[j].name==node.ref)work.push_back(j);
                }
            }
        }
        if(usesLevel)result+="#include \"global:scene.as\"\n\n";
        // Typed bounded collections compile to ordinary AngelScript classes using
        // the existing native array add-on. Only emit classes the project uses.
        for(const std::string collection:{"Map","Set"})for(const std::string element:{"Int","String"})
        {
            const bool isMap=collection=="Map";
            const std::string type="BP_"+collection+element,scalar=element=="Int"?"int":"string",key=isMap?"string":scalar;
            bool used=false;
            for(const auto& g:document.graphs)
            {
                for(const auto& f:g.fields)if(f.type==type)used=true;
                for(const auto& f:g.globals)if(f.type==type)used=true;
                for(const auto& n:g.nodes)if(n.kind=="var:get:"+type||n.kind=="global:get:"+type||n.kind.rfind("collection:"+type+":",0)==0)used=true;
            }
            if(!used)continue;
            result+="class "+type+"\n{\n    private array<"+key+"> keys;\n";
            if(isMap)result+="    private array<"+scalar+"> values;\n";
            result+="    private int bp_find("+key+" key) { for (uint i = 0; i < keys.length(); ++i) if (keys[i] == key) return int(i); return -1; }\n";
            result+="    uint bp_length() { return keys.length(); }\n    bool bp_contains("+key+" key) { return bp_find(key) >= 0; }\n";
            result+="    void bp_clear() { keys.resize(0);"+std::string(isMap?" values.resize(0);":"")+" }\n";
            result+="    bool bp_remove("+key+" key) { int at = bp_find(key); if (at < 0) return false; keys.removeAt(uint(at));"+std::string(isMap?" values.removeAt(uint(at));":"")+" return true; }\n";
            if(isMap)
            {
                result+="    "+scalar+" bp_get(string key, "+scalar+" fallback) { int at = bp_find(key); if (at < 0) return fallback; return values[uint(at)]; }\n";
                result+="    bool bp_set(string key, "+scalar+" value) { int at = bp_find(key); if (at >= 0) { values[uint(at)] = value; return true; } if (keys.length() >= 4096) return false; keys.insertLast(key); values.insertLast(value); return true; }\n";
            }
            else result+="    bool bp_add("+key+" key) { if (bp_find(key) >= 0 || keys.length() >= 4096) return false; keys.insertLast(key); return true; }\n";
            result+="}\n";
        }
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
        for(const auto& g:document.graphs)if(g.dataRole=="enum"||g.dataRole=="struct")
        {
            const auto type="BP_Data_"+g.name;
            if(g.dataRole=="enum")
            {
                result+="enum "+type+" { ";
                for(size_t i=0;i<g.parameters.size();++i){if(i)result+=", ";result+="bp_"+g.parameters[i].name+" = "+std::to_string(i);}
                result+=" }\n";
            }
            else
            {
                result+="class "+type+"\n{\n";
                std::string args,body;
                for(const auto& p:g.parameters)
                {
                    result+="    "+p.type+" bp_"+p.name+";\n";
                    if(!args.empty())args+=", ";
                    args+=p.type+" value_"+p.name;
                    body+="        bp_"+p.name+" = value_"+p.name+";\n";
                }
                result+="    "+type+"() {}\n    "+type+"("+args+")\n    {\n"+body+"    }\n}\n";
            }
        }
        for(size_t gi=0;gi<document.graphs.size();++gi)
        {
            const auto& g=document.graphs[gi];if(!g.sharedLibrary)continue;
            const auto entry=std::find_if(g.nodes.begin(),g.nodes.end(),[](const BlueprintDocument::Node& n){return n.kind=="entry";});
            if(entry==g.nodes.end()){error="Missing library entry.";return false;}
            const auto index=static_cast<size_t>(entry-g.nodes.begin());
            std::string scratch;std::vector<ByteSpan> ignored;
            if(g.inlineMacro)
            {
                Emitter e{document,g,scratch,error,std::vector<bool>(g.nodes.size(),false),std::vector<bool>(g.nodes.size(),false),gi,location,ignored};
                if(!e.flow(index,"then",1,0))return false;
                continue;
            }
            std::string signature;for(const auto& p:g.parameters){if(!signature.empty())signature+=", ";signature+=p.type+" bp_arg_"+p.name;}
            const auto start=result.size();result+=g.returnType+" bp_lib_"+g.name+"("+signature+")\n{\n";
            spans.push_back({start,result.size(),{gi,index}});
            Emitter e{document,g,result,error,std::vector<bool>(g.nodes.size(),false),std::vector<bool>(g.nodes.size(),false),gi,location,spans};
            if(g.returnType!="void"&&!e.returns(index,"then",0))return e.fail(index,"Not every library execution path returns a value.");
            if(!e.flow(index,"then",1,0))return false;
            result+="}\n";
        }
        std::vector<std::string> classes;
        for(const auto& name:document.behaviours())
        {
            const auto classOwner="behaviour:"+name;
            const auto scope=document.moduleForOwner(classOwner);
            if(scope.empty()||scope==owner)classes.push_back(classOwner);
        }
        const auto depth=[&](const std::string& value){unsigned n=0;for(auto at=document.parentBehaviour(value);!at.empty()&&n<16;at=document.parentBehaviour(at))++n;return n;};
        std::stable_sort(classes.begin(),classes.end(),[&](const std::string& a,const std::string& b){return depth(a)<depth(b);});
        classes.push_back(owner);
        for(const auto& classOwner:classes)
        {
        const bool behaviour=classOwner!=owner;
        const auto parent=document.parentBehaviour(classOwner);
        result+=behaviour?"class BP_Behaviour_"+classOwner.substr(10)+" : "+(parent.empty()?"Behaviour":"BP_Behaviour_"+parent.substr(10))+"\n{\n":"class BP_NativeScreen : Screen\n{\n";
        if(!behaviour&&usesLevel)result+="    Level bp_level;\n";
        // Each callable void graph can be used as an instance-local named signal.
        // Fields live on the declaring class, so inherited handlers share the binding.
        for(const auto& g:document.graphs)
            if(g.owner==classOwner&&g.kind=="function"&&!g.sharedLibrary&&g.dataRole!="enum"&&g.dataRole!="struct"&&g.returnType=="void"&&!g.inlineMacro&&
                !document.function(parent,g.name))
                result+="    bool bp_bound_"+g.name+" = false;\n";
        for(const auto& g:document.graphs)if(g.owner==classOwner&&g.kind=="function")
        {
            const auto listeners=document.dispatcherListeners(classOwner,g.name);if(listeners.empty()||document.function(classOwner,g.name)!=&g)continue;
            const auto key="bp_listener_"+std::to_string(g.name.size())+"_"+g.name+"_";
            for(const auto* handler:listeners)result+="    bool "+key+handler->name+" = false;\n";
            std::string signature,args;
            for(const auto& p:g.parameters){if(!args.empty()){args+=", ";signature+=", ";}args+="bp_arg_"+p.name;signature+=p.type+" bp_arg_"+p.name;}
            result+="    void bp_broadcast_"+g.name+"("+signature+")\n    {\n";
            result+="        bool bp_primary = bp_bound_"+g.name+";\n";
            for(size_t i=0;i<listeners.size();++i)result+="        bool bp_snapshot_"+std::to_string(i)+" = "+key+listeners[i]->name+";\n";
            result+="        if (bp_primary) bp_fn_"+g.name+"("+args+");\n";
            for(size_t i=0;i<listeners.size();++i)result+="        if (bp_snapshot_"+std::to_string(i)+") bp_fn_"+listeners[i]->name+"("+args+");\n";
            result+="    }\n";
        }
        if(!document.variables(vars,error,classOwner))return false;
        std::vector<BlueprintPin> inherited;
        if(!parent.empty()&&!document.variables(inherited,error,parent))return false;
        std::string initializers;
        for(const auto& v:vars)
        {
            if(std::any_of(inherited.begin(),inherited.end(),[&](const BlueprintPin& p){return p.name==v.name;}))
            {
                for(const auto& g:document.graphs)if(g.owner==classOwner)for(const auto& field:g.fields)if(field.name==v.name&&field.initialized)
                {
                    std::string literal;Emitter e{document,g,result,error,{}, {},0,location,spans};
                    if(!e.literalValue(0,field.type,field.value,literal))return false;
                    initializers+="        bp_var_"+field.name+" = "+literal+";\n";
                }
                continue;
            }
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
        if(!initializers.empty())result+="    BP_Behaviour_"+classOwner.substr(10)+"()\n    {\n"+initializers+"    }\n";
        std::vector<std::pair<size_t,size_t>> deferred;
        // Each Timeline belongs to one screen/Behaviour instance and one Update node.
        for(size_t gi=0;gi<document.graphs.size();++gi)
        {
            const auto& g=document.graphs[gi];if(g.owner!=classOwner)continue;
            for(size_t ni=0;ni<g.nodes.size();++ni)
            {
                const auto& kind=g.nodes[ni].kind;
                if(deferredTime(kind))
                {
                    deferred.emplace_back(gi,ni);
                    const auto key=deferredKey(gi,ni);
                    result+="    bool "+key+"_active = false;\n    bool "+key+"_loop = false;\n    float "+key+"_period = 0;\n    float "+key+"_remaining = 0;\n";
                }
                else if(kind=="time:timeline"||kind=="camera:orbit-timed")
                    result+="    float bp_timeline_state_"+std::to_string(gi)+"_"+std::to_string(ni)+" = 0;\n";
                else if(kind=="flow:do-once"||kind=="flow:do-n"||kind=="flow:flip-flop"||kind=="flow:multi-gate"||kind=="flow:controlled-gate")
                    result+="    int bp_flow_state_"+std::to_string(gi)+"_"+std::to_string(ni)+" = 0;\n";
            }
        }
        std::vector<std::string> ticking;
        for(auto at=classOwner;!at.empty();at=document.parentBehaviour(at))
            if(hasDeferred(document,at))ticking.push_back(at);
        std::reverse(ticking.begin(),ticking.end());
        for(size_t graphIndex=0;graphIndex<document.graphs.size();++graphIndex)
        {
            const auto& graph=document.graphs[graphIndex];
            if(graph.owner!=classOwner||graph.sharedLibrary||graph.dataRole=="enum"||graph.dataRole=="struct")continue;
            const bool inheritedEmpty=!parent.empty()&&graph.kind=="event"&&graph.nodes.size()==1;
            const bool timerWrapper=inheritedEmpty&&graph.name=="update"&&!deferred.empty();
            if(inheritedEmpty&&!timerWrapper)continue;
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
            if(graph.kind=="event"&&graph.name=="update")
            {
                if(timerWrapper)
                {
                    result+="        BP_Behaviour_"+parent.substr(10)+"::update(dt);\n";
                    result+="        if (entity is null || !entity.alive) return;\n";
                    result+="        "+deferredTick(classOwner)+"(dt);\n";
                }
                else for(const auto& at:ticking)
                {
                    result+="        "+deferredTick(at)+"(dt);\n";
                    if(behaviour)result+="        if (entity is null || !entity.alive) return;\n";
                }
            }
            // Break the Level -> delegate -> Screen reference cycle on exit.
            if(!behaviour&&graph.name=="onExit")
            {
                if(touch)result+="        @bp_level.onTouch = null;\n";
                if(agent)result+="        @bp_level.onAgent = null;\n";
                for(const auto& item:deferred)result+="        "+deferredKey(item.first,item.second)+"_active = false;\n";
            }
            spans.push_back({start,result.size(),{graphIndex,index}});
            Emitter e{document,graph,result,error,std::vector<bool>(graph.nodes.size(),false),std::vector<bool>(graph.nodes.size(),false),graphIndex,location,spans};
            if(graph.returnType!="void"&&!e.returns(index,"then",0))return e.fail(index,"Not every execution path returns a value.");
            if(!e.flow(index,"then",2,0))return false;
            result+="    }\n";
            if(result.size()>1024*1024){error="Generated source exceeds the size limit.";return false;}
        }
        if(!deferred.empty())
        {
            result+="    void "+deferredTick(classOwner)+"(float dt)\n    {\n";
            // Snapshot/decrement first so a callback cannot make a new timer fire in the same tick.
            for(const auto& item:deferred)
            {
                const auto key=deferredKey(item.first,item.second);
                result+="        bool "+key+"_due = false;\n";
                result+="        if ("+key+"_active) { "+key+"_remaining -= max(float(0), dt); "+key+"_due = "+key+"_remaining <= 0; }\n";
            }
            for(const auto& item:deferred)
            {
                const auto key=deferredKey(item.first,item.second);
                result+="        if ("+key+"_due && "+key+"_active && "+key+"_remaining <= 0)\n        {\n";
                result+="            "+key+"_active = "+key+"_loop;\n";
                result+="            "+key+"_remaining = "+key+"_period;\n";
                result+="            "+key+"_resume(dt);\n";
                if(behaviour)result+="            if (entity is null || !entity.alive) return;\n";
                result+="        }\n";
            }
            result+="    }\n";
            for(const auto& item:deferred)
            {
                const auto& g=document.graphs[item.first];
                result+="    void "+deferredKey(item.first,item.second)+"_resume(float dt)\n    {\n";
                Emitter e{document,g,result,error,std::vector<bool>(g.nodes.size(),false),std::vector<bool>(g.nodes.size(),false),item.first,location,spans};
                e.resuming=true;
                if(!e.flow(item.second,"completed",2,0))return false;
                result+="    }\n";
                if(result.size()>1024*1024){error="Generated source exceeds the size limit.";return false;}
            }
        }
        result+="}\n";
        }
        for(const auto& className:classes)if(className!=owner)
        {
            const auto type=className.substr(10);
            bool used=false,entityUsed=false;for(const auto& g:document.graphs)for(const auto& n:g.nodes)
            {
                if(n.kind=="actor:is-class"&&n.ref==type)used=true;
                if(n.kind=="actor:class-entity"&&n.ref==type)entityUsed=true;
            }
            if(entityUsed)result+="Entity@ bp_class_entity_"+type+"(BP_Behaviour_"+type+"@ target)\n{\n    if (target is null || !Space::validEntity(target.entity)) return null;\n    return target.entity;\n}\n";
            if(!used)continue;
            result+="bool bp_is_class_"+type+"(Entity@ actor)\n{\n";
            result+="    return Space::validEntity(actor) && cast<BP_Behaviour_"+type+">(actor.behaviour) !is null;\n}\n";
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
