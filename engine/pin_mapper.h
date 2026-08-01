#pragma once
#include "interpreter.h"
#include "net_resolver.h"

struct PinConnection
{
	std::string component_ref;
	std::string pin_number;
	std::string pin_name;

	uint32_t node_id;
	uint32_t net_id;

	std::string net_name;


};

class PinMapper
{
private:

	const Schematic& schematic;
	const NetResolver& resolver;

	std::vector<PinConnection> connections;

public:
	PinMapper(const Schematic&s,
		const NetResolver&r
		);

	void Build();

	void PrintConnections() const;
	
	const std::vector<PinConnection>& GetConnections() const;




};

