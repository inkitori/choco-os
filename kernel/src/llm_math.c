// Freestanding float math, accurate enough for LLM inference.
// Compiled with SSE; uses double precision internally where it is cheap.
#include "llm_math.h"

#include <stdint.h>

float fabsf_k(float x)
{
	return x < 0.0f ? -x : x;
}

float sqrtf_k(float x)
{
	float r;
	__asm__("sqrtss %1, %0" : "=x"(r) : "x"(x));
	return r;
}

// exp(x) = 2^(x/ln2): split into integer + fractional power of two.
float expf_k(float x)
{
	if (x > 88.0f)
		return 3.4e38f;
	if (x < -87.0f)
		return 0.0f;

	double xd = (double)x * 1.4426950408889634074; // x / ln(2)
	int n = (int)(xd + (xd >= 0 ? 0.5 : -0.5));
	double f = xd - n; // in [-0.5, 0.5]

	// 2^f for f in [-0.5,0.5]: degree-6 minimax-ish polynomial of exp(f*ln2)
	double t = f * 0.69314718055994530942;
	double p = 1.0 +
			   t * (1.0 +
					t * (0.5 +
						 t * (1.0 / 6.0 +
							  t * (1.0 / 24.0 +
								   t * (1.0 / 120.0 + t * (1.0 / 720.0))))));

	// scale by 2^n via exponent bits
	union
	{
		double d;
		uint64_t u;
	} sc;
	sc.u = (uint64_t)(1023 + n) << 52;
	return (float)(p * sc.d);
}

float logf_k(float x)
{
	if (x <= 0.0f)
		return -1e38f;

	union
	{
		float f;
		uint32_t u;
	} v = {x};

	int e = (int)((v.u >> 23) & 0xFF) - 127;
	v.u = (v.u & 0x007FFFFF) | 0x3F800000; // mantissa in [1,2)
	double m = (double)v.f;

	// log(m) via atanh identity: log(m) = 2*atanh((m-1)/(m+1))
	double s = (m - 1.0) / (m + 1.0);
	double s2 = s * s;
	double r = 2.0 * s *
			   (1.0 + s2 * (1.0 / 3.0 +
							s2 * (1.0 / 5.0 + s2 * (1.0 / 7.0 + s2 / 9.0))));

	return (float)(r + e * 0.69314718055994530942);
}

float powf_k(float a, float b)
{
	if (a <= 0.0f)
		return 0.0f;
	return expf_k(b * logf_k(a));
}

// sin via range reduction to [-pi, pi] then Taylor-ish polynomial.
static double sin_poly(double x)
{
	double x2 = x * x;
	return x * (1.0 +
				x2 * (-1.0 / 6.0 +
					  x2 * (1.0 / 120.0 +
							x2 * (-1.0 / 5040.0 +
								  x2 * (1.0 / 362880.0 +
										x2 * (-1.0 / 39916800.0))))));
}

float sinf_k(float x)
{
	const double TWO_PI = 6.283185307179586477;
	double xd = (double)x;
	double k = xd / TWO_PI;
	k = (double)(long)(k + (k >= 0 ? 0.5 : -0.5));
	xd -= k * TWO_PI; // now in [-pi, pi]
	return (float)sin_poly(xd);
}

float cosf_k(float x)
{
	return sinf_k(x + 1.5707963267948966f);
}
