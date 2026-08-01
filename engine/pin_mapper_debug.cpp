#include "pin_mapper_debug.h"

#include <cmath>
#include <iostream>
#include <iomanip>

void PinMapperDebug::PrintComponent(
    const Component& component)
{
    std::cout
        << "\n====================================\n";

    std::cout
        << "COMPONENT DEBUG\n";

    std::cout
        << "====================================\n";

    std::cout
        << "Reference : "
        << component.reference
        << "\n";

    std::cout
        << "Value     : "
        << component.value
        << "\n";

    std::cout
        << "Position  : "
        << component.location.x
        << ", "
        << component.location.y
        << "\n";

    std::cout
        << "Rotation  : "
        << component.rotation
        << "\n";

    std::cout
        << "Pins      : "
        << component.pins.size()
        << "\n";

    for (const auto& pin : component.pins)
    {
        std::cout
            << "\n--------------------------------\n";

        std::cout
            << "Pin Number : "
            << pin.number
            << "\n";

        std::cout
            << "Pin Name   : "
            << pin.name
            << "\n";

        std::cout
            << "Local      : "
            << pin.location.x
            << ", "
            << pin.location.y
            << "\n";

        std::cout
            << "World      : "
            << pin.world_location.x
            << ", "
            << pin.world_location.y
            << "\n";
    }
}


void PinMapperDebug::PrintNearestNodes(
    const Component& component,
    const NetResolver& resolver)
{
    constexpr double SNAP_DISTANCE = 0.5;

    const auto& nodes =
        resolver.GetNodes();

    std::cout
        << "\n====================================\n";

    std::cout
        << "NEAREST NODE DEBUG\n";

    std::cout
        << "====================================\n";

    std::cout
        << "Component : "
        << component.reference
        << "\n\n";

    for (const auto& pin : component.pins)
    {
        uint32_t best_node =
            UINT32_MAX;

        double best_distance =
            1e9;

        Point nearest_position;

        for (const auto& node : nodes)
        {
            double dx =
                node.position.x -
                pin.world_location.x;

            double dy =
                node.position.y -
                pin.world_location.y;

            double dist =
                std::sqrt(
                    dx * dx +
                    dy * dy);

            if (dist < best_distance)
            {
                best_distance =
                    dist;

                best_node =
                    node.id;

                nearest_position =
                    node.position;
            }
        }

        std::cout
            << "------------------------------------\n";

        std::cout
            << "Pin : "
            << pin.number
            << " "
            << pin.name
            << "\n";

        std::cout
            << "Pin Position : "
            << pin.world_location.x
            << ", "
            << pin.world_location.y
            << "\n";

        std::cout
            << "Nearest Node : "
            << best_node
            << "\n";

        std::cout
            << "Node Position : "
            << nearest_position.x
            << ", "
            << nearest_position.y
            << "\n";

        std::cout
            << "Distance : "
            << best_distance;

        if (best_distance <= SNAP_DISTANCE)
        {
            std::cout
                << "  [CONNECTED]";
        }
        else
        {
            std::cout
                << "  [MISS]";
        }

        std::cout
            << "\n\n";
    }
}
