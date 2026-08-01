// ============================================================================
//  main.cpp
//  KiCad Schematic Parsing, Interpreting, and Connectivity Pipeline
// ============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <string>

#include "lexer.h"
#include "parser.h"
#include "interpreter.h"
#include "net_resolver.h"
#include "geometry.h"
#include "pin_transform.h"
#include "pin_mapper.h"
#include "net_database.h"
#include "pin_mapper_debug.h"
#include "graph.h"
#include "graph_builder.h"
#include "graph_debug.h"
#include "graph_search.h"

int main(int argc, char** argv)
{
    // ========================================================================
    // FILE PATH CONFIGURATION (Terminal Input)
    // ========================================================================
    std::string filepath;

    if (argc > 1)
    {
        filepath = argv[1];
    }
    else
    {
        std::cout << "=========================================================\n";
        std::cout << "  KiCad Schematic Parser\n";
        std::cout << "=========================================================\n";
        std::cout << "Please enter the path to the .kicad_sch file.\n";
        std::cout << "You can use an absolute path (e.g., C:\\Users\\...\\file.kicad_sch)\n";
        std::cout << "or a relative path if the file is in the current directory.\n";
        std::cout << "> ";

        std::getline(std::cin, filepath);

        // Basic cleanup: remove quotes if the user drag-and-dropped the file into the terminal
        if (!filepath.empty() && filepath.front() == '"' && filepath.back() == '"')
        {
            filepath = filepath.substr(1, filepath.length() - 2);
        }

        if (filepath.empty())
        {
            std::cout << "No file path provided. Using default 'ESP32_Status_Monitor.kicad_sch'.\n";
            filepath = "ESP32_Status_Monitor.kicad_sch";
        }
    }

    // ========================================================================
    // BINARY FILE INGESTION
    // ========================================================================
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);

    if (!file.is_open())
    {
        std::cerr << "ERROR: Failed to open file:\n" << filepath << "\n";
        std::cerr << "Please ensure the path is correct and the file exists.\n";
        return 1;
    }

    const size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<char> buffer(file_size + 1);
    file.read(buffer.data(), file_size);
    buffer[file_size] = '\0';
    file.close();

    std::cout << "Successfully loaded: " << filepath << " (" << file_size << " bytes)\n";

    // ========================================================================
    // LEXER & AST PARSER TIMING PIPELINE
    // ========================================================================
    auto parse_start = std::chrono::high_resolution_clock::now();

    Lexer lexer(buffer.data());
    Parser parser(lexer);
    uint32_t root_node_idx = parser.Parse();

    auto parse_end = std::chrono::high_resolution_clock::now();
    const double parse_time_ms = std::chrono::duration<double, std::milli>(parse_end - parse_start).count();

    const auto& node_pool = parser.GetPool();

    if (root_node_idx == 0)
    {
        std::cerr << "ERROR: Parser returned empty tree.\n";
        return 1;
    }

    // ========================================================================
    // SCHEMATIC S-EXPRESSION INTERPRETER
    // ========================================================================
    Interpreter interpreter(node_pool);
    Schematic schematic = interpreter.Execute(root_node_idx);

    // ========================================================================
    // GEOMETRIC NET RESOLVE
    // ========================================================================
    NetResolver resolver(schematic);
    resolver.Resolve();
    std::cout << "the PrintLabelledNets";
    resolver.PrintLabelledNets();

    // ========================================================================
    // DATA SERIALIZATION EXPORTS
    // ========================================================================
    resolver.ExportDashboardData("dashboard_data.json");
    resolver.ExportNetsJSON("nets.json");

    // ========================================================================
    // CONSOLE PIPELINE REPORT
    // ========================================================================
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "                KICAD PIPELINE REPORT\n";
    std::cout << "=========================================================\n";

    std::cout << "\nFILE\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "Path        : " << filepath << "\n";
    std::cout << "Size        : " << file_size << " bytes\n";
    std::cout << "Parse Time  : " << parse_time_ms << " ms\n";

    std::cout << "\nPARSER\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "AST Nodes   : " << node_pool.size() << "\n";
    std::cout << "Root Node   : " << root_node_idx << "\n";

    std::cout << "\nINTERPRETER\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "Wires       : " << schematic.wires.size() << "\n";
    std::cout << "Junctions   : " << schematic.junctions.size() << "\n";
    std::cout << "Labels      : " << schematic.labels.size() << "\n";
    std::cout << "Components  : " << schematic.components.size() << "\n";

    std::cout << "\nNET RESOLVER\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "Nodes       : " << resolver.GetNodes().size() << "\n";
    std::cout << "Edges       : " << resolver.GetEdges().size() << "\n";
    std::cout << "Nets        : " << resolver.GetNets().size() << "\n";
    std::cout << "Largest Net : " << resolver.GetLargestNetSize() << " nodes\n";


    std::cout << "\nOUTPUT\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "dashboard_data.json\n";
    std::cout << "nets.json\n";

    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "                NET CRITICAL INSPECTION\n";
    std::cout << "=========================================================\n";
    // Isolated verification to drill into target named net behavior
    resolver.PrintLargestNet();

    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "                NET PREVIEW (FIRST 10)\n";
    std::cout << "=========================================================\n";

    const auto& nets = resolver.GetNets();
    size_t preview_count = std::min<size_t>(nets.size(), 10);

    for (size_t i = 0; i < preview_count; i++)
    {
        const auto& net = nets[i];

        std::cout << "\nNet " << net.id << " (Root Node ID: " << net.root_node << ")\n";
        std::cout << "  Nodes      : " << net.node_ids.size() << "\n";
        std::cout << "  Labels     : " << net.labels.size() << "\n";
        std::cout << "  Components : " << net.components.size() << "\n";

        if (!net.labels.empty())
        {
            std::cout << "  Label Names: ";
            for (const auto& label : net.labels)
            {
                std::cout << label << " ";
            }
            std::cout << "\n";
        }
    }

    std::cout << "\n=========================================================\n";
    std::cout << "                RAW COMPONENT MANIFEST\n";
    std::cout << "=========================================================\n";

    for (const auto& c :
        schematic.components)
    {
        std::cout
            << c.reference
            << "  "
            << c.value
            << "\n";

        std::cout
            << "Pins : "
            << c.pins.size()
            << "\n";
    }
    

    
    


    std::cout
        << "\n====================================\n";

    std::cout
        << "PIN TRANSFORM TEST\n";

    std::cout
        << "====================================\n";

    for (auto& comp : schematic.components)
    {
        std::cout
            << comp.reference
            << " Pins="
            << comp.pins.size()
            << "\n";

        PinTransform::ComputeWorldPins(comp);
    }

    std::cout
        << "\n====================================\n";

    std::cout
        << "PRINTING COMPONENT PINS\n";

    std::cout
        << "====================================\n";

    for (const auto& comp : schematic.components)
    {
        if (!comp.pins.empty())
        {
            PinTransform::PrintPins(comp);
        }
    }



    PinMapper mapper(
        schematic,
        resolver);

    mapper.Build();

    mapper.PrintConnections();

    NetDatabase database(
        mapper);

    database.Build();

    database.Print();



    std::cout << "\nThe debug class is\n";

    for (auto& comp : schematic.components)
    {
        PinTransform::ComputeWorldPins(comp);
    }

    for (const auto& comp : schematic.components)
    {
        if (comp.reference == "U1")
        {
            PinMapperDebug::PrintComponent(comp);
        }
    }


    std::cout
        << "\n====================================\n";

    std::cout
        << "PIN PIPELINE DEBUG\n";

    std::cout
        << "====================================\n";

    for (const auto& comp : schematic.components)
    {
        if (comp.reference != "U1")
            continue;

        std::cout
            << "\nComponent : "
            << comp.reference
            << "\n";

        std::cout
            << "Position : "
            << comp.location.x
            << ", "
            << comp.location.y
            << "\n";

        std::cout
            << "Rotation : "
            << comp.rotation
            << "\n";

        std::cout
            << "------------------------------------\n";

        for (const auto& pin : comp.pins)
        {
            std::cout
                << pin.number
                << " "
                << pin.name
                << "\n";

            std::cout
                << "Pin Location : "
                << pin.location.x
                << ", "
                << pin.location.y
                << "\n";

            std::cout
                << "World        : "
                << pin.world_location.x
                << ", "
                << pin.world_location.y
                << "\n";

            std::cout
                << "\n";
        }
    }
    

    std::cout << "\nNearest Node Debug\n";

    for (const auto& comp : schematic.components)
    {
        if (comp.reference == "U1")
        {
            PinMapperDebug::PrintNearestNodes(
                comp,
                resolver);
        }
    }


    std::cout << "\n graph builder test\n";

    GraphBuilder builder;

    Graph graph =
        builder.Build(schematic);

    std::cout
        << "\n============================\n";

    std::cout
        << "GRAPH TEST\n";

    std::cout
        << "============================\n";

    std::cout
        << "Vertices : "
        << graph.GetVertices().size()
        << "\n";

    std::cout
        << "Edges : "
        << graph.GetEdges().size()
        << "\n";


    std::cout << "\nthese are the debug of the graphs\n";


    // 1. Run the new search/net extraction
    auto graph_nets = GraphSearch::ExtractNets(graph);
    std::cout << "Graph nets : " << graph_nets.size() << "\n";

    // 2. Print the extracted electrical nets
    // This will show you exactly which pins connect to which wires
    GraphDebug::PrintNets(graph);


    GraphDebug::PrintVertices(graph);

    GraphDebug::PrintEdges(graph);

    GraphDebug::PrintAdjacency(graph);




    std::cout
        << "\nWEB TEST\n";

    std::cout
        << "Components: "
        << schematic.components.size()
        << "\n";

    std::cout
        << "Nodes: "
        << resolver.GetNodes().size()
        << "\n";

    std::cout
        << "Edges: "
        << resolver.GetEdges().size()
        << "\n";

    std::cout
        << "Nets: "
        << resolver.GetNets().size()
        << "\n";








    // Windows-only WebDashboard removed from the core build path (T0.1/T0.2).
    std::cout << "\nPipeline complete (web dashboard disabled in core build).\n";
    return 0;

}