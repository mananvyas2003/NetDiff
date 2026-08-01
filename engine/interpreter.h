#pragma once

#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include "parser.h"

struct Point
{
    double x = 0.0;
    double y = 0.0;
};

struct WireSegment
{
    Point start;
    Point end;
};

struct Junction
{
    Point location;
};

enum class LabelType
{
    Local,
    Global, // <--- Typo: "Gloabal"
    Hierarchical
};


struct NetLabel
{
    std::string name;
    Point location;
    LabelType type = LabelType::Local;
};



struct Pin
{
    std::string number;
    std::string name;

    Point location;
    Point world_location;

};

struct Component
{
    std::string reference;
    std::string value;
    std::string footprint;
    std::string lib_id;

    Point location;

    double rotation = 0.0;

    std::vector<Pin> pins;

    // KiCad multi-instance path → reference (for reused hierarchical sheets).
    std::vector<std::pair<std::string, std::string>> instance_refs;
};

struct SheetPin
{
    std::string name;
    std::string shape;  // input / output / bidirectional / ...
    Point location;
};

struct SheetInstance
{
    std::string name;   // Sheetname property
    std::string file;   // Sheetfile property
    std::string uuid;
    std::vector<SheetPin> pins;
    Point at;
    Point size;  // width/height
};

struct NoConnect
{
    Point location;
};

struct BusSegment
{
    Point start;
    Point end;
};

struct Schematic
{
    std::string uuid;

    std::vector<WireSegment> wires;
    std::vector<Junction> junctions;
    std::vector<NetLabel> labels;
    std::vector<Component> components;
    std::vector<SheetInstance> sheets;
    std::vector<NoConnect> no_connects;
    std::vector<BusSegment> buses;
};


struct LibraryPin
{
    std::string number;
    std::string name;

    Point offset;

    double rotation = 0.0;
};

struct LibrarySymbol
{
    std::string name;

    std::vector<LibraryPin> pins;
};



class Interpreter
{
private:

    const std::vector<ASTNode>& pool;

    Schematic schematic;

    void Visit(uint32_t idx);

    bool NodeNameEquals(    
        uint32_t idx,
        const char* expected);

    uint32_t FindNamedChild(
        uint32_t idx,
        const char* name);

    double GetNumber(
        uint32_t idx);

    std::string GetText(
        uint32_t idx);

    bool ExtractXY(
        uint32_t xy_node,
        Point& point);

    void ExtractWire(
        uint32_t idx);

    void ExtractJunction(
        uint32_t idx);

public:

    explicit Interpreter(
        const std::vector<ASTNode>& p);

    Schematic Execute(
        uint32_t root);
    

    std::string GetSecondChildText(uint32_t idx);

    bool ExtractAt(
        uint32_t at_node,
        Point& point,
        double& rotation);

    void ExtractComponent(uint32_t idx);

    void ExtractProperty(uint32_t property_node, Component& component);

    bool ExtractLabelData(
        uint32_t idx,
        NetLabel& label);

    void ExtractLabel(uint32_t idx, NetLabel& label);

    void ExtractGlobalLabel(uint32_t idx);

    void ExtractHierarchicalLabel(
        uint32_t idx);

    void ExtractPins(
        uint32_t idx,
        Component& component);

    void ExtractSheet(uint32_t idx);

    void ExtractSheetPin(
        uint32_t idx,
        SheetInstance& sheet);

    void ExtractNoConnect(uint32_t idx);

    void ExtractBus(uint32_t idx);

    void ExtractBusEntry(uint32_t idx);

    void ExtractComponentInstances(
        uint32_t idx,
        Component& component);


    std::unordered_map<
        std::string,
        LibrarySymbol
    > library_symbols;

    void ExtractLibrarySymbols(uint32_t idx);

    void ExtractLibrarySymbol(uint32_t idx);

    void PrintLibrarySymbols() const;


    void ExtractLibraryPin(
        uint32_t idx,
        LibrarySymbol& symbol);

    void PrintLibrarySymbolDetails() const;

    void ExtractLibraryPinsRecursive(
        uint32_t idx,
        LibrarySymbol& symbol);



};