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
    };
    const std::vector<BlueprintNodeSpec>& blueprintCatalogue();
    const BlueprintNodeSpec* blueprintSpec(const std::string& kind);

    // Native version 2: one canonical model. Node indices are internal; persisted links use stable IDs.
    // The pre-release browser format is deliberately not supported.
    struct BlueprintDocument
    {
        struct Node { std::string id, kind, ref; float x=0,y=0; };
        struct Link { size_t from=0,to=0; std::string output,input; };
        struct Graph
        {
            std::string owner,name,kind;
            std::vector<Node> nodes;
            std::vector<Link> links;
        };
        std::string name;
        std::vector<Graph> graphs;
        void create();
        bool load(const char* data,size_t size,std::string& error);
        bool serialize(std::string& result,std::string& error) const;
        bool move(size_t graph,size_t node,float x,float y);
        bool addNode(size_t graph,const std::string& kind,float x,float y,std::string& error);
        bool deleteNode(size_t graph,size_t node,std::string& error);
        bool connect(size_t graph,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error);
        bool disconnect(size_t graph,size_t node,const std::string& pin,bool output);
        bool setLiteral(size_t graph,size_t node,const std::string& value,std::string& error);
        bool setReference(size_t graph,size_t node,const std::string& value,std::string& error);
        bool addFunction(const std::string& name,std::string& error);
        bool removeFunction(size_t graph,std::string& error);
        bool functions(std::string& error) const;
        // Collect named, per-instance variables. A name must have exactly one type across all graphs.
        bool variables(std::vector<BlueprintPin>& values,std::string& error) const;
        bool undo();
        bool redo();
        bool dirty()const{return m_revision!=m_savedRevision;}
        bool canUndo()const{return !m_undo.empty();}
        bool canRedo()const{return !m_redo.empty();}
        bool isProject()const{return false;}
        void markSaved(){m_savedRevision=m_revision;}
    private:
        struct Edit { size_t graph; Graph state; size_t revision; std::vector<Graph> all; };
        void record(size_t graph);
        void recordGraphList();
        std::vector<Edit> m_undo,m_redo;
        size_t m_revision=0,m_savedRevision=0,m_nextRevision=0;
        unsigned m_nextId=0;
    };
}
