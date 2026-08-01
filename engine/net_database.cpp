#include "net_database.h"
#include<iostream>


NetDatabase::NetDatabase(const PinMapper& m) : mapper(m)
{
}

void NetDatabase :: Build()
{
	nets.clear();
	const auto& connections = mapper.GetConnections();

	for (size_t i = 0; i < connections.size(); i++)
	{
		const auto& connection = connections[i];

		auto& net = nets[connection.net_name];
		net.name = connection.net_name;

		net.pins.push_back(i);

	}

	std::cout << "\nNetDatabase Built\n";
	std::cout << "Unique Nets :" << nets.size() << "\n";


}

const NetRecord* NetDatabase::FindNet(const std::string& name) const
{
	auto it = nets.find(name);

	if (it == nets.end()) return nullptr;

	return &it->second;

	
}

void NetDatabase::Print() const
{
    std::cout
        << "\n====================================\n";

    std::cout
        << "NET DATABASE\n";

    std::cout
        << "====================================\n";

    const auto& connections =
        mapper.GetConnections();

    for (const auto& pair : nets)
    {
        const NetRecord& net =
            pair.second;

        std::cout
            << "\nNET : "
            << net.name
            << "\n";

        std::cout
            << "Pins :\n";

        for (size_t index : net.pins)
        {
            const auto& c =
                connections[index];

            std::cout
                << "   "
                << c.component_ref
                << "."
                << c.pin_name
                << "\n";
        }
    }
}



