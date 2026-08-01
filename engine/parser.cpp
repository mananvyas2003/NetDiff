#include "parser.h"
#include <iostream>

// -----------------------------------------------------------------------------
// Constructor: Primes the parser and sets up the NULL node
// -----------------------------------------------------------------------------
Parser::Parser(Lexer& lexer_instance)
    : lexer(lexer_instance)
{
    // Reserve index 0 as the universal "NULL" pointer in our flat array.
    // Any node with first_child == 0 or next_sibling == 0 has no child/sibling.
    ASTNode null_node;
    null_node.type = NodeType::Symbol; // Arbitrary, this node is never read
    node_pool.push_back(null_node);

    // Fetch the very first token to kick off the pipeline
    Consume();
}

// -----------------------------------------------------------------------------
// Core Engine: Advances the token stream
// -----------------------------------------------------------------------------
void Parser::Consume()
{
    current_token = lexer.NextToken();
}

// -----------------------------------------------------------------------------
// Memory Engine: Creates a node in the flat array and returns its integer index
// -----------------------------------------------------------------------------
uint32_t Parser::AllocateNode(NodeType type, TokenView text, double num_val)
{
    ASTNode node;
    node.type = type;
    node.text = text;
    node.number_value = num_val;
    node.first_child = 0;
    node.next_sibling = 0;

    node_pool.push_back(node);

    // Return the absolute index of this new node
    return static_cast<uint32_t>(node_pool.size() - 1);
}

// -----------------------------------------------------------------------------
// Top-Level Entry Point
// -----------------------------------------------------------------------------
uint32_t Parser::Parse()
{
    uint32_t root_index = 0;
    uint32_t last_expression = 0;

    // A file might contain multiple top-level S-Expressions.
    // We parse them all and link them as siblings.
    while (current_token.type != TokenType::Eof)
    {
        if (current_token.type == TokenType::Error)
        {
            std::cerr << "Parser Fatal: Lexer error encountered at Line "
                << current_token.line << ", Column " << current_token.column << "\n";
            break;
        }

        uint32_t expr_idx = ParseExpression();

        if (expr_idx != 0)
        {
            if (root_index == 0)
            {
                root_index = expr_idx; // First valid expression is our master root
            }
            else
            {
                // Link subsequent top-level expressions to the sibling chain
                node_pool[last_expression].next_sibling = expr_idx;
            }
            last_expression = expr_idx;
        }
    }

    return root_index;
}

// -----------------------------------------------------------------------------
// Expression Router: Decides what kind of node to build based on the current token
// -----------------------------------------------------------------------------
uint32_t Parser::ParseExpression()
{
    switch (current_token.type)
    {
    case TokenType::LParem:
    {
        // Hit a '(', step into list parsing mode
        return ParseList();
    }
    case TokenType::Symbol:
    {
        uint32_t idx = AllocateNode(NodeType::Symbol, current_token.text);
        Consume();
        return idx;
    }
    case TokenType::Number:
    {
        uint32_t idx = AllocateNode(NodeType::Number, current_token.text, current_token.number_value);
        Consume();
        return idx;
    }
    case TokenType::String:
    {
        uint32_t idx = AllocateNode(NodeType::String, current_token.text);
        Consume();
        return idx;
    }
    case TokenType::RParem:
    {
        // We should never hit a ')' outside of a ParseList() loop.
        // If we do, the parentheses in the file are unbalanced.
        std::cerr << "Parser Warning: Unexpected closing parenthesis at Line "
            << current_token.line << "\n";
        Consume();
        return 0;
    }
    default:
    {
        Consume();
        return 0;
    }
    }
}

// -----------------------------------------------------------------------------
// List Builder: Handles everything inside (...)
// -----------------------------------------------------------------------------
uint32_t Parser::ParseList()
{
    Consume(); // Advance past the opening '('

    // Create the parent List node. It doesn't have intrinsic text or numbers.
    uint32_t list_node_idx = AllocateNode(NodeType::List, { nullptr, 0 });

    uint32_t head_child_idx = 0;
    uint32_t current_sibling_idx = 0;

    // Keep evaluating expressions until we hit the matching ')' or the end of the file
    while (current_token.type != TokenType::RParem && current_token.type != TokenType::Eof)
    {
        uint32_t child_idx = ParseExpression();

        if (child_idx == 0) continue; // Skip invalid nodes

        if (head_child_idx == 0)
        {
            // The very first item inside the (...) is the left-child
            head_child_idx = child_idx;
            node_pool[list_node_idx].first_child = head_child_idx;
        }
        else
        {
            // All subsequent items are added to the right-sibling chain
            node_pool[current_sibling_idx].next_sibling = child_idx;
        }

        // Update the tail of our sibling chain
        current_sibling_idx = child_idx;
    }

    if (current_token.type == TokenType::RParem)
    {
        Consume(); // Cleanly consume the closing ')'
    }
    else
    {
        std::cerr << "Parser Error: Reached EOF before closing list started at Line "
            << current_token.line << "\n";
    }

    return list_node_idx;
}