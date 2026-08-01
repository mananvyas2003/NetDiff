#pragma once

#include "interpreter.h"

struct Mat3
{
    double m[3][3];
};

namespace Geometry
{
    Mat3 Identity();

    Mat3 Translation(
        double x,
        double y);

    Mat3 Rotation(
        double degrees);

    Mat3 Multiply(
        const Mat3& a,
        const Mat3& b);

    Point TransformPoint(
        const Mat3& m,
        const Point& p);
}