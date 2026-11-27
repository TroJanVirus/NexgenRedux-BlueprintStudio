#include "tools/blueprint/native_document.h"
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <algorithm>

namespace nx
{
    namespace
    {
        using Value = JsonTree::Value;
        constexpr size_t kMaxFile = 16 * 1024 * 1024;
        constexpr size_t kMaxGraphs = 512;
        constexpr size_t kMaxNodes = 4096;
        constexpr size_t kMaxLinks = 16384;

        bool validName(Value value)
        {
            return value.isString() && value.text()[0] && std::strlen(value.text()) <= 512;
        }

        bool oneKey(Value object, const char* key)
        {
            unsigned count = 0;
            for (auto child = object.first(); child.exists(); child = child.next())
                if (std::strcmp(child.key(), key) == 0) ++count;
            return count == 1;
        }

        bool append(BlueprintDocument& doc, Value bp, const std::string& owner, std::string& error, unsigned depth = 0)
        {
            const auto graphs = bp["graphs"];
            if (depth > 8 || !bp.isObject() || !oneKey(bp,"graphs") || !bp["version"].isNumber() || bp["version"].exact() != 1 ||
                !validName(bp["name"]) || !graphs.isArray() ||
                (std::strcmp(bp["kind"].text(), "Screen") != 0 && std::strcmp(bp["kind"].text(), "Behaviour") != 0))
            {
                error = "Unsupported Blueprint schema. Expected a version-1 Screen or Behaviour.";
                return false;
            }
            for (auto g = graphs.first(); g.exists(); g = g.next())
            {
                const auto nodes = g["nodes"], links = g["links"];
                if (doc.graphs.size() >= kMaxGraphs || !oneKey(g,"nodes") || !oneKey(g,"links") || !validName(g["name"]) || !validName(g["kind"]) ||
                    !nodes.isArray() || !links.isArray() || nodes.count() > kMaxNodes || links.count() > kMaxLinks)
                {
                    error = "Invalid graph or graph exceeds the native preview limits.";
                    return false;
                }
                BlueprintDocument::Graph graph;
                graph.owner = owner; graph.name = g["name"].text(); graph.kind = g["kind"].text();
                std::unordered_map<std::string, size_t> ids;
                for (auto n = nodes.first(); n.exists(); n = n.next())
                {
                    const double x = n["x"].exact(), y = n["y"].exact();
                    if (!validName(n["id"]) || !validName(n["kind"]) || !n["x"].isNumber() || !n["y"].isNumber() ||
                        !oneKey(n,"x") || !oneKey(n,"y") || !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000 ||
                        !ids.emplace(n["id"].text(), graph.nodes.size()).second)
                    {
                        error = "Invalid or duplicate node in " + graph.name;
                        return false;
                    }
                    graph.nodes.push_back({n["id"].text(), n["kind"].text(), n["ref"].text(), static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(x), static_cast<float>(y), n["x"].numberOffset(), n["y"].numberOffset()});
                }
                for (auto l = links.first(); l.exists(); l = l.next())
                {
                    const auto from = l["from"], to = l["to"];
                    const auto f = ids.find(from["node"].text()), t = ids.find(to["node"].text());
                    if (f == ids.end() || t == ids.end() || !validName(from["pin"]) || !validName(to["pin"]))
                    {
                        error = "Dangling or invalid link in " + graph.name;
                        return false;
                    }
                    graph.links.push_back({f->second, t->second, from["pin"].text(), to["pin"].text()});
                }
                doc.graphs.push_back(std::move(graph));
            }
            const auto parent = bp["parent"], components = bp["components"];
            if (parent.isObject() && (!oneKey(bp,"parent") || !append(doc,parent,owner+" / parent: "+parent["name"].text(),error,depth+1)))
            { if(error.empty()) error="Ambiguous parent."; return false; }
            if (components.exists() && !components.isArray()) { error="Invalid components."; return false; }
            if (components.isArray())
            {
                if (!oneKey(bp,"components")) { error="Ambiguous components."; return false; }
                for(auto component=components.first();component.exists();component=component.next())
                    if(!append(doc,component,owner+" / "+component["name"].text(),error,depth+1)) return false;
            }
            return true;
        }
    }

    bool BlueprintDocument::load(const char* data, size_t size, std::string& error)
    {
        if (!data || !size || size > kMaxFile || std::memchr(data, 0, size))
        {
            error = "Choose a non-empty Blueprint JSON file of at most 16 MiB.";
            return false;
        }
        JsonTree tree;
        if (!tree.parse(data, size))
        {
            error = "JSON line " + std::to_string(tree.errorLine()) + ": " + tree.error();
            return false;
        }
        const auto root = tree.root();
        if (!root.isObject() || !root["version"].isNumber() || root["version"].exact() != 1 || !validName(root["name"]))
        {
            error = "Not a supported version-1 Blueprint file.";
            return false;
        }
        BlueprintDocument candidate;
        error.clear();
        candidate.name = root["name"].text();
        if (std::strcmp(root["kind"].text(), "BlueprintProject") == 0)
        {
            const auto screens = root["screens"];
            candidate.m_project = true;
            if (!screens.isArray() || !oneKey(root,"screens")) { error = "Project has no unique screen list."; return false; }
            for (auto screen = screens.first(); screen.exists(); screen = screen.next())
            {
                if (!validName(screen["id"]) || !oneKey(screen,"blueprint")) { error = "Invalid screen id or ambiguous Blueprint."; return false; }
                if (!append(candidate, screen["blueprint"], screen["id"].text(), error)) return false;
            }
        }
        else if (!append(candidate, root, candidate.name, error)) return false;
        if (candidate.graphs.empty()) { error = "No visual graphs to display."; return false; }
        candidate.m_source.assign(data,size);
        *this = std::move(candidate);
        error.clear();
        return true;
    }

    bool BlueprintDocument::move(size_t graph, size_t node, float x, float y)
    {
        if (graph >= graphs.size() || node >= graphs[graph].nodes.size() ||
            !std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1000000 || std::abs(y)>1000000) return false;
        auto& n = graphs[graph].nodes[node];
        // Preserve untouched axes, including sub-millipixel values from old projects.
        if(x!=n.x)x=static_cast<float>(std::round(static_cast<double>(x)*1000)/1000);
        if(y!=n.y)y=static_cast<float>(std::round(static_cast<double>(y)*1000)/1000);
        if (n.x==x && n.y==y) return false;
        // One entry per completed drag, not per mouse update.
        if (m_undo.size()>=512) m_undo.erase(m_undo.begin());
        m_undo.push_back({graph,node,n.x,n.y,x,y,m_revision,++m_nextRevision});
        m_revision=m_nextRevision; m_redo.clear(); n.x=x; n.y=y;
        return true;
    }

    bool BlueprintDocument::undo()
    {
        if(m_undo.empty()) return false;
        const auto e=m_undo.back(); m_undo.pop_back();
        auto& n=graphs[e.graph].nodes[e.node]; n.x=e.x1;n.y=e.y1;
        m_revision=e.before;m_redo.push_back(e);return true;
    }

    bool BlueprintDocument::redo()
    {
        if(m_redo.empty()) return false;
        const auto e=m_redo.back();m_redo.pop_back();
        auto& n=graphs[e.graph].nodes[e.node];n.x=e.x2;n.y=e.y2;
        m_revision=e.after;m_undo.push_back(e);return true;
    }

    bool BlueprintDocument::serialize(std::string& result, std::string& error) const
    {
        if(m_source.empty()){error="No document open.";return false;}
        struct Patch { size_t start,end; std::string value; };
        std::vector<Patch> patches;
        auto add=[&](size_t offset,float value)
        {
            if(offset>=m_source.size())return false;
            // Number offsets come from the engine parser. Use the same number lexing
            // as it does, and locale-independent integer formatting for edited positions.
            size_t end=offset;
            while(end<m_source.size() && std::strchr("0123456789.eE+-",m_source[end]))++end;
            if(end==offset)return false;
            // Integer millipixels avoid locale-sensitive float formatting on older macOS libraries.
            const auto scaled=static_cast<long long>(std::round(static_cast<double>(value)*1000));
            const auto magnitude=scaled<0?-scaled:scaled;
            std::string number=(scaled<0?"-":"")+std::to_string(magnitude/1000);
            if(magnitude%1000)
            {
                number+="."+std::to_string(1000+magnitude%1000).substr(1);
                while(number.back()=='0')number.pop_back();
            }
            patches.push_back({offset,end,std::move(number)});return true;
        };
        for(const auto& graph:graphs)for(const auto& n:graph.nodes)
        {
            if((n.x!=n.originalX&&!add(n.xOffset,n.x))||(n.y!=n.originalY&&!add(n.yOffset,n.y)))
            {error="Could not locate original coordinate.";return false;}
        }
        std::sort(patches.begin(),patches.end(),[](const Patch& a,const Patch& b){return a.start<b.start;});
        std::string output; output.reserve(m_source.size());size_t at=0;
        for(const auto& p:patches)
        {
            if(p.start<at){error="Overlapping source coordinates.";return false;}
            output.append(m_source,at,p.start-at);output+=p.value;at=p.end;
        }
        output.append(m_source,at,std::string::npos);
        // Refuse to emit a document our own parser cannot reopen.
        BlueprintDocument check;
        if(!check.load(output.data(),output.size(),error))return false;
        result.swap(output);error.clear();return true;
    }
}
