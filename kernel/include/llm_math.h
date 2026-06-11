#ifndef LLM_MATH_H
#define LLM_MATH_H

// Minimal float math for the in-kernel LLM. Only compiled into hard-float
// translation units (llm.c etc.); the rest of the kernel is soft-float and
// must not call these.
float expf_k(float x);
float logf_k(float x);
float powf_k(float a, float b);
float sqrtf_k(float x);
float sinf_k(float x);
float cosf_k(float x);
float fabsf_k(float x);

#endif
