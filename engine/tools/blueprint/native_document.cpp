#include "tools/blueprint/native_document.h"
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

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

        bool append(BlueprintDocument& doc, Value bp, const std::string& owner, std::string& error)
        {
            const auto graphs = bp["graphs"];
            if (!bp.isObject() || !bp["version"].isNumber() || bp["version"].exact() != 1 ||
                !validName(bp["name"]) || !graphs.isArray() ||
                (std::strcmp(bp["kind"].text(), "Screen") != 0 && std::strcmp(bp["kind"].text(), "Behaviour") != 0))
            {
                error = "Unsupported Blueprint schema. Expected a version-1 Screen or Behaviour.";
                return false;
            }
            for (auto g = graphs.first(); g.exists(); g = g.next())
            {
                const auto nodes = g["nodes"], links = g["links"];
                if (doc.graphs.size() >= kMaxGraphs || !validName(g["name"]) || !validName(g["kind"]) ||
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
                        !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000 ||
                        !ids.emplace(n["id"].text(), graph.nodes.size()).second)
                    {
                        error = "Invalid or duplicate node in " + graph.name;
                        return false;
                    }
                    graph.nodes.push_back({n["id"].text(), n["kind"].text(), n["ref"].text(), static_cast<float>(x), static_cast<float>(y)});
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
        candidate.name = root["name"].text();
        if (std::strcmp(root["kind"].text(), "BlueprintProject") == 0)
        {
            const auto screens = root["screens"];
            if (!screens.isArray()) { error = "Project has no screen list."; return false; }
            for (auto screen = screens.first(); screen.exists(); screen = screen.next())
            {
                if (!validName(screen["id"])) { error = "Invalid screen id."; return false; }
                if (!append(candidate, screen["blueprint"], screen["id"].text(), error)) return false;
            }
        }
        else if (!append(candidate, root, candidate.name, error)) return false;
        if (candidate.graphs.empty()) { error = "No visual graphs to display."; return false; }
        *this = std::move(candidate);
        error.clear();
        return true;
    }
}
