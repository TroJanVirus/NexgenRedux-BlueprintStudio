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
            {"branch","Branch","Flow","in:exec|condition:bool","true:exec|false:exec"},
            {"literal:string","String","Values","","value:string"},
            {"literal:bool","Boolean","Values","","value:bool"},
            {"literal:int","Integer","Values","","value:int"},
            {"literal:float","Float","Values","","value:float"},
            {"literal:uint","Unsigned integer","Values","","value:uint"},
            {"literal:double","Double","Values","","value:double"},
            {"add:float","Add (float)","Math","a:float|b:float","result:float"},
            {"less:float","Less than (float)","Math","a:float|b:float","result:bool"},
            {"make:Vec2","Make Vec2","Values","x:float|y:float","result:Vec2"},
            {"make:Vec3","Make Vec3","Values","x:float|y:float|z:float","result:Vec3"},
            {"make:Color","Make Color","Values","r:float|g:float|b:float|a:float","result:Color"},
            {"event:delta","Delta time","Events","","value:float"},
            {"event:canvas","Canvas","Events","","value:Canvas@"},
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
            {"repeat","Repeat (bounded)","Flow","in:exec|count:int","body:exec|then:exec|index:int"},
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
        {return name=="onEnter"||name=="update"||name=="draw"||name=="onSuspend"||name=="onResume"||name=="onExit";}
        bool variableKind(const std::string& kind){return kind.rfind("var:get:",0)==0||kind.rfind("var:set:",0)==0;}
        bool identifier(const std::string& s)
        {
            if(s.empty()||s.size()>64)return false;
            for(size_t i=0;i<s.size();++i){const char c=s[i];if(!(c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(i&&c>='0'&&c<='9')))return false;}
            return true;
        }
        bool context(const std::string& event,const std::string& kind)
        {return (kind!="event:delta"||event=="update")&&(kind!="event:canvas"||event=="draw");}
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
        bool validWire(const BlueprintDocument::Graph& g,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error)
        {
            if(from>=g.nodes.size()||to>=g.nodes.size()||from==to){error="Choose two different nodes.";return false;}
            const auto* a=blueprintSpec(g.nodes[from].kind);const auto* b=blueprintSpec(g.nodes[to].kind);
            if(!a||!b){error="Unknown node.";return false;}
            const auto* out=findPin(a->outputs,output);const auto* in=findPin(b->inputs,input);
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
    const std::vector<BlueprintNodeSpec>& blueprintCatalogue()
    {
        static const auto catalogue=[]{
            std::vector<BlueprintNodeSpec> result;
            result.reserve(sizeof(rows)/sizeof(rows[0]));
            for(const auto& row:rows)result.push_back({row.id,row.title,row.category,parsePins(row.inputs),parsePins(row.outputs),row.call,row.property,row.result,*row.member!=0,*row.pure!=0,*row.hasOut!=0,parsePins(row.arguments),parsePins(row.defaults)});
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
        std::unordered_set<std::string> graphNames;
        for(auto value=gs.first();value.exists();value=value.next())
        {
            auto ns=value["nodes"],ls=value["links"];
            if(!validText(value["name"])||!graphNames.emplace(value["name"].text()).second||
                !ns.isArray()||!ls.isArray()||ns.count()>kMaxNodes||ls.count()>kMaxLinks)
            {error="Invalid or duplicate graph.";return false;}
            Graph g;g.name=value["name"].text();g.owner="Screen";g.kind="event";
            const auto kind=value["kind"].text("event");
            g.kind=kind;
            if((g.kind=="event"&&!lifecycle(g.name))||
                (g.kind=="function"&&(!identifier(g.name)||lifecycle(g.name)))||(g.kind!="event"&&g.kind!="function"))
            {error="Invalid event or function graph.";return false;}
            std::unordered_map<std::string,size_t> ids;unsigned entries=0;
            for(auto n=ns.first();n.exists();n=n.next())
            {
                const std::string kind=n["kind"].text(),ref=n["value"].text();
                const float x=n["x"].number(),y=n["y"].number();
                if(!validText(n["id"])||!blueprintSpec(kind)||!context(g.name,kind)||!n["x"].isNumber()||!n["y"].isNumber()||
                    !coordinate(x)||!coordinate(y)||!ids.emplace(n["id"].text(),g.nodes.size()).second||
                    (kind.rfind("literal:",0)==0&&(!n["value"].isString()||!literal(kind,ref)))||
                    ((variableKind(kind)||kind=="call:function")&&(!n["value"].isString()||!identifier(ref))))
                {error="Invalid node in "+g.name;return false;}
                if(kind=="entry")++entries;
                g.nodes.push_back({n["id"].text(),kind,ref,x,y});
            }
            if(entries!=1){error="Each graph needs exactly one entry.";return false;}
            for(auto l=ls.first();l.exists();l=l.next())
            {
                auto a=ids.find(l["from"].text()),b=ids.find(l["to"].text());
                if(a==ids.end()||b==ids.end()||!validWire(g,a->second,l["output"].text(),b->second,l["input"].text(),error))
                {if(a==ids.end()||b==ids.end())error="Dangling connection.";return false;}
                g.links.push_back({a->second,b->second,l["output"].text(),l["input"].text()});
            }
            candidate.graphs.push_back(std::move(g));
        }
        for(const char* event:{"onEnter","update","draw"})
            if(!graphNames.count(event)){error="Missing required event graph.";return false;}
        std::vector<BlueprintPin> vars;
        if(!candidate.variables(vars,error)||!candidate.functions(error))return false;
        *this=std::move(candidate);error.clear();return true;
    }
    bool BlueprintDocument::serialize(std::string& result,std::string& error)const
    {
        if(graphs.empty()){error="No Blueprint open.";return false;}
        auto fail=[&](){error="Not enough memory to serialize Blueprint.";return false;};
        JsonValue root;
        if(!number(root,"version",2)||!text(root,"kind","NativeBlueprint")||!text(root,"name",name))return fail();
        auto* gs=root.put("graphs");if(!gs)return fail();gs->setArray();
        for(const auto& g:graphs)
        {
            auto* v=gs->add();if(!v||!text(*v,"name",g.name)||!text(*v,"kind",g.kind))return fail();
            auto* ns=v->put("nodes");if(!ns)return fail();ns->setArray();
            for(const auto& n:g.nodes)
            {
                auto* j=ns->add();if(!j||!text(*j,"id",n.id)||!text(*j,"kind",n.kind)||!number(*j,"x",n.x)||!number(*j,"y",n.y))return fail();
                if((n.kind.rfind("literal:",0)==0||variableKind(n.kind)||n.kind=="call:function")&&!text(*j,"value",n.ref))return fail();
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
        if(!context(g.name,kind)){error="This event value belongs to a different event graph.";return false;}
        std::string id;
        do{id="n"+std::to_string(++m_nextId);}while(std::any_of(g.nodes.begin(),g.nodes.end(),[&](const Node& n){return n.id==id;}));
        std::string value=kind=="literal:string"?"":kind=="literal:bool"?"false":kind.rfind("literal:",0)==0?"0":"";
        if(variableKind(kind))
        {
            const auto type=kind.substr(8);
            value="value_"+type; if(value.back()=='@')value.pop_back();
            std::vector<BlueprintPin> vars;
            if(!variables(vars,error))return false;
            for(unsigned i=1;std::any_of(vars.begin(),vars.end(),[&](const BlueprintPin& p){return p.name==value&&p.type!=type;});++i)value="variable_"+std::to_string(i);
        }
        if(kind=="call:function")
        {
            // Choose the first valid target, not merely the first function in the list.
            bool valid=false;
            for(const auto& fn:graphs)
            {
                if(fn.kind!="function"||fn.name==g.name)continue;
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
        auto& g=graphs[graph];if(!validWire(g,from,output,to,input,error))return false;
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
    bool BlueprintDocument::setLiteral(size_t graph,size_t node,const std::string& value,std::string& error)
    {
        if(graph>=graphs.size()||node>=graphs[graph].nodes.size()||!literal(graphs[graph].nodes[node].kind,value))
        {error="Invalid value for this literal type.";return false;}
        auto& n=graphs[graph].nodes[node];if(n.ref==value){error.clear();return true;}
        record(graph);n.ref=value;error.clear();return true;
    }
    bool BlueprintDocument::variables(std::vector<BlueprintPin>& values,std::string& error)const
    {
        std::vector<BlueprintPin> result;
        for(const auto& graph:graphs)for(const auto& node:graph.nodes)
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
            (!variableKind(graphs[graph].nodes[node].kind)&&graphs[graph].nodes[node].kind!="call:function")||!identifier(value))
        {error="Use a variable name containing letters, digits and underscores.";return false;}
        auto& n=graphs[graph].nodes[node];
        if(n.ref==value){error.clear();return true;}
        if(n.kind=="call:function")
        {
            const std::string previous=n.ref;n.ref=value;
            const bool valid=functions(error);n.ref=previous;if(!valid)return false;
            record(graph);n.ref=value;error.clear();return true;
        }
        const auto type=n.kind.substr(8);
        for(const auto& g:graphs)for(const auto& other:g.nodes)
            if(&other!=&n&&variableKind(other.kind)&&other.ref==value&&other.kind.substr(8)!=type)
            {error="Variable name already has a different type.";return false;}
        record(graph);n.ref=value;error.clear();return true;
    }
    bool BlueprintDocument::functions(std::string& error) const
    {
        std::unordered_map<std::string,size_t> index;
        for(size_t i=0;i<graphs.size();++i)
            if(graphs[i].kind=="function")index.emplace(graphs[i].name,i);
        std::vector<std::vector<size_t>> calls(graphs.size());
        for(size_t i=0;i<graphs.size();++i)for(const auto& node:graphs[i].nodes)
        {
            if(node.kind!="call:function")continue;
            const auto it=index.find(node.ref);
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
    bool BlueprintDocument::addFunction(const std::string& functionName,std::string& error)
    {
        if(!identifier(functionName)||graphs.size()>=kMaxGraphs||
            std::any_of(graphs.begin(),graphs.end(),[&](const Graph& g){return g.name==functionName;})||
            lifecycle(functionName))
        {error="Choose a unique function name (letters, digits and underscores).";return false;}
        recordGraphList();
        Graph g;g.owner="Screen";g.name=functionName;g.kind="function";
        g.nodes.push_back({"entry","entry","",24,60});graphs.push_back(std::move(g));
        error.clear();return true;
    }
    bool BlueprintDocument::removeFunction(size_t graph,std::string& error)
    {
        if(graph>=graphs.size()||graphs[graph].kind!="function")
        {error="Only function graphs can be deleted.";return false;}
        for(const auto& g:graphs)for(const auto& n:g.nodes)
            if(n.kind=="call:function"&&n.ref==graphs[graph].name)
            {error="Remove calls to this function before deleting it.";return false;}
        recordGraphList();graphs.erase(graphs.begin()+static_cast<std::ptrdiff_t>(graph));error.clear();return true;
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
