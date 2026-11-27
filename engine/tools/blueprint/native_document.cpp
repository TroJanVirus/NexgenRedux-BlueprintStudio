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
            const char* call="";const char* property="";const char* member="";const char* pure="";const char* result="";const char* hasOut="";};
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
        void text(JsonValue& j,const char* key,const std::string& value){j.put(key)->setText(value);}
        void number(JsonValue& j,const char* key,double value){j.put(key)->setNumber(value);}
    }
    const std::vector<BlueprintNodeSpec>& blueprintCatalogue()
    {
        static const auto catalogue=[]{
            std::vector<BlueprintNodeSpec> result;
            result.reserve(sizeof(rows)/sizeof(rows[0]));
            for(const auto& row:rows)result.push_back({row.id,row.title,row.category,parsePins(row.inputs),parsePins(row.outputs),row.call,row.property,row.result,*row.member!=0,*row.pure!=0,*row.hasOut!=0});
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
        for(const char* event:{"onEnter","update","draw"})
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
            if(g.name!="onEnter"&&g.name!="update"&&g.name!="draw"){error="Unsupported event graph.";return false;}
            std::unordered_map<std::string,size_t> ids;unsigned entries=0;
            for(auto n=ns.first();n.exists();n=n.next())
            {
                const std::string kind=n["kind"].text(),ref=n["value"].text();
                const float x=n["x"].number(),y=n["y"].number();
                if(!validText(n["id"])||!blueprintSpec(kind)||!n["x"].isNumber()||!n["y"].isNumber()||
                    !coordinate(x)||!coordinate(y)||!ids.emplace(n["id"].text(),g.nodes.size()).second||
                    (kind.rfind("literal:",0)==0&&(!n["value"].isString()||!literal(kind,ref))))
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
        *this=std::move(candidate);error.clear();return true;
    }
    bool BlueprintDocument::serialize(std::string& result,std::string& error)const
    {
        if(graphs.empty()){error="No Blueprint open.";return false;}
        JsonValue root;number(root,"version",2);text(root,"kind","NativeBlueprint");text(root,"name",name);
        auto* gs=root.put("graphs");gs->setArray();
        for(const auto& g:graphs)
        {
            auto* v=gs->add();text(*v,"name",g.name);auto* ns=v->put("nodes");ns->setArray();
            for(const auto& n:g.nodes)
            {
                auto* j=ns->add();text(*j,"id",n.id);text(*j,"kind",n.kind);
                number(*j,"x",n.x);number(*j,"y",n.y);
                if(n.kind.rfind("literal:",0)==0)text(*j,"value",n.ref);
            }
            auto* ls=v->put("links");ls->setArray();
            for(const auto& l:g.links)
            {
                if(l.from>=g.nodes.size()||l.to>=g.nodes.size()){error="Dangling connection.";return false;}
                auto* j=ls->add();text(*j,"from",g.nodes[l.from].id);text(*j,"to",g.nodes[l.to].id);
                text(*j,"output",l.output);text(*j,"input",l.input);
            }
        }
        auto output=root.write(false);BlueprintDocument check;
        if(!check.load(output.data(),output.size(),error))return false;
        result.swap(output);return true;
    }
    void BlueprintDocument::record(size_t graph)
    {
        if(m_undo.size()>=kHistory)m_undo.erase(m_undo.begin());
        m_undo.push_back({graph,graphs[graph],m_revision});m_redo.clear();m_revision=++m_nextRevision;
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
        auto& g=graphs[graph];std::string id;
        do{id="n"+std::to_string(++m_nextId);}while(std::any_of(g.nodes.begin(),g.nodes.end(),[&](const Node& n){return n.id==id;}));
        const std::string value=kind=="literal:string"?"":kind=="literal:bool"?"false":kind.rfind("literal:",0)==0?"0":"";
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
    bool BlueprintDocument::undo()
    {
        if(m_undo.empty())return false;
        auto e=std::move(m_undo.back());m_undo.pop_back();
        m_redo.push_back({e.graph,std::move(graphs[e.graph]),m_revision});graphs[e.graph]=std::move(e.state);m_revision=e.revision;return true;
    }
    bool BlueprintDocument::redo()
    {
        if(m_redo.empty())return false;
        auto e=std::move(m_redo.back());m_redo.pop_back();
        m_undo.push_back({e.graph,std::move(graphs[e.graph]),m_revision});graphs[e.graph]=std::move(e.state);m_revision=e.revision;return true;
    }
}
