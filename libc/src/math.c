#include <math.h>

double fabs(double value)
{
    return value < 0.0 ? -value : value;
}

static double reduce_angle(double value)
{
    while (value > M_PI) value -= 2.0 * M_PI;
    while (value < -M_PI) value += 2.0 * M_PI;
    return value;
}

double sin(double value)
{
    value = reduce_angle(value);
    if (value > M_PI / 2.0) value = M_PI - value;
    else if (value < -M_PI / 2.0) value = -M_PI - value;

    double square = value * value;
    double term = value;
    double result = value;
    for (int n = 1; n <= 8; ++n) {
        term *= -square / ((2.0 * n) * (2.0 * n + 1.0));
        result += term;
    }
    return result;
}

double cos(double value)
{
    return sin(value + M_PI / 2.0);
}

double tan(double value)
{
    double denominator = cos(value);
    if (fabs(denominator) < 1e-12)
        return value < 0.0 ? -1e12 : 1e12;
    return sin(value) / denominator;
}

double atan(double value)
{
    int negative = value < 0.0;
    int reciprocal = 0;
    if (negative) value = -value;

    double offset = 0.0;
    if (value > 1.0) {
        value = 1.0 / value;
        offset = M_PI / 2.0;
        reciprocal = 1;
    }
    if (value > 0.4142135623730950) {
        value = (value - 1.0) / (value + 1.0);
        offset += M_PI / 4.0;
    }

    double square = value * value;
    double term = value;
    double result = value;
    for (int n = 1; n <= 16; ++n) {
        term *= -square;
        result += term / (2.0 * n + 1.0);
    }

    result = reciprocal ? offset - result : offset + result;
    return negative ? -result : result;
}
