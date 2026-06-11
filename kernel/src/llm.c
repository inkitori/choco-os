// Llama-2 architecture inference in the kernel. Port of Andrej Karpathy's
// llama2.c (MIT). Weights are read in place from Limine module memory;
// activations and the KV cache live on the kernel heap.
//
// This translation unit is compiled with SSE enabled (hard float); the
// kernel proper is soft-float. No float crosses the llm.h API boundary.
#include "llm.h"
#include "llm_math.h"
#include "malloc.h"
#include "string.h"
#include "kprintf.h"
#include "initrd.h"
#include "sched.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------- Transformer model ----------------

typedef struct
{
	int dim;		// transformer dimension
	int hidden_dim; // for ffn layers
	int n_layers;
	int n_heads;
	int n_kv_heads;
	int vocab_size; // byte-level: 32000 for llama2 tokenizer
	int seq_len;	// max sequence length
} Config;

typedef struct
{
	float *token_embedding_table; // (vocab_size, dim)
	float *rms_att_weight;		  // (layer, dim)
	float *rms_ffn_weight;		  // (layer, dim)
	float *wq;					  // (layer, dim, n_heads * head_size)
	float *wk;					  // (layer, dim, n_kv_heads * head_size)
	float *wv;					  // (layer, dim, n_kv_heads * head_size)
	float *wo;					  // (layer, n_heads * head_size, dim)
	float *w1;					  // (layer, hidden_dim, dim)
	float *w2;					  // (layer, dim, hidden_dim)
	float *w3;					  // (layer, hidden_dim, dim)
	float *rms_final_weight;	  // (dim,)
	float *wcls;				  // (vocab_size, dim) classifier
} TransformerWeights;

typedef struct
{
	float *x, *xb, *xb2;	  // activations (dim,)
	float *hb, *hb2;		  // ffn buffers (hidden_dim,)
	float *q;				  // query (dim,)
	float *att;				  // attention scores (n_heads, seq_len)
	float *logits;			  // (vocab_size,)
	float *key_cache;		  // (layer, seq_len, kv_dim)
	float *value_cache;		  // (layer, seq_len, kv_dim)
} RunState;

typedef struct
{
	Config config;
	TransformerWeights weights;
	RunState state;
} Transformer;

static void memory_map_weights(TransformerWeights *w, Config *p, float *ptr,
							   int shared_weights)
{
	int head_size = p->dim / p->n_heads;
	uint64_t n_layers = p->n_layers;

	w->token_embedding_table = ptr;
	ptr += (uint64_t)p->vocab_size * p->dim;
	w->rms_att_weight = ptr;
	ptr += n_layers * p->dim;
	w->wq = ptr;
	ptr += n_layers * p->dim * (p->n_heads * head_size);
	w->wk = ptr;
	ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
	w->wv = ptr;
	ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
	w->wo = ptr;
	ptr += n_layers * (p->n_heads * head_size) * p->dim;
	w->rms_ffn_weight = ptr;
	ptr += n_layers * p->dim;
	w->w1 = ptr;
	ptr += n_layers * p->dim * p->hidden_dim;
	w->w2 = ptr;
	ptr += n_layers * p->hidden_dim * p->dim;
	w->w3 = ptr;
	ptr += n_layers * p->dim * p->hidden_dim;
	w->rms_final_weight = ptr;
	ptr += p->dim;
	ptr += p->seq_len * head_size / 2; // skip legacy RoPE freq_cis_real
	ptr += p->seq_len * head_size / 2; // skip legacy RoPE freq_cis_imag
	w->wcls = shared_weights ? w->token_embedding_table : ptr;
}

static int alloc_run_state(RunState *s, Config *p)
{
	uint64_t kv_dim = ((uint64_t)p->dim * p->n_kv_heads) / p->n_heads;
	s->x = calloc(p->dim, sizeof(float));
	s->xb = calloc(p->dim, sizeof(float));
	s->xb2 = calloc(p->dim, sizeof(float));
	s->hb = calloc(p->hidden_dim, sizeof(float));
	s->hb2 = calloc(p->hidden_dim, sizeof(float));
	s->q = calloc(p->dim, sizeof(float));
	s->att = calloc((uint64_t)p->n_heads * p->seq_len, sizeof(float));
	s->logits = calloc(p->vocab_size, sizeof(float));
	s->key_cache = calloc((uint64_t)p->n_layers * p->seq_len * kv_dim, sizeof(float));
	s->value_cache = calloc((uint64_t)p->n_layers * p->seq_len * kv_dim, sizeof(float));

	if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q || !s->att ||
		!s->logits || !s->key_cache || !s->value_cache)
		return -1;
	return 0;
}

// ---------------- math kernels ----------------

static void rmsnorm(float *o, float *x, float *weight, int size)
{
	float ss = 0.0f;
	for (int j = 0; j < size; j++)
		ss += x[j] * x[j];
	ss /= size;
	ss += 1e-5f;
	ss = 1.0f / sqrtf_k(ss);
	for (int j = 0; j < size; j++)
		o[j] = weight[j] * (ss * x[j]);
}

static void softmax(float *x, int size)
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

// W (d,n) @ x (n,) -> xout (d,). Unrolled with 4 accumulators: this is
// where nearly all the cycles go, and TCG appreciates the ILP.
static void matmul(float *xout, const float *x, const float *w, int n, int d)
{
	for (int i = 0; i < d; i++)
	{
		const float *row = w + (uint64_t)i * n;
		float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
		int j = 0;
		for (; j + 4 <= n; j += 4)
		{
			a0 += row[j] * x[j];
			a1 += row[j + 1] * x[j + 1];
			a2 += row[j + 2] * x[j + 2];
			a3 += row[j + 3] * x[j + 3];
		}
		float val = (a0 + a1) + (a2 + a3);
		for (; j < n; j++)
			val += row[j] * x[j];
		xout[i] = val;
	}
}

static float *forward(Transformer *t, int token, int pos)
{
	Config *p = &t->config;
	TransformerWeights *w = &t->weights;
	RunState *s = &t->state;
	float *x = s->x;
	int dim = p->dim;
	int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
	int kv_mul = p->n_heads / p->n_kv_heads;
	int hidden_dim = p->hidden_dim;
	int head_size = dim / p->n_heads;

	// copy the token embedding into x
	memcpy(x, w->token_embedding_table + (uint64_t)token * dim,
		   dim * sizeof(float));

	for (uint64_t l = 0; l < (uint64_t)p->n_layers; l++)
	{
		rmsnorm(s->xb, x, w->rms_att_weight + l * dim, dim);

		// kv cache positions for this layer + pos
		uint64_t loff = l * p->seq_len * kv_dim;
		float *k = s->key_cache + loff + (uint64_t)pos * kv_dim;
		float *v = s->value_cache + loff + (uint64_t)pos * kv_dim;

		matmul(s->q, s->xb, w->wq + l * dim * dim, dim, dim);
		matmul(k, s->xb, w->wk + l * dim * kv_dim, dim, kv_dim);
		matmul(v, s->xb, w->wv + l * dim * kv_dim, dim, kv_dim);

		// RoPE: rotate q and k pairs with position-dependent frequencies
		for (int i = 0; i < dim; i += 2)
		{
			int head_dim = i % head_size;
			float freq = 1.0f / powf_k(10000.0f, head_dim / (float)head_size);
			float val = pos * freq;
			float fcr = cosf_k(val);
			float fci = sinf_k(val);
			int rotn = i < kv_dim ? 2 : 1; // rotate q and maybe k
			for (int vi = 0; vi < rotn; vi++)
			{
				float *vec = vi == 0 ? s->q : k;
				float v0 = vec[i];
				float v1 = vec[i + 1];
				vec[i] = v0 * fcr - v1 * fci;
				vec[i + 1] = v0 * fci + v1 * fcr;
			}
		}

		// multihead attention
		for (int h = 0; h < p->n_heads; h++)
		{
			float *q = s->q + (uint64_t)h * head_size;
			float *att = s->att + (uint64_t)h * p->seq_len;
			for (int tp = 0; tp <= pos; tp++)
			{
				float *kt = s->key_cache + loff + (uint64_t)tp * kv_dim +
							(uint64_t)(h / kv_mul) * head_size;
				float score = 0.0f;
				for (int i = 0; i < head_size; i++)
					score += q[i] * kt[i];
				att[tp] = score / sqrtf_k((float)head_size);
			}

			softmax(att, pos + 1);

			float *xb = s->xb + (uint64_t)h * head_size;
			memset(xb, 0, head_size * sizeof(float));
			for (int tp = 0; tp <= pos; tp++)
			{
				float *vt = s->value_cache + loff + (uint64_t)tp * kv_dim +
							(uint64_t)(h / kv_mul) * head_size;
				float a = att[tp];
				for (int i = 0; i < head_size; i++)
					xb[i] += a * vt[i];
			}
		}

		matmul(s->xb2, s->xb, w->wo + l * dim * dim, dim, dim);
		for (int i = 0; i < dim; i++)
			x[i] += s->xb2[i];

		// ffn: self.w2(F.silu(self.w1(x)) * self.w3(x))
		rmsnorm(s->xb, x, w->rms_ffn_weight + l * dim, dim);
		matmul(s->hb, s->xb, w->w1 + l * dim * hidden_dim, dim, hidden_dim);
		matmul(s->hb2, s->xb, w->w3 + l * dim * hidden_dim, dim, hidden_dim);
		for (int i = 0; i < hidden_dim; i++)
		{
			float val = s->hb[i];
			val *= (1.0f / (1.0f + expf_k(-val))); // silu
			val *= s->hb2[i];
			s->hb[i] = val;
		}
		matmul(s->xb, s->hb, w->w2 + l * dim * hidden_dim, hidden_dim, dim);
		for (int i = 0; i < dim; i++)
			x[i] += s->xb[i];
	}

	rmsnorm(x, x, w->rms_final_weight, dim);
	matmul(s->logits, x, w->wcls, p->dim, p->vocab_size);
	return s->logits;
}

// ---------------- tokenizer ----------------

typedef struct
{
	const char *str;
	int id;
} TokenIndex;

typedef struct
{
	char **vocab;
	float *vocab_scores;
	TokenIndex *sorted_vocab;
	int vocab_size;
	unsigned int max_token_length;
	unsigned char byte_pieces[512]; // stores all single-byte strings
} Tokenizer;

static int compare_tokens(const TokenIndex *a, const TokenIndex *b)
{
	return strcmp(a->str, b->str);
}

// In-place heapsort: no recursion, no libc qsort.
static void sort_tokens(TokenIndex *arr, int n)
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

static int str_lookup(const char *str, TokenIndex *sorted_vocab, int vocab_size)
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

static int build_tokenizer(Tokenizer *t, const uint8_t *data, uint64_t size,
						   int vocab_size)
{
	t->vocab_size = vocab_size;
	t->vocab = malloc(vocab_size * sizeof(char *));
	t->vocab_scores = malloc(vocab_size * sizeof(float));
	if (!t->vocab || !t->vocab_scores)
		return -1;

	for (int i = 0; i < 256; i++)
	{
		t->byte_pieces[i * 2] = (unsigned char)i;
		t->byte_pieces[i * 2 + 1] = '\0';
	}

	const uint8_t *p = data;
	const uint8_t *end = data + size;
	if (p + 4 > end)
		return -1;
	memcpy(&t->max_token_length, p, 4);
	p += 4;

	for (int i = 0; i < vocab_size; i++)
	{
		if (p + 8 > end)
			return -1;
		memcpy(&t->vocab_scores[i], p, 4);
		p += 4;
		int len;
		memcpy(&len, p, 4);
		p += 4;
		if (len < 0 || p + len > end)
			return -1;
		t->vocab[i] = malloc(len + 1);
		if (!t->vocab[i])
			return -1;
		memcpy(t->vocab[i], p, len);
		t->vocab[i][len] = '\0';
		p += len;
	}

	t->sorted_vocab = malloc(vocab_size * sizeof(TokenIndex));
	if (!t->sorted_vocab)
		return -1;
	for (int i = 0; i < vocab_size; i++)
	{
		t->sorted_vocab[i].str = t->vocab[i];
		t->sorted_vocab[i].id = i;
	}
	sort_tokens(t->sorted_vocab, vocab_size);
	return 0;
}

static const char *decode(Tokenizer *t, int prev_token, int token)
{
	const char *piece = t->vocab[token];
	// after BOS(1), sentencepiece strips the leading space
	if (prev_token == 1 && piece[0] == ' ')
		piece++;
	// raw byte tokens look like "<0x0A>"
	if (piece[0] == '<' && piece[1] == '0' && piece[2] == 'x' && piece[5] == '>')
	{
		int hi = piece[3] >= 'A' ? piece[3] - 'A' + 10 : piece[3] - '0';
		int lo = piece[4] >= 'A' ? piece[4] - 'A' + 10 : piece[4] - '0';
		int byte_val = hi * 16 + lo;
		return (const char *)t->byte_pieces + byte_val * 2;
	}
	return piece;
}

// BPE-encode text into tokens. Caller provides tokens[] sized strlen+3.
static void encode(Tokenizer *t, const char *text, int bos, int eos,
				   int *tokens, int *n_tokens)
{
	char *str_buffer = malloc(t->max_token_length * 2 + 1 + 2);
	uint64_t str_len = 0;
	*n_tokens = 0;

	if (bos)
		tokens[(*n_tokens)++] = 1;

	// dummy prefix: sentencepiece prepends a space to non-empty input
	if (text[0] != '\0')
	{
		int dummy_prefix = str_lookup(" ", t->sorted_vocab, t->vocab_size);
		if (dummy_prefix != -1)
			tokens[(*n_tokens)++] = dummy_prefix;
	}

	// process raw bytes, merging UTF-8 sequences into single codepoints
	for (const char *c = text; *c != '\0'; c++)
	{
		if ((*c & 0xC0) != 0x80)
			str_len = 0; // not a continuation byte: start new codepoint

		str_buffer[str_len++] = *c;
		str_buffer[str_len] = '\0';

		// defer lookup if the next byte continues this codepoint
		if ((*(c + 1) & 0xC0) == 0x80 && str_len < 4)
			continue;

		int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
		if (id != -1)
		{
			tokens[(*n_tokens)++] = id;
		}
		else
		{
			// byte fallback: each byte becomes token (byte + 3)
			for (uint64_t i = 0; i < str_len; i++)
				tokens[(*n_tokens)++] = (unsigned char)str_buffer[i] + 3;
		}
		str_len = 0;
	}

	// greedy BPE merges: repeatedly merge the best-scoring pair
	for (;;)
	{
		float best_score = -1e10f;
		int best_id = -1;
		int best_idx = -1;

		for (int i = 0; i < (*n_tokens - 1); i++)
		{
			snprintf(str_buffer, t->max_token_length * 2 + 3, "%s%s",
					 t->vocab[tokens[i]], t->vocab[tokens[i + 1]]);
			int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
			if (id != -1 && t->vocab_scores[id] > best_score)
			{
				best_score = t->vocab_scores[id];
				best_id = id;
				best_idx = i;
			}
		}

		if (best_idx == -1)
			break;

		tokens[best_idx] = best_id;
		for (int i = best_idx + 1; i < (*n_tokens - 1); i++)
			tokens[i] = tokens[i + 1];
		(*n_tokens)--;
	}

	if (eos)
		tokens[(*n_tokens)++] = 2;

	free(str_buffer);
}

// The terminal font is ASCII-only; map common UTF-8 punctuation the model
// emits (smart quotes, dashes) to ASCII and drop anything else non-printable.
static void emit_sanitized(const char *piece, llm_emit_fn emit, void *ud)
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

// ---------------- sampler ----------------

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

static int sample(Sampler *s, float *logits)
{
	if (s->temperature == 0.0f)
		return sample_argmax(logits, s->vocab_size);

	for (int q = 0; q < s->vocab_size; q++)
		logits[q] /= s->temperature;
	softmax(logits, s->vocab_size);

	float coin = random_f32(&s->rng_state);
	if (s->topp <= 0 || s->topp >= 1)
		return sample_mult(logits, s->vocab_size, coin);
	return sample_topp(logits, s->vocab_size, s->topp, s->probindex, coin);
}

// ---------------- model registry ----------------

typedef struct
{
	char name[32];
	bool loaded;
	Transformer transformer;
	Tokenizer tokenizer;
	char describe[128];
} ModelSlot;

static ModelSlot slots[2];
static volatile bool llm_busy = false;

static const char *tokenizer_for(const char *model)
{
	if (strcmp(model, "stories260K") == 0)
		return "tok512.bin";
	return "tokenizer.bin";
}

static ModelSlot *load_model(const char *model)
{
	for (unsigned i = 0; i < 2; i++)
		if (slots[i].loaded && strcmp(slots[i].name, model) == 0)
			return &slots[i];

	ModelSlot *slot = NULL;
	for (unsigned i = 0; i < 2; i++)
		if (!slots[i].loaded)
		{
			slot = &slots[i];
			break;
		}
	if (!slot)
		return NULL; // both slots taken (only 2 models exist anyway)

	char fname[64];
	snprintf(fname, sizeof(fname), "%s.bin", model);
	struct limine_file *mod = module_find(fname);
	if (!mod)
		return NULL;

	struct limine_file *tok = module_find(tokenizer_for(model));
	if (!tok)
		return NULL;

	Transformer *t = &slot->transformer;
	memcpy(&t->config, mod->address, sizeof(Config));
	int shared_weights = t->config.vocab_size > 0 ? 1 : 0;
	if (t->config.vocab_size < 0)
		t->config.vocab_size = -t->config.vocab_size;

	float *weights_ptr = (float *)((uint8_t *)mod->address + sizeof(Config));
	memory_map_weights(&t->weights, &t->config, weights_ptr, shared_weights);

	if (alloc_run_state(&t->state, &t->config) < 0)
		return NULL;

	if (build_tokenizer(&slot->tokenizer, tok->address, tok->size,
						t->config.vocab_size) < 0)
		return NULL;

	snprintf(slot->describe, sizeof(slot->describe),
			 "dim=%d hidden=%d layers=%d heads=%d kv_heads=%d vocab=%d ctx=%d",
			 t->config.dim, t->config.hidden_dim, t->config.n_layers,
			 t->config.n_heads, t->config.n_kv_heads, t->config.vocab_size,
			 t->config.seq_len);

	strlcpy(slot->name, model, sizeof(slot->name));
	slot->loaded = true;
	kprintf("llm: loaded %s (%s)\n", model, slot->describe);
	return slot;
}

const char *llm_describe(const char *model)
{
	ModelSlot *slot = load_model(model);
	return slot ? slot->describe : NULL;
}

int llm_generate(const char *model, const char *prompt, int max_tokens,
				 int temp_centi, int topp_centi, unsigned long seed,
				 int echo_prompt, llm_emit_fn emit, void *ud)
{
	if (llm_busy)
		return LLM_ERR_BUSY;
	llm_busy = true;

	ModelSlot *slot = load_model(model);
	if (!slot)
	{
		llm_busy = false;
		return LLM_ERR_NO_MODEL;
	}

	Transformer *t = &slot->transformer;
	Tokenizer *tok = &slot->tokenizer;

	Sampler sampler;
	sampler.vocab_size = t->config.vocab_size;
	sampler.temperature = temp_centi / 100.0f;
	sampler.topp = topp_centi / 100.0f;
	sampler.rng_state = seed ? seed : 42;
	sampler.probindex = malloc(sampler.vocab_size * sizeof(ProbIndex));
	if (!sampler.probindex)
	{
		llm_busy = false;
		return LLM_ERR_OOM;
	}

	int steps = t->config.seq_len;
	if (max_tokens > 0 && max_tokens < steps)
		steps = max_tokens;

	int *prompt_tokens = malloc((strlen(prompt) + 3) * sizeof(int));
	if (!prompt_tokens)
	{
		free(sampler.probindex);
		llm_busy = false;
		return LLM_ERR_OOM;
	}
	int num_prompt_tokens = 0;
	encode(tok, prompt, 1, 0, prompt_tokens, &num_prompt_tokens);
	if (num_prompt_tokens < 1)
	{
		prompt_tokens[num_prompt_tokens++] = 1; // BOS
	}

	int token = prompt_tokens[0];
	int pos = 0;
	int generated = 0;

	while (pos < steps)
	{
		float *logits = forward(t, token, pos);

		int next;
		bool sampled = false;
		if (pos < num_prompt_tokens - 1)
			next = prompt_tokens[pos + 1];
		else
		{
			next = sample(&sampler, logits);
			sampled = true;
			generated++;
		}
		pos++;

		if (next == 1 || next == 2) // BOS/EOS ends the story
			break;

		// During prefill the caller already knows the prompt; only emit it
		// back when echo_prompt is set (the chat TUI wants the full story).
		if (sampled || echo_prompt)
		{
			const char *piece = decode(tok, token, next);
			if (piece && piece[0] != '\0')
				emit_sanitized(piece, emit, ud);
		}
		token = next;

		// stay polite: let other threads (shell, UI) run between tokens
		if (sched_active())
			sched_yield();
	}

	free(prompt_tokens);
	free(sampler.probindex);
	llm_busy = false;
	return generated;
}
