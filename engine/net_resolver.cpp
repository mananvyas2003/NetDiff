// ============================================================
//  net_resolver.cpp
//  Unified Geometric Intersection & Connectivity Net Resolver
// ============================================================

#include "net_resolver.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm> // Required library

namespace {
    // ----- JSON string escaping helper -----------------------
    std::string EscapeJsonString(const std::string& input)
    {
        std::stringstream ss;
        for (char c : input) {
            switch (c) {
            case '\\': ss << "\\\\"; break;
            case '"':  ss << "\\\""; break;
            case '\b': ss << "\\b";  break;
            case '\f': ss << "\\f";  break;
            case '\n': ss << "\\n";  break;
            case '\r': ss << "\\r";  break;
            case '\t': ss << "\\t";  break;
            default:
                if (c >= 0 && c < 32) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                }
                else {
                    ss << c;
                }
                break;
            }
        }
        return ss.str();
    }
}

// ------------------------------------------------------------
// Constructor
// ------------------------------------------------------------
NetResolver::NetResolver(const Schematic& sch)
    : schematic(sch)
{
}

// ------------------------------------------------------------
// MakeKey
// Converts double coordinates into 1/1000 mil integer grid keys
// ------------------------------------------------------------
PointKey NetResolver::MakeKey(const Point& p) const
{
    PointKey key;
    key.x = static_cast<int>(std::round(p.x * 1000.0));
    key.y = static_cast<int>(std::round(p.y * 1000.0));
    return key;
}

// ------------------------------------------------------------
// GetOrCreateNode
// Spatial allocation manager inside our flat arena pool
// ------------------------------------------------------------
uint32_t NetResolver::GetOrCreateNode(const Point& point)
{
    PointKey key = MakeKey(point);
    auto it = point_to_node.find(key);
    if (it != point_to_node.end())
    {
        return it->second;
    }

    ConnectivityNode node;
    node.id = static_cast<uint32_t>(nodes.size());
    node.position = point;

    nodes.push_back(node);
    point_to_node[key] = node.id; 
    return node.id;
}


void NetResolver::AttachPinsToNodes()
{
    constexpr double SNAP_DISTANCE = 0.05;

    // ----------------------------------------------------
    // Dump first few nodes once
    // ----------------------------------------------------

    static bool first_dump = true;

    if (first_dump)
    {
        first_dump = false;

        std::cout
            << "\n====================\n"
            << "NODE DUMP\n"
            << "====================\n";

        for (size_t i = 0;
            i < nodes.size() && i < 20;
            i++)
        {
            std::cout
                << nodes[i].position.x
                << ", "
                << nodes[i].position.y
                << "\n";
        }
    }

    // ----------------------------------------------------
    // Attach every pin
    // ----------------------------------------------------

    for (const auto& comp : schematic.components)
    {
        // Debug ESP32 only
        if (comp.reference == "U1")
        {
            std::cout
                << "\n====================\n"
                << "ESP32 PIN DUMP\n"
                << "====================\n";

            for (size_t i = 0;
                i < comp.pins.size() && i < 10;
                i++)
            {
                std::cout
                    << comp.pins[i].number
                    << " "
                    << comp.pins[i].name
                    << " -> "
                    << comp.pins[i].location.x
                    << ", "
                    << comp.pins[i].location.y
                    << "\n";
            }
        }

        for (const auto& pin : comp.pins)
        {
            uint32_t best_node =
                UINT32_MAX;

            double best_distance =
                1e9;

            // ------------------------------------
            // Find nearest node
            // ------------------------------------

            for (const auto& node : nodes)
            {
                double dx =
                    node.position.x -
                    pin.location.x;

                double dy =
                    node.position.y -
                    pin.location.y;

                double dist =
                    std::sqrt(
                        dx * dx +
                        dy * dy);

                if (dist < best_distance)
                {
                    best_distance =
                        dist;

                    best_node =
                        node.id;
                }
            }

            // ------------------------------------
            // Debug failures
            // ------------------------------------

            if (best_node == UINT32_MAX)
            {
                std::cout
                    << "NO NODE FOUND FOR PIN "
                    << comp.reference
                    << " "
                    << pin.name
                    << "\n";

                continue;
            }

            if (best_distance > SNAP_DISTANCE)
            {
                std::cout
                    << "PIN MISS : "
                    << comp.reference
                    << " "
                    << pin.name
                    << " Dist="
                    << best_distance
                    << "\n";

                continue;
            }

            // ------------------------------------
            // Attach component to node
            // ------------------------------------

            std::string desc =
                comp.reference;

            if (!comp.value.empty())
            {
                desc +=
                    " (" +
                    comp.value +
                    ")";
            }

            
            ConnectivityNode& target =
                nodes.at(best_node);

            target.connected_components.push_back(
                desc);

            target.is_pin = true;
            target.pin_name = pin.name;
            target.component_ref = comp.reference;

            nodes[best_node].is_pin = true;

            nodes[best_node].pin_name =
                pin.name;

            nodes[best_node].component_ref =
                comp.reference;

            std::cout
                << "PIN ATTACHED : "
                << comp.reference
                << " "
                << pin.name
                << " -> Node "
                << best_node
                << " Dist="
                << best_distance
                << "\n";
        }
    }


    std::cout
        << "\n====================\n"
        << "NODE COMPONENT CHECK\n"
        << "====================\n";

    for (const auto& node : nodes)
    {
        if (!node.connected_components.empty())
        {
            std::cout
                << "Node "
                << node.id
                << " Components="
                << node.connected_components.size()
                << "\n";
        }
    }

    std::cout
        << "\nPIN ATTACHMENT COMPLETE\n";
}


// ------------------------------------------------------------
// Resolve
// Unified sequential execution method to run geometric intersections
// ------------------------------------------------------------
void NetResolver::Resolve()
{
    nodes.clear();
    edges.clear();
    nets.clear();
    point_to_node.clear();

    // ----------------------------------------
    // Build Wire Graph
    // ----------------------------------------

    for (const auto& wire : schematic.wires)
    {
        uint32_t start_id =
            GetOrCreateNode(
                wire.start);

        uint32_t end_id =
            GetOrCreateNode(
                wire.end);

        if (start_id == end_id)
            continue;

        Edge edge;

        edge.id =
            static_cast<uint32_t>(
                edges.size());

        edge.start_node =
            start_id;

        edge.end_node =
            end_id;

        edges.push_back(edge);
    }

    // ----------------------------------------
    // Explicit Junctions
    // ----------------------------------------

    for (const auto& junction :
        schematic.junctions)
    {
        GetOrCreateNode(
            junction.location);
    }

    // ----------------------------------------
    // Labels
    // ----------------------------------------

    for (const auto& label :
        schematic.labels)
    {
        uint32_t node_id =
            GetOrCreateNode(
                label.location);

        if (!label.name.empty())
        {
            nodes[node_id]
                .labels
                .push_back(
                    label.name);
        }
    }

    // ----------------------------------------
    // Components
    // ----------------------------------------

    AttachPinsToNodes();


    // ----------------------------------------
    // DSU Init
    // ----------------------------------------

    parent.resize(nodes.size());
    rank.resize(nodes.size());

    for (uint32_t i = 0;
        i < nodes.size();
        ++i)
    {
        parent[i] = i;
        rank[i] = 0;
    }

    // ----------------------------------------
    // Wire Connectivity
    // ----------------------------------------

    for (const auto& edge :
        edges)
    {
        Union(
            edge.start_node,
            edge.end_node);
    }

    // ----------------------------------------
    // Junction Connectivity
    // ----------------------------------------

    for (const auto& junction :
        schematic.junctions)
    {
        PointKey key =
            MakeKey(
                junction.location);

        auto it =
            point_to_node.find(
                key);

        if (it ==
            point_to_node.end())
        {
            continue;
        }

        uint32_t junction_node =
            it->second;

        for (const auto& edge :
            edges)
        {
            if (edge.start_node ==
                junction_node)
            {
                Union(
                    junction_node,
                    edge.end_node);
            }

            if (edge.end_node ==
                junction_node)
            {
                Union(
                    junction_node,
                    edge.start_node);
            }
        }
    }

    // ----------------------------------------
    // Build Nets
    // ----------------------------------------

    BuildNets();

    // ----------------------------------------
    // Merge Same-Name Nets
    // ----------------------------------------

    MergeNamedNets();

    // ----------------------------------------
    // Rebuild After Label Merge
    // ----------------------------------------

    BuildNets();

    // ----------------------------------------
    // Debug
    // ----------------------------------------

    std::unordered_set<uint32_t>
        roots;

    for (uint32_t i = 0;
        i < nodes.size();
        ++i)
    {
        roots.insert(
            Find(i));
    }

    std::cout
        << "\nUnique DSU Roots : "
        << roots.size()
        << "\n";
}

// ------------------------------------------------------------
// Disjoint-Set Operations (Find / Union)
// ------------------------------------------------------------
uint32_t NetResolver::Find(uint32_t x)
{
    if (parent[x] == x)
    {
        return x;
    }
    return parent[x] = Find(parent[x]); // Path compression optimization
}

void NetResolver::Union(uint32_t a, uint32_t b)
{
    uint32_t root_a = Find(a);
    uint32_t root_b = Find(b);

    if (root_a == root_b) return;

    // Union by rank optimization
    if (rank[root_a] < rank[root_b])
    {
        parent[root_a] = root_b;
    }
    else if (rank[root_a] > rank[root_b])
    {
        parent[root_b] = root_a;
    }
    else
    {
        parent[root_b] = root_a;
        rank[root_a]++;
    }
}

// ------------------------------------------------------------
// BuildNets
// Group connected nodes and collect unique tracking labels
// ------------------------------------------------------------

void NetResolver::BuildNets()
{
    nets.clear();

    std::unordered_map<
        uint32_t,
        uint32_t> root_to_net;

    for (const auto& node : nodes)
    {
        uint32_t root =
            Find(node.id);

        auto it =
            root_to_net.find(root);

        if (it == root_to_net.end())
        {
            Net net;

            net.id =
                static_cast<uint32_t>(
                    nets.size());

            net.root_node =
                root;

            nets.push_back(net);

            root_to_net[root] =
                net.id;

            it =
                root_to_net.find(root);
        }

        Net& net =
            nets[it->second];

        net.node_ids.push_back(
            node.id);

        std::cout
            << "Node "
            << node.id
            << " Labels="
            << node.labels.size()
            << " Components="
            << node.connected_components.size()
            << "\n";

        // ---------------------------------
        // Labels
        // ---------------------------------

        for (const auto& label :
            node.labels)
        {
            bool exists = false;

            for (const auto& existing :
                net.labels)
            {
                if (existing == label)
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
            {
                net.labels.push_back(
                    label);

                if (net.name.empty())
                {
                    net.name =
                        label;
                }
            }
        }

        // ---------------------------------
        // Components
        // ---------------------------------

        for (const auto& comp :
            node.connected_components)
        {
            bool exists = false;

            for (const auto& existing :
                net.components)
            {
                if (existing == comp)
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
            {
                net.components.push_back(
                    comp);

                std::cout
                    << "  +COMP "
                    << comp
                    << "\n";
            }
        }
    }

    std::cout
        << "\nBuildNets Complete\n";

    std::cout
        << "Total Nets : "
        << nets.size()
        << "\n";
}

void NetResolver::MergeNamedNets()
{
    std::unordered_map<
        std::string,
        uint32_t> label_root;

    for (const auto& net : nets)
    {
        if (net.labels.empty())
            continue;

        uint32_t root =
            net.root_node;

        for (const auto& label :
            net.labels)
        {
            auto it =
                label_root.find(label);

            if (it == label_root.end())
            {
                label_root[label] =
                    root;
            }
            else
            {
                Union(
                    root,
                    it->second);
            }
        }
    }
}



// ------------------------------------------------------------
// Serialized File Exporters & Getters
// ------------------------------------------------------------
void NetResolver::ExportDashboardData(const std::string& outputPath) const
{
    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        std::cerr << "[Error] Failed to open export path: " << outputPath << "\n";
        return;
    }

    out << "{\n  \"wires\": [\n";
    for (size_t i = 0; i < schematic.wires.size(); ++i) {
        out << "    {\"start\": {\"x\":" << schematic.wires[i].start.x << ",\"y\":" << schematic.wires[i].start.y
            << "}, \"end\": {\"x\":" << schematic.wires[i].end.x << ",\"y\":" << schematic.wires[i].end.y << "}}";
        if (i + 1 < schematic.wires.size()) out << ",";
        out << "\n";
    }

    out << "  ],\n  \"junctions\": [\n";
    for (size_t i = 0; i < schematic.junctions.size(); ++i) {
        out << "    {\"x\":" << schematic.junctions[i].location.x << ",\"y\":" << schematic.junctions[i].location.y << "}";
        if (i + 1 < schematic.junctions.size()) out << ",";
        out << "\n";
    }

    out << "  ],\n  \"components\": [\n";
    for (size_t i = 0; i < schematic.components.size(); ++i) {
        out << "    {\n"
            << "      \"reference\": \"" << EscapeJsonString(schematic.components[i].reference) << "\",\n"
            << "      \"value\": \"" << EscapeJsonString(schematic.components[i].value) << "\",\n"
            << "      \"location\": {\"x\":" << schematic.components[i].location.x << ",\"y\":" << schematic.components[i].location.y << "},\n"
            << "      \"rotation\": " << schematic.components[i].rotation << "\n"
            << "    }";
        if (i + 1 < schematic.components.size()) out << ",";
        out << "\n";
    }

    out << "  ],\n  \"nodes\": [\n";
    for (size_t i = 0; i < nodes.size(); ++i) {
        out << "    {\n"
            << "      \"id\": " << nodes[i].id << ",\n"
            << "      \"x\": " << nodes[i].position.x << ",\n"
            << "      \"y\": " << nodes[i].position.y << ",\n"
            << "      \"labels\": [";
        for (size_t j = 0; j < nodes[i].labels.size(); ++j) {
            out << "\"" << EscapeJsonString(nodes[i].labels[j]) << "\"";
            if (j + 1 < nodes[i].labels.size()) out << ",";
        }
        out << "],\n"
            << "      \"components\": [";
        for (size_t j = 0; j < nodes[i].connected_components.size(); ++j) {
            out << "\"" << EscapeJsonString(nodes[i].connected_components[j]) << "\"";
            if (j + 1 < nodes[i].connected_components.size()) out << ",";
        }
        out << "]\n    }";
        if (i + 1 < nodes.size()) out << ",";
        out << "\n";
    }

    out << "  ],\n  \"edges\": [\n";
    for (size_t i = 0; i < edges.size(); ++i) {
        out << "    {\"id\":" << edges[i].id << ",\"start_node\":" << edges[i].start_node << ",\"end_node\":" << edges[i].end_node << "}";
        if (i + 1 < edges.size()) out << ",";
        out << "\n";
    }

    out << "  ],\n  \"nets\": [\n";
    for (size_t i = 0; i < nets.size(); ++i) {
        const Net& net = nets[i];
        out << "    {\n"
            << "      \"id\": " << net.id << ",\n"
            << "      \"root_node\": " << net.root_node << ",\n"
            << "      \"node_count\": " << net.node_ids.size() << ",\n"
            << "      \"node_ids\": [";
        for (size_t j = 0; j < net.node_ids.size(); ++j) {
            out << net.node_ids[j];
            if (j + 1 < net.node_ids.size()) out << ",";
        }
        out << "],\n"
            << "      \"labels\": [";
        for (size_t j = 0; j < net.labels.size(); ++j) {
            out << "\"" << EscapeJsonString(net.labels[j]) << "\"";
            if (j + 1 < net.labels.size()) out << ",";
        }
        out << "],\n"
            << "      \"components\": [";
        for (size_t j = 0; j < net.components.size(); ++j) {
            out << "\"" << EscapeJsonString(net.components[j]) << "\"";
            if (j + 1 < net.components.size()) out << ",";
        }
        out << "]\n    }";
        if (i + 1 < nets.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    out.close();
}

const std::vector<ConnectivityNode>& NetResolver::GetNodes() const { return nodes; }
const std::vector<Edge>& NetResolver::GetEdges() const { return edges; }
const std::vector<Net>& NetResolver::GetNets() const { return nets; }

// ------------------------------------------------------------
// Terminal Diagnostic Log Utilities
// ------------------------------------------------------------
void NetResolver::PrintNodes() const
{
    std::cout << "\n===================================\n";
    std::cout << "CONNECTIVITY NODES (" << nodes.size() << ")\n";
    std::cout << "===================================\n";
    for (const auto& node : nodes)
    {
        std::cout << "Node " << node.id << " -> (" << node.position.x << ", " << node.position.y << ")";
        if (!node.labels.empty())
        {
            std::cout << " [Labels:";
            for (const auto& lbl : node.labels) std::cout << " " << lbl;
            std::cout << "]";
        }
        if (!node.connected_components.empty())
        {
            std::cout << " [Comps:";
            for (const auto& comp : node.connected_components) std::cout << " " << comp;
            std::cout << "]";
        }
        std::cout << "\n";
    }
}



void NetResolver::PrintEdges() const
{
    std::cout << "\n===================================\n";
    std::cout << "CONNECTIVITY EDGES (" << edges.size() << ")\n";
    std::cout << "===================================\n";
    for (const auto& edge : edges)
    {
        std::cout << "Edge " << edge.id << " : Node " << edge.start_node << " <--> Node " << edge.end_node << "\n";
    }
}


void NetResolver::PrintNets() const
{
    std::cout
        << "\n===================================\n";

    std::cout
        << "NETS ("
        << nets.size()
        << ")\n";

    std::cout
        << "===================================\n";

    for (const auto& net : nets)
    {
        std::cout
            << "\nNet "
            << net.id
            << " : "
            << net.name
            << "\n";

        std::cout
            << "  Root Node : "
            << net.root_node
            << "\n";

        std::cout
            << "  Nodes     : "
            << net.node_ids.size()
            << "\n";

        std::cout
            << "  Labels    : "
            << net.labels.size()
            << "\n";

        std::cout
            << "  Components: "
            << net.components.size()
            << "\n";
    }
}



size_t NetResolver::GetLargestNetSize() const
{
    size_t largest = 0;

    for (const auto& net : nets)
    {
        if (net.node_ids.size() > largest)
        {
            largest =
                net.node_ids.size();
        }
    }

    return largest;
}

void NetResolver::ExportNetsJSON(
    const std::string& path) const
{
    std::ofstream file(path);

    if (!file.is_open())
        return;

    file << "{\n";
    file << "  \"nets\": [\n";

    for (size_t i = 0;
        i < nets.size();
        ++i)
    {
        const Net& net =
            nets[i];

        file << "    {\n";

        file << "      \"id\": "
            << net.id
            << ",\n";

        file << "      \"name\": \""
            << net.name
            << "\",\n";

        file << "      \"node_count\": "
            << net.node_ids.size()
            << ",\n";

        file << "      \"component_count\": "
            << net.components.size()
            << "\n";

        file << "    }";

        if (i + 1 < nets.size())
            file << ",";

        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";
}

void NetResolver::PrintLargestNet() const
{
    if (nets.empty())
        return;

    const Net* largest =
        &nets[0];

    for (const auto& net : nets)
    {
        if (net.node_ids.size() >
            largest->node_ids.size())
        {
            largest = &net;
        }
    }

    std::cout
        << "\n====================================\n";

    std::cout
        << "LARGEST NET\n";

    std::cout
        << "====================================\n";

    std::cout
        << "Name      : "
        << largest->name
        << "\n";

    std::cout
        << "Nodes     : "
        << largest->node_ids.size()
        << "\n";

    std::cout
        << "Labels    : "
        << largest->labels.size()
        << "\n";

    std::cout
        << "Components: "
        << largest->components.size()
        << "\n";

    if (!largest->labels.empty())
    {
        std::cout
            << "\nLabel List\n";

        for (const auto& label :
            largest->labels)
        {
            std::cout
                << "  "
                << label
                << "\n";
        }
    }

    if (!largest->components.empty())
    {
        std::cout
            << "\nComponent List\n";

        for (const auto& comp :
            largest->components)
        {
            std::cout
                << "  "
                << comp
                << "\n";
        }
    }
}

void NetResolver::PrintLabelledNets() const
{
    std::cout
        << "\n====================================\n";

    std::cout
        << "LABELLED NETS\n";

    std::cout
        << "====================================\n";

    for (const auto& net : nets)
    {
        if (net.labels.empty())
            continue;

        std::cout
            << "\nNet "
            << net.id
            << "\n";

        std::cout
            << "Name : "
            << net.name
            << "\n";

        std::cout
            << "Nodes: "
            << net.node_ids.size()
            << "\n";

        std::cout
            << "Components: "
            << net.components.size()
            << "\n";

        for (const auto& comp :
            net.components)
        {
            std::cout
                << "   "
                << comp
                << "\n";
        }
    }
}
