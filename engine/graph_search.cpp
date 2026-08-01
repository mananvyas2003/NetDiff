#include "graph_search.h"

void GraphSearch::ResetVisited(Graph& graph) {
    // Note: We need a way to modify vertices. 
    // Assuming Graph::GetVertices() returns const, we iterate via ID.
    for (size_t i = 0; i < graph.GetVertices().size(); ++i) {
        graph.GetVertex(static_cast<VertexID>(i)).visited = false;
    }
}

void GraphSearch::DFS(Graph& graph, VertexID current, std::vector<VertexID>& connected_nodes) {
    GraphVertex& vertex = graph.GetVertex(current);

    if (vertex.visited) return;

    vertex.visited = true;
    connected_nodes.push_back(current);

    // Recursively visit all adjacent vertices
    auto adjacent = graph.GetAdjacentVertices(current);
    for (VertexID neighbor : adjacent) {
        if (!graph.GetVertex(neighbor).visited) {
            DFS(graph, neighbor, connected_nodes);
        }
    }
}

std::vector<std::vector<VertexID>> GraphSearch::ExtractNets(Graph& graph) {
    std::vector<std::vector<VertexID>> all_nets;
    ResetVisited(graph);

    for (const auto& vertex : graph.GetVertices()) {
        if (!vertex.visited) {
            std::vector<VertexID> current_net;
            DFS(graph, vertex.id, current_net);

            // Only keep nets that actually connect multiple things
            if (current_net.size() > 1) {
                all_nets.push_back(current_net);
            }
        }
    }
    return all_nets;
}