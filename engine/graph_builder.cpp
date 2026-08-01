#include "graph_builder.h"
#include <iostream>
#include <limits>

Graph GraphBuilder::Build(const Schematic& schematic) {
    Graph graph;

    // Clear state
    vertex_map.clear();
    wire_connections.clear();
    pin_vertices.clear();
    label_vertices.clear();

    // Phase 1: Physical Nodes
    BuildWireVertices(schematic, graph);
    BuildJunctionVertices(schematic, graph);
    BuildPinVertices(schematic, graph);
    BuildLabelVertices(schematic, graph);

    // Phase 2: Electrical Relationships
    BuildWireEdges(schematic, graph);
    BuildPinEdges(schematic, graph);
    BuildLabelEdges(schematic, graph);

    return graph;
}

VertexID GraphBuilder::GetOrCreateVertex(Graph& graph, const Point& point, VertexType type) {
    auto it = vertex_map.find(point);
    if (it != vertex_map.end()) {
        return it->second;
    }

    VertexID new_id = graph.AddVertex(point, type);
    vertex_map[point] = new_id;
    return new_id;
}

// ---------------------------------------------------------
// PHASE 1: VERTEX BUILDERS
// ---------------------------------------------------------

void GraphBuilder::BuildWireVertices(const Schematic& schematic, Graph& graph) {
    for (const auto& wire : schematic.wires) {
        VertexID start = GetOrCreateVertex(graph, wire.start, VertexType::WireEndpoint);
        VertexID end = GetOrCreateVertex(graph, wire.end, VertexType::WireEndpoint);
        wire_connections.push_back({ start, end });
    }
}

void GraphBuilder::BuildJunctionVertices(const Schematic& schematic, Graph& graph) {
    for (const auto& junction : schematic.junctions) {
        GetOrCreateVertex(graph, junction.location, VertexType::Junction);
    }
}

void GraphBuilder::BuildPinVertices(const Schematic& schematic, Graph& graph) {
    for (const auto& comp : schematic.components) {
        for (const auto& pin : comp.pins) {
            // Note: using world_location as defined in your struct
            VertexID id = GetOrCreateVertex(graph, pin.world_location, VertexType::Pin);

            GraphVertex& vertex = graph.GetVertex(id);
            vertex.component = comp.reference;
            vertex.pin = pin.number;

            pin_vertices.push_back(id);
        }
    }
}

void GraphBuilder::BuildLabelVertices(const Schematic& schematic, Graph& graph) {
    for (const auto& label : schematic.labels) {
        VertexID id = GetOrCreateVertex(graph, label.location, VertexType::Label);

        GraphVertex& vertex = graph.GetVertex(id);
        vertex.label = label.name; // using 'name' from NetLabel

        label_vertices.push_back(id);
    }
}

// ---------------------------------------------------------
// TEMPORARY HELPER: FindNearestWire
// ---------------------------------------------------------
VertexID FindNearestWire(const Graph& graph, const Point& target) {
    VertexID nearest = INVALID_VERTEX;
    double min_dist = std::numeric_limits<double>::max();

    for (const auto& vertex : graph.GetVertices()) {
        if (vertex.type == VertexType::WireEndpoint || vertex.type == VertexType::Junction) {
            double dx = vertex.position.x - target.x;
            double dy = vertex.position.y - target.y;
            double dist_sq = (dx * dx) + (dy * dy);

            if (dist_sq < min_dist) {
                min_dist = dist_sq;
                nearest = vertex.id;
            }
        }
    }
    return nearest;
}

// ---------------------------------------------------------
// PHASE 2: EDGE BUILDERS
// ---------------------------------------------------------

void GraphBuilder::BuildWireEdges(const Schematic&, Graph& graph) {
    for (const auto& wire : wire_connections) {
        graph.AddEdge(wire.start, wire.end, EdgeType::Wire);
    }
}

void GraphBuilder::BuildPinEdges(const Schematic&, Graph& graph) {
    for (VertexID pin_id : pin_vertices) {
        const GraphVertex& pin_vertex = graph.GetVertex(pin_id);

        VertexID wire_id = FindNearestWire(graph, pin_vertex.position);
        if (wire_id != INVALID_VERTEX) {
            graph.AddEdge(pin_id, wire_id, EdgeType::PinConnection);
        }
    }
}

void GraphBuilder::BuildLabelEdges(const Schematic&, Graph& graph) {
    for (VertexID label_id : label_vertices) {
        const GraphVertex& label_vertex = graph.GetVertex(label_id);

        VertexID wire_id = FindNearestWire(graph, label_vertex.position);
        if (wire_id != INVALID_VERTEX) {
            graph.AddEdge(label_id, wire_id, EdgeType::LabelConnection);
        }
    }
}