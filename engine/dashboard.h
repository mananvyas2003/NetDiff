#pragma once

#include <string>
#include <vector>

#include "parser.h"
#include "interpreter.h"

struct ParserStats
{
    size_t total_nodes = 0;

    size_t list_nodes = 0;
    size_t symbol_nodes = 0;
    size_t number_nodes = 0;
    size_t string_nodes = 0;
};

class Dashboard
{
public:

    static ParserStats CalculateParserStats(
        const std::vector<ASTNode>& pool);

    static void PrintBanner();

    static void PrintFileInfo(
        const std::string& filepath,
        size_t file_size_bytes,
        double parse_time_ms);

    static void PrintParserStats(
        const ParserStats& stats);

    static void PrintInterpreterStats(
        const Schematic& schematic);

    static void PrintWirePreview(
        const Schematic& schematic,
        size_t max_count = 10);

    static void PrintJunctionPreview(
        const Schematic& schematic,
        size_t max_count = 10);

    static void PrintFooter();

    static void ExportInterpreterJson(
        const Schematic& schematic,
        const std::string& filename);

    static void PrintFullDashboard(
        const std::string& filepath,
        size_t file_size,
        double parse_time_ms,
        const std::vector<ASTNode>& pool,
        const Schematic& schematic);
};