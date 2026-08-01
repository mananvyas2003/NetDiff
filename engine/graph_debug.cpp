#include "graph_debug.h"
#include "graph_search.h"
#include <iostream>



void GraphDebug::PrintVertices(
    const Graph& graph)
{
    std::cout
        << "\n====================================\n";

    std::cout
        << "GRAPH VERTICES\n";

    std::cout
        << "====================================\n";

    for (const auto& vertex :
        graph.GetVertices())
    {
        std::cout
            << "\nVertex "
            << vertex.id
            << "\n";

        std::cout
            << "Position : "
            << vertex.position.x
            << ", "
            << vertex.position.y
            << "\n";

        std::cout
            << "Type : ";

        switch (vertex.type)
        {
        case VertexType::WireEndpoint:
            std::cout << "WireEndpoint";
            break;

        case VertexType::Junction:
            std::cout << "Junction";
            break;

        case VertexType::Pin:
            std::cout << "Pin";
            break;

        case VertexType::Label:
            std::cout << "Label";
            break;

        case VertexType::PowerSymbol:
            std::cout << "Power";
            break;

        case VertexType::NoConnect:
            std::cout << "NoConnect";
            break;

        case VertexType::Bus:
            std::cout << "Bus";
            break;

        case VertexType::SheetPort:
            std::cout << "SheetPort";
            break;
        }

        std::cout << "\n";

        std::cout
            << "Connected Edges : "
            << vertex.edges.size()
            << "\n";
    }
}

void GraphDebug::PrintEdges(
    const Graph& graph)
{
    std::cout
        << "\n====================================\n";

    std::cout
        << "GRAPH EDGES\n";

    std::cout
        << "====================================\n";

    for (const auto& edge :
        graph.GetEdges())
    {
        std::cout
            << "\nEdge "
            << edge.id
            << "\n";

        std::cout
            << "Source : "
            << edge.source
            << "\n";

        std::cout
            << "Target : "
            << edge.target
            << "\n";

        std::cout
            << "Type : ";

        switch (edge.type)
        {
        case EdgeType::Wire:
            std::cout << "Wire";
            break;

        case EdgeType::PinConnection:
            std::cout << "Pin";
            break;

        case EdgeType::BusConnection:
            std::cout << "Bus";
            break;

        case EdgeType::HiddenConnection:
            std::cout << "Hidden";
            break;

        case EdgeType::LabelConnection:
            std::cout << "Label";
            break;

        case EdgeType::PowerConnection:
            std::cout << "Power";
            break;
        }

        std::cout << "\n";
    }
}


void GraphDebug::PrintAdjacency(
    const Graph& graph)
{
    std::cout
        << "\n====================================\n";

    std::cout
        << "GRAPH ADJACENCY\n";

    std::cout
        << "====================================\n";

    for (const auto& vertex :
        graph.GetVertices())
    {
        std::cout
            << "\nVertex "
            << vertex.id
            << "\n";

        auto adjacent =
            graph.GetAdjacentVertices(
                vertex.id);

        for (uint32_t id :
        adjacent)
        {
            std::cout
                << "   -> "
                << id
                << "\n";
        }
    }
}



std::string GraphDebug::VertexTypeToString(VertexType type) {
    switch (type) {
    case VertexType::WireEndpoint: return "Wire";
    case VertexType::Junction:     return "Junc";
    case VertexType::Pin:          return "Pin ";
    case VertexType::Label:        return "Labl";
    default:                       return "Unkn";
    }
}

void GraphDebug::PrintTopology(const Graph& graph) {
    std::cout << "\n========================================\n";
    std::cout << "          GRAPH TOPOLOGY DUMP           \n";
    std::cout << "========================================\n";

    for (const auto& vertex : graph.GetVertices()) {
        std::cout << "[" << vertex.id << "] "
            << VertexTypeToString(vertex.type) << " | "
            << "(" << vertex.position.x << "," << vertex.position.y << ")";

        if (vertex.type == VertexType::Pin) {
            std::cout << " | Ref: " << vertex.component << " Pin: " << vertex.pin;
        }
        else if (vertex.type == VertexType::Label) {
            std::cout << " | Name: " << vertex.label;
        }

        std::cout << "\n      Adjacent -> ";
        if (vertex.edges.empty()) {
            std::cout << "(None - FLOATING)";
        }
        else {
            for (EdgeID edge_id : vertex.edges) {
                const auto& edge = graph.GetEdge(edge_id);
                VertexID neighbor = (edge.source == vertex.id) ? edge.target : edge.source;
                std::cout << neighbor << " ";
            }
        }
        std::cout << "\n";
    }
    std::cout << "========================================\n\n";
}
void GraphDebug::PrintNets(Graph& graph) {
    std::cout << "\n========================================\n";
    std::cout << "          ELECTRICAL NETS EXTRACTED     \n";
    std::cout << "========================================\n";

    auto nets = GraphSearch::ExtractNets(graph);
    int net_index = 1;

    for (const auto& net : nets) {
        std::cout << "NET " << net_index++ << " routes through " << net.size() << " nodes:\n  -> ";

        for (size_t i = 0; i < net.size(); ++i) {
            const auto& v = graph.GetVertex(net[i]);

            if (v.type == VertexType::Pin) {
                std::cout << "[" << v.component << "." << v.pin << "]";
            }
            else if (v.type == VertexType::Label) {
                std::cout << "[Label:" << v.label << "]";
            }
            else if (v.type == VertexType::WireEndpoint) {
                std::cout << "Wire";
            }
            else if (v.type == VertexType::Junction) {
                std::cout << "Junc";
            }

            if (i < net.size() - 1) std::cout << " -- ";
        }
        std::cout << "\n\n";
    }
}

void GraphDebug::ExportToDOT(const Graph& graph) {
    std::cout << "\n========================================\n";
    std::cout << " COPY PASTE INTO: http://magjac.com/graphviz/ \n";
    std::cout << "========================================\n\n";

    std::cout << "graph Schematic {\n";
    std::cout << "  node [fontname=\"Arial\"];\n";

    // Print Nodes
    for (const auto& vertex : graph.GetVertices()) {
        std::cout << "  " << vertex.id << " [";
        if (vertex.type == VertexType::Pin) {
            std::cout << "label=\"" << vertex.component << "." << vertex.pin << "\", shape=box, style=filled, fillcolor=lightblue";
        }
        else if (vertex.type == VertexType::Label) {
            std::cout << "label=\"" << vertex.label << "\", shape=hexagon, style=filled, fillcolor=yellow";
        }
        else {
            std::cout << "label=\"\", shape=circle, width=0.1, height=0.1, style=filled, fillcolor=black";
        }
        std::cout << "];\n";
    }

    // Print Edges
    for (const auto& edge : graph.GetEdges()) {
        std::cout << "  " << edge.source << " -- " << edge.target << ";\n";
    }
    std::cout << "}\n\n";
}
