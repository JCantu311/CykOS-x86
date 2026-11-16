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

double log(double value)
{
    if (value != value || value < 0.0) return NAN;
    if (value == 0.0) return -HUGE_VAL;
    if (value == HUGE_VAL) return HUGE_VAL;

    int exponent = 0;
    while (value >= 1.5) { value *= 0.5; exponent++; }
    while (value < 0.75) { value *= 2.0; exponent--; }

    double z = (value - 1.0) / (value + 1.0);
    double z2 = z * z;
    double term = z;
    double sum = z;
    for (int denominator = 3; denominator <= 101; denominator += 2) {
        term *= z2;
        sum += term / denominator;
    }
    return 2.0 * sum + exponent * 0.69314718055994530942;
}

double exp(double value)
{
    if (value != value) return NAN;
    if (value > 709.0) return HUGE_VAL;
    if (value < -745.0) return 0.0;

    const double ln2 = 0.69314718055994530942;
    int exponent = (int)(value / ln2);
    double reduced = value - exponent * ln2;
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 32; n++) {
        term *= reduced / n;
        sum += term;
    }
    while (exponent > 0) { sum *= 2.0; exponent--; }
    while (exponent < 0) { sum *= 0.5; exponent++; }
    return sum;
}

double pow(double base, double exponent)
{
    if (exponent == 0.0 || base == 1.0) return 1.0;
    if (base != base || exponent != exponent) return NAN;
    if (base == 0.0) return exponent < 0.0 ? HUGE_VAL : 0.0;

    if (exponent >= -2147483648.0 && exponent <= 2147483647.0) {
        long integer_exponent = (long)exponent;
        if ((double)integer_exponent == exponent) {
            unsigned int power = integer_exponent < 0
                ? 0u - (unsigned int)integer_exponent
                : (unsigned int)integer_exponent;
            double result = 1.0;
            double factor = base;
            while (power) {
                if (power & 1u) result *= factor;
                power >>= 1;
                if (power) factor *= factor;
            }
            return integer_exponent < 0 ? 1.0 / result : result;
        }
    }

    if (base < 0.0) return NAN;
    return exp(exponent * log(base));
}

double fmod(double numerator, double denominator)
{
    if (numerator != numerator || denominator != denominator || denominator == 0.0)
        return NAN;
    if (numerator == HUGE_VAL || numerator == -HUGE_VAL) return NAN;
    if (denominator == HUGE_VAL || denominator == -HUGE_VAL) return numerator;

    double remainder = fabs(numerator);
    double divisor = fabs(denominator);
    if (remainder < divisor) return numerator;

    int scale = 0;
    while (divisor <= remainder * 0.5) {
        divisor *= 2.0;
        scale++;
    }
    while (scale >= 0) {
        if (remainder >= divisor) remainder -= divisor;
        divisor *= 0.5;
        scale--;
    }
    return numerator < 0.0 ? -remainder : remainder;
}
