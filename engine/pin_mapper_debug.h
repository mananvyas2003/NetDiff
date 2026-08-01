#pragma once

#include "pin_mapper.h"
#include "net_resolver.h"

class PinMapperDebug
{
public:

    static void PrintComponent(
        const Component& component);

    static void PrintNearestNodes(
        const Component& component,
        const NetResolver& resolver);

    static void VerifyConnections(
        const PinMapper& mapper);

    static void PrintStatistics(
        const PinMapper& mapper);
};