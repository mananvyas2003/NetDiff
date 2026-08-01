#pragma once

#include "graph.h"
#include "interpreter.h"
#include "point_hash.h"
#include "graph_types.h"
#include <unordered_map>
#include <vector>

// Domain-Specific Records for relationships
struct WireRecord {
    VertexID start;
    VertexID end;
};

struct PinRecord {
    VertexID pin_vertex;
    VertexID wire_vertex;
};

struct LabelRecord {
    VertexID label_vertex;
    VertexID wire_vertex;
};

struct PowerRecord {
    VertexID power_vertex;
    VertexID wire_vertex;
};

class GraphBuilder {
public:
    Graph Build(const Schematic& schematic);

    VertexID GetOrCreateVertex(Graph& graph, const Point& p, VertexType type);

private:
    // Phase 1: Vertex Construction
    void BuildWireVertices(const Schematic& schematic, Graph& graph);
    void BuildJunctionVertices(const Schematic& schematic, Graph& graph);
    void BuildPinVertices(const Schematic& schematic, Graph& graph);
    void BuildLabelVertices(const Schematic& schematic, Graph& graph);

    // Phase 2: Edge (Relationship) Construction
    void BuildWireEdges(const Schematic& schematic, Graph& graph);
    void BuildPinEdges(const Schematic& schematic, Graph& graph);
    void BuildLabelEdges(const Schematic& schematic, Graph& graph);
    void BuildPowerEdges(const Schematic& schematic, Graph& graph);

    // Internal Builder State
    std::vector<WireRecord> wire_connections;

    std::unordered_map<Point, VertexID, PointHash, PointEqual> vertex_map;

    std::vector<VertexID> pin_vertices;
    std::vector<VertexID> label_vertices;
    std::vector<VertexID> power_vertices;
};