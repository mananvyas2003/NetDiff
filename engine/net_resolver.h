#pragma once

#include "interpreter.h"

#include <cstdint>
#include <cstddef>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <string>

struct PointKey
{
    int x;
    int y;

    bool operator==(const PointKey& rhs) const
    {
        return x == rhs.x && y == rhs.y;
    }
};

struct PointHasher
{
    size_t operator()(const PointKey& p) const
    {
        // Clean bit rotation for hash combining
        return std::hash<int>()(p.x) ^ (std::hash<int>()(p.y) << 1);
    }
};



struct ConnectivityNode
{
    uint32_t id = 0;
    Point position;

    // Attaching metadata directly to graph nodes for the UI engine
    std::vector<std::string> labels;
    std::vector<std::string> connected_components;

    // --- Add these fields below to satisfy the pin-resolver expectations ---
    bool is_pin = false;              // Tracks whether this coordinate maps to a physical component pin
    std::string pin_name;             // The name/number of the pin (e.g., "1", "GND", "VCC")
    std::string component_ref;        // The clean isolated reference of the parent component (e.g., "U1", "R1")
};

struct Edge
{
    uint32_t id = 0;
    uint32_t start_node = 0;
    uint32_t end_node = 0;
};

struct Net
{
    uint32_t id = 0;

    uint32_t root_node = 0;

    std::string name;

    std::vector<uint32_t> node_ids;

    std::vector<std::string> labels;

    std::vector<std::string> components;
};

struct Node
{
    uint32_t id;

    Point position;

    std::vector<std::string> labels;

    std::vector<std::string> connected_components;

    bool is_pin = false;

    std::string pin_name;
    std::string component_ref;
};

class NetResolver
{
private:
    const Schematic& schematic;

    std::vector<ConnectivityNode> nodes;
    std::vector<Edge> edges;
    std::vector<Net> nets;

    std::unordered_map<PointKey, uint32_t, PointHasher> point_to_node;

    PointKey MakeKey(const Point& p) const;
    uint32_t GetOrCreateNode(const Point& point);

    std::vector<uint32_t> parent;
    std::vector<uint32_t> rank;

    uint32_t Find(uint32_t x);
    void Union(uint32_t a, uint32_t b);





public:
    explicit NetResolver(const Schematic& sch);

    // Unified resolve method
    void Resolve();

    void PrintNodes() const;
    void PrintEdges() const;

    const std::vector<ConnectivityNode>& GetNodes() const;
    const std::vector<Edge>& GetEdges() const;

   
    void ExportDashboardData(const std::string& outputPath) const;

    void BuildNets();
    void PrintNets() const;

    const std::vector<Net>& GetNets() const;

    size_t GetLargestNetSize() const;

    void ExportNetsJSON(const std::string& path) const;
    void MergeNamedNets();
    void PrintLargestNet() const;
    void PrintLabelledNets() const;

    void AttachPinsToNodes();


};