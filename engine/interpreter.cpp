#include "interpreter.h"

#include <cctype>
#include <cstring>
#include <iostream>

// ---------------------------------------------------------
// Constructor
// ---------------------------------------------------------
Interpreter::Interpreter(const std::vector<ASTNode>& p)
    : pool(p)
{
}

// ---------------------------------------------------------
// Execute
// ---------------------------------------------------------
Schematic Interpreter::Execute(uint32_t root)
{
    Visit(root);

    const uint32_t uuid_node = FindNamedChild(root, "uuid");
    if (uuid_node != 0)
    {
        schematic.uuid = GetSecondChildText(uuid_node);
    }

    PrintLibrarySymbols();

    PrintLibrarySymbolDetails();

    return schematic;
}


void Interpreter::ExtractLibraryPinsRecursive(
    uint32_t idx,
    LibrarySymbol& symbol,
    int current_unit)
{
    if (idx == 0)
        return;

    if (NodeNameEquals(idx, "pin"))
    {
        ExtractLibraryPin(
            idx,
            symbol,
            current_unit);

        return;
    }

    // Nested unit/body sub-symbol: (symbol "R_1_1" ...)
    if (NodeNameEquals(idx, "symbol"))
    {
        uint32_t child = pool[idx].first_child;
        if (child != 0)
        {
            child = pool[child].next_sibling;
        }
        if (child != 0)
        {
            const std::string sub_name = GetText(child);
            const int unit = UnitFromSubSymbolName(sub_name);
            child = pool[child].next_sibling;
            while (child != 0)
            {
                ExtractLibraryPinsRecursive(
                    child,
                    symbol,
                    unit > 0 ? unit : current_unit);
                child = pool[child].next_sibling;
            }
            return;
        }
    }

    uint32_t child =
        pool[idx].first_child;

    while (child != 0)
    {
        ExtractLibraryPinsRecursive(
            child,
            symbol,
            current_unit);

        child =
            pool[child].next_sibling;
    }
}


void Interpreter::ExtractLibrarySymbols(uint32_t idx)
{
    uint32_t child =
        pool[idx].first_child;

    if (child == 0)
        return;

    child =
        pool[child].next_sibling;

    while (child != 0)
    {
        if (NodeNameEquals(child, "symbol"))
        {
            ExtractLibrarySymbol(child);
        }

        child =
            pool[child].next_sibling;
    }
}

void Interpreter::ExtractLibrarySymbol(uint32_t idx)
{
    uint32_t child =
        pool[idx].first_child;

    if (child == 0)
        return;

    child =
        pool[child].next_sibling;

    if (child == 0)
        return;

    LibrarySymbol symbol;

    symbol.name =
        GetText(child);

    std::cout
        << "\nLIB SYMBOL : "
        << symbol.name
        << "\n";

    ExtractLibraryPinsRecursive(
        idx,
        symbol,
        0);

    std::cout
        << "Pins Found : "
        << symbol.pins.size()
        << "\n";

    library_symbols[symbol.name] =
        symbol;
}

void Interpreter::ExtractLibraryPin(
    uint32_t idx,
    LibrarySymbol& symbol,
    int unit)
{
    LibraryPin pin;
    pin.unit = unit;

    uint32_t child =
        pool[idx].first_child;

    child =
        pool[child].next_sibling;

    while (child != 0)
    {
        if (NodeNameEquals(child, "number"))
        {
            pin.number =
                GetSecondChildText(child);
        }

        else if (NodeNameEquals(child, "name"))
        {
            pin.name =
                GetSecondChildText(child);
        }

        else if (NodeNameEquals(child, "at"))
        {
            ExtractAt(
                child,
                pin.offset,
                pin.rotation);

            // KiCad stores library/symbol coords with Y opposite schematic:
            // parseXY(/*aInvertY=*/true) negates Y on load.
            pin.offset.y = -pin.offset.y;
        }

        child =
            pool[child].next_sibling;
    }

    symbol.pins.push_back(pin);
    std::cout
        << "PIN "
        << pin.number
        << " "
        << pin.name
        << " unit="
        << pin.unit
        << " @ "
        << pin.offset.x
        << ", "
        << pin.offset.y
        << "\n";
}


void Interpreter::PrintLibrarySymbolDetails() const
{
    std::cout
        << "\n==============================\n"
        << "LIBRARY PIN DATABASE\n"
        << "==============================\n";

    for (const auto& kv : library_symbols)
    {
        const LibrarySymbol& symbol =
            kv.second;

        std::cout
            << "\n"
            << symbol.name
            << "\n";

        std::cout
            << "Pins : "
            << symbol.pins.size()
            << "\n";

        for (size_t i = 0;
            i < symbol.pins.size() && i < 5;
            i++)
        {
            const auto& pin =
                symbol.pins[i];

            std::cout
                << "  "
                << pin.number
                << " "
                << pin.name
                << " @ "
                << pin.offset.x
                << ", "
                << pin.offset.y
                << "\n";
        }
    }
}


// ---------------------------------------------------------
// GetText
// ---------------------------------------------------------
std::string Interpreter::GetText(uint32_t idx)
{
    if (idx == 0) return "";
    const ASTNode& node = pool[idx];
    if (node.text.start == nullptr) return "";

    return std::string(node.text.start, node.text.length);
}

// ---------------------------------------------------------
// GetNumber
// ---------------------------------------------------------
double Interpreter::GetNumber(uint32_t idx)
{
    if (idx == 0) return 0.0;
    return pool[idx].number_value;
}

// ---------------------------------------------------------
// NodeNameEquals
// ---------------------------------------------------------
bool Interpreter::NodeNameEquals(uint32_t idx, const char* expected)
{
    if (idx == 0) return false;
    const ASTNode& node = pool[idx];
    if (node.type != NodeType::List) return false;

    uint32_t first = node.first_child;
    if (first == 0) return false;

    const ASTNode& name_node = pool[first];
    if (name_node.type != NodeType::Symbol) return false;

    size_t expected_len = std::strlen(expected);
    if (name_node.text.length != expected_len) return false;

    return std::memcmp(name_node.text.start, expected, expected_len) == 0;
}

// ---------------------------------------------------------
// FindNamedChild
// ---------------------------------------------------------
uint32_t Interpreter::FindNamedChild(uint32_t idx, const char* name)
{
    if (idx == 0) return 0;
    uint32_t child = pool[idx].first_child;
    if (child != 0)
    {
        child = pool[child].next_sibling;
    }

    while (child != 0)
    {
        if (NodeNameEquals(child, name)) return child;
        child = pool[child].next_sibling;
    }
    return 0;
}

// ---------------------------------------------------------
// ExtractXY
// ---------------------------------------------------------
bool Interpreter::ExtractXY(uint32_t xy_node, Point& point)
{
    if (!NodeNameEquals(xy_node, "xy")) return false;

    uint32_t child = pool[xy_node].first_child;
    child = pool[child].next_sibling;
    if (child == 0) return false;
    point.x = GetNumber(child);

    child = pool[child].next_sibling;
    if (child == 0) return false;
    point.y = GetNumber(child);

    return true;
}

// ---------------------------------------------------------
// ExtractWire
// ---------------------------------------------------------
void Interpreter::ExtractWire(uint32_t idx)
{
    uint32_t pts = FindNamedChild(idx, "pts");
    if (pts == 0) return;

    uint32_t child = pool[pts].first_child;
    child = pool[child].next_sibling;

    Point p1{};
    Point p2{};
    bool found_first = false;

    while (child != 0)
    {
        if (NodeNameEquals(child, "xy"))
        {
            if (!found_first)
            {
                ExtractXY(child, p1);
                found_first = true;
            }
            else
            {
                ExtractXY(child, p2);
                WireSegment wire;
                wire.start = p1;
                wire.end = p2;
                schematic.wires.push_back(wire);

                std::cout << "Wire: (" << p1.x << ", " << p1.y << ") -> (" << p2.x << ", " << p2.y << ")\n";
                break;
            }
        }
        child = pool[child].next_sibling;
    }
}

// ---------------------------------------------------------
// ExtractJunction
// ---------------------------------------------------------
void Interpreter::ExtractJunction(uint32_t idx)
{
    uint32_t at = FindNamedChild(idx, "at");
    if (at == 0) return;

    uint32_t child = pool[at].first_child;
    child = pool[child].next_sibling;
    if (child == 0) return;

    Junction junction;
    junction.location.x = GetNumber(child);

    child = pool[child].next_sibling;
    if (child == 0) return;
    junction.location.y = GetNumber(child);

    schematic.junctions.push_back(junction);
    std::cout << "Junction: (" << junction.location.x << ", " << junction.location.y << ")\n";
}

std::string Interpreter::GetSecondChildText(uint32_t list_idx)
{
    if (list_idx == 0) return "";
    uint32_t child = pool[list_idx].first_child;
    if (child == 0) return "";

    child = pool[child].next_sibling;
    if (child == 0) return "";

    return GetText(child);
}

bool Interpreter::ExtractAt(uint32_t at_node, Point& point, double& rotation)
{
    if (!NodeNameEquals(at_node, "at")) return false;

    uint32_t child = pool[at_node].first_child;
    child = pool[child].next_sibling;
    if (child == 0) return false;
    point.x = GetNumber(child);

    child = pool[child].next_sibling;
    if (child == 0) return false;
    point.y = GetNumber(child);

    child = pool[child].next_sibling;
    if (child != 0)
    {
        rotation = GetNumber(child);
    }
    return true;
}

void Interpreter::ExtractProperty(
    uint32_t property_node,
    Component& component)
{
    uint32_t child =
        pool[property_node].first_child;

    child =
        pool[child].next_sibling;

    if (child == 0)
        return;

    std::string property_name =
        GetText(child);

    child =
        pool[child].next_sibling;

    if (child == 0)
        return;

    std::string property_value =
        GetText(child);

    std::cout
        << "Property : "
        << property_name
        << " = "
        << property_value
        << "\n";

    if (property_name == "Reference")
    {
        component.reference =
            property_value;
    }
    else if (property_name == "Value")
    {
        component.value =
            property_value;
    }
    else if (property_name == "Footprint")
    {
        component.footprint =
            property_value;
    }
}


Point RotatePoint(
    const Point& p,
    double rotation)
{
    Point result;

    // KiCad TRANSFORM + TransformCoordinate(x' = x1*x + y1*y, y' = x2*x + y2*y):
    //   file 90  → TRANSFORM(0,1,-1,0) → (x,y) → (y,-x)
    //   file 270 → TRANSFORM(0,-1,1,0) → (x,y) → (-y,x)
    if (rotation == 0)
    {
        result = p;
    }
    else if (rotation == 90)
    {
        result.x = p.y;
        result.y = -p.x;
    }
    else if (rotation == 180)
    {
        result.x = -p.x;
        result.y = -p.y;
    }
    else if (rotation == 270)
    {
        result.x = -p.y;
        result.y = p.x;
    }
    else
    {
        result = p;
    }

    return result;
}

Point Interpreter::TransformLibOffset(
    Point offset,
    double rotation,
    bool mirror_x,
    bool mirror_y)
{
    // Match KiCad sexpr load: SetTransform(angle), then SetOrientation(mirror).
    Point r = RotatePoint(offset, rotation);
    if (mirror_y)
    {
        r.x = -r.x;
    }
    if (mirror_x)
    {
        r.y = -r.y;
    }
    return r;
}

int Interpreter::UnitFromSubSymbolName(const std::string& name)
{
    std::string base = name;
    const auto colon = base.rfind(':');
    if (colon != std::string::npos)
    {
        base = base.substr(colon + 1);
    }

    // Expect "..._<unit>_<bodyStyle>" e.g. R_1_1, ECC83_2_1, CP_0_1
    const auto u2 = base.rfind('_');
    if (u2 == std::string::npos || u2 == 0)
    {
        return 0;
    }
    const auto u1 = base.rfind('_', u2 - 1);
    if (u1 == std::string::npos)
    {
        return 0;
    }

    const std::string unit_str = base.substr(u1 + 1, u2 - u1 - 1);
    if (unit_str.empty())
    {
        return 0;
    }
    for (char c : unit_str)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            return 0;
        }
    }
    return std::stoi(unit_str);
}




void Interpreter::ExtractComponent(uint32_t idx)
{
    // ----------------------------------------------------
    // Filter out library symbol definitions
    // ----------------------------------------------------

    uint32_t lib_id =
        FindNamedChild(idx, "lib_id");

    std::string lib_symbol_name;

    if (lib_id != 0)
    {
        lib_symbol_name =
            GetSecondChildText(lib_id);
    }

    uint32_t at =
        FindNamedChild(idx, "at");

    if (lib_id == 0 || at == 0)
    {
        return;
    }



    Component component;

    component.lib_id = lib_symbol_name;

    // ----------------------------------------------------
    // Location / Rotation
    // ----------------------------------------------------

    double rotation = 0.0;

    ExtractAt(
        at,
        component.location,
        rotation);

    component.rotation =
        rotation;

    // ----------------------------------------------------
    // Mirror / unit (KiCad instance fields)
    // ----------------------------------------------------

    const uint32_t mirror_node =
        FindNamedChild(idx, "mirror");
    if (mirror_node != 0)
    {
        uint32_t mchild = pool[mirror_node].first_child;
        if (mchild != 0)
        {
            mchild = pool[mchild].next_sibling;
        }
        if (mchild != 0)
        {
            const std::string axis = GetText(mchild);
            if (axis == "x")
            {
                component.mirror_x = true;
            }
            else if (axis == "y")
            {
                component.mirror_y = true;
            }
        }
    }

    const uint32_t unit_node =
        FindNamedChild(idx, "unit");
    if (unit_node != 0)
    {
        uint32_t uchild = pool[unit_node].first_child;
        if (uchild != 0)
        {
            uchild = pool[uchild].next_sibling;
        }
        if (uchild != 0)
        {
            component.unit = static_cast<int>(GetNumber(uchild));
            if (component.unit < 1)
            {
                component.unit = 1;
            }
        }
    }

    // ----------------------------------------------------
    // Debug Header
    // ----------------------------------------------------

    std::cout
        << "\n====================================\n";

    std::cout
        << "COMPONENT FOUND\n";

    std::cout
        << "====================================\n";

    std::cout
        << "Location : "
        << component.location.x
        << ", "
        << component.location.y
        << "\n";

    std::cout
        << "Rotation : "
        << component.rotation
        << "\n";

    // ----------------------------------------------------
    // Extract Properties
    // ----------------------------------------------------

    uint32_t child =
        pool[idx].first_child;

    while (child != 0)
    {
        if (NodeNameEquals(child, "property"))
        {
            std::cout
                << "Property Node Found\n";

            ExtractProperty(
                child,
                component);
        }

        child =
            pool[child].next_sibling;
    }

    // ----------------------------------------------------
    // Final Debug
    // ----------------------------------------------------

    std::cout
        << "Reference : "
        << component.reference
        << "\n";

    std::cout
        << "Value     : "
        << component.value
        << "\n";

    // ----------------------------------------------------
    // Extra dump if something looks wrong
    // ----------------------------------------------------

    if (component.reference.empty())
    {
        std::cout
            << "*** WARNING : EMPTY REFERENCE ***\n";

        child =
            pool[idx].first_child;

        while (child != 0)
        {
            const ASTNode& node =
                pool[child];

            if (node.type != NodeType::List)
            {
                std::cout
                    << "Child Text: "
                    << GetText(child)
                    << "\n";
            }

            child =
                node.next_sibling;
        }
    }

    // ----------------------------------------------------
    // Instance-specific references (reused hierarchical sheets)
    // ----------------------------------------------------

    const uint32_t instances =
        FindNamedChild(idx, "instances");
    if (instances != 0)
    {
        ExtractComponentInstances(
            instances,
            component);
    }

    // ----------------------------------------------------
    // Store Component
    // ----------------------------------------------------

    ExtractPins(
        idx,
        component);

    std::cout
        << "Pins Found : "
        << component.pins.size()
        << "\n";

    auto lib_it =
        library_symbols.find(
            lib_symbol_name);

    if (lib_it != library_symbols.end())
    {
        const LibrarySymbol& lib =
            lib_it->second;

        component.pins.reserve(
            lib.pins.size());

        for (const auto& lib_pin : lib.pins)
        {
            if (lib_pin.unit != 0 && lib_pin.unit != component.unit)
            {
                continue;
            }

            Pin pin;

            pin.number =
                lib_pin.number;

            pin.name =
                lib_pin.name;

            Point transformed =
                TransformLibOffset(
                    lib_pin.offset,
                    component.rotation,
                    component.mirror_x,
                    component.mirror_y);

            pin.location.x =
                component.location.x +
                transformed.x;

            pin.location.y =
                component.location.y +
                transformed.y;

            component.pins.push_back(pin);
        }
    }

    // ----------------------------------------------------
    // Convert KiCad Power Symbols Into Labels
    // (connection point = transformed power pin, not origin)
    //
    // Power instances are referenced #PWR…, #U01…, etc. (any '#' except
    // power-flags). Value is the net name (GND, +12V, …).
    // #FLG* (PWR_FLAG) must NOT become a net label — Value is "PWR_FLAG"
    // and would short every flagged net via MergeNamedNets.
    // ----------------------------------------------------

    const bool is_flag =
        component.reference.find("#FLG") == 0 ||
        component.value == "PWR_FLAG";
    const bool is_power_sym =
        !component.reference.empty() &&
        component.reference[0] == '#' &&
        !is_flag;

    if (is_power_sym && !component.value.empty())
    {
        NetLabel label;

        label.name =
            UnescapeKicadText(component.value);

        label.location =
            component.pins.empty()
                ? component.location
                : component.pins.front().location;

        label.type = LabelType::Global;

        schematic.labels.push_back(
            label);

        std::cout
            << "[POWER NET] "
            << label.name
            << " @ ("
            << label.location.x
            << ", "
            << label.location.y
            << ")\n";
    }

    schematic.components.push_back(
        component);

    std::cout
        << "Pins Found : "
        << component.pins.size()
        << "\n";

    if (component.reference == "U1")
    {
        for (size_t i = 0;
            i < component.pins.size() && i < 10;
            i++)
        {
            const auto& pin =
                component.pins[i];

            std::cout
                << pin.number
                << " "
                << pin.name
                << " @ "
                << pin.location.x
                << ", "
                << pin.location.y
                << "\n";
        }
    }

}

void Interpreter::ExtractPins(
    uint32_t idx,
    Component& component)
{
    (void)component;

    uint32_t child =
        pool[idx].first_child;

    while (child != 0)
    {
        if (NodeNameEquals(child, "pin"))
        {
            std::cout
                << "\nPIN NODE FOUND\n";

            uint32_t pin_child =
                pool[child].first_child;

            while (pin_child != 0)
            {
                std::cout
                    << "   CHILD : "
                    << GetText(pin_child)
                    << "\n";

                pin_child =
                    pool[pin_child].next_sibling;
            }
        }

        child =
            pool[child].next_sibling;
    }
}

void Interpreter::ExtractComponentInstances(
    uint32_t idx,
    Component& component)
{
    // (instances (project "..." (path "/uuid/..." (reference "R1") ...) ...))
    uint32_t child = pool[idx].first_child;
    if (child != 0)
    {
        child = pool[child].next_sibling;
    }

    while (child != 0)
    {
        if (NodeNameEquals(child, "project"))
        {
            uint32_t proj_child = pool[child].first_child;
            if (proj_child != 0)
            {
                proj_child = pool[proj_child].next_sibling;
            }
            // skip project name string
            if (proj_child != 0)
            {
                proj_child = pool[proj_child].next_sibling;
            }

            while (proj_child != 0)
            {
                if (NodeNameEquals(proj_child, "path"))
                {
                    std::string path;
                    std::string reference;

                    uint32_t path_child =
                        pool[proj_child].first_child;
                    if (path_child != 0)
                    {
                        path_child =
                            pool[path_child].next_sibling;
                    }
                    if (path_child != 0)
                    {
                        path = GetText(path_child);
                        path_child =
                            pool[path_child].next_sibling;
                    }

                    while (path_child != 0)
                    {
                        if (NodeNameEquals(path_child, "reference"))
                        {
                            reference =
                                GetSecondChildText(path_child);
                        }
                        path_child =
                            pool[path_child].next_sibling;
                    }

                    if (!path.empty() && !reference.empty())
                    {
                        component.instance_refs.emplace_back(
                            path,
                            reference);
                    }
                }
                proj_child =
                    pool[proj_child].next_sibling;
            }
        }
        child = pool[child].next_sibling;
    }
}

void Interpreter::ExtractSheetPin(
    uint32_t idx,
    SheetInstance& sheet)
{
    // (pin "NAME" shape (at x y rot) ...)
    SheetPin pin;

    uint32_t child = pool[idx].first_child;
    if (child == 0)
    {
        return;
    }
    child = pool[child].next_sibling;
    if (child == 0)
    {
        return;
    }

    pin.name = UnescapeKicadText(GetText(child));
    child = pool[child].next_sibling;
    if (child != 0 &&
        (pool[child].type == NodeType::Symbol ||
         pool[child].type == NodeType::String))
    {
        pin.shape = GetText(child);
        child = pool[child].next_sibling;
    }

    const uint32_t at = FindNamedChild(idx, "at");
    if (at != 0)
    {
        double rot = 0.0;
        ExtractAt(at, pin.location, rot);
        (void)rot;
    }

    sheet.pins.push_back(std::move(pin));
}

void Interpreter::ExtractSheet(uint32_t idx)
{
    SheetInstance sheet;

    const uint32_t at = FindNamedChild(idx, "at");
    if (at != 0)
    {
        double rot = 0.0;
        ExtractAt(at, sheet.at, rot);
        (void)rot;
    }

    const uint32_t size = FindNamedChild(idx, "size");
    if (size != 0)
    {
        uint32_t child = pool[size].first_child;
        if (child != 0)
        {
            child = pool[child].next_sibling;
        }
        if (child != 0)
        {
            sheet.size.x = GetNumber(child);
            child = pool[child].next_sibling;
        }
        if (child != 0)
        {
            sheet.size.y = GetNumber(child);
        }
    }

    const uint32_t uuid = FindNamedChild(idx, "uuid");
    if (uuid != 0)
    {
        sheet.uuid = GetSecondChildText(uuid);
    }

    uint32_t child = pool[idx].first_child;
    while (child != 0)
    {
        if (NodeNameEquals(child, "property"))
        {
            uint32_t p = pool[child].first_child;
            if (p != 0)
            {
                p = pool[p].next_sibling;
            }
            if (p == 0)
            {
                child = pool[child].next_sibling;
                continue;
            }
            const std::string pname = GetText(p);
            p = pool[p].next_sibling;
            if (p == 0)
            {
                child = pool[child].next_sibling;
                continue;
            }
            const std::string pvalue = GetText(p);
            if (pname == "Sheetname")
            {
                sheet.name = pvalue;
            }
            else if (pname == "Sheetfile")
            {
                sheet.file = pvalue;
            }
        }
        else if (NodeNameEquals(child, "pin"))
        {
            // Sheet hierarchical pins — not symbol pins.
            ExtractSheetPin(child, sheet);
        }

        child = pool[child].next_sibling;
    }

    schematic.sheets.push_back(std::move(sheet));
}

void Interpreter::ExtractNoConnect(uint32_t idx)
{
    const uint32_t at = FindNamedChild(idx, "at");
    if (at == 0)
    {
        return;
    }
    NoConnect nc;
    double rot = 0.0;
    if (!ExtractAt(at, nc.location, rot))
    {
        return;
    }
    schematic.no_connects.push_back(nc);
}

void Interpreter::ExtractBus(uint32_t idx)
{
    // Parse bus geometry only — do NOT expand members ({A B} / [0..N]).
    const uint32_t pts = FindNamedChild(idx, "pts");
    if (pts == 0)
    {
        return;
    }

    uint32_t child = pool[pts].first_child;
    if (child != 0)
    {
        child = pool[child].next_sibling;
    }

    Point p1{};
    Point p2{};
    bool found_first = false;

    while (child != 0)
    {
        if (NodeNameEquals(child, "xy"))
        {
            if (!found_first)
            {
                ExtractXY(child, p1);
                found_first = true;
            }
            else
            {
                ExtractXY(child, p2);
                BusSegment seg;
                seg.start = p1;
                seg.end = p2;
                schematic.buses.push_back(seg);
                p1 = p2;
            }
        }
        child = pool[child].next_sibling;
    }
}

void Interpreter::ExtractBusEntry(uint32_t idx)
{
    // bus_entry: (at x y) (size dx dy) — store as a short segment.
    const uint32_t at = FindNamedChild(idx, "at");
    if (at == 0)
    {
        return;
    }
    Point origin{};
    double rot = 0.0;
    if (!ExtractAt(at, origin, rot))
    {
        return;
    }

    Point delta{2.54, 2.54};
    const uint32_t size = FindNamedChild(idx, "size");
    if (size != 0)
    {
        uint32_t child = pool[size].first_child;
        if (child != 0)
        {
            child = pool[child].next_sibling;
        }
        if (child != 0)
        {
            delta.x = GetNumber(child);
            child = pool[child].next_sibling;
        }
        if (child != 0)
        {
            delta.y = GetNumber(child);
        }
    }

    BusSegment seg;
    seg.start = origin;
    seg.end.x = origin.x + delta.x;
    seg.end.y = origin.y + delta.y;
    schematic.buses.push_back(seg);
}

// ---------------------------------------------------------
// Label Handlers
// ---------------------------------------------------------
bool Interpreter::ExtractLabelData(uint32_t idx, NetLabel& label)
{
    uint32_t child = pool[idx].first_child;
    if (child == 0) return false;

    child = pool[child].next_sibling;
    bool found_name = false;

    while (child != 0)
    {
        if (pool[child].type == NodeType::String || pool[child].type == NodeType::Symbol)
        {
            label.name = GetText(child);
            found_name = true;
            break;
        }
        child = pool[child].next_sibling;
    }

    if (!found_name) return false;

    label.name = UnescapeKicadText(label.name);

    uint32_t at = FindNamedChild(idx, "at");
    if (at != 0)
    {
        double dummy = 0.0;
        ExtractAt(at, label.location, dummy);
    }
    return true;
}

// Added missing base function implementation
void Interpreter::ExtractLabel(uint32_t idx, NetLabel& label)
{
    if (ExtractLabelData(idx, label))
    {
        schematic.labels.push_back(label);
    }
}

void Interpreter::ExtractGlobalLabel(uint32_t idx)
{
    NetLabel label;
    label.type = LabelType::Global;

    if (ExtractLabelData(idx, label))
    {
        schematic.labels.push_back(label);
    }
}

void Interpreter::ExtractHierarchicalLabel(uint32_t idx)
{
    NetLabel label;
    label.type = LabelType::Hierarchical;

    if (ExtractLabelData(idx, label))
    {
        schematic.labels.push_back(label);
    }
}

void Interpreter::PrintLibrarySymbols() const
{
    std::cout
        << "\n====================\n"
        << "LIBRARY SYMBOLS\n"
        << "====================\n";

    for (const auto& kv : library_symbols)
    {
        std::cout
            << kv.first
            << "\n";
    }
}

// ---------------------------------------------------------
// Visit
// ---------------------------------------------------------
void Interpreter::Visit(uint32_t idx)
{
    if (idx == 0) return;

    bool is_top_level_entity = false;

    if (NodeNameEquals(idx, "wire"))
    {
        ExtractWire(idx);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "junction"))
    {
        ExtractJunction(idx);
        is_top_level_entity = true;
    }

    else if (NodeNameEquals(idx, "lib_symbols"))
    {
        ExtractLibrarySymbols(idx);

        is_top_level_entity = true;
    }

    else if (NodeNameEquals(idx, "symbol"))
    {
        ExtractComponent(idx);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "label"))
    {
        NetLabel label;
        label.type = LabelType::Local;
        ExtractLabel(idx, label);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "global_label"))
    {
        ExtractGlobalLabel(idx);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "hierarchical_label"))
    {
        ExtractHierarchicalLabel(idx);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "sheet"))
    {
        ExtractSheet(idx);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "no_connect"))
    {
        ExtractNoConnect(idx);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "bus"))
    {
        ExtractBus(idx);
        is_top_level_entity = true;
    }
    else if (NodeNameEquals(idx, "bus_entry"))
    {
        ExtractBusEntry(idx);
        is_top_level_entity = true;
    }

    if (!is_top_level_entity)
    {
        uint32_t child = pool[idx].first_child;
        while (child != 0)
        {
            Visit(child);
            child = pool[child].next_sibling;
        }
    }
}