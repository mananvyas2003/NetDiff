#pragma once
#include "graph.h"
#include <vector>

class GraphSearch {
public:
    // Clears the 'visited' flag on all vertices
    static void ResetVisited(Graph& graph);

    // Walks the graph from a starting vertex and collects all connected vertices
    static void DFS(Graph& graph, VertexID current, std::vector<VertexID>& connected_nodes);

    // Extracts every distinct electrical net in the schematic
    static std::vector<std::vector<VertexID>> ExtractNets(Graph& graph);
};