#pragma once
#include "graph.h"


class GraphDebug
{
public:
	static void PrintVertices(const Graph& graph);
	static void PrintEdges(const Graph& graph);
	static void PrintAdjacency(const Graph& graph);
	static void PrintTopology(const Graph& graph);
	static std::string VertexTypeToString(VertexType type);
	// Prints the actual routing paths (e.g., U1.IO22 -> Wire -> U2.RX)
	static void PrintNets(Graph& graph);

	// Generates a string you can paste into WebGraphviz.com
	static void ExportToDOT(const Graph& graph);



};

