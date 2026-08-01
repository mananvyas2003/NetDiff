#pragma once

#include "geometry.h"
#include "interpreter.h"

class PinTransform
{
public:

    static void ComputeWorldPins(
        Component& component);

    static void PrintPins(
        const Component& component);
};