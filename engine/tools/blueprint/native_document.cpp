#include "tools/blueprint/native_document.h"
#include "base/json_tree.h"
#include "base/json_value.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace nx
{
    namespace
    {
        struct Row {const char* id;const char* title;const char* category;const char* inputs;const char* outputs;
            const char* call="";const char* property="";const char* member="";const char* pure="";const char* result="";const char* hasOut="";const char* arguments="";const char* defaults="";};
        constexpr Row rows[]={
            {"entry","Event entry","Flow","","then:exec"},
            {"branch","Branch","Flow","in:exec|condition:bool","true:exec|false:exec|then:exec"},
            {"literal:string","String","Values","","value:string"},
            {"literal:bool","Boolean","Values","","value:bool"},
            {"literal:int","Integer","Values","","value:int"},
            {"literal:float","Float","Values","","value:float"},
            {"literal:uint","Unsigned integer","Values","","value:uint"},
            {"literal:double","Double","Values","","value:double"},
            {"make:Vec2","Make Vec2","Values","x:float|y:float","result:Vec2"},
            {"make:Vec3","Make Vec3","Values","x:float|y:float|z:float","result:Vec3"},
            {"make:Color","Make Color","Values","r:float|g:float|b:float|a:float","result:Color"},
            {"event:delta","Delta time","Events","","value:float"},
            {"event:canvas","Canvas","Events","","value:Canvas@"},
            {"event:entity","Owning entity","Events","","value:Entity@"},
            {"event:other","Collision partner","Events","","value:Entity@"},
            {"event:levelName","Level event name","Events","","value:string"},
            {"event:levelWho","Trigger visitor","Events","","value:Entity@"},
            {"event:levelWhat","Agent event","Events","","value:string"},
            {"level:attach","Attach behaviour to named level entity","Level","in:exec|name:string","then:exec|result:bool"},
            {"attach:behaviour","Attach behaviour","Behaviours","in:exec|target:Entity@","then:exec|result:bool"},
            {"var:get:bool","Get bool variable","Variables","","value:bool"},
            {"var:set:bool","Set bool variable","Variables","in:exec|value:bool","then:exec"},
            {"var:get:int","Get int variable","Variables","","value:int"},
            {"var:set:int","Set int variable","Variables","in:exec|value:int","then:exec"},
            {"var:get:uint","Get uint variable","Variables","","value:uint"},
            {"var:set:uint","Set uint variable","Variables","in:exec|value:uint","then:exec"},
            {"var:get:float","Get float variable","Variables","","value:float"},
            {"var:set:float","Set float variable","Variables","in:exec|value:float","then:exec"},
            {"var:get:double","Get double variable","Variables","","value:double"},
            {"var:set:double","Set double variable","Variables","in:exec|value:double","then:exec"},
            {"var:get:string","Get string variable","Variables","","value:string"},
            {"var:set:string","Set string variable","Variables","in:exec|value:string","then:exec"},
            {"var:get:Vec2","Get Vec2 variable","Variables","","value:Vec2"},
            {"var:set:Vec2","Set Vec2 variable","Variables","in:exec|value:Vec2","then:exec"},
            {"var:get:Vec3","Get Vec3 variable","Variables","","value:Vec3"},
            {"var:set:Vec3","Set Vec3 variable","Variables","in:exec|value:Vec3","then:exec"},
            {"var:get:Color","Get Color variable","Variables","","value:Color"},
            {"var:set:Color","Set Color variable","Variables","in:exec|value:Color","then:exec"},
            {"var:get:Entity@","Get Entity@ variable","Variables","","value:Entity@"},
            {"var:set:Entity@","Set Entity@ variable","Variables","in:exec|value:Entity@","then:exec"},
            {"sequence","Sequence","Flow","in:exec","first:exec|second:exec|then:exec"},
            {"call:function","Call function","Functions","in:exec","then:exec"},
            {"return:function","Return","Functions","in:exec",""},
            {"repeat","Repeat (bounded)","Flow","in:exec|count:int","body:exec|then:exec|index:int"},
            // The installed global:scene.as helper (ordinary AngelScript, not a second scene runtime).
            {"level:load","Load native level","Level","in:exec|file:string","then:exec|result:bool","bp_level.load","","","","bool","","in:file"},
            {"level:clear","Clear level","Level","in:exec","then:exec","bp_level.clear","","","","void"},
            {"level:update","Update native level","Level","in:exec|dt:float|who:Entity@|shot:bool|shotAt:Vec3","then:exec|result:string","bp_level.update","","","","string","","in:dt|in:who|in:shot|in:shotAt","who:null|shot:false|shotAt:Vec3()"},
            {"level:drawHud","Draw level HUD","Level","in:exec|canvas:Canvas@","then:exec","bp_level.drawHud","","","","void","","in:canvas"},
            {"level:find","Find named level entity","Level","name:string","result:Entity@","bp_level.find","","","1","Entity@","","in:name"},
            {"level:show","Show level entity","Level","in:exec|name:string|seconds:float|how:string","then:exec","bp_level.show","","","","void","","in:name|in:seconds|in:how","seconds:0.6f|how:\"\""},
            {"level:hide","Hide level entity","Level","in:exec|name:string|seconds:float","then:exec","bp_level.hide","","","","void","","in:name|in:seconds","seconds:0.4f"},
            {"level:switchOn","Switch level trigger","Level","in:exec|name:string|on:bool","then:exec","bp_level.switchOn","","","","void","","in:name|in:on"},
            {"level:isOn","Level trigger enabled","Level","name:string","result:bool","bp_level.isOn","","","1","bool","","in:name"},
            {"level:arrival","Find level arrival","Level","in:exec|name:string","then:exec|result:bool|place:Vec3|turned:float","bp_level.arrival","","","","bool","1","in:name|out:place|out:turned"},
            {"level:set:number","Set HUD number","Level","in:exec|name:string|value:float","then:exec","bp_level.set","","","","void","","in:name|in:value"},
            {"level:set:text","Set HUD text","Level","in:exec|name:string|value:string","then:exec","bp_level.set","","","","void","","in:name|in:value"},
            {"level:number","Get HUD number","Level","name:string","result:float","bp_level.number","","","1","float","","in:name"},
            {"level:text","Get HUD text","Level","name:string","result:string","bp_level.text","","","1","string","","in:name"},
            {"level:say","Show level message","Level","in:exec|text:string|seconds:float","then:exec","bp_level.say","","","","void","","in:text|in:seconds","seconds:3"},
#include "tools/blueprint/native_catalog.inc"
        };
        std::vector<BlueprintPin> parsePins(const std::string& text)
        {
            std::vector<BlueprintPin> result;
            for(size_t start=0;start<text.size();)
            {
                const size_t sep=text.find(':',start),end=text.find('|',start);
                if(sep==std::string::npos||(end!=std::string::npos&&sep>end))break;
                result.push_back({text.substr(start,sep-start),text.substr(sep+1,end==std::string::npos?end:end-sep-1)});
                if(end==std::string::npos)break;
                start=end+1;
            }
            return result;
        }
        const BlueprintPin* findPin(const std::vector<BlueprintPin>& pins,const std::string& name)
        {
            const auto p=std::find_if(pins.begin(),pins.end(),[&](const BlueprintPin& x){return x.name==name;});
            return p==pins.end()?nullptr:&*p;
        }
        constexpr size_t kMaxNodes=1024,kMaxLinks=4096,kMaxGraphs=64,kHistory=48;
        bool coordinate(float v){return std::isfinite(v)&&std::abs(v)<=1000000;}
        bool lifecycle(const std::string& name)
        {return name=="onEnter"||name=="update"||name=="draw"||name=="onSuspend"||name=="onResume"||name=="onExit"||name=="onLevelTouch"||name=="onLevelAgent";}
        bool variableKind(const std::string& kind){return kind.rfind("var:get:",0)==0||kind.rfind("var:set:",0)==0;}
        bool identifier(const std::string& s)
        {
            if(s.empty()||s.size()>64)return false;
            for(size_t i=0;i<s.size();++i){const char c=s[i];if(!(c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(i&&c>='0'&&c<='9')))return false;}
            return true;
        }
        bool screenId(const std::string& s)
        {
            if(s=="Screen")return true; // Initial screen, retained for native v2 drafts.
            if(s.empty()||s.size()>40||s=="main"||s=="screen"||s.front()<'a'||s.front()>'z')return false;
            return blueprintAssetName(s,true);
        }
        std::string scoped(const std::string& owner,const std::string& name){return owner+"/"+name;}
        bool behaviourOwner(const std::string& owner){return owner.rfind("behaviour:",0)==0&&identifier(owner.substr(10));}
        bool behaviourEvent(const std::string& name)
        {return name=="start"||name=="update"||name=="onHit"||name=="onLeave"||name=="onGone";}
        bool ownerEvent(const std::string& owner,const std::string& name)
        {return behaviourOwner(owner)?behaviourEvent(name):lifecycle(name);}
        bool context(const std::string& event,const std::string& kind,const std::string& owner)
        {
            const bool behaviour=behaviourOwner(owner);
            return (kind!="event:delta"||event=="update")&&(kind!="event:canvas"||(!behaviour&&event=="draw"))&&
                (kind!="event:entity"||behaviour)&&(kind!="event:other"||(behaviour&&(event=="onHit"||event=="onLeave")))&&
                (kind!="event:levelName"||(!behaviour&&(event=="onLevelTouch"||event=="onLevelAgent")))&&
                (kind!="event:levelWho"||(!behaviour&&event=="onLevelTouch"))&&
                (kind!="event:levelWhat"||(!behaviour&&event=="onLevelAgent"))&&
                (kind!="attach:behaviour"||!behaviour)&&(!behaviour||kind.rfind("level:",0)!=0);
        }
        bool signatureType(const std::string& type)
        {return blueprintLiteralType(type)||type=="Vec2"||type=="Vec3"||type=="Color"||type=="Entity@";}
        bool validSignature(const std::vector<BlueprintPin>& parameters,const std::string& result)
        {
            if(parameters.size()>8||(result!="void"&&!signatureType(result)))return false;
            std::unordered_set<std::string> names;
            for(const auto& p:parameters)if(!identifier(p.name)||!signatureType(p.type)||!names.insert(p.name).second)return false;
            return true;
        }
        bool validText(JsonTree::Value v){return v.isString()&&v.text()[0]&&std::strlen(v.text())<=512;}
        bool literal(const std::string& kind,const std::string& value)
        {
            if(kind=="literal:string")return value.size()<=4096&&value.find('\0')==std::string::npos;
            if(kind=="literal:bool")return value=="true"||value=="false";
            if(kind.rfind("literal:",0)!=0||value.empty()||value.size()>64)return false;
            if(value.find_first_not_of("0123456789.eE+-")!=std::string::npos)return false;
            char* end=nullptr;const double n=std::strtod(value.c_str(),&end);
            if(end!=value.c_str()+value.size()||!std::isfinite(n))return false;
            if(kind=="literal:int"||kind=="literal:uint")
            {
                if(value.find_first_of(".eE+")!=std::string::npos||std::floor(n)!=n)return false;
                return kind=="literal:int"?n>=-2147483648.0&&n<=2147483647.0:n>=0&&n<=4294967295.0;
            }
            return kind=="literal:double"||(kind=="literal:float"&&std::abs(n)<=3.402823466e38);
        }
        bool validWire(const BlueprintDocument& doc,const BlueprintDocument::Graph& g,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error)
        {
            if(from>=g.nodes.size()||to>=g.nodes.size()||from==to){error="Choose two different nodes.";return false;}
            BlueprintNodeSpec a,b;
            if(!doc.nodeSpec(g,g.nodes[from],a)||!doc.nodeSpec(g,g.nodes[to],b)){error="Unknown node or function.";return false;}
            const auto* out=findPin(a.outputs,output);const auto* in=findPin(b.inputs,input);
            if(!out||!in||out->type!=in->type){error="Pins must have matching types and opposite directions.";return false;}
            for(const auto& l:g.links)
            {
                if(l.to==to&&l.input==input){error="Input already connected. Disconnect it first.";return false;}
                if(out->type=="exec"&&l.from==from&&l.output==output){error="Execution output already connected.";return false;}
            }
            // Until loop nodes have explicit semantics, forbid implicit cycles. Use a linear adjacency scan.
            std::vector<std::vector<size_t>> next(g.nodes.size());
            for(const auto& l:g.links)next[l.from].push_back(l.to);
            std::vector<bool> seen(g.nodes.size(),false);std::vector<size_t> stack{to};seen[to]=true;
            while(!stack.empty())
            {
                const auto n=stack.back();stack.pop_back();
                if(n==from){error="This connection creates a cycle.";return false;}
                for(const auto child:next[n])if(!seen[child]){seen[child]=true;stack.push_back(child);}
            }
            return true;
        }
        bool text(JsonValue& j,const char* key,const std::string& value){auto* v=j.put(key);if(!v)return false;v->setText(value);return true;}
        bool number(JsonValue& j,const char* key,double value){auto* v=j.put(key);if(!v)return false;v->setNumber(value);return true;}
    }
    bool blueprintLiteralType(const std::string& type)
    {return type=="bool"||type=="int"||type=="uint"||type=="float"||type=="double"||type=="string";}
    bool blueprintFileName(const std::string& file)
    {
        return file.size()>8&&file.size()<=240&&file.ends_with(".bp.json")&&file!="blueprint-recovery.bp.json"&&
            file.front()!='.'&&file.find("..") == std::string::npos&&file.find_first_of("/\\:") == std::string::npos&&
            std::none_of(file.begin(),file.end(),[](unsigned char c){return c<32||c==127;});
    }
    bool blueprintAssetRoot(const std::string& root)
    {
        return root.size()>4&&root.size()<=32&&root.rfind("bpa-",0)==0&&
            root[4]>='0'&&root[4]<='9'&&root.back()>='0'&&root.back()<='9'&&
            root.find_first_not_of("0123456789-",4)==std::string::npos;
    }
    bool blueprintAssetName(const std::string& name,bool folder)
    {
        if(name.empty()||name.size()>64||name.front()=='.'||name.back()=='.'||name.find("..")!=std::string::npos||name.ends_with(".new"))return false;
        for(char c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||(!folder&&c=='.')))return false;
        const auto stem=name.substr(0,name.find('.'));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||
            (stem.size()==4&&(stem.rfind("com",0)==0||stem.rfind("lpt",0)==0)&&stem[3]>='0'&&stem[3]<='9'))return false;
        return true;
    }
    bool blueprintManagedName(const std::string& name,std::string& file)
    {
        if(name.empty()||name.size()>64||name.front()==' '||name.back()==' ')return false;
        for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c==' '))return false;
        file="blueprint-project-"+name+".bp.json";return true;
    }
    const std::vector<BlueprintNodeSpec>& blueprintCatalogue()
    {
        static const auto catalogue=[]{
            std::vector<BlueprintNodeSpec> result;
            result.reserve(sizeof(rows)/sizeof(rows[0]));
            for(const auto& row:rows)result.push_back({row.id,row.title,row.category,parsePins(row.inputs),parsePins(row.outputs),row.call,row.property,row.result,*row.member!=0,*row.pure!=0,*row.hasOut!=0,parsePins(row.arguments),parsePins(row.defaults)});
            auto operation=[&](const std::string& id,const std::string& title,const std::string& category,
                const std::string& input,const std::string& output,const std::string& op,bool unary=false)
            {
                BlueprintNodeSpec spec;
                spec.id=id;spec.title=title;spec.category=category;spec.pure=true;spec.result=output;spec.operation=op;
                spec.inputs.push_back({"a",input});
                if(!unary)spec.inputs.push_back({"b",input});
                spec.outputs.push_back({"result",output});
                result.push_back(std::move(spec));
            };
            struct Operator {const char* id;const char* title;const char* op;bool comparison;};
            constexpr Operator numeric[]={
                {"add","Add","+",false},{"subtract","Subtract","-",false},{"multiply","Multiply","*",false},
                {"divide","Divide","/",false},{"equal","Equal","==",true},{"not_equal","Not equal","!=",true},
                {"less","Less than","<",true},{"less_equal","Less or equal","<=",true},
                {"greater","Greater than",">",true},{"greater_equal","Greater or equal",">=",true}
            };
            for(const std::string type:{"int","uint","float","double"})
            {
                for(const auto& op:numeric)
                    operation(std::string(op.id)+":"+type,std::string(op.title)+" ("+type+")","Math",type,op.comparison?"bool":type,op.op);
                if(type=="int"||type=="uint")operation("mod:"+type,"Remainder ("+type+")","Math",type,type,"%");
                if(type!="uint")operation("negate:"+type,"Negate ("+type+")","Math",type,type,"-",true);
                for(const std::string target:{"int","uint","float","double"})
                    if(target!=type)operation("cast:"+type+":"+target,"Convert "+type+" to "+target,"Conversions",type,target,"cast",true);
            }
            operation("and:bool","And","Logic","bool","bool","&&");
            operation("or:bool","Or","Logic","bool","bool","||");
            operation("not:bool","Not","Logic","bool","bool","!",true);
            operation("equal:bool","Equal (bool)","Logic","bool","bool","==");
            operation("not_equal:bool","Not equal (bool)","Logic","bool","bool","!=");
            operation("add:string","Concatenate","String","string","string","+");
            operation("equal:string","Equal (string)","String","string","bool","==");
            operation("not_equal:string","Not equal (string)","String","string","bool","!=");
            return result;
        }();
        return catalogue;
    }
    const BlueprintNodeSpec* blueprintSpec(const std::string& kind)
    {
        static const auto index=[]{
            std::unordered_map<std::string,const BlueprintNodeSpec*> result;
            for(const auto& spec:blueprintCatalogue())result.emplace(spec.id,&spec);
            return result;
        }();
        const auto found=index.find(kind);
        return found==index.end()?nullptr:found->second;
    }
    void BlueprintDocument::create()
    {
        *this=BlueprintDocument{};
        name="Untitled";
        for(const char* event:{"onEnter","update","draw","onSuspend","onResume","onExit"})
        {
            Graph g;g.owner="Screen";g.name=event;g.kind="event";
            g.nodes.push_back({"entry","entry","",24,60});graphs.push_back(std::move(g));
        }
    }
    bool BlueprintDocument::load(const char* data,size_t size,std::string& error)
    {
        if(!data||!size||size>16*1024*1024||std::memchr(data,0,size)){error="Invalid or oversized native Blueprint file.";return false;}
        JsonTree tree;
        if(!tree.parse(data,size)){error="JSON line "+std::to_string(tree.errorLine())+": "+tree.error();return false;}
        auto root=tree.root();auto gs=root["graphs"];
        if(!root.isObject()||root["version"].exact()!=2||std::strcmp(root["kind"].text(),"NativeBlueprint")||
            !validText(root["name"])||!gs.isArray()||!gs.count()||gs.count()>kMaxGraphs)
        {error="Expected native Blueprint version 2. Browser prototypes are not supported.";return false;}
        BlueprintDocument candidate;candidate.name=root["name"].text();
        candidate.assetRoot=root["assetRoot"].text();
        if(!candidate.assetRoot.empty()&&!blueprintAssetRoot(candidate.assetRoot)){error="Invalid project asset directory.";return false;}
        std::unordered_set<std::string> graphNames;
        // Load every graph header first so forward function references resolve consistently.
        for(auto value=gs.first();value.exists();value=value.next())
        {
            auto ns=value["nodes"],ls=value["links"];
            const std::string owner=value["owner"].text("Screen");
            if((!screenId(owner)&&!behaviourOwner(owner))||!validText(value["name"])||!graphNames.emplace(scoped(owner,value["name"].text())).second||
                !ns.isArray()||!ls.isArray()||ns.count()>kMaxNodes||ls.count()>kMaxLinks)
            {error="Invalid or duplicate graph.";return false;}
            Graph g;g.name=value["name"].text();g.owner=owner;g.kind="event";
            const auto kind=value["kind"].text("event");
            g.kind=kind;
            if((g.kind=="event"&&!ownerEvent(owner,g.name))||
                (g.kind=="function"&&(!identifier(g.name)||ownerEvent(owner,g.name)))||(g.kind!="event"&&g.kind!="function"))
            {error="Invalid event or function graph.";return false;}
            auto parameters=value["parameters"];
            if(parameters.exists())
            {
                if(!parameters.isArray()||parameters.count()>8){error="Invalid function parameters.";return false;}
                for(auto p=parameters.first();p.exists();p=p.next())
                    g.parameters.push_back({p["name"].text(),p["type"].text()});
            }
            g.returnType=value["returnType"].text("void");
            if(!validSignature(g.parameters,g.returnType)||(g.kind!="function"&&(!g.parameters.empty()||g.returnType!="void")))
            {error="Invalid function signature in "+g.name;return false;}
            candidate.graphs.push_back(std::move(g));
        }
        size_t graphIndex=0;
        for(auto value=gs.first();value.exists();value=value.next(),++graphIndex)
        {
            auto& g=candidate.graphs[graphIndex];
            auto ns=value["nodes"],ls=value["links"];
            std::unordered_map<std::string,size_t> ids;unsigned entries=0;
            for(auto n=ns.first();n.exists();n=n.next())
            {
                const std::string kind=n["kind"].text(),ref=n["value"].text();
                const float x=n["x"].number(),y=n["y"].number();
                if(!validText(n["id"])||!blueprintSpec(kind)||!context(g.name,kind,g.owner)||!n["x"].isNumber()||!n["y"].isNumber()||
                    !coordinate(x)||!coordinate(y)||!ids.emplace(n["id"].text(),g.nodes.size()).second||
                    (kind.rfind("literal:",0)==0&&(!n["value"].isString()||!literal(kind,ref)))||
                    ((variableKind(kind)||kind=="call:function"||(kind=="attach:behaviour"||kind=="level:attach"))&&(!n["value"].isString()||!identifier(ref))))
                {error="Invalid node in "+g.name;return false;}
                if(kind=="entry")++entries;
                Node node{n["id"].text(),kind,ref,x,y};
                if(n["watch"].exists()&&n["watch"].type()!=JsonTree::Type::Bool){error="Invalid node watch flag.";return false;}
                node.watch=n["watch"].flag();
                BlueprintNodeSpec resolved;
                if(!candidate.nodeSpec(g,node,resolved)){error="Invalid function node in "+g.name;return false;}
                const auto values=n["values"];
                if(values.exists())
                {
                    if(!values.isObject()||values.count()>resolved.inputs.size())
                    {error="Invalid input values in "+g.name;return false;}
                    for(auto v=values.first();v.exists();v=v.next())
                    {
                        const std::string pin=v.key();
                        const auto* input=findPin(resolved.inputs,pin);
                        if(!input||!blueprintLiteralType(input->type)||!v.isString()||
                            !literal("literal:"+input->type,v.text())||
                            std::any_of(node.values.begin(),node.values.end(),[&](const InputValue& old){return old.pin==pin;}))
                        {error="Invalid or duplicate input value: "+pin;return false;}
                        node.values.push_back({pin,v.text()});
                    }
                }
                g.nodes.push_back(std::move(node));
            }
            if(entries!=1){error="Each graph needs exactly one entry.";return false;}
            for(auto l=ls.first();l.exists();l=l.next())
            {
                auto a=ids.find(l["from"].text()),b=ids.find(l["to"].text());
                if(a==ids.end()||b==ids.end()||!validWire(candidate,g,a->second,l["output"].text(),b->second,l["input"].text(),error))
                {if(a==ids.end()||b==ids.end())error="Dangling connection.";return false;}
                g.links.push_back({a->second,b->second,l["output"].text(),l["input"].text()});
            }
        }
        for(const auto& owner:candidate.screens())
        {
            for(const char* event:{"onEnter","update","draw"})
                if(!graphNames.count(scoped(owner,event))){error="Missing required event graph in "+owner;return false;}
            std::vector<BlueprintPin> vars;if(!candidate.variables(vars,error,owner))return false;
        }
        if(candidate.screens().empty()){error="A project requires at least one screen.";return false;}
        const auto behaviours=candidate.behaviours();
        for(const auto& behaviour:behaviours)
        {
            const auto owner="behaviour:"+behaviour;
            for(const char* event:{"start","update","onHit","onLeave","onGone"})
                if(!graphNames.count(scoped(owner,event))){error="Missing behaviour event "+owner+"/"+event;return false;}
            std::vector<BlueprintPin> vars;if(!candidate.variables(vars,error,owner))return false;
        }
        for(const auto& g:candidate.graphs)for(const auto& n:g.nodes)
            if((n.kind=="attach:behaviour"||n.kind=="level:attach")&&std::find(behaviours.begin(),behaviours.end(),n.ref)==behaviours.end())
            {error="Missing behaviour "+n.ref;return false;}
        if(!candidate.functions(error))return false;
        *this=std::move(candidate);error.clear();return true;
    }
    bool BlueprintDocument::serialize(std::string& result,std::string& error)const
    {
        if(graphs.empty()){error="No Blueprint open.";return false;}
        auto fail=[&](){error="Not enough memory to serialize Blueprint.";return false;};
        JsonValue root;
        if(!number(root,"version",2)||!text(root,"kind","NativeBlueprint")||!text(root,"name",name)||!text(root,"assetRoot",assetRoot))return fail();
        auto* gs=root.put("graphs");if(!gs)return fail();gs->setArray();
        for(const auto& g:graphs)
        {
            auto* v=gs->add();if(!v||!text(*v,"owner",g.owner)||!text(*v,"name",g.name)||!text(*v,"kind",g.kind))return fail();
            if(g.kind=="function")
            {
                if(!text(*v,"returnType",g.returnType))return fail();
                auto* parameters=v->put("parameters");if(!parameters)return fail();parameters->setArray();
                for(const auto& p:g.parameters)
                {
                    auto* item=parameters->add();if(!item||!text(*item,"name",p.name)||!text(*item,"type",p.type))return fail();
                }
            }
            auto* ns=v->put("nodes");if(!ns)return fail();ns->setArray();
            for(const auto& n:g.nodes)
            {
                auto* j=ns->add();if(!j||!text(*j,"id",n.id)||!text(*j,"kind",n.kind)||!number(*j,"x",n.x)||!number(*j,"y",n.y))return fail();
                if((n.kind.rfind("literal:",0)==0||variableKind(n.kind)||n.kind=="call:function"||(n.kind=="attach:behaviour"||n.kind=="level:attach"))&&!text(*j,"value",n.ref))return fail();
                if(n.watch){auto* watch=j->put("watch");if(!watch)return fail();watch->setFlag(true);}
                if(!n.values.empty())
                {
                    auto* values=j->put("values");if(!values)return fail();
                    for(const auto& v:n.values)if(!text(*values,v.pin.c_str(),v.value))return fail();
                }
            }
            auto* ls=v->put("links");if(!ls)return fail();ls->setArray();
            for(const auto& l:g.links)
            {
                if(l.from>=g.nodes.size()||l.to>=g.nodes.size()){error="Dangling connection.";return false;}
                auto* j=ls->add();
                if(!j||!text(*j,"from",g.nodes[l.from].id)||!text(*j,"to",g.nodes[l.to].id)||
                    !text(*j,"output",l.output)||!text(*j,"input",l.input))return fail();
            }
        }
        auto output=root.write(false);BlueprintDocument check;
        if(!check.load(output.data(),output.size(),error))return false;
        result.swap(output);return true;
    }
    void BlueprintDocument::record(size_t graph)
    {
        if(m_undo.size()>=kHistory)m_undo.erase(m_undo.begin());
        m_undo.push_back({graph,graphs[graph],m_revision,{}});m_redo.clear();m_revision=++m_nextRevision;
    }
    bool BlueprintDocument::move(size_t graph,size_t node,float x,float y)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()||!coordinate(x)||!coordinate(y))return false;
        auto& n=graphs[graph].nodes[node];if(n.x==x&&n.y==y)return false;
        record(graph);n.x=x;n.y=y;return true;
    }
    bool BlueprintDocument::addNode(size_t graph,const std::string& kind,float x,float y,std::string& error)
    {
        if(graph>=graphs.size()||!blueprintSpec(kind)||kind=="entry"||!coordinate(x)||!coordinate(y)||graphs[graph].nodes.size()>=kMaxNodes)
        {error="Cannot add this node.";return false;}
        auto& g=graphs[graph];
        if(kind=="return:function"&&g.kind!="function"){error="Return belongs in a function graph.";return false;}
        if(!context(g.name,kind,g.owner)){error="This event value belongs to a different event graph.";return false;}
        std::string id;
        do{id="n"+std::to_string(++m_nextId);}while(std::any_of(g.nodes.begin(),g.nodes.end(),[&](const Node& n){return n.id==id;}));
        std::string value=kind=="literal:string"?"":kind=="literal:bool"?"false":kind.rfind("literal:",0)==0?"0":"";
        if(variableKind(kind))
        {
            const auto type=kind.substr(8);
            value="value_"+type; if(value.back()=='@')value.pop_back();
            std::vector<BlueprintPin> vars;
            if(!variables(vars,error,g.owner))return false;
            for(unsigned i=1;std::any_of(vars.begin(),vars.end(),[&](const BlueprintPin& p){return p.name==value&&p.type!=type;});++i)value="variable_"+std::to_string(i);
        }
        if((kind=="attach:behaviour"||kind=="level:attach"))
        {
            const auto names=behaviours();if(names.empty()){error="Create a behaviour first.";return false;}value=names.front();
        }
        if(kind=="call:function")
        {
            // Choose the first valid target, not merely the first function in the list.
            bool valid=false;
            for(const auto& fn:graphs)
            {
                if(fn.owner!=g.owner||fn.kind!="function"||fn.name==g.name)continue;
                value=fn.name;g.nodes.push_back({id,kind,value,x,y});
                valid=functions(error);g.nodes.pop_back();if(valid)break;
            }
            if(!valid){error="No nonrecursive function target. Create another function first.";return false;}
        }
        record(graph);g.nodes.push_back({id,kind,value,x,y});error.clear();return true;
    }
    bool BlueprintDocument::deleteNode(size_t graph,size_t node,std::string& error)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()||graphs[graph].nodes[node].kind=="entry")
        {error="The event entry cannot be deleted.";return false;}
        auto& g=graphs[graph];record(graph);
        g.links.erase(std::remove_if(g.links.begin(),g.links.end(),[&](const Link& l){return l.from==node||l.to==node;}),g.links.end());
        for(auto& l:g.links){if(l.from>node)--l.from;if(l.to>node)--l.to;}
        g.nodes.erase(g.nodes.begin()+static_cast<std::ptrdiff_t>(node));error.clear();return true;
    }
    bool BlueprintDocument::connect(size_t graph,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error)
    {
        if(graph>=graphs.size()||graphs[graph].links.size()>=kMaxLinks){error="Graph link limit reached.";return false;}
        auto& g=graphs[graph];if(!validWire(*this,g,from,output,to,input,error))return false;
        record(graph);g.links.push_back({from,to,output,input});error.clear();return true;
    }
    bool BlueprintDocument::disconnect(size_t graph,size_t node,const std::string& pin,bool output)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size())return false;
        auto& g=graphs[graph];
        const auto match=[&](const Link& l){return output?l.from==node&&l.output==pin:l.to==node&&l.input==pin;};
        if(std::none_of(g.links.begin(),g.links.end(),match))return false;
        record(graph);g.links.erase(std::remove_if(g.links.begin(),g.links.end(),match),g.links.end());return true;
    }
    bool BlueprintDocument::toggleWatch(size_t graph,size_t node)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size())return false;
        BlueprintNodeSpec spec;const auto& g=graphs[graph];
        if(!nodeSpec(g,g.nodes[node],spec))return false;
        const auto exec=[](const BlueprintPin& p){return p.type=="exec";};
        if(!std::any_of(spec.inputs.begin(),spec.inputs.end(),exec)&&!std::any_of(spec.outputs.begin(),spec.outputs.end(),exec))return false;
        record(graph);graphs[graph].nodes[node].watch=!graphs[graph].nodes[node].watch;return true;
    }
    bool BlueprintDocument::setLiteral(size_t graph,size_t node,const std::string& value,std::string& error)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()||!literal(graphs[graph].nodes[node].kind,value))
        {error="Invalid value for this literal type.";return false;}
        auto& n=graphs[graph].nodes[node];if(n.ref==value){error.clear();return true;}
        record(graph);n.ref=value;error.clear();return true;
    }
    bool BlueprintDocument::setInputValue(size_t graph,size_t node,const std::string& pin,const std::string& value,std::string& error)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()){error="Invalid node.";return false;}
        auto& n=graphs[graph].nodes[node];
        BlueprintNodeSpec spec;
        const auto* input=nodeSpec(graphs[graph],n,spec)?findPin(spec.inputs,pin):nullptr;
        if(!input||!blueprintLiteralType(input->type)||!literal("literal:"+input->type,value))
        {error="Invalid value for input "+pin+".";return false;}
        const auto it=std::find_if(n.values.begin(),n.values.end(),[&](const InputValue& v){return v.pin==pin;});
        if(it!=n.values.end()&&it->value==value){error.clear();return true;}
        record(graph);
        if(it==n.values.end())n.values.push_back({pin,value});
        else it->value=value;
        error.clear();return true;
    }
    bool BlueprintDocument::clearInputValue(size_t graph,size_t node,const std::string& pin)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size())return false;
        auto& values=graphs[graph].nodes[node].values;
        const auto it=std::find_if(values.begin(),values.end(),[&](const InputValue& v){return v.pin==pin;});
        if(it==values.end())return false;
        record(graph);values.erase(it);return true;
    }
    bool BlueprintDocument::variables(std::vector<BlueprintPin>& values,std::string& error,const std::string& owner)const
    {
        std::vector<BlueprintPin> result;
        for(const auto& graph:graphs)if(graph.owner==owner)for(const auto& node:graph.nodes)
        {
            if(!variableKind(node.kind))continue;
            if(!identifier(node.ref)){error="Invalid variable name in "+graph.name;return false;}
            const auto type=node.kind.substr(8);
            const auto found=std::find_if(result.begin(),result.end(),[&](const BlueprintPin& p){return p.name==node.ref;});
            if(found!=result.end())
            {
                if(found->type!=type){error="Variable "+node.ref+" has conflicting types.";return false;}
            }
            else result.push_back({node.ref,type});
        }
        std::sort(result.begin(),result.end(),[](const BlueprintPin& a,const BlueprintPin& b){return a.name<b.name;});
        values.swap(result);error.clear();return true;
    }
    bool BlueprintDocument::setReference(size_t graph,size_t node,const std::string& value,std::string& error)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()||
            (!variableKind(graphs[graph].nodes[node].kind)&&graphs[graph].nodes[node].kind!="call:function"&&graphs[graph].nodes[node].kind!="attach:behaviour"&&graphs[graph].nodes[node].kind!="level:attach")||!identifier(value))
        {error="Use a variable name containing letters, digits and underscores.";return false;}
        auto& n=graphs[graph].nodes[node];
        if(n.ref==value){error.clear();return true;}
        if((n.kind=="attach:behaviour"||n.kind=="level:attach"))
        {
            const auto names=behaviours();if(std::find(names.begin(),names.end(),value)==names.end()){error="Unknown behaviour "+value;return false;}
            record(graph);n.ref=value;error.clear();return true;
        }
        if(n.kind=="call:function")
        {
            const std::string previous=n.ref;n.ref=value;
            std::string checked;
            const bool valid=functions(error)&&serialize(checked,error);n.ref=previous;if(!valid)return false;
            record(graph);n.ref=value;error.clear();return true;
        }
        const auto type=n.kind.substr(8);
        for(const auto& g:graphs)if(g.owner==graphs[graph].owner)for(const auto& other:g.nodes)
            if(&other!=&n&&variableKind(other.kind)&&other.ref==value&&other.kind.substr(8)!=type)
            {error="Variable name already has a different type.";return false;}
        record(graph);n.ref=value;error.clear();return true;
    }
    bool BlueprintDocument::functions(std::string& error) const
    {
        std::unordered_map<std::string,size_t> index;
        for(size_t i=0;i<graphs.size();++i)
            if(graphs[i].kind=="function")index.emplace(scoped(graphs[i].owner,graphs[i].name),i);
        std::vector<std::vector<size_t>> calls(graphs.size());
        for(size_t i=0;i<graphs.size();++i)for(const auto& node:graphs[i].nodes)
        {
            if(node.kind!="call:function")continue;
            const auto it=index.find(scoped(graphs[i].owner,node.ref));
            if(it==index.end()){error="Missing function: "+node.ref;return false;}
            calls[i].push_back(it->second);
        }
        // Topological elimination avoids recursion and catches indirect cycles.
        std::vector<size_t> indegree(graphs.size(),0),ready;
        for(const auto& edges:calls)for(size_t to:edges)++indegree[to];
        for(size_t i=0;i<graphs.size();++i)if(!indegree[i])ready.push_back(i);
        size_t visited=0;
        while(!ready.empty())
        {
            const size_t i=ready.back();ready.pop_back();++visited;
            for(size_t to:calls[i])if(--indegree[to]==0)ready.push_back(to);
        }
        if(visited!=graphs.size()){error="Recursive function calls are not supported.";return false;}
        error.clear();return true;
    }

    void BlueprintDocument::recordGraphList()
    {
        if(m_undo.size()>=kHistory)m_undo.erase(m_undo.begin());
        m_undo.push_back({static_cast<size_t>(-1),{},m_revision,graphs});
        m_redo.clear();m_revision=++m_nextRevision;
    }
    const BlueprintDocument::Graph* BlueprintDocument::function(const std::string& owner,const std::string& name) const
    {
        const auto found=std::find_if(graphs.begin(),graphs.end(),[&](const Graph& g)
        {return g.owner==owner&&g.name==name&&g.kind=="function";});
        return found==graphs.end()?nullptr:&*found;
    }
    bool BlueprintDocument::nodeSpec(const Graph& graph,const Node& node,BlueprintNodeSpec& spec) const
    {
        const auto* base=blueprintSpec(node.kind);if(!base)return false;
        spec=*base;
        if(node.kind=="entry"&&graph.kind=="function")
            for(const auto& p:graph.parameters)spec.outputs.push_back({"arg_"+p.name,p.type});
        if(node.kind=="return:function")
        {
            if(graph.kind!="function")return false;
            if(graph.returnType!="void")spec.inputs.push_back({"value",graph.returnType});
        }
        if(node.kind=="call:function")
        {
            const auto* target=function(graph.owner,node.ref);if(!target)return false;
            for(const auto& p:target->parameters)spec.inputs.push_back({"arg_"+p.name,p.type});
            spec.result=target->returnType;
            if(spec.result!="void")spec.outputs.push_back({"result",spec.result});
        }
        return true;
    }
    bool BlueprintDocument::setFunctionSignature(size_t graph,const std::vector<BlueprintPin>& parameters,const std::string& returnType,std::string& error)
    {
        if(graph>=graphs.size()||graphs[graph].kind!="function"||!validSignature(parameters,returnType))
        {error="Invalid signature: up to eight uniquely named typed parameters.";return false;}
        auto& g=graphs[graph];
        const auto old=g.parameters;const auto oldReturn=g.returnType;
        if(oldReturn==returnType&&old.size()==parameters.size()&&
            std::equal(old.begin(),old.end(),parameters.begin(),[](const BlueprintPin& a,const BlueprintPin& b){return a.name==b.name&&a.type==b.type;}))
        {error.clear();return true;}
        // A signature is a contract. Reject incompatible connected pins or stored input values.
        g.parameters=parameters;g.returnType=returnType;
        std::string checked;
        const bool valid=serialize(checked,error);
        g.parameters=old;g.returnType=oldReturn;
        if(!valid){error="Signature would invalidate existing pins: "+error;return false;}
        record(graph);g.parameters=parameters;g.returnType=returnType;error.clear();return true;
    }
    std::string BlueprintDocument::functionSignature(size_t graph)const
    {
        if(graph>=graphs.size()||graphs[graph].kind!="function")return "";
        const auto& g=graphs[graph];std::string s=g.returnType+"(";
        for(size_t i=0;i<g.parameters.size();++i)
        {if(i)s+=", ";s+=g.parameters[i].name+":"+g.parameters[i].type;}
        return s+")";
    }
    bool BlueprintDocument::editFunctionSignature(size_t graph,const std::string& signature,std::string& error)
    {
        const auto trim=[](const std::string& s)
        {
            const auto first=s.find_first_not_of(" \t"),last=s.find_last_not_of(" \t");
            return first==std::string::npos?std::string():s.substr(first,last-first+1);
        };
        const auto s=trim(signature);const auto open=s.find('(');
        auto fail=[&](){error="Use returnType(name:type, name:type), e.g. float(amount:float).";return false;};
        if(open==std::string::npos||s.empty()||s.back()!=')')return fail();
        const auto result=trim(s.substr(0,open));const auto body=trim(s.substr(open+1,s.size()-open-2));
        std::vector<BlueprintPin> parameters;
        if(!body.empty())
        {
            for(size_t at=0;;)
            {
                const auto comma=body.find(',',at);const auto item=trim(body.substr(at,comma==std::string::npos?comma:comma-at));
                const auto colon=item.find(':');if(colon==std::string::npos)return fail();
                parameters.push_back({trim(item.substr(0,colon)),trim(item.substr(colon+1))});
                if(comma==std::string::npos)break;
                at=comma+1;
            }
        }
        return setFunctionSignature(graph,parameters,result,error);
    }
    bool BlueprintDocument::addFunction(const std::string& functionName,std::string& error,const std::string& owner)
    {
        if(!identifier(functionName)||graphs.size()>=kMaxGraphs||
            std::none_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner;})||
            std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner&&g.name==functionName;})||
            ownerEvent(owner,functionName))
        {error="Choose a unique function name (letters, digits and underscores).";return false;}
        recordGraphList();
        Graph g;g.owner=owner;g.name=functionName;g.kind="function";
        g.nodes.push_back({"entry","entry","",24,60});graphs.push_back(std::move(g));
        error.clear();return true;
    }
    bool BlueprintDocument::removeFunction(size_t graph,std::string& error)
    {
        if(graph>=graphs.size()||graphs[graph].kind!="function")
        {error="Only function graphs can be deleted.";return false;}
        for(const auto& g:graphs)if(g.owner==graphs[graph].owner)for(const auto& n:g.nodes)
            if(n.kind=="call:function"&&n.ref==graphs[graph].name)
            {error="Remove calls to this function before deleting it.";return false;}
        recordGraphList();graphs.erase(graphs.begin()+static_cast<std::ptrdiff_t>(graph));error.clear();return true;
    }
    std::vector<std::string> BlueprintDocument::screens() const
    {
        std::vector<std::string> result;
        for(const auto& g:graphs)if(!behaviourOwner(g.owner)&&std::find(result.begin(),result.end(),g.owner)==result.end())result.push_back(g.owner);
        return result;
    }
    std::vector<std::string> BlueprintDocument::behaviours() const
    {
        std::vector<std::string> result;
        for(const auto& g:graphs)if(behaviourOwner(g.owner))
        {
            const auto name=g.owner.substr(10);
            if(std::find(result.begin(),result.end(),name)==result.end())result.push_back(name);
        }
        return result;
    }
    bool BlueprintDocument::addBehaviour(const std::string& name,std::string& error)
    {
        const auto owner="behaviour:"+name;
        if(!identifier(name)||graphs.size()+5>kMaxGraphs||
            std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner;}))
        {error="Choose a unique behaviour identifier.";return false;}
        recordGraphList();
        for(const char* event:{"start","update","onHit","onLeave","onGone"})
        {
            Graph g;g.owner=owner;g.name=event;g.kind="event";
            g.nodes.push_back({"entry","entry","",24,60});graphs.push_back(std::move(g));
        }
        error.clear();return true;
    }
    bool BlueprintDocument::removeBehaviour(const std::string& name,std::string& error)
    {
        const auto owner="behaviour:"+name;
        if(std::none_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner;}))
        {error="Unknown behaviour.";return false;}
        for(const auto& g:graphs)for(const auto& n:g.nodes)
            if((n.kind=="attach:behaviour"||n.kind=="level:attach")&&n.ref==name){error="Remove attachment nodes referencing "+name+" first.";return false;}
        recordGraphList();
        graphs.erase(std::remove_if(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner;}),graphs.end());
        error.clear();return true;
    }
    bool BlueprintDocument::addLevelEvents(const std::string& owner,std::string& error)
    {
        const auto owners=screens();
        if(std::find(owners.begin(),owners.end(),owner)==owners.end())
        {error="Select a screen graph.";return false;}
        std::vector<std::string> missing;
        for(const char* name:{"onLevelTouch","onLevelAgent"})
            if(std::none_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner&&g.name==name;}))missing.emplace_back(name);
        if(missing.empty()){error="Level events already exist for this screen.";return false;}
        if(graphs.size()+missing.size()>kMaxGraphs){error="Graph limit reached.";return false;}
        recordGraphList();
        for(const auto& name:missing)
        {
            Graph g;g.owner=owner;g.name=name;g.kind="event";
            g.nodes.push_back({"entry","entry","",24,60});graphs.push_back(std::move(g));
        }
        error.clear();return true;
    }
    bool BlueprintDocument::addScreen(const std::string& id,std::string& error)
    {
        if(!screenId(id)||graphs.size()+6>kMaxGraphs||
            std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==id;}))
        {error="Choose a unique lowercase screen ID (letters, digits, hyphens or underscores; not main).";return false;}
        recordGraphList();
        for(const char* event:{"onEnter","update","draw","onSuspend","onResume","onExit"})
        {
            Graph g;g.owner=id;g.name=event;g.kind="event";
            g.nodes.push_back({"entry","entry","",24,60});graphs.push_back(std::move(g));
        }
        error.clear();return true;
    }
    bool BlueprintDocument::removeScreen(const std::string& id,std::string& error)
    {
        const auto owners=screens();
        if(owners.size()<2||id==owners.front()||std::find(owners.begin(),owners.end(),id)==owners.end())
        {error="The initial screen cannot be deleted.";return false;}
        // Reject references in both stored input values and wired constant nodes.
        for(const auto& g:graphs)if(g.owner!=id)for(size_t i=0;i<g.nodes.size();++i)
        {
            const auto& n=g.nodes[i];
            if(n.kind!="api:Screens:Screens::push:string"&&n.kind!="api:Screens:Screens::replace:string")continue;
            for(const auto& v:n.values)if(v.pin=="id"&&v.value==id)
            {error="Remove navigation references to "+id+" first.";return false;}
            for(const auto& l:g.links)if(l.to==i&&l.input=="id"&&g.nodes[l.from].kind=="literal:string"&&g.nodes[l.from].ref==id)
            {error="Remove navigation references to "+id+" first.";return false;}
        }
        recordGraphList();
        graphs.erase(std::remove_if(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==id;}),graphs.end());
        error.clear();return true;
    }
    bool BlueprintDocument::undo()
    {
        if(m_undo.empty())return false;
        auto e=std::move(m_undo.back());m_undo.pop_back();
        if(e.graph==static_cast<size_t>(-1))
        {
            m_redo.push_back({e.graph,{},m_revision,std::move(graphs)});graphs=std::move(e.all);
        }
        else
        {
            m_redo.push_back({e.graph,std::move(graphs[e.graph]),m_revision,{}});graphs[e.graph]=std::move(e.state);
        }
        m_revision=e.revision;return true;
    }
    bool BlueprintDocument::redo()
    {
        if(m_redo.empty())return false;
        auto e=std::move(m_redo.back());m_redo.pop_back();
        if(e.graph==static_cast<size_t>(-1))
        {
            m_undo.push_back({e.graph,{},m_revision,std::move(graphs)});graphs=std::move(e.all);
        }
        else
        {
            m_undo.push_back({e.graph,std::move(graphs[e.graph]),m_revision,{}});graphs[e.graph]=std::move(e.state);
        }
        m_revision=e.revision;return true;
    }
}
