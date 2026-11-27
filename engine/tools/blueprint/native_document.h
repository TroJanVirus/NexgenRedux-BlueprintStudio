#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace nx
{
    struct BlueprintPin { std::string name, type; };
    struct BlueprintNodeSpec
    {
        std::string id, title, category;
        std::vector<BlueprintPin> inputs, outputs;
        std::string call,property,result;
        bool member=false,pure=false,hasOut=false;
        // name=in/out is encoded as a pin pair; defaults are trusted generated expressions.
        std::vector<BlueprintPin> arguments, defaults;
        // Trusted builtin operator or cast from the native catalogue, never from document text.
        std::string operation{};
    };
    const std::vector<BlueprintNodeSpec>& blueprintCatalogue();
    const BlueprintNodeSpec* blueprintSpec(const std::string& kind);
    bool blueprintLiteralType(const std::string& type);
    // Managed authoring files are direct children of Redux's own folder.
    bool blueprintFileName(const std::string& file);
    bool blueprintManagedName(const std::string& name,std::string& file);
    bool blueprintAssetRoot(const std::string& root);
    bool blueprintAssetName(const std::string& name,bool folder=false);

    // Native version 2: one canonical model. Node indices are internal; persisted links use stable IDs.
    // The pre-release browser format is deliberately not supported.
    struct BlueprintDocument
    {
        struct InputValue { std::string pin,value; };
        struct Node { std::string id, kind, ref; float x=0,y=0; std::vector<InputValue> values{}; bool watch=false; };
        struct Link { size_t from=0,to=0; std::string output,input; };
        struct Field { std::string name,type,value; bool initialized=false; };
        struct Graph
        {
            std::string owner,name,kind;
            std::vector<Node> nodes;
            std::vector<Link> links;
            std::vector<BlueprintPin> parameters{};
            std::string returnType="void";
            // Explicit imported fields live on the owner lifecycle entry graph.
            std::vector<Field> fields{};
            std::vector<Field> globals{};
            std::string moduleOwner{}; // optional screen scope on Behaviour start graph
        };
        std::string name;
        std::string assetRoot; // external project directory, separate from graph history
        std::vector<Graph> graphs;
        struct ScreenManifest { std::string owner, source; };
        // Original screen.json bytes, optional for backwards-compatible v2 documents.
        std::vector<ScreenManifest> manifests;
        const std::string* screenManifest(const std::string& owner) const;
        bool importScreenManifest(const std::string& owner,const std::string& source,std::string& error);
        void create();
        bool load(const char* data,size_t size,std::string& error);
        bool serialize(std::string& result,std::string& error) const;
        bool move(size_t graph,size_t node,float x,float y);
        bool addNode(size_t graph,const std::string& kind,float x,float y,std::string& error);
        bool deleteNode(size_t graph,size_t node,std::string& error);
        bool connect(size_t graph,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error);
        bool disconnect(size_t graph,size_t node,const std::string& pin,bool output);
        bool setLiteral(size_t graph,size_t node,const std::string& value,std::string& error);
        bool toggleWatch(size_t graph,size_t node);
        bool setInputValue(size_t graph,size_t node,const std::string& pin,const std::string& value,std::string& error);
        bool clearInputValue(size_t graph,size_t node,const std::string& pin);
        bool setReference(size_t graph,size_t node,const std::string& value,std::string& error);
        bool addFunction(const std::string& name,std::string& error,const std::string& owner="Screen");
        const Graph* function(const std::string& owner,const std::string& name) const;
        bool nodeSpec(const Graph& graph,const Node& node,BlueprintNodeSpec& spec) const;
        bool setFunctionSignature(size_t graph,const std::vector<BlueprintPin>& parameters,const std::string& returnType,std::string& error);
        bool editFunctionSignature(size_t graph,const std::string& signature,std::string& error);
        std::string functionSignature(size_t graph) const;
        bool addScreen(const std::string& id,std::string& error);
        bool addLevelEvents(const std::string& owner,std::string& error);
        bool removeScreen(const std::string& id,std::string& error);
        std::vector<std::string> screens() const;
        std::vector<std::string> behaviours() const;
        bool addBehaviour(const std::string& name,std::string& error);
        bool removeBehaviour(const std::string& name,std::string& error);
        bool removeFunction(size_t graph,std::string& error);
        bool functions(std::string& error) const;
        // Named instance variables and function names are scoped to their owning screen.
        std::string moduleForOwner(const std::string& owner) const;
        const std::vector<Field>& moduleGlobals(const std::string& owner="") const;
        bool canAttach(const std::string& owner,const std::string& behaviour) const;
        bool variables(std::vector<BlueprintPin>& values,std::string& error,const std::string& owner="Screen") const;
        bool undo();
        bool redo();
        bool dirty()const{return m_revision!=m_savedRevision;}
        bool canUndo()const{return !m_undo.empty();}
        bool canRedo()const{return !m_redo.empty();}
        bool isProject()const{return screens().size()>1;}
        void markSaved(){m_savedRevision=m_revision;}
        size_t revision()const{return m_revision;}
        void markRecovered(){m_revision=++m_nextRevision;}
    private:
        struct Edit { size_t graph; Graph state; size_t revision; std::vector<Graph> all; std::vector<ScreenManifest> manifests{}; };
        void record(size_t graph);
        void recordGraphList();
        std::vector<Edit> m_undo,m_redo;
        size_t m_revision=0,m_savedRevision=0,m_nextRevision=0;
        unsigned m_nextId=0;
    };
}
