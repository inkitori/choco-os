#ifndef LLM_COMMON_H
#define LLM_COMMON_H

// Helpers shared by the in-kernel LLM engines (llm.c, qwen.c). This TU is
// compiled hard-float (SSE) like the engines; nothing here may be called
// from the soft-float kernel, and no float crosses the llm.h boundary.

#include <stdint.h>
#include "llm.h"

void llm_softmax(float *x, int size);

// Sorted-vocab token index with binary-search lookup.
typedef struct
{
	const char *str;
	int id;
} TokenIndex;

void tok_sort(TokenIndex *arr, int n);
int tok_lookup(const char *str, const TokenIndex *sorted_vocab, int vocab_size);

// Temperature / top-p sampler.
typedef struct
{
	float prob;
	int index;
} ProbIndex;

typedef struct
{
	int vocab_size;
	ProbIndex *probindex;
	float temperature;
	float topp;
	uint64_t rng_state;
} Sampler;

int llm_sample(Sampler *s, float *logits);

// ASCII-only terminal output: maps common UTF-8 punctuation, drops the rest.
void emit_sanitized(const char *piece, llm_emit_fn emit, void *ud);

#endif
