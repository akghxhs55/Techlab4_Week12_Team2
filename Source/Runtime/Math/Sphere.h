#pragma once

#include "Matrix.h"
#include "Vector.h"
#include <algorithm>
#include <cmath>
#include <limits>

struct FSphere
{
    FSphere(FVector InCenter, float InRadius) : Center(InCenter), Radius(InRadius) {}

    FVector Center;
    float Radius;
};
