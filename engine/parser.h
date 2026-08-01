#pragma once
#include "lexer.h"
#include <vector>

enum class NodeType : uint8_t {
    List,     // An S-Expression group enclosed in ( )
    Symbol,   // Unquoted identifiers (e.g., kicad_sch, symbol, at)
    Number,   // Float or integer values
    String    // Quoted strings
};

struct ASTNode {
    NodeType type;

    uint32_t first_child = 0; // Index in the flat vector (0 means NULL/None)
    uint32_t next_sibling = 0; // Index in the flat vector (0 means NULL/None)

    TokenView text;            // Direct slice reference back to the original file
    double number_value = 0.0; // Only utilized if type == NodeType::Number
};

class Parser {
private:
    Lexer& lexer;
    Token current_token;

    // The unified flat memory block containing our entire tree hierarchy
    std::vector<ASTNode> node_pool;

    // Secure helper to allocate a node inside our cache-friendly pool
    uint32_t AllocateNode(NodeType type, TokenView text, double num_val = 0.0);

    // Core token management mechanics
    void Consume();

    // Recursive parsing vectors
    uint32_t ParseExpression();
    uint32_t ParseList();

public:
    explicit Parser(Lexer& lexer_instance);

    // Triggers execution and returns the root node index
    uint32_t Parse();

    // Debugging utility to inspect the pool if needed
    const std::vector<ASTNode>& GetPool() const { return node_pool; }

    void PrintTree(const std::vector<ASTNode>& pool, uint32_t idx, int depth);


};