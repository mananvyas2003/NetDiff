#include "geometry.h"
#include <cmath>



Mat3 Geometry :: Identity()
{
	Mat3 result{};

	result.m[0][0] = 1.0;
	result.m[1][1] = 1.0;
	result.m[2][2] = 1.0;

	return result;

}

Mat3 Geometry::Translation(double x, double y)
{
	Mat3 result = Identity();


	result.m[0][2] = x;
	result.m[1][2] = y;

	return result;

}

Mat3 Geometry::Rotation(double degrees)
{
	constexpr double PI = 3.14159265358979323846;

	double radians = degrees * PI / 180.0;

	double c = std::cos(radians);
	double s = std::sin(radians);
	Mat3 result = Identity();


	result.m[0][0] = c;
	result.m[0][1] = -s;


	result.m[1][0] = s;
	result.m[1][1] = c;

	return result;

}

Mat3 Geometry::Multiply(const Mat3& a, const Mat3& b)
{
	Mat3 result{};

	for (int row = 0; row < 3; row++)
	{
		for (int col = 0; col < 3; col++)
		{
			for (int k = 0; k < 3; k++)
			{
				result.m[row][col] += a.m[row][k] * b.m[k][col];

			}
		}
	}

	return result;

}

Point Geometry::TransformPoint(const Mat3& m, const Point& p)
{
	Point result;

	result.x =
		m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2];

	result.y =
		m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2];

	return result;


}