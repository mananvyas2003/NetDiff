#include "dashboard.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>

// ----------------------------------------------------
// Parser Statistics
// ----------------------------------------------------

ParserStats Dashboard::CalculateParserStats(
    const std::vector<ASTNode>& pool)
{
    ParserStats stats;

    stats.total_nodes = pool.size();

    for (const auto& node : pool)
    {
        switch (node.type)
        {
        case NodeType::List:
            stats.list_nodes++;
            break;

        case NodeType::Symbol:
            stats.symbol_nodes++;
            break;

        case NodeType::Number:
            stats.number_nodes++;
            break;

        case NodeType::String:
            stats.string_nodes++;
            break;
        }
    }

    return stats;
}

// ----------------------------------------------------
// Banner
// ----------------------------------------------------

void Dashboard::PrintBanner()
{
    std::cout
        << "\n"
        << "============================================================\n"
        << "                 KICAD PARSER DASHBOARD\n"
        << "============================================================\n";
}

// ----------------------------------------------------
// File Info
// ----------------------------------------------------

void Dashboard::PrintFileInfo(
    const std::string& filepath,
    size_t file_size_bytes,
    double parse_time_ms)
{
    std::cout
        << "\nFILE INFORMATION\n"
        << "------------------------------------------------------------\n";

    std::cout
        << "Path       : "
        << filepath
        << "\n";

    std::cout
        << "Size       : "
        << std::fixed
        << std::setprecision(2)
        << (double)file_size_bytes / 1024.0
        << " KB\n";

    std::cout
        << "Parse Time : "
        << parse_time_ms
        << " ms\n";
}

// ----------------------------------------------------
// Parser Stats
// ----------------------------------------------------

void Dashboard::PrintParserStats(
    const ParserStats& stats)
{
    std::cout
        << "\nPARSER STATISTICS\n"
        << "------------------------------------------------------------\n";

    std::cout << "AST Nodes : " << stats.total_nodes << "\n";
    std::cout << "Lists     : " << stats.list_nodes << "\n";
    std::cout << "Symbols   : " << stats.symbol_nodes << "\n";
    std::cout << "Numbers   : " << stats.number_nodes << "\n";
    std::cout << "Strings   : " << stats.string_nodes << "\n";
}

// ----------------------------------------------------
// Interpreter Stats
// ----------------------------------------------------

void Dashboard::PrintInterpreterStats(
    const Schematic& schematic)
{
    std::cout
        << "\nINTERPRETER STATISTICS\n"
        << "------------------------------------------------------------\n";

    std::cout
        << "Wires      : "
        << schematic.wires.size()
        << "\n";

    std::cout
        << "Junctions  : "
        << schematic.junctions.size()
        << "\n";

    std::cout
        << "Labels     : "
        << schematic.labels.size()
        << "\n";

    std::cout
        << "Components : "
        << schematic.components.size()
        << "\n";
}

// ----------------------------------------------------
// Wire Preview
// ----------------------------------------------------

void Dashboard::PrintWirePreview(
    const Schematic& schematic,
    size_t max_count)
{
    std::cout
        << "\nWIRE PREVIEW\n"
        << "------------------------------------------------------------\n";

    size_t count =
        std::min(
            schematic.wires.size(),
            max_count);

    for (size_t i = 0; i < count; i++)
    {
        const auto& wire =
            schematic.wires[i];

        std::cout
            << "#"
            << i
            << "  ("
            << wire.start.x
            << ", "
            << wire.start.y
            << ") -> ("
            << wire.end.x
            << ", "
            << wire.end.y
            << ")\n";
    }
}

// ----------------------------------------------------
// Junction Preview
// ----------------------------------------------------

void Dashboard::PrintJunctionPreview(
    const Schematic& schematic,
    size_t max_count)
{
    std::cout
        << "\nJUNCTION PREVIEW\n"
        << "------------------------------------------------------------\n";

    size_t count =
        std::min(
            schematic.junctions.size(),
            max_count);

    for (size_t i = 0; i < count; i++)
    {
        const auto& junction =
            schematic.junctions[i];

        std::cout
            << "#"
            << i
            << "  ("
            << junction.location.x
            << ", "
            << junction.location.y
            << ")\n";
    }
}

// ----------------------------------------------------
// JSON Export
// ----------------------------------------------------

void Dashboard::ExportInterpreterJson(
    const Schematic& schematic,
    const std::string& filename)
{
    std::ofstream file(filename);

    if (!file.is_open())
        return;

    file << "{\n";

    file << "  \"wire_count\": "
        << schematic.wires.size()
        << ",\n";

    file << "  \"junction_count\": "
        << schematic.junctions.size()
        << ",\n";

    file << "  \"wires\": [\n";

    for (size_t i = 0;
        i < schematic.wires.size();
        i++)
    {
        const auto& wire =
            schematic.wires[i];

        file
            << "    {\n"
            << "      \"x1\": "
            << wire.start.x
            << ",\n"

            << "      \"y1\": "
            << wire.start.y
            << ",\n"

            << "      \"x2\": "
            << wire.end.x
            << ",\n"

            << "      \"y2\": "
            << wire.end.y
            << "\n"
            << "    }";

        if (i + 1 < schematic.wires.size())
            file << ",";

        file << "\n";
    }

    file << "  ],\n";

    file << "  \"junctions\": [\n";

    for (size_t i = 0;
        i < schematic.junctions.size();
        i++)
    {
        const auto& j =
            schematic.junctions[i];

        file
            << "    {\n"
            << "      \"x\": "
            << j.location.x
            << ",\n"

            << "      \"y\": "
            << j.location.y
            << "\n"
            << "    }";

        if (i + 1 < schematic.junctions.size())
            file << ",";

        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";
}

// ----------------------------------------------------
// Footer
// ----------------------------------------------------

void Dashboard::PrintFooter()
{
    std::cout
        << "\n============================================================\n"
        << "                  EXECUTION COMPLETE\n"
        << "============================================================\n";
}

// ----------------------------------------------------
// Full Dashboard
// ----------------------------------------------------

void Dashboard::PrintFullDashboard(
    const std::string& filepath,
    size_t file_size,
    double parse_time_ms,
    const std::vector<ASTNode>& pool,
    const Schematic& schematic)
{
    PrintBanner();

    PrintFileInfo(
        filepath,
        file_size,
        parse_time_ms);

    ParserStats stats =
        CalculateParserStats(pool);

    PrintParserStats(stats);

    PrintInterpreterStats(schematic);

    PrintWirePreview(schematic);

    PrintJunctionPreview(schematic);

    PrintFooter();
}