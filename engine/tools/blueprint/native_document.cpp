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
            {"attach:direct","Attach behaviour (direct)","Behaviours","in:exec|target:Entity@","then:exec"},
            {"global:get:bool","Get bool global","Module variables","","value:bool"},
            {"global:set:bool","Set bool global","Module variables","in:exec|value:bool","then:exec"},
            {"global:get:int","Get int global","Module variables","","value:int"},
            {"global:set:int","Set int global","Module variables","in:exec|value:int","then:exec"},
            {"global:get:uint","Get uint global","Module variables","","value:uint"},
            {"global:set:uint","Set uint global","Module variables","in:exec|value:uint","then:exec"},
            {"global:get:float","Get float global","Module variables","","value:float"},
            {"global:set:float","Set float global","Module variables","in:exec|value:float","then:exec"},
            {"global:get:double","Get double global","Module variables","","value:double"},
            {"global:set:double","Set double global","Module variables","in:exec|value:double","then:exec"},
            {"global:get:string","Get string global","Module variables","","value:string"},
            {"global:set:string","Set string global","Module variables","in:exec|value:string","then:exec"},
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
        bool globalKind(const std::string& kind){return kind.rfind("global:get:",0)==0||kind.rfind("global:set:",0)==0;}
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
            if(s.empty()||s.size()>40||s=="screen"||s.front()<'a'||s.front()>'z')return false;
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
    bool blueprintLosslessConversion(const std::string& from,const std::string& to)
    {return to=="double"&&(from=="int"||from=="uint"||from=="float");}
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
            for(const std::string type:{"bool","int","uint","float","double","string","Vec2","Vec3","Color","Entity@"})
            {
                BlueprintNodeSpec spec;
                spec.id="capture:"+type;spec.title="Capture "+type;spec.category="Variables";
                spec.inputs={{"in","exec"},{"value",type}};
                spec.outputs={{"then","exec"},{"result",type},{"local","local:"+type}};
                spec.result=type;result.push_back(std::move(spec));
                BlueprintNodeSpec assign;
                assign.id="local:set:"+type;assign.title="Assign local "+type;assign.category="Variables";
                assign.inputs={{"in","exec"},{"local","local:"+type},{"value",type}};
                assign.outputs={{"then","exec"}};result.push_back(std::move(assign));
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
    const std::string* BlueprintDocument::screenManifest(const std::string& owner) const
    {
        for(const auto& m:manifests)if(m.owner==owner)return &m.source;
        return nullptr;
    }
    bool BlueprintDocument::importScreenManifest(const std::string& owner,const std::string& source,std::string& error)
    {
        const auto owners=screens();
        if(std::find(owners.begin(),owners.end(),owner)==owners.end()||screenManifest(owner))
        {error="Unknown or duplicate screen manifest: "+owner;return false;}
        if(source.empty()||source.size()>64*1024||source.find('\0')!=std::string::npos)
        {error="Invalid or oversized screen.json: "+owner;return false;}
        JsonTree tree;
        if(!tree.parse(source.data(),source.size())||!tree.root().isObject()||!validText(tree.root()["title"]))
        {error="screen.json requires a valid object and nonempty title: "+owner;return false;}
        manifests.push_back({owner,source});error.clear();return true;
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
            const auto macro=value["inlineMacro"];
            if(macro.exists()&&(macro.type()!=JsonTree::Type::Bool||g.kind!="function"||g.returnType!="void")){error="Invalid inline macro.";return false;}
            g.inlineMacro=macro.flag();
            if(!validSignature(g.parameters,g.returnType)||(g.kind!="function"&&(!g.parameters.empty()||g.returnType!="void")))
            {error="Invalid function signature in "+g.name;return false;}
            const auto declarations=value["fields"];
            if(declarations.exists())
            {
                if(g.name!=(behaviourOwner(owner)?"start":"onEnter")||g.kind!="event"||!declarations.isArray()||declarations.count()>256)
                {error="Invalid field declarations.";return false;}
                std::unordered_set<std::string> names;
                for(auto f=declarations.first();f.exists();f=f.next())
                {
                    Field field{f["name"].text(),f["type"].text(),f["value"].text(),f["value"].exists()};
                    if(!f.isObject()||!identifier(field.name)||!blueprintLiteralType(field.type)||!names.insert(field.name).second||
                        (behaviourOwner(owner)&&field.name=="entity")||
                        (field.initialized&&(!f["value"].isString()||!literal("literal:"+field.type,field.value))))
                    {error="Invalid or duplicate field declaration.";return false;}
                    g.fields.push_back(std::move(field));
                }
            }
            const auto scope=value["moduleOwner"];
            if(scope.exists())
            {
                if(!behaviourOwner(owner)||g.name!="start"||g.kind!="event"||!scope.isString()||!screenId(scope.text()))
                {error="Invalid Behaviour module scope.";return false;}
                g.moduleOwner=scope.text();
            }
            const auto globals=value["globals"];
            if(globals.exists())
            {
                if(behaviourOwner(owner)||g.name!="onEnter"||g.kind!="event"||!globals.isArray()||globals.count()>256)
                {error="Invalid module variable declarations.";return false;}
                std::unordered_set<std::string> names;
                for(auto f=globals.first();f.exists();f=f.next())
                {
                    Field field{f["name"].text(),f["type"].text(),f["value"].text(),f["value"].exists()};
                    if(!f.isObject()||!identifier(field.name)||!blueprintLiteralType(field.type)||!names.insert(field.name).second||
                        (field.initialized&&(!f["value"].isString()||!literal("literal:"+field.type,field.value))))
                    {error="Invalid or duplicate module variable.";return false;}
                    g.globals.push_back(std::move(field));
                }
            }
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
                    ((variableKind(kind)||globalKind(kind)||kind.rfind("capture:",0)==0||kind=="call:function"||(kind=="attach:behaviour"||kind=="attach:direct"||kind=="level:attach"))&&(!n["value"].isString()||!identifier(ref))))
                {error="Invalid node in "+g.name;return false;}
                if(g.inlineMacro&&kind=="return:function"){error="Inline macros cannot return from the caller.";return false;}
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
                Link wire{a->second,b->second,l["output"].text(),l["input"].text()};
                const auto route=l["route"];
                if(route.exists())
                {
                    if(!route.isArray()||route.count()>16){error="Invalid wire route.";return false;}
                    for(auto p=route.first();p.exists();p=p.next())
                    {
                        const float x=p["x"].number(),y=p["y"].number();
                        if(!p.isObject()||!p["x"].isNumber()||!p["y"].isNumber()||!coordinate(x)||!coordinate(y)){error="Invalid route point.";return false;}
                        wire.route.push_back({x,y});
                    }
                }
                g.links.push_back(std::move(wire));
            }
            const auto comments=value["comments"];
            if(comments.exists())
            {
                if(!comments.isArray()||comments.count()>128){error="Invalid comment groups.";return false;}
                std::unordered_set<std::string> claimed;
                for(auto item=comments.first();item.exists();item=item.next())
                {
                    const std::string title=item["title"].text();const auto members=item["members"];
                    if(!item.isObject()||!item["title"].isString()||title.empty()||title.size()>256||!members.isArray()||!members.count()||members.count()>kMaxNodes)
                    {error="Invalid comment group.";return false;}
                    Comment comment{title,{}};
                    for(auto member=members.first();member.exists();member=member.next())
                    {
                        const std::string id=member.text();
                        if(!member.isString()||!ids.count(id)||!claimed.insert(id).second){error="Unknown or overlapping comment member.";return false;}
                        comment.members.push_back(id);
                    }
                    g.comments.push_back(std::move(comment));
                }
            }
        }
        for(const auto& owner:candidate.screens())
        {
            for(const char* event:{"onEnter","update","draw"})
                if(!graphNames.count(scoped(owner,event))){error="Missing required event graph in "+owner;return false;}
            std::vector<BlueprintPin> vars;if(!candidate.variables(vars,error,owner))return false;
        }
        if(candidate.screens().empty()){error="A project requires at least one screen.";return false;}
        const auto metadata=root["manifests"];
        if(metadata.exists())
        {
            if(!metadata.isArray()||metadata.count()>candidate.screens().size())
            {error="Invalid screen manifest list.";return false;}
            for(auto m=metadata.first();m.exists();m=m.next())
            {
                if(!m.isObject()||!m["owner"].isString()||!m["source"].isString())
                {error="Invalid screen manifest entry.";return false;}
                if(!candidate.importScreenManifest(m["owner"].text(),m["source"].text(),error))return false;
            }
        }
        const auto behaviours=candidate.behaviours();
        for(const auto& behaviour:behaviours)
        {
            const auto owner="behaviour:"+behaviour;
            for(const char* event:{"start","update","onHit","onLeave","onGone"})
                if(!graphNames.count(scoped(owner,event))){error="Missing behaviour event "+owner+"/"+event;return false;}
            std::vector<BlueprintPin> vars;if(!candidate.variables(vars,error,owner))return false;
        }
        const auto screens=candidate.screens();
        // Migrate older single-screen documents whose Behaviours used implicit module scope.
        if(screens.size()==1&&!candidate.moduleGlobals(screens.front()).empty())
        {
            bool changed=true;
            while(changed)
            {
                changed=false;
                for(auto& g:candidate.graphs)if(behaviourOwner(g.owner)&&g.name=="start"&&g.moduleOwner.empty())
                {
                    bool needsScope=false;
                    for(const auto& method:candidate.graphs)if(method.owner==g.owner)for(const auto& n:method.nodes)
                        if(globalKind(n.kind)||((n.kind=="attach:behaviour"||n.kind=="attach:direct"||n.kind=="level:attach")&&!candidate.moduleForOwner("behaviour:"+n.ref).empty()))needsScope=true;
                    if(needsScope){g.moduleOwner=screens.front();changed=true;}
                }
            }
        }
        for(const auto& g:candidate.graphs)
        {
            if(!g.moduleOwner.empty()&&std::find(screens.begin(),screens.end(),g.moduleOwner)==screens.end())
            {error="Missing Behaviour module: "+g.moduleOwner;return false;}
            const auto& globals=candidate.moduleGlobals(g.owner);
            for(const auto& n:g.nodes)
            {
                if((n.kind=="attach:behaviour"||n.kind=="attach:direct"||n.kind=="level:attach")&&!candidate.canAttach(g.owner,n.ref))
                {error="Missing or out-of-scope Behaviour: "+n.ref;return false;}
                if(globalKind(n.kind)&&std::none_of(globals.begin(),globals.end(),[&](const Field& f){return f.name==n.ref&&f.type==n.kind.substr(11);}))
                {error="Unknown or incompatible module variable: "+n.ref;return false;}
            }
        }
        if(!candidate.functions(error))return false;
        *this=std::move(candidate);error.clear();return true;
    }
    bool BlueprintDocument::serialize(std::string& result,std::string& error)const
    {
        if(graphs.empty()){error="No Blueprint open.";return false;}
        auto fail=[&](){error="Not enough memory to serialize Blueprint.";return false;};
        JsonValue root;
        if(!number(root,"version",2)||!text(root,"kind","NativeBlueprint")||!text(root,"name",name)||!text(root,"assetRoot",assetRoot))return fail();
        if(!manifests.empty())
        {
            auto* metadata=root.put("manifests");if(!metadata)return fail();metadata->setArray();
            for(const auto& m:manifests)
            {
                if(m.source.find('\0')!=std::string::npos){error="Invalid screen manifest bytes.";return false;}
                auto* entry=metadata->add();if(!entry||!text(*entry,"owner",m.owner)||!text(*entry,"source",m.source))return fail();
            }
        }
        auto* gs=root.put("graphs");if(!gs)return fail();gs->setArray();
        for(const auto& g:graphs)
        {
            auto* v=gs->add();if(!v||!text(*v,"owner",g.owner)||!text(*v,"name",g.name)||!text(*v,"kind",g.kind))return fail();
            if(g.inlineMacro){auto* macro=v->put("inlineMacro");if(!macro)return fail();macro->setFlag(true);}
            if(g.kind=="function")
            {
                if(!text(*v,"returnType",g.returnType))return fail();
                auto* parameters=v->put("parameters");if(!parameters)return fail();parameters->setArray();
                for(const auto& p:g.parameters)
                {
                    auto* item=parameters->add();if(!item||!text(*item,"name",p.name)||!text(*item,"type",p.type))return fail();
                }
            }
            if(!g.fields.empty())
            {
                auto* fields=v->put("fields");if(!fields)return fail();fields->setArray();
                for(const auto& f:g.fields)
                {
                    auto* item=fields->add();if(!item||!text(*item,"name",f.name)||!text(*item,"type",f.type))return fail();
                    if(f.initialized&&!text(*item,"value",f.value))return fail();
                }
            }
            if(!g.moduleOwner.empty()&&!text(*v,"moduleOwner",g.moduleOwner))return fail();
            if(!g.globals.empty())
            {
                auto* fields=v->put("globals");if(!fields)return fail();fields->setArray();
                for(const auto& f:g.globals)
                {
                    auto* item=fields->add();if(!item||!text(*item,"name",f.name)||!text(*item,"type",f.type))return fail();
                    if(f.initialized&&!text(*item,"value",f.value))return fail();
                }
            }
            auto* ns=v->put("nodes");if(!ns)return fail();ns->setArray();
            for(const auto& n:g.nodes)
            {
                auto* j=ns->add();if(!j||!text(*j,"id",n.id)||!text(*j,"kind",n.kind)||!number(*j,"x",n.x)||!number(*j,"y",n.y))return fail();
                if((n.kind.rfind("literal:",0)==0||variableKind(n.kind)||globalKind(n.kind)||n.kind.rfind("capture:",0)==0||n.kind=="call:function"||(n.kind=="attach:behaviour"||n.kind=="attach:direct"||n.kind=="level:attach"))&&!text(*j,"value",n.ref))return fail();
                if(n.watch){auto* watch=j->put("watch");if(!watch)return fail();watch->setFlag(true);}
                if(!n.values.empty())
                {
                    auto* values=j->put("values");if(!values)return fail();
                    for(const auto& input:n.values)if(!text(*values,input.pin.c_str(),input.value))return fail();
                }
            }
            if(!g.comments.empty())
            {
                auto* comments=v->put("comments");if(!comments)return fail();comments->setArray();
                for(const auto& comment:g.comments)
                {
                    auto* item=comments->add();if(!item||!text(*item,"title",comment.title))return fail();
                    auto* members=item->put("members");if(!members)return fail();members->setArray();
                    for(const auto& id:comment.members){auto* member=members->add();if(!member)return fail();member->setText(id);}
                }
            }
            auto* ls=v->put("links");if(!ls)return fail();ls->setArray();
            for(const auto& l:g.links)
            {
                if(l.from>=g.nodes.size()||l.to>=g.nodes.size()){error="Dangling connection.";return false;}
                auto* j=ls->add();
                if(!j||!text(*j,"from",g.nodes[l.from].id)||!text(*j,"to",g.nodes[l.to].id)||
                    !text(*j,"output",l.output)||!text(*j,"input",l.input))return fail();
                if(!l.route.empty())
                {
                    auto* route=j->put("route");if(!route)return fail();route->setArray();
                    for(const auto& p:l.route){auto* item=route->add();if(!item||!number(*item,"x",p.x)||!number(*item,"y",p.y))return fail();}
                }
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
    bool BlueprintDocument::moveNodes(size_t graph,const std::vector<size_t>& nodes,float dx,float dy)
    {
        if(graph>=graphs.size()||nodes.empty()||(!dx&&!dy)||!std::isfinite(dx)||!std::isfinite(dy))return false;
        auto& g=graphs[graph];std::unordered_set<size_t> selected(nodes.begin(),nodes.end());
        for(const auto i:selected)if(i>=g.nodes.size()||!coordinate(g.nodes[i].x+dx)||!coordinate(g.nodes[i].y+dy))return false;
        for(const auto& l:g.links)if(selected.count(l.from)&&selected.count(l.to))
            for(const auto& p:l.route)if(!coordinate(p.x+dx)||!coordinate(p.y+dy))return false;
        record(graph);
        for(const auto i:selected){g.nodes[i].x+=dx;g.nodes[i].y+=dy;}
        for(auto& l:g.links)if(selected.count(l.from)&&selected.count(l.to))for(auto& p:l.route){p.x+=dx;p.y+=dy;}
        return true;
    }
    bool BlueprintDocument::arrange(size_t graph,std::string& error)
    {
        if(graph>=graphs.size()){error="Unknown graph.";return false;}
        auto& g=graphs[graph];const size_t count=g.nodes.size();
        std::vector<size_t> indegree(count,0),rank(count,0),order;
        std::vector<std::vector<size_t>> outgoing(count);
        for(const auto& l:g.links)
        {
            if(l.from>=count||l.to>=count){error="Invalid graph connection.";return false;}
            ++indegree[l.to];outgoing[l.from].push_back(l.to);
        }
        for(size_t i=0;i<count;++i)if(!indegree[i])order.push_back(i);
        for(size_t i=0;i<order.size();++i)for(const auto child:outgoing[order[i]])
        {rank[child]=std::max(rank[child],rank[order[i]]+1);if(!--indegree[child])order.push_back(child);}
        if(order.size()!=count){error="Cannot arrange cyclic graph.";return false;}
        std::vector<float> offsets(count+1,60.f);std::vector<Point> positions(count);
        for(const auto i:order)
        {
            BlueprintNodeSpec spec;if(!nodeSpec(g,g.nodes[i],spec)){error="Unknown node.";return false;}
            positions[i]={24.f+252.f*static_cast<float>(rank[i]),offsets[rank[i]]};
            offsets[rank[i]]+=std::max(72.f,46.f+22.f*static_cast<float>(std::max(spec.inputs.size(),spec.outputs.size())))+36.f;
        }
        bool changed=std::any_of(g.links.begin(),g.links.end(),[](const Link& l){return !l.route.empty();});
        for(size_t i=0;i<count;++i)changed=changed||g.nodes[i].x!=positions[i].x||g.nodes[i].y!=positions[i].y;
        if(changed){record(graph);for(size_t i=0;i<count;++i){g.nodes[i].x=positions[i].x;g.nodes[i].y=positions[i].y;}for(auto& l:g.links)l.route.clear();}
        error.clear();return true;
    }
    bool BlueprintDocument::alignNodes(size_t graph,const std::vector<size_t>& nodes,bool horizontal,std::string& error)
    {
        if(graph>=graphs.size()||nodes.size()<2){error="Select at least two nodes.";return false;}
        auto& g=graphs[graph];float position=1000000.f;
        for(const auto i:nodes){if(i>=g.nodes.size()){error="Invalid node selection.";return false;}position=std::min(position,horizontal?g.nodes[i].y:g.nodes[i].x);}
        bool changed=false;for(const auto i:nodes)changed=changed||(horizontal?g.nodes[i].y:g.nodes[i].x)!=position;
        if(changed){record(graph);for(const auto i:nodes){if(horizontal)g.nodes[i].y=position;else g.nodes[i].x=position;}}
        error.clear();return true;
    }
    bool BlueprintDocument::addComment(size_t graph,const std::vector<size_t>& nodes,const std::string& title,std::string& error)
    {
        if(graph>=graphs.size()||nodes.empty()||title.empty()||title.size()>256||title.find('\0')!=std::string::npos||graphs[graph].comments.size()>=128){error="Select nodes and enter a comment title.";return false;}
        auto& g=graphs[graph];Comment comment{title,{}};
        for(const auto i:nodes)
        {
            if(i>=g.nodes.size()){error="Invalid node selection.";return false;}
            const auto& id=g.nodes[i].id;
            if(std::find(comment.members.begin(),comment.members.end(),id)!=comment.members.end())continue;
            for(const auto& other:g.comments)if(std::find(other.members.begin(),other.members.end(),id)!=other.members.end()){error="A selected node already belongs to a comment group.";return false;}
            comment.members.push_back(id);
        }
        record(graph);g.comments.push_back(std::move(comment));error.clear();return true;
    }
    bool BlueprintDocument::renameComment(size_t graph,size_t comment,const std::string& title,std::string& error)
    {
        if(graph>=graphs.size()||comment>=graphs[graph].comments.size()||title.empty()||title.size()>256||title.find('\0')!=std::string::npos){error="Invalid comment title.";return false;}
        auto& item=graphs[graph].comments[comment];if(item.title!=title){record(graph);item.title=title;}error.clear();return true;
    }
    bool BlueprintDocument::deleteComment(size_t graph,size_t comment)
    {
        if(graph>=graphs.size()||comment>=graphs[graph].comments.size())return false;
        record(graph);auto& items=graphs[graph].comments;items.erase(items.begin()+static_cast<std::ptrdiff_t>(comment));return true;
    }
    bool BlueprintDocument::insertRoutePoint(size_t graph,size_t link,size_t point,float x,float y)
    {
        if(graph>=graphs.size()||link>=graphs[graph].links.size()||!coordinate(x)||!coordinate(y))return false;
        auto& route=graphs[graph].links[link].route;if(point>route.size()||route.size()>=16)return false;
        record(graph);route.insert(route.begin()+static_cast<std::ptrdiff_t>(point),{x,y});return true;
    }
    bool BlueprintDocument::moveRoutePoint(size_t graph,size_t link,size_t point,float x,float y)
    {
        if(graph>=graphs.size()||link>=graphs[graph].links.size()||point>=graphs[graph].links[link].route.size()||!coordinate(x)||!coordinate(y))return false;
        auto& p=graphs[graph].links[link].route[point];if(p.x==x&&p.y==y)return false;
        record(graph);p={x,y};return true;
    }
    bool BlueprintDocument::deleteRoutePoint(size_t graph,size_t link,size_t point)
    {
        if(graph>=graphs.size()||link>=graphs[graph].links.size()||point>=graphs[graph].links[link].route.size())return false;
        record(graph);auto& route=graphs[graph].links[link].route;route.erase(route.begin()+static_cast<std::ptrdiff_t>(point));return true;
    }
    bool BlueprintDocument::addNode(size_t graph,const std::string& kind,float x,float y,std::string& error,const std::string& functionTarget)
    {
        if(graph>=graphs.size()||!blueprintSpec(kind)||kind=="entry"||!coordinate(x)||!coordinate(y)||graphs[graph].nodes.size()>=kMaxNodes)
        {error="Cannot add this node.";return false;}
        auto& g=graphs[graph];
        if(kind=="return:function"&&g.inlineMacro){error="Inline macros cannot contain return nodes.";return false;}
        if(kind=="return:function"&&g.kind!="function"){error="Return belongs in a function graph.";return false;}
        if(!context(g.name,kind,g.owner)){error="This event value belongs to a different event graph.";return false;}
        std::string id;
        do{id="n"+std::to_string(++m_nextId);}while(std::any_of(g.nodes.begin(),g.nodes.end(),[&](const Node& n){return n.id==id;}));
        std::string value=kind=="literal:string"?"":kind=="literal:bool"?"false":kind.rfind("literal:",0)==0?"0":"";
        if(kind.rfind("capture:",0)==0)value="captured";
        if(globalKind(kind))
        {
            const auto& globals=moduleGlobals(g.owner);
            const auto found=std::find_if(globals.begin(),globals.end(),[&](const Field& f){return f.type==kind.substr(11);});
            if(found==globals.end()){error="No declared module variable of this type.";return false;}
            value=found->name;
        }
        if(variableKind(kind))
        {
            const auto type=kind.substr(8);
            value="value_"+type; if(value.back()=='@')value.pop_back();
            std::vector<BlueprintPin> vars;
            if(!variables(vars,error,g.owner))return false;
            for(unsigned i=1;std::any_of(vars.begin(),vars.end(),[&](const BlueprintPin& p){return p.name==value&&p.type!=type;});++i)value="variable_"+std::to_string(i);
        }
        if((kind=="attach:behaviour"||kind=="attach:direct"||kind=="level:attach"))
        {
            const auto names=behaviours();const auto found=std::find_if(names.begin(),names.end(),[&](const std::string& n){return canAttach(g.owner,n);});
            if(found==names.end()){error="No Behaviour available in this module.";return false;}value=*found;
        }
        if(kind=="call:function")
        {
            // Choose the first valid target, not merely the first function in the list.
            bool valid=false;
            for(const auto& fn:graphs)
            {
                if(fn.owner!=g.owner||fn.kind!="function"||fn.name==g.name||(!functionTarget.empty()&&fn.name!=functionTarget))continue;
                value=fn.name;g.nodes.push_back({id,kind,value,x,y});
                valid=functions(error);g.nodes.pop_back();if(valid)break;
            }
            if(!valid){error="No nonrecursive function target. Create another function first.";return false;}
        }
        record(graph);g.nodes.push_back({id,kind,value,x,y});error.clear();return true;
    }
    bool BlueprintDocument::addConnectedNode(size_t graph,size_t from,const std::string& output,const std::string& kind,float x,float y,std::string& error)
    {return placeContextNode(graph,from,output,true,kind,"","",x,y,error);}
    bool BlueprintDocument::canCallFunction(size_t graph,const std::string& target) const
    {
        if(graph>=graphs.size())return false;
        const auto& caller=graphs[graph];
        const auto* first=function(caller.owner,target);
        if(!first)return false;
        std::vector<const Graph*> work{first},seen;
        while(!work.empty())
        {
            const auto* g=work.back();work.pop_back();
            if(g==&caller)return false;
            if(std::find(seen.begin(),seen.end(),g)!=seen.end())continue;
            seen.push_back(g);
            for(const auto& n:g->nodes)if(n.kind=="call:function")
            {const auto* child=function(g->owner,n.ref);if(child)work.push_back(child);}
        }
        return true;
    }
    bool BlueprintDocument::connectCompatible(size_t graph,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error)
    {
        if(graph>=graphs.size()||from>=graphs[graph].nodes.size()||to>=graphs[graph].nodes.size())
        {error="Invalid connection endpoints.";return false;}
        const auto& g=graphs[graph];BlueprintNodeSpec a,b;
        if(!nodeSpec(g,g.nodes[from],a)||!nodeSpec(g,g.nodes[to],b)){error="Unknown node.";return false;}
        const auto* out=findPin(a.outputs,output);const auto* in=findPin(b.inputs,input);
        if(!out||!in){error="Unknown pin.";return false;}
        if(out->type==in->type)return connect(graph,from,output,to,input,error);
        if(!blueprintLosslessConversion(out->type,in->type)){error="Pins need matching types or an explicit conversion.";return false;}
        BlueprintDocument staged;staged.name=name;staged.assetRoot=assetRoot;staged.graphs=graphs;staged.manifests=manifests;staged.m_nextId=m_nextId;
        const float x=(g.nodes[from].x+g.nodes[to].x)*0.5f,y=(g.nodes[from].y+g.nodes[to].y)*0.5f+100.f;
        if(!staged.addNode(graph,"cast:"+out->type+":"+in->type,x,y,error))return false;
        const auto cast=staged.graphs[graph].nodes.size()-1;
        if(!staged.connect(graph,from,output,cast,"a",error)||!staged.connect(graph,cast,"result",to,input,error))return false;
        record(graph);graphs[graph]=std::move(staged.graphs[graph]);m_nextId=staged.m_nextId;error.clear();return true;
    }
    bool BlueprintDocument::placeContextNode(size_t graph,size_t existing,const std::string& pin,bool output,const std::string& kind,const std::string& reference,const std::string& otherPin,float x,float y,std::string& error)
    {
        if(graph>=graphs.size()||(existing!=static_cast<size_t>(-1)&&existing>=graphs[graph].nodes.size())){error="Invalid source pin.";return false;}
        BlueprintDocument staged;staged.name=name;staged.assetRoot=assetRoot;staged.graphs=graphs;staged.manifests=manifests;staged.m_nextId=m_nextId;
        if(!reference.empty()&&(globalKind(kind)||variableKind(kind)))
        {
            if(!staged.addVariableNode(graph,reference,globalKind(kind),kind.find(":set:")!=std::string::npos,x,y,error))return false;
            if(staged.graphs[graph].nodes.back().kind!=kind){error="Variable type changed.";return false;}
        }
        else if(!staged.addNode(graph,kind,x,y,error,kind=="call:function"?reference:""))return false;
        const auto added=staged.graphs[graph].nodes.size()-1;
        if(existing!=static_cast<size_t>(-1))
        {
            BlueprintNodeSpec sourceSpec,targetSpec;
            if(!staged.nodeSpec(staged.graphs[graph],staged.graphs[graph].nodes[existing],sourceSpec)||
               !staged.nodeSpec(staged.graphs[graph],staged.graphs[graph].nodes[added],targetSpec)){error="Invalid node.";return false;}
            const auto* endpoint=findPin(output?sourceSpec.outputs:sourceSpec.inputs,pin);
            if(!endpoint){error="Invalid source pin.";return false;}
            const auto& candidates=output?targetSpec.inputs:targetSpec.outputs;
            const BlueprintPin* match=nullptr;
            for(const auto& candidate:candidates)if((otherPin.empty()||otherPin==candidate.name)&&candidate.type==endpoint->type){match=&candidate;break;}
            if(!match)for(const auto& candidate:candidates)if((otherPin.empty()||otherPin==candidate.name)&&
                (output?blueprintLosslessConversion(endpoint->type,candidate.type):blueprintLosslessConversion(candidate.type,endpoint->type))){match=&candidate;break;}
            if(!match){error="No compatible pin on this node.";return false;}
            if(!(output?staged.connectCompatible(graph,existing,pin,added,match->name,error):staged.connectCompatible(graph,added,match->name,existing,pin,error)))return false;
        }
        record(graph);graphs[graph]=std::move(staged.graphs[graph]);m_nextId=staged.m_nextId;error.clear();return true;
    }
    bool BlueprintDocument::addVariableNode(size_t graph,const std::string& variableName,bool global,bool setter,float x,float y,std::string& error)
    {
        if(graph>=graphs.size()||!identifier(variableName)||!coordinate(x)||!coordinate(y)||graphs[graph].nodes.size()>=kMaxNodes)
        {error="Cannot place variable node.";return false;}
        auto& g=graphs[graph];std::string type;
        if(global)
        {
            for(const auto& f:moduleGlobals(g.owner))if(f.name==variableName){type=f.type;break;}
        }
        else
        {
            std::vector<BlueprintPin> vars;if(!variables(vars,error,g.owner))return false;
            for(const auto& f:vars)if(f.name==variableName){type=f.type;break;}
        }
        const auto kind=std::string(global?"global:":"var:")+(setter?"set:":"get:")+type;
        if(type.empty()||!blueprintSpec(kind)||!context(g.name,kind,g.owner)){error="Variable is not available in this graph.";return false;}
        std::string id;do{id="n"+std::to_string(++m_nextId);}while(std::any_of(g.nodes.begin(),g.nodes.end(),[&](const Node& n){return n.id==id;}));
        record(graph);g.nodes.push_back({id,kind,variableName,x,y});error.clear();return true;
    }
    bool BlueprintDocument::deleteNode(size_t graph,size_t node,std::string& error)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()||graphs[graph].nodes[node].kind=="entry")
        {error="The event entry cannot be deleted.";return false;}
        auto& g=graphs[graph];record(graph);
        const auto id=g.nodes[node].id;
        for(auto& comment:g.comments)comment.members.erase(std::remove(comment.members.begin(),comment.members.end(),id),comment.members.end());
        g.comments.erase(std::remove_if(g.comments.begin(),g.comments.end(),[](const Comment& item){return item.members.empty();}),g.comments.end());
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
    std::string BlueprintDocument::moduleForOwner(const std::string& owner) const
    {
        if(!behaviourOwner(owner))return owner;
        for(const auto& g:graphs)if(g.owner==owner&&g.name=="start")return g.moduleOwner;
        return "";
    }
    const std::vector<BlueprintDocument::Field>& BlueprintDocument::moduleGlobals(const std::string& owner) const
    {
        static const std::vector<Field> empty;
        const auto owners=screens();
        const auto module=owner.empty()?(owners.size()==1?owners.front():""):moduleForOwner(owner);
        if(module.empty())return empty;
        for(const auto& g:graphs)if(g.owner==module&&g.name=="onEnter")return g.globals;
        return empty;
    }
    bool BlueprintDocument::canAttach(const std::string& owner,const std::string& behaviour) const
    {
        const auto target="behaviour:"+behaviour;
        if(std::none_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==target&&g.name=="start";}))return false;
        const auto scope=moduleForOwner(target);
        return scope.empty()||scope==moduleForOwner(owner);
    }
    bool BlueprintDocument::variables(std::vector<BlueprintPin>& values,std::string& error,const std::string& owner)const
    {
        std::vector<BlueprintPin> result;
        for(const auto& graph:graphs)if(graph.owner==owner)for(const auto& field:graph.fields)
        {
            if(!identifier(field.name)||!blueprintLiteralType(field.type)||std::any_of(result.begin(),result.end(),[&](const BlueprintPin& p){return p.name==field.name;}))
            {error="Invalid or duplicate declared field: "+field.name;return false;}
            result.push_back({field.name,field.type});
        }
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
    bool BlueprintDocument::editVariableDeclaration(const std::string& owner,const std::string& command,std::string& error)
    {
        const bool global=command.rfind("global ",0)==0;
        if(!global&&command.rfind("field ",0)!=0){error="Use field or global followed by name:type.";return false;}
        const auto body=command.substr(global?7:6);
        const auto scope=global?moduleForOwner(owner):owner;
        const auto event=behaviourOwner(scope)?"start":"onEnter";
        if(scope.empty()){error="Shared Behaviours have no single module. Select a screen for global variables.";return false;}
        BlueprintDocument staged;staged.name=name;staged.assetRoot=assetRoot;staged.graphs=graphs;staged.manifests=manifests;
        auto graph=std::find_if(staged.graphs.begin(),staged.graphs.end(),[&](const Graph& g){return g.owner==scope&&g.name==event;});
        if(graph==staged.graphs.end()){error="Unknown declaration owner.";return false;}
        auto& fields=global?graph->globals:graph->fields;
        const bool remove=!body.empty()&&body.front()=='-';
        std::string oldName;Field next;
        if(remove)oldName=body.substr(1);
        else
        {
            const auto colon=body.find(':');
            if(colon==std::string::npos){error="Use name:type, optionally =value.";return false;}
            const auto names=body.substr(0,colon);const auto arrow=names.find('>');
            oldName=arrow==std::string::npos?names:names.substr(0,arrow);
            next.name=arrow==std::string::npos?names:names.substr(arrow+1);
            const auto equals=body.find('=',colon+1);
            next.type=body.substr(colon+1,equals==std::string::npos?equals:equals-colon-1);
            next.initialized=equals!=std::string::npos;
            if(next.initialized)next.value=body.substr(equals+1);
            if(!identifier(next.name)||!blueprintLiteralType(next.type)||(!global&&behaviourOwner(scope)&&next.name=="entity")||
                (next.initialized&&!literal("literal:"+next.type,next.value)))
            {error="Invalid name, primitive type or literal initializer.";return false;}
        }
        if(!identifier(oldName)){error="Invalid variable name.";return false;}
        auto old=std::find_if(fields.begin(),fields.end(),[&](const Field& f){return f.name==oldName;});
        const auto matches=[&](const Graph& g,const Node& n,const std::string& ref)
        {return (global?staged.moduleForOwner(g.owner)==scope&&globalKind(n.kind):g.owner==scope&&variableKind(n.kind))&&n.ref==ref;};
        bool referenced=false;
        for(const auto& g:staged.graphs)for(const auto& n:g.nodes)if(matches(g,n,oldName))
        {
            referenced=true;
            if(remove||n.kind.substr(global?11:8)!=next.type){error="Remove or replace variable nodes before deleting or changing its type.";return false;}
        }
        if(remove)
        {
            if(old==fields.end()){error="No explicit declaration with that name.";return false;}
            fields.erase(old);
        }
        else
        {
            if(oldName!=next.name)
            {
                if(old==fields.end()&&!referenced){error="Unknown declaration to rename.";return false;}
                if(std::any_of(fields.begin(),fields.end(),[&](const Field& f){return f.name==next.name;}))
                {error="Destination variable already exists.";return false;}
                for(const auto& g:staged.graphs)for(const auto& n:g.nodes)if(matches(g,n,next.name))
                {error="Destination variable already has nodes.";return false;}
                for(auto& g:staged.graphs)for(auto& n:g.nodes)if(matches(g,n,oldName))n.ref=next.name;
            }
            if(old==fields.end())
            {
                if(fields.size()>=256){error="Declaration limit reached.";return false;}
                fields.push_back(std::move(next));
            }
            else *old=std::move(next);
        }
        std::string checked;
        if(!staged.serialize(checked,error))return false;
        recordGraphList();graphs.swap(staged.graphs);error.clear();return true;
    }
    bool BlueprintDocument::setReference(size_t graph,size_t node,const std::string& value,std::string& error)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()||
            (!variableKind(graphs[graph].nodes[node].kind)&&!globalKind(graphs[graph].nodes[node].kind)&&graphs[graph].nodes[node].kind.rfind("capture:",0)!=0&&graphs[graph].nodes[node].kind!="call:function"&&graphs[graph].nodes[node].kind!="attach:behaviour"&&graphs[graph].nodes[node].kind!="attach:direct"&&graphs[graph].nodes[node].kind!="level:attach")||!identifier(value))
        {error="Use a variable name containing letters, digits and underscores.";return false;}
        auto& n=graphs[graph].nodes[node];
        if(n.ref==value){error.clear();return true;}
        if(globalKind(n.kind))
        {
            const auto& globals=moduleGlobals(graphs[graph].owner);
            if(std::none_of(globals.begin(),globals.end(),[&](const Field& f){return f.name==value&&f.type==n.kind.substr(11);}))
            {error="Unknown or incompatible module variable.";return false;}
            record(graph);n.ref=value;error.clear();return true;
        }
        if(n.kind.rfind("capture:",0)==0){record(graph);n.ref=value;error.clear();return true;}
        if((n.kind=="attach:behaviour"||n.kind=="attach:direct"||n.kind=="level:attach"))
        {
            if(!canAttach(graphs[graph].owner,value)){error="Unknown or out-of-scope Behaviour "+value;return false;}
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
        for(const auto& g:graphs)if(g.owner==graphs[graph].owner)for(const auto& f:g.fields)
            if(f.name==value&&f.type!=type){error="Declared variable has a different type.";return false;}
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
        m_undo.push_back({static_cast<size_t>(-1),{},m_revision,graphs,manifests});
        m_redo.clear();m_revision=++m_nextRevision;
    }
    const BlueprintDocument::Graph* BlueprintDocument::function(const std::string& owner,const std::string& functionName) const
    {
        const auto found=std::find_if(graphs.begin(),graphs.end(),[&](const Graph& g)
        {return g.owner==owner&&g.name==functionName&&g.kind=="function";});
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
            if(graph.kind!="function"||graph.inlineMacro)return false;
            if(graph.returnType!="void")spec.inputs.push_back({"value",graph.returnType});
        }
        if(node.kind=="call:function")
        {
            const auto* target=function(graph.owner,node.ref);if(!target)return false;
            for(const auto& p:target->parameters)spec.inputs.push_back({"arg_"+p.name,p.type});
            if(target->inlineMacro)spec.title="Macro "+target->name;
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
        if(g.inlineMacro&&returnType!="void"){error="Inline macros currently require a void signature.";return false;}
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
    bool BlueprintDocument::setInlineMacro(size_t graph,bool enabled,std::string& error)
    {
        if(graph>=graphs.size()||graphs[graph].kind!="function"){error="Select a reusable graph.";return false;}
        auto& g=graphs[graph];
        if(enabled&&(g.returnType!="void"||std::any_of(g.nodes.begin(),g.nodes.end(),[](const Node& n){return n.kind=="return:function";})))
        {error="Inline macros require a void signature and no return nodes.";return false;}
        if(g.inlineMacro!=enabled){record(graph);g.inlineMacro=enabled;}error.clear();return true;
    }
    bool BlueprintDocument::renameFunction(size_t graph,const std::string& functionName,std::string& error)
    {
        if(graph>=graphs.size()||graphs[graph].kind!="function"||!identifier(functionName)||ownerEvent(graphs[graph].owner,functionName))
        {error="Choose a valid function name.";return false;}
        const auto owner=graphs[graph].owner,old=graphs[graph].name;
        if(old==functionName){error.clear();return true;}
        if(std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner&&g.name==functionName;}))
        {error="A graph already uses that name.";return false;}
        recordGraphList();graphs[graph].name=functionName;
        for(auto& g:graphs)if(g.owner==owner)for(auto& n:g.nodes)if(n.kind=="call:function"&&n.ref==old)n.ref=functionName;
        error.clear();return true;
    }
    bool BlueprintDocument::duplicateFunction(size_t graph,const std::string& functionName,std::string& error)
    {
        if(graph>=graphs.size()||graphs[graph].kind!="function"||graphs.size()>=kMaxGraphs||!identifier(functionName)||ownerEvent(graphs[graph].owner,functionName)||
            std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==graphs[graph].owner&&g.name==functionName;}))
        {error="Choose a unique name for the copied function.";return false;}
        Graph copy=graphs[graph];copy.name=functionName;
        recordGraphList();graphs.push_back(std::move(copy));error.clear();return true;
    }
    bool BlueprintDocument::collapseToFunction(size_t graph,const std::vector<size_t>& nodes,const std::string& functionName,std::string& error)
    {
        const auto fail=[&](const std::string& reason){error=reason;return false;};
        if(graph>=graphs.size()||nodes.empty()||graphs.size()>=kMaxGraphs||!identifier(functionName))return fail("Select nodes and choose a unique function name.");
        const auto& original=graphs[graph];const size_t count=original.nodes.size(),none=static_cast<size_t>(-1);
        if(ownerEvent(original.owner,functionName)||std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==original.owner&&g.name==functionName;}))
            return fail("A graph already uses that function name.");
        std::vector<bool> selected(count,false),action(count,false);bool structured=false;
        std::vector<BlueprintNodeSpec> specs(count);size_t actions=0,selectedCount=0;
        for(const auto i:nodes){if(i>=count)return fail("Invalid selection.");if(!selected[i]){selected[i]=true;++selectedCount;}}
        for(size_t i=0;i<count;++i)
        {
            const auto& n=original.nodes[i];
            if(!nodeSpec(original,n,specs[i]))return fail("Unknown node in graph.");
            if(!selected[i])continue;
            if(n.kind=="entry"||n.kind=="return:function"||n.kind.rfind("event:",0)==0||n.kind.rfind("local:set:",0)==0)
                return fail("Event entries, returns, event values and mutable local assignments cannot be extracted.");
            const auto isExec=[](const BlueprintPin& p){return p.type=="exec";};
            const auto inputs=std::count_if(specs[i].inputs.begin(),specs[i].inputs.end(),isExec);
            const auto outputs=std::count_if(specs[i].outputs.begin(),specs[i].outputs.end(),isExec);
            if(inputs||outputs)
            {
                if(inputs!=1||!findPin(specs[i].inputs,"in")||!findPin(specs[i].outputs,"then"))return fail("Unsupported execution boundary.");
                if(outputs!=1)
                {
                    if(n.kind!="branch"&&n.kind!="sequence"&&n.kind!="repeat")return fail("Unsupported structured flow node.");
                    structured=true;
                }
                action[i]=true;++actions;
            }
        }
        if(!actions)return fail("Select at least one connected execution node.");
        struct Boundary { size_t node;std::string pin,type; };
        std::vector<Boundary> arguments;Boundary returned{none,"","void"};
        const Link* entering=nullptr;const Link* leaving=nullptr;
        std::vector<size_t> next(count,none);std::vector<std::vector<size_t>> execChildren(count);
        for(const auto& l:original.links)
        {
            if(l.from>=count||l.to>=count)return fail("Invalid graph link.");
            const auto* source=findPin(specs[l.from].outputs,l.output);
            if(!source)return fail("Unknown source pin.");
            const bool a=selected[l.from],b=selected[l.to];
            if(source->type=="exec")
            {
                if(!a&&b){if(entering)return fail("Selection has more than one execution entry.");entering=&l;}
                else if(a&&!b){if(leaving||l.output!="then")return fail("Include all branch and loop bodies; only a single Then continuation may leave.");leaving=&l;}
                else if(a&&b){execChildren[l.from].push_back(l.to);if(l.output=="then")next[l.from]=l.to;}
                continue;
            }
            if(!a&&b)
            {
                const auto& n=original.nodes[l.from];
                const bool stable=n.kind.rfind("literal:",0)==0||(n.kind=="entry"&&l.output.rfind("arg_",0)==0)||n.kind.rfind("event:",0)==0||
                    std::any_of(specs[l.from].inputs.begin(),specs[l.from].inputs.end(),[](const BlueprintPin& p){return p.type=="exec";});
                if(!stable||!signatureType(source->type))return fail("Include live expression dependencies in the selection; only stable typed boundary inputs can become parameters.");
                if(std::none_of(arguments.begin(),arguments.end(),[&](const Boundary& p){return p.node==l.from&&p.pin==l.output;}))
                {if(arguments.size()>=8)return fail("Extraction supports at most eight distinct input values.");arguments.push_back({l.from,l.output,source->type});}
            }
            else if(a&&!b)
            {
                if(!action[l.from]||!signatureType(source->type))return fail("Only an executed value can cross the function return boundary.");
                if(returned.node!=none&&(returned.node!=l.from||returned.pin!=l.output))return fail("Extraction supports one returned value.");
                returned={l.from,l.output,source->type};
            }
        }
        if(!entering)return fail("Selection must be connected to an execution entry.");
        size_t cursor=entering->to,last=none,visited=0;
        while(cursor!=none&&visited<=actions)
        {if(!action[cursor])return fail("Invalid selected execution chain.");last=cursor;cursor=next[cursor];++visited;}
        if(cursor!=none||(leaving&&leaving->from!=last))return fail("Selected actions must have one outer continuation.");
        if(structured&&returned.node!=none)return fail("Structured extraction currently requires no outgoing data value.");
        std::vector<bool> reached(count,false);std::vector<size_t> flowWork{entering->to};reached[entering->to]=true;
        for(size_t i=0;i<flowWork.size();++i)for(const auto child:execChildren[flowWork[i]])if(!reached[child]){reached[child]=true;flowWork.push_back(child);}
        if(flowWork.size()!=actions)return fail("Select one connected execution region including all nested bodies.");
        std::vector<bool> used(count,false);std::vector<size_t> work;
        for(size_t i=0;i<count;++i)if(action[i]){used[i]=true;work.push_back(i);}
        std::vector<std::vector<size_t>> predecessors(count);
        for(const auto& l:original.links)if(selected[l.from]&&selected[l.to])predecessors[l.to].push_back(l.from);
        for(size_t i=0;i<work.size();++i)for(const auto dep:predecessors[work[i]])if(!used[dep]){used[dep]=true;work.push_back(dep);}
        for(size_t i=0;i<count;++i)if(selected[i]&&!used[i])return fail("Selection includes unused value nodes.");
        for(const auto& comment:original.comments)
        {
            size_t included=0;
            for(size_t i=0;i<count;++i)if(selected[i]&&std::find(comment.members.begin(),comment.members.end(),original.nodes[i].id)!=comment.members.end())++included;
            if(included&&included!=comment.members.size())return fail("Select the complete comment group or remove it before extraction.");
        }
        if(selectedCount+2>kMaxNodes)return fail("Extracted graph exceeds node limit.");
        BlueprintDocument staged;staged.name=name;staged.assetRoot=assetRoot;staged.graphs=graphs;staged.manifests=manifests;staged.m_nextId=m_nextId;
        Graph outer=original,inner;outer.nodes.clear();outer.links.clear();outer.comments.clear();
        inner.owner=original.owner;inner.name=functionName;inner.kind="function";inner.returnType=returned.type;
        for(size_t i=0;i<arguments.size();++i)inner.parameters.push_back({"input_"+std::to_string(i+1),arguments[i].type});
        std::unordered_set<std::string> ids;for(const auto& n:original.nodes)ids.insert(n.id);
        auto newId=[&](){std::string id;do{id="n"+std::to_string(++staged.m_nextId);}while(!ids.insert(id).second);return id;};
        inner.nodes.push_back({newId(),"entry","",24,60});
        std::vector<size_t> outside(count,none),inside(count,none);
        const auto& first=original.nodes[entering->to];
        for(size_t i=0;i<count;++i)
        {
            if(selected[i]){inside[i]=inner.nodes.size();inner.nodes.push_back(original.nodes[i]);}
            else {outside[i]=outer.nodes.size();outer.nodes.push_back(original.nodes[i]);}
        }
        const size_t call=outer.nodes.size();outer.nodes.push_back({newId(),"call:function",functionName,first.x,first.y});
        const size_t ret=inner.nodes.size();
        if(returned.node!=none)inner.nodes.push_back({newId(),"return:function","",24,60});
        for(const auto& comment:original.comments)
        {
            const bool moved=std::any_of(inner.nodes.begin(),inner.nodes.end(),[&](const Node& n){return n.id==comment.members.front();});
            (moved?inner.comments:outer.comments).push_back(comment);
        }
        for(const auto& l:original.links)
        {
            const bool a=selected[l.from],b=selected[l.to];
            if(a&&b){Link copy=l;copy.from=inside[l.from];copy.to=inside[l.to];inner.links.push_back(std::move(copy));}
            else if(!a&&!b){Link copy=l;copy.from=outside[l.from];copy.to=outside[l.to];outer.links.push_back(std::move(copy));}
            else if(&l==entering){outer.links.push_back({outside[l.from],call,l.output,"in"});inner.links.push_back({0,inside[l.to],"then",l.input});}
            else if(&l==leaving)outer.links.push_back({call,outside[l.to],"then",l.input});
            else if(!a&&b)
            {
                const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const Boundary& p){return p.node==l.from&&p.pin==l.output;});
                if(found==arguments.end())return fail("Missing extraction argument.");
                const auto arg="arg_input_"+std::to_string(static_cast<size_t>(found-arguments.begin())+1);
                if(std::none_of(outer.links.begin(),outer.links.end(),[&](const Link& wire){return wire.to==call&&wire.input==arg;}))
                    outer.links.push_back({outside[l.from],call,l.output,arg});
                inner.links.push_back({0,inside[l.to],arg,l.input});
            }
            else outer.links.push_back({call,outside[l.to],"result",l.input});
        }
        if(returned.node!=none){inner.links.push_back({inside[last],ret,"then","in"});inner.links.push_back({inside[returned.node],ret,returned.pin,"value"});}
        staged.graphs[graph]=std::move(outer);staged.graphs.push_back(std::move(inner));
        if(!staged.arrange(staged.graphs.size()-1,error))return false;
        std::string checked;if(!staged.serialize(checked,error))return false;
        recordGraphList();graphs.swap(staged.graphs);m_nextId=staged.m_nextId;error.clear();return true;
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
            const auto behaviourName=g.owner.substr(10);
            if(std::find(result.begin(),result.end(),behaviourName)==result.end())result.push_back(behaviourName);
        }
        return result;
    }
    bool BlueprintDocument::addBehaviour(const std::string& behaviourName,std::string& error)
    {
        const auto owner="behaviour:"+behaviourName;
        if(!identifier(behaviourName)||graphs.size()+5>kMaxGraphs||
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
    bool BlueprintDocument::removeBehaviour(const std::string& behaviourName,std::string& error)
    {
        const auto owner="behaviour:"+behaviourName;
        if(std::none_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner;}))
        {error="Unknown behaviour.";return false;}
        for(const auto& g:graphs)for(const auto& n:g.nodes)
            if((n.kind=="attach:behaviour"||n.kind=="attach:direct"||n.kind=="level:attach")&&n.ref==behaviourName){error="Remove attachment nodes referencing "+behaviourName+" first.";return false;}
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
        for(const char* eventName:{"onLevelTouch","onLevelAgent"})
            if(std::none_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==owner&&g.name==eventName;}))missing.emplace_back(eventName);
        if(missing.empty()){error="Level events already exist for this screen.";return false;}
        if(graphs.size()+missing.size()>kMaxGraphs){error="Graph limit reached.";return false;}
        recordGraphList();
        for(const auto& eventName:missing)
        {
            Graph g;g.owner=owner;g.name=eventName;g.kind="event";
            g.nodes.push_back({"entry","entry","",24,60});graphs.push_back(std::move(g));
        }
        error.clear();return true;
    }
    bool BlueprintDocument::addScreen(const std::string& id,std::string& error)
    {
        if(!screenId(id)||graphs.size()+6>kMaxGraphs||
            std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.owner==id;}))
        {error="Choose a unique lowercase screen ID (letters, digits, hyphens or underscores; not screen).";return false;}
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
        for(const auto& g:graphs)if(g.moduleOwner==id){error="Remove module-scoped Behaviours before deleting "+id+".";return false;}
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
        manifests.erase(std::remove_if(manifests.begin(),manifests.end(),[&](const ScreenManifest& m){return m.owner==id;}),manifests.end());
        error.clear();return true;
    }
    bool BlueprintDocument::undo()
    {
        if(m_undo.empty())return false;
        auto e=std::move(m_undo.back());m_undo.pop_back();
        if(e.graph==static_cast<size_t>(-1))
        {
            m_redo.push_back({e.graph,{},m_revision,std::move(graphs),std::move(manifests)});graphs=std::move(e.all);manifests=std::move(e.manifests);
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
            m_undo.push_back({e.graph,{},m_revision,std::move(graphs),std::move(manifests)});graphs=std::move(e.all);manifests=std::move(e.manifests);
        }
        else
        {
            m_undo.push_back({e.graph,std::move(graphs[e.graph]),m_revision,{}});graphs[e.graph]=std::move(e.state);
        }
        m_revision=e.revision;return true;
    }
}
