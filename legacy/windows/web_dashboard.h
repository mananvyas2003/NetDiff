#pragma once

// ============================================================
//  web_dashboard.h
//  Native Windows (Winsock2) HTTP server that serves a
//  single-page browser dashboard for the KiCad parser output.
// ============================================================

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

#include "interpreter.h"     // Schematic, WireSegment, Junction, NetLabel, Component, Point
#include "net_resolver.h"    // ConnectivityNode, Edge
#include "dashboard.h"       // ParserStats

// Everything the web view needs, gathered in one place.
// Pointers reference data owned elsewhere (e.g. main's stack);
// they must stay alive for as long as the server runs.
struct WebDashboardData
{
    std::string filepath;
    std::size_t file_size_bytes = 0;
    double      parse_time_ms = 0.0;

    std::size_t ast_node_count = 0;
    std::uint32_t root_node_idx = 0;
    ParserStats parser_stats;

    const Schematic* schematic = nullptr;
    const std::vector<ConnectivityNode>* nodes = nullptr;
    const std::vector<Edge>* edges = nullptr;
    const std::vector<Net>* nets = nullptr;
};

class WebDashboard
{
public:
    // Starts the HTTP server bound to 127.0.0.1:<port>.
    // This call BLOCKS (serves requests in a loop) until the
    // process is terminated (Ctrl+C). Returns non-zero on a
    // startup error (socket / bind / listen failure).
    static int Serve(
        const WebDashboardData& data,
        int port = 8080,
        bool open_browser = true);
};