#include "pin_transform.h"

#include <iostream>

void PinTransform::ComputeWorldPins(
    Component& component)
{
    for (auto& pin : component.pins)
    {
        pin.world_location =
            pin.location;
    }
}


void PinTransform::PrintPins(
    const Component& component)
{
    std::cout
        << "\n================================\n";

    std::cout
        << component.reference
        << " WORLD PINS\n";

    std::cout
        << "================================\n";

    std::cout
        << "Component Position : "
        << component.location.x
        << ", "
        << component.location.y
        << "\n";

    std::cout
        << "Rotation : "
        << component.rotation
        << "\n";

    std::cout
        << "Pin Count : "
        << component.pins.size()
        << "\n\n";

    for (const auto& pin : component.pins)
    {
        std::cout
            << "Pin "
            << pin.number
            << " : "
            << pin.name
            << "\n";

        std::cout
            << "   Local : "
            << pin.location.x
            << ", "
            << pin.location.y
            << "\n";

        std::cout
            << "   World : "
            << pin.world_location.x
            << ", "
            << pin.world_location.y
            << "\n\n";
    }

    for (const auto& pin : component.pins)
    {
        if (
            pin.name == "VDD" ||
            pin.name == "GND" ||
            pin.name == "IO21" ||
            pin.name == "IO22")
        {
            std::cout
                << "\n"
                << pin.name
                << "\n";

            std::cout
                << "Pin Location : "
                << pin.location.x
                << ", "
                << pin.location.y
                << "\n";

            std::cout
                << "World Location : "
                << pin.world_location.x
                << ", "
                << pin.world_location.y
                << "\n";
        }
    }
}