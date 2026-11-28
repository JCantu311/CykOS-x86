#ifndef _MATH_H
#define _MATH_H

double sin(double value);
double cos(double value);
double tan(double value);
double atan(double value);
double fabs(double value);
double floor(double value);
double frexp(double value, int* exponent);
double ldexp(double value, int exponent);
double exp(double value);
double log(double value);
double pow(double base, double exponent);
double fmod(double numerator, double denominator);

#define M_PI 3.14159265358979323846
#define HUGE_VAL __builtin_huge_val()
#define NAN __builtin_nan("")

#endif
