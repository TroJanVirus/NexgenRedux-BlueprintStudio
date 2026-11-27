#pragma once
#include "base/json_tree.h"

#include <string>
#include <vector>

namespace nx
{
    // Editable layout projection. Original JSON is retained; only changed coordinate tokens are replaced.
    struct BlueprintDocument
    {
        struct Node
        {
            std::string id, kind, ref;
            float x = 0, y = 0;
            float originalX = 0, originalY = 0;
            size_t xOffset = 0, yOffset = 0;
        };
        struct Link
        {
            size_t from = 0, to = 0;
            std::string output, input;
        };
        struct Graph
        {
            std::string owner, name, kind;
            std::vector<Node> nodes;
            std::vector<Link> links;
        };
        std::string name;
        std::vector<Graph> graphs;
        // Failure leaves the currently open document untouched.
        bool load(const char* data, size_t size, std::string& error);
        bool move(size_t graph, size_t node, float x, float y);
        bool undo();
        bool redo();
        bool dirty() const { return m_revision != m_savedRevision; }
        bool canUndo() const { return !m_undo.empty(); }
        bool canRedo() const { return !m_redo.empty(); }
        bool isProject() const { return m_project; }
        void markSaved() { m_savedRevision = m_revision; }
        bool serialize(std::string& result, std::string& error) const;
    private:
        struct Edit { size_t graph, node; float x1, y1, x2, y2; size_t before, after; };
        std::string m_source;
        std::vector<Edit> m_undo, m_redo;
        size_t m_revision = 0, m_savedRevision = 0, m_nextRevision = 0;
        bool m_project = false;
    };
}
