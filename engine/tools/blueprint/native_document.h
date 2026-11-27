#pragma once
#include "base/json_tree.h"

#include <string>
#include <vector>

namespace nx
{
    // A read-only projection. Never serialize this view back over a project.
    struct BlueprintDocument
    {
        struct Node
        {
            std::string id, kind, ref;
            float x = 0, y = 0;
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
    };
}
