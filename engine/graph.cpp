#include "graph.h"

VertexID Graph::AddVertex(const Point& position, VertexType type) {
    VertexID id = static_cast<VertexID>(vertices.size());

    GraphVertex vertex;
    vertex.id = id;
    vertex.position = position;
    vertex.type = type;

    vertices.push_back(vertex);
    return id;
}

EdgeID Graph::AddEdge(VertexID source, VertexID target, EdgeType type) {
    EdgeID id = static_cast<EdgeID>(edges.size());

    GraphEdge edge;
    edge.id = id;
    edge.source = source;
    edge.target = target;
    edge.type = type;

    edges.push_back(edge);

    // Update adjacency lists for both vertices
    vertices[source].edges.push_back(id);
    vertices[target].edges.push_back(id);

    return id;
}

GraphVertex& Graph::GetVertex(VertexID id) {
    return vertices.at(id);
}

const GraphVertex& Graph::GetVertex(VertexID id) const {
    return vertices.at(id);
}

GraphEdge& Graph::GetEdge(EdgeID id) {
    return edges.at(id);
}

const GraphEdge& Graph::GetEdge(EdgeID id) const {
    return edges.at(id);
}



std::vector<VertexID> Graph::GetAdjacentVertices(VertexID vertex_id) const {
    std::vector<VertexID> adjacent;
    const auto& vertex = vertices.at(vertex_id);

    for (EdgeID edge_id : vertex.edges) {
        const auto& edge = edges.at(edge_id);

        if (edge.source == vertex_id) {
            adjacent.push_back(edge.target);
        }
        else {
            adjacent.push_back(edge.source);
        }
    }

    return adjacent;
}