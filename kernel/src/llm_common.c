// Helpers shared by the LLM engines: softmax, sorted-vocab lookup, the
// temperature/top-p sampler, and ASCII-safe terminal emission. Compiled
// hard-float (SSE); see llm.h for the float-boundary rules.
#include "llm_common.h"
#include "llm_math.h"
#include "string.h"

#include <stdint.h>

void llm_softmax(float *x, int size)
{
	float max_val = x[0];
	for (int i = 1; i < size; i++)
		if (x[i] > max_val)
			max_val = x[i];
	float sum = 0.0f;
	for (int i = 0; i < size; i++)
	{
		x[i] = expf_k(x[i] - max_val);
		sum += x[i];
	}
	for (int i = 0; i < size; i++)
		x[i] /= sum;
}

// ---------------- sorted vocab ----------------

static int compare_tokens(const TokenIndex *a, const TokenIndex *b)
{
	return strcmp(a->str, b->str);
}

// In-place heapsort: no recursion, no libc qsort.
void tok_sort(TokenIndex *arr, int n)
{
	for (int start = n / 2 - 1; start >= 0; start--)
	{
		int root = start;
		for (;;)
		{
			int child = 2 * root + 1;
			if (child >= n)
				break;
			if (child + 1 < n && compare_tokens(&arr[child], &arr[child + 1]) < 0)
				child++;
			if (compare_tokens(&arr[root], &arr[child]) >= 0)
				break;
			TokenIndex tmp = arr[root];
			arr[root] = arr[child];
			arr[child] = tmp;
			root = child;
		}
	}
	for (int end = n - 1; end > 0; end--)
	{
		TokenIndex tmp = arr[0];
		arr[0] = arr[end];
		arr[end] = tmp;
		int root = 0;
		for (;;)
		{
			int child = 2 * root + 1;
			if (child >= end)
				break;
			if (child + 1 < end && compare_tokens(&arr[child], &arr[child + 1]) < 0)
				child++;
			if (compare_tokens(&arr[root], &arr[child]) >= 0)
				break;
			TokenIndex t2 = arr[root];
			arr[root] = arr[child];
			arr[child] = t2;
			root = child;
		}
	}
}

int tok_lookup(const char *str, const TokenIndex *sorted_vocab, int vocab_size)
{
	int lo = 0, hi = vocab_size - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		int c = strcmp(str, sorted_vocab[mid].str);
		if (c == 0)
			return sorted_vocab[mid].id;
		if (c < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return -1;
}

// ---------------- sampler ----------------

static unsigned int random_u32(uint64_t *state)
{
	*state ^= *state >> 12;
	*state ^= *state << 25;
	*state ^= *state >> 27;
	return (*state * 0x2545F4914F6CDD1Dull) >> 32;
}

static float random_f32(uint64_t *state)
{
	return (random_u32(state) >> 8) / 16777216.0f;
}

static int sample_argmax(float *probabilities, int n)
{
	int max_i = 0;
	float max_p = probabilities[0];
	for (int i = 1; i < n; i++)
	{
		if (probabilities[i] > max_p)
		{
			max_i = i;
			max_p = probabilities[i];
		}
	}
	return max_i;
}

static int sample_mult(float *probabilities, int n, float coin)
{
	float cdf = 0.0f;
	for (int i = 0; i < n; i++)
	{
		cdf += probabilities[i];
		if (coin < cdf)
			return i;
	}
	return n - 1;
}

static void sort_probindex(ProbIndex *arr, int n)
{
	// insertion sort descending by prob (the top-p candidate list is small)
	for (int i = 1; i < n; i++)
	{
		ProbIndex key = arr[i];
		int j = i - 1;
		while (j >= 0 && arr[j].prob < key.prob)
		{
			arr[j + 1] = arr[j];
			j--;
		}
		arr[j + 1] = key;
	}
}

static int sample_topp(float *probabilities, int n, float topp,
					   ProbIndex *probindex, float coin)
{
	int n0 = 0;
	// cut tokens whose probability can't make the nucleus
	const float cutoff = (1.0f - topp) / (n - 1);
	for (int i = 0; i < n; i++)
	{
		if (probabilities[i] >= cutoff)
		{
			probindex[n0].index = i;
			probindex[n0].prob = probabilities[i];
			n0++;
		}
	}
	sort_probindex(probindex, n0);

	float cumulative_prob = 0.0f;
	int last_idx = n0 - 1;
	for (int i = 0; i < n0; i++)
	{
		cumulative_prob += probindex[i].prob;
		if (cumulative_prob > topp)
		{
			last_idx = i;
			break;
		}
	}

	float r = coin * cumulative_prob;
	float cdf = 0.0f;
	for (int i = 0; i <= last_idx; i++)
	{
		cdf += probindex[i].prob;
		if (r < cdf)
			return probindex[i].index;
	}
	return probindex[last_idx].index;
}

int llm_sample(Sampler *s, float *logits)
{
	if (s->temperature == 0.0f)
		return sample_argmax(logits, s->vocab_size);

	for (int q = 0; q < s->vocab_size; q++)
		logits[q] /= s->temperature;
	llm_softmax(logits, s->vocab_size);

	float coin = random_f32(&s->rng_state);
	if (s->topp <= 0 || s->topp >= 1)
		return sample_mult(logits, s->vocab_size, coin);
	return sample_topp(logits, s->vocab_size, s->topp, s->probindex, coin);
}

// The terminal font is ASCII-only; map common UTF-8 punctuation the model
// emits (smart quotes, dashes) to ASCII and drop anything else non-printable.
void emit_sanitized(const char *piece, llm_emit_fn emit, void *ud)
{
	char out[80];
	size_t o = 0;
	const unsigned char *p = (const unsigned char *)piece;

	while (*p && o < sizeof(out) - 4)
	{
		unsigned char c = *p;
		if (c == '\n' || c == '\t' || (c >= 32 && c < 127))
		{
			out[o++] = (char)c;
			p++;
			continue;
		}

		unsigned int cp = 0;
		if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
		{
			cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
			p += 2;
		}
		else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
				 (p[2] & 0xC0) == 0x80)
		{
			cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			p += 3;
		}
		else
		{
			p++;
			continue;
		}

		switch (cp)
		{
		case 0x2018:
		case 0x2019:
			out[o++] = '\'';
			break;
		case 0x201C:
		case 0x201D:
			out[o++] = '"';
			break;
		case 0x2013:
		case 0x2014:
			out[o++] = '-';
			break;
		case 0x2026:
			out[o++] = '.';
			out[o++] = '.';
			out[o++] = '.';
			break;
		default:
			break; // drop
		}
	}

	if (o > 0)
	{
		out[o] = '\0';
		emit(out, ud);
	}
}
