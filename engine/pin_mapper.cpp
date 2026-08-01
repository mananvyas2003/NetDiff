#include "pin_mapper.h"
#include <cmath>


PinMapper::PinMapper
(const Schematic& s,
	const NetResolver& r) :
	schematic(s),
	resolver(r)
{
}

const std::vector<PinConnection>&
PinMapper::GetConnections() const
{
	return connections;
}



void PinMapper::Build()
{
    connections.clear();

    constexpr double SNAP_DISTANCE =
        0.5;

    const auto& nodes =
        resolver.GetNodes();

    const auto& nets =
        resolver.GetNets();

    for (const auto& comp :
        schematic.components)
    {
        for (const auto& pin :
            comp.pins)
        {
            uint32_t best_node =
                UINT32_MAX;

            double best_distance =
                1e9;

            //--------------------------------------------------
            // Find nearest node
            //--------------------------------------------------

            for (const auto& node :
                nodes)
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
                }
            }

            if (best_node == UINT32_MAX)
            {
                continue;
            }

            if (best_distance > SNAP_DISTANCE)
            {
                continue;
            }

            //--------------------------------------------------
            // Find net owning this node
            //--------------------------------------------------

            uint32_t found_net =
                UINT32_MAX;

            std::string net_name;

            for (const auto& net :
                nets)
            {
                for (uint32_t node_id :
                net.node_ids)
                {
                    if (node_id == best_node)
                    {
                        found_net =
                            net.id;

                        net_name =
                            net.name;

                        break;
                    }
                }

                if (found_net != UINT32_MAX)
                {
                    break;
                }
            }

            //--------------------------------------------------
            // Build connection
            //--------------------------------------------------

            PinConnection c;

            c.component_ref =
                comp.reference;

            c.pin_number =
                pin.number;

            c.pin_name =
                pin.name;

            c.node_id =
                best_node;

            c.net_id =
                found_net;

            c.net_name =
                net_name;

            connections.push_back(c);
        }
    }

    std::cout
        << "\nPinMapper Connections : "
        << connections.size()
        << "\n";

    
}


void PinMapper::PrintConnections() const
{
    std::cout
        << "\n====================================\n";

    std::cout
        << "PIN CONNECTION TABLE\n";

    std::cout
        << "====================================\n";

    for (const auto& c :
        connections)
    {
        std::cout
            << c.component_ref
            << "."
            << c.pin_name
            << "  ->  "
            << c.net_name
            << "\n";
    }

    


}

