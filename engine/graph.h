#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "graph_types.h"
#include "interpreter.h"

enum class VertexType {
    WireEndpoint,
    Junction,
    Pin,
    Label,
    PowerSymbol,
    NoConnect,
    Bus,
    SheetPort
};

enum class EdgeType {
    Wire,
    PinConnection,
    BusConnection,
    HiddenConnection,
    LabelConnection, // Added for BuildLabelEdges
    PowerConnection  // Added for BuildPowerEdges
};

struct GraphEdge {
    EdgeID id = INVALID_EDGE;
    VertexID source = INVALID_VERTEX;
    VertexID target = INVALID_VERTEX;

    EdgeType type = EdgeType::Wire;
    bool traversable = true;
    double length = 0.0;
};

struct GraphVertex {
    VertexID id = INVALID_VERTEX;
    Point position{ 0, 0 };
    VertexType type = VertexType::WireEndpoint;

    std::vector<EdgeID> edges;

    std::string label;
    std::string component;
    std::string pin;

    bool visited = false;
};

class Graph {
public:
    // Adding Elements
    VertexID AddVertex(const Point& position, VertexType type);
    EdgeID AddEdge(VertexID source, VertexID target, EdgeType type);

    // Accessors
    GraphVertex& GetVertex(VertexID id);
    const GraphVertex& GetVertex(VertexID id) const;

    GraphEdge& GetEdge(EdgeID id);
    const GraphEdge& GetEdge(EdgeID id) const;

    // Queries
    std::vector<VertexID> GetAdjacentVertices(VertexID vertex_id) const;

    const std::vector<GraphVertex>& GetVertices() const { return vertices; }
    const std::vector<GraphEdge>& GetEdges() const { return edges; }

private:
    std::vector<GraphVertex> vertices;
    std::vector<GraphEdge> edges;
};