// Qwen3 inference in the kernel, int8 (Q8_0) forward pass. Port of Adrian
// Cable's qwen3.c (MIT), adapted for freestanding use: weights are read in
// place from Limine module memory, the token-embedding row is dequantized
// on the fly instead of expanding the whole table to fp32 (which would cost
// ~620 MiB for the 0.6B model), and the context is capped so the KV cache
// fits the kernel heap. Compiled hard-float (SSE); see llm.h.
//
// Differences from the llama2.c engine in llm.c: byte-level BPE with
// special tokens instead of sentencepiece, QK-RMSNorm, half-split RoPE
// (theta 1e6), head_dim independent of dim, and quantized matmuls.
#include "qwen.h"
#include "llm_common.h"
#include "llm_math.h"
#include "malloc.h"
#include "string.h"
#include "kprintf.h"
#include "initrd.h"
#include "sched.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define QWEN_MODEL_FILE "qwen3.bin"
#define QWEN_TOKENIZER_FILE "qwen3.tokenizer"
#define QWEN_MAGIC 0x616A6331 // "ajc1", the qwen3.c checkpoint magic
#define QWEN_CTX_MAX 1024	  // KV cache for the full 40k context is ~9 GiB

// Qwen3 chat template (non-thinking), as rendered by tools/export_qwen3.py.
static const char TMPL_PRE[] = "<|im_start|>user\n";
static const char TMPL_POST[] =
	"<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";

// ---------------- model ----------------

typedef struct
{
	int dim;
	int hidden_dim;
	int n_layers;
	int n_heads;
	int n_kv_heads;
	int vocab_size;
	int seq_len;
	int head_dim;
	int shared_classifier;
	int group_size;
} Config;

typedef struct
{
	const int8_t *q; // quantized values
	const float *s;	 // per-group scale factors
} QuantizedTensor;

typedef struct
{
	QuantizedTensor q_tokens;  // (vocab_size, dim), dequantized per row
	const float *rms_att_weight; // (layer, dim)
	const float *rms_ffn_weight; // (layer, dim)
	const float *rms_final_weight; // (dim,)
	const float *q_norm_weight;	 // (layer, head_dim)
	const float *k_norm_weight;	 // (layer, head_dim)
	QuantizedTensor *wq; // (layer, n_heads * head_dim, dim)
	QuantizedTensor *wk; // (layer, n_kv_heads * head_dim, dim)
	QuantizedTensor *wv; // (layer, n_kv_heads * head_dim, dim)
	QuantizedTensor *wo; // (layer, dim, n_heads * head_dim)
	QuantizedTensor *w1; // (layer, hidden_dim, dim)
	QuantizedTensor *w2; // (layer, dim, hidden_dim)
	QuantizedTensor *w3; // (layer, hidden_dim, dim)
	QuantizedTensor wcls; // (vocab_size, dim), shared with q_tokens
} TransformerWeights;

typedef struct
{
	float *x;	 // activation (dim,)
	float *xb;	 // scratch, sized max(dim, n_heads * head_dim)
	float *hb, *hb2; // ffn buffers (hidden_dim,)
	int8_t *xq;	 // quantized activation
	float *xq_s;
	int8_t *hq; // quantized ffn buffer
	float *hq_s;
	float *q;	// query (n_heads * head_dim,)
	float *att; // (n_heads, seq_len)
	float *logits;
	float *key_cache;	// (layer, seq_len, kv_dim)
	float *value_cache; // (layer, seq_len, kv_dim)
	float *rope_freq;	// (head_dim / 2,) precomputed RoPE frequencies
} RunState;

// ---------------- tokenizer ----------------

typedef struct
{
	char **vocab;
	float *merge_scores;
	TokenIndex *sorted_vocab;
	int vocab_size;
	unsigned int max_token_length;
	unsigned int bos_token_id;
	unsigned int eos_token_id;
} Tokenizer;

static struct
{
	bool loaded;
	int gs; // quantization group size
	Config config;
	TransformerWeights weights;
	RunState state;
	Tokenizer tokenizer;
	char describe[160];
} qwen;

// ---------------- quantization ----------------

static void dequantize_row(float *out, const QuantizedTensor *t,
						   uint64_t offset, int n)
{
	for (int i = 0; i < n; i++)
		out[i] = t->q[offset + i] * t->s[(offset + i) / qwen.gs];
}

static void quantize(int8_t *q, float *s, const float *x, int n)
{
	int gs = qwen.gs;
	for (int group = 0; group < n / gs; group++)
	{
		float wmax = 0.0f;
		for (int i = 0; i < gs; i++)
		{
			float val = fabsf_k(x[group * gs + i]);
			if (val > wmax)
				wmax = val;
		}
		float scale = wmax / 127.0f;
		s[group] = scale;
		if (scale == 0.0f)
		{
			for (int i = 0; i < gs; i++)
				q[group * gs + i] = 0;
			continue;
		}
		for (int i = 0; i < gs; i++)
		{
			float v = x[group * gs + i] / scale;
			q[group * gs + i] = (int8_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
		}
	}
}

// W (d,n) @ x (n,) -> xout (d,), both int8 with per-group scales.
static void matmul(float *xout, const int8_t *xq, const float *xs,
				   const QuantizedTensor *w, int n, int d)
{
	int gs = qwen.gs;
	for (int i = 0; i < d; i++)
	{
		float val = 0.0f;
		uint64_t in = (uint64_t)i * n;
		for (int j = 0; j <= n - gs; j += gs)
		{
			const int8_t *xr = xq + j;
			const int8_t *wr = w->q + in + j;
			int32_t i0 = 0, i1 = 0, i2 = 0, i3 = 0;
			for (int k = 0; k < gs; k += 4)
			{
				i0 += xr[k] * wr[k];
				i1 += xr[k + 1] * wr[k + 1];
				i2 += xr[k + 2] * wr[k + 2];
				i3 += xr[k + 3] * wr[k + 3];
			}
			val += (float)((i0 + i1) + (i2 + i3)) * w->s[(in + j) / gs] *
				   xs[j / gs];
		}
		xout[i] = val;
	}
}

static void rmsnorm(float *o, const float *x, const float *weight, int size)
{
	float ss = 0.0f;
	for (int j = 0; j < size; j++)
		ss += x[j] * x[j];
	ss = 1.0f / sqrtf_k(ss / size + 1e-6f);
	for (int j = 0; j < size; j++)
		o[j] = weight[j] * (ss * x[j]);
}

// ---------------- loading ----------------

static QuantizedTensor *map_tensors(const uint8_t **ptr, int n,
									uint64_t size_each)
{
	QuantizedTensor *res = malloc(n * sizeof(QuantizedTensor));
	if (!res)
		return NULL;
	const uint8_t *p = *ptr;
	for (int i = 0; i < n; i++)
	{
		res[i].q = (const int8_t *)p;
		p += size_each;
		res[i].s = (const float *)p;
		p += size_each / qwen.gs * sizeof(float);
	}
	*ptr = p;
	return res;
}

static int alloc_run_state(RunState *s, const Config *p)
{
	uint64_t kv_dim = (uint64_t)p->n_kv_heads * p->head_dim;
	int all_heads_dim = p->n_heads * p->head_dim;
	int xb_dim = all_heads_dim > p->dim ? all_heads_dim : p->dim;

	s->x = calloc(p->dim, sizeof(float));
	s->xb = calloc(xb_dim, sizeof(float));
	s->hb = calloc(p->hidden_dim, sizeof(float));
	s->hb2 = calloc(p->hidden_dim, sizeof(float));
	s->xq = calloc(xb_dim, sizeof(int8_t));
	s->xq_s = calloc(xb_dim / qwen.gs, sizeof(float));
	s->hq = calloc(p->hidden_dim, sizeof(int8_t));
	s->hq_s = calloc(p->hidden_dim / qwen.gs, sizeof(float));
	s->q = calloc(all_heads_dim, sizeof(float));
	s->att = calloc((uint64_t)p->n_heads * p->seq_len, sizeof(float));
	s->logits = calloc(p->vocab_size, sizeof(float));
	s->key_cache = calloc((uint64_t)p->n_layers * p->seq_len * kv_dim,
						  sizeof(float));
	s->value_cache = calloc((uint64_t)p->n_layers * p->seq_len * kv_dim,
							sizeof(float));
	s->rope_freq = calloc(p->head_dim / 2, sizeof(float));

	if (!s->x || !s->xb || !s->hb || !s->hb2 || !s->xq || !s->xq_s ||
		!s->hq || !s->hq_s || !s->q || !s->att || !s->logits ||
		!s->key_cache || !s->value_cache || !s->rope_freq)
		return -1;

	// RoPE frequency table: theta 1e6, half-split rotation
	for (int j = 0; j < p->head_dim / 2; j++)
		s->rope_freq[j] =
			powf_k(1e6f, -(float)j / (float)(p->head_dim / 2));
	return 0;
}

static int build_tokenizer(Tokenizer *t, const uint8_t *data, uint64_t size,
						   int vocab_size)
{
	const uint8_t *p = data;
	const uint8_t *end = data + size;
	if (p + 12 > end)
		return -1;
	memcpy(&t->max_token_length, p, 4);
	memcpy(&t->bos_token_id, p + 4, 4);
	memcpy(&t->eos_token_id, p + 8, 4);
	p += 12;

	t->vocab_size = vocab_size;
	t->vocab = malloc(vocab_size * sizeof(char *));
	t->merge_scores = malloc(vocab_size * sizeof(float));
	// arena for NUL-terminated copies: string bytes + one NUL each is always
	// smaller than the file (which spends 8 bytes of headers per token)
	char *arena = malloc(size);
	if (!t->vocab || !t->merge_scores || !arena)
		return -1;

	for (int i = 0; i < vocab_size; i++)
	{
		if (p + 8 > end)
			return -1;
		memcpy(&t->merge_scores[i], p, 4);
		uint32_t len;
		memcpy(&len, p + 4, 4);
		p += 8;
		if (p + len > end)
			return -1;
		t->vocab[i] = arena;
		memcpy(arena, p, len);
		arena[len] = '\0';
		arena += len + 1;
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
	tok_sort(t->sorted_vocab, vocab_size);
	return 0;
}

static bool load_qwen(void)
{
	if (qwen.loaded)
		return true;

	struct limine_file *mod = module_find(QWEN_MODEL_FILE);
	struct limine_file *tok = module_find(QWEN_TOKENIZER_FILE);
	if (!mod || !tok)
		return false;

	const uint8_t *base = mod->address;
	uint32_t magic;
	int version;
	memcpy(&magic, base, 4);
	memcpy(&version, base + 4, 4);
	if (magic != QWEN_MAGIC || version != 1)
	{
		kprintf("qwen: bad checkpoint (magic %x version %d)\n", magic,
				version);
		return false;
	}

	Config *p = &qwen.config;
	memcpy(p, base + 8, sizeof(Config));
	if (p->seq_len > QWEN_CTX_MAX)
		p->seq_len = QWEN_CTX_MAX;
	qwen.gs = p->group_size;

	TransformerWeights *w = &qwen.weights;
	const float *fptr = (const float *)(base + 256);
	w->rms_att_weight = fptr;
	fptr += (uint64_t)p->n_layers * p->dim;
	w->rms_ffn_weight = fptr;
	fptr += (uint64_t)p->n_layers * p->dim;
	w->rms_final_weight = fptr;
	fptr += p->dim;
	w->q_norm_weight = fptr;
	fptr += (uint64_t)p->n_layers * p->head_dim;
	w->k_norm_weight = fptr;
	fptr += (uint64_t)p->n_layers * p->head_dim;

	const uint8_t *ptr = (const uint8_t *)fptr;
	uint64_t all_heads_dim = (uint64_t)p->n_heads * p->head_dim;
	uint64_t kv_dim = (uint64_t)p->n_kv_heads * p->head_dim;

	w->q_tokens.q = (const int8_t *)ptr;
	ptr += (uint64_t)p->vocab_size * p->dim;
	w->q_tokens.s = (const float *)ptr;
	ptr += (uint64_t)p->vocab_size * p->dim / qwen.gs * sizeof(float);

	w->wq = map_tensors(&ptr, p->n_layers, (uint64_t)p->dim * all_heads_dim);
	w->wk = map_tensors(&ptr, p->n_layers, (uint64_t)p->dim * kv_dim);
	w->wv = map_tensors(&ptr, p->n_layers, (uint64_t)p->dim * kv_dim);
	w->wo = map_tensors(&ptr, p->n_layers, all_heads_dim * (uint64_t)p->dim);
	w->w1 = map_tensors(&ptr, p->n_layers, (uint64_t)p->dim * p->hidden_dim);
	w->w2 = map_tensors(&ptr, p->n_layers, (uint64_t)p->hidden_dim * p->dim);
	w->w3 = map_tensors(&ptr, p->n_layers, (uint64_t)p->dim * p->hidden_dim);
	if (!w->wq || !w->wk || !w->wv || !w->wo || !w->w1 || !w->w2 || !w->w3)
		return false;
	if (!p->shared_classifier)
	{
		w->wcls.q = (const int8_t *)ptr;
		ptr += (uint64_t)p->dim * p->vocab_size;
		w->wcls.s = (const float *)ptr;
	}
	else
		w->wcls = w->q_tokens;

	if (alloc_run_state(&qwen.state, p) < 0)
		return false;
	if (build_tokenizer(&qwen.tokenizer, tok->address, tok->size,
						p->vocab_size) < 0)
		return false;

	snprintf(qwen.describe, sizeof(qwen.describe),
			 "dim=%d hidden=%d layers=%d heads=%d kv_heads=%d head_dim=%d "
			 "vocab=%d ctx=%d q8_0(gs=%d)",
			 p->dim, p->hidden_dim, p->n_layers, p->n_heads, p->n_kv_heads,
			 p->head_dim, p->vocab_size, p->seq_len, qwen.gs);
	qwen.loaded = true;
	kprintf("qwen: loaded %s (%s)\n", QWEN_MODEL_FILE, qwen.describe);
	return true;
}

// ---------------- forward pass ----------------

static float *forward(int token, int pos)
{
	const Config *p = &qwen.config;
	TransformerWeights *w = &qwen.weights;
	RunState *s = &qwen.state;
	int dim = p->dim;
	int head_dim = p->head_dim;
	int half = head_dim / 2;
	uint64_t kv_dim = (uint64_t)p->n_kv_heads * head_dim;
	int kv_mul = p->n_heads / p->n_kv_heads;
	int all_heads_dim = p->n_heads * head_dim;
	int hidden_dim = p->hidden_dim;

	dequantize_row(s->x, &w->q_tokens, (uint64_t)token * dim, dim);

	for (uint64_t l = 0; l < (uint64_t)p->n_layers; l++)
	{
		uint64_t loff = l * p->seq_len * kv_dim;
		float *k = s->key_cache + loff + (uint64_t)pos * kv_dim;
		float *v = s->value_cache + loff + (uint64_t)pos * kv_dim;

		rmsnorm(s->xb, s->x, w->rms_att_weight + l * dim, dim);

		quantize(s->xq, s->xq_s, s->xb, dim);
		matmul(s->q, s->xq, s->xq_s, &w->wq[l], dim, all_heads_dim);
		matmul(k, s->xq, s->xq_s, &w->wk[l], dim, kv_dim);
		matmul(v, s->xq, s->xq_s, &w->wv[l], dim, kv_dim);

		// QK-RMSNorm, then half-split RoPE (rotate [j] with [j + half])
		for (int h = 0; h < p->n_heads; h++)
		{
			float *q = s->q + (uint64_t)h * head_dim;
			rmsnorm(q, q, w->q_norm_weight + l * head_dim, head_dim);
			for (int j = 0; j < half; j++)
			{
				float val = pos * s->rope_freq[j];
				float fcr = cosf_k(val), fci = sinf_k(val);
				float re = q[j], im = q[j + half];
				q[j] = re * fcr - im * fci;
				q[j + half] = re * fci + im * fcr;
			}
		}
		for (int h = 0; h < p->n_kv_heads; h++)
		{
			float *kh = k + (uint64_t)h * head_dim;
			rmsnorm(kh, kh, w->k_norm_weight + l * head_dim, head_dim);
			for (int j = 0; j < half; j++)
			{
				float val = pos * s->rope_freq[j];
				float fcr = cosf_k(val), fci = sinf_k(val);
				float re = kh[j], im = kh[j + half];
				kh[j] = re * fcr - im * fci;
				kh[j + half] = re * fci + im * fcr;
			}
		}

		// multihead attention
		for (int h = 0; h < p->n_heads; h++)
		{
			float *q = s->q + (uint64_t)h * head_dim;
			float *att = s->att + (uint64_t)h * p->seq_len;
			for (int tp = 0; tp <= pos; tp++)
			{
				float *kt = s->key_cache + loff + (uint64_t)tp * kv_dim +
							(uint64_t)(h / kv_mul) * head_dim;
				float score = 0.0f;
				for (int i = 0; i < head_dim; i++)
					score += q[i] * kt[i];
				att[tp] = score / sqrtf_k((float)head_dim);
			}

			llm_softmax(att, pos + 1);

			float *xb = s->xb + (uint64_t)h * head_dim;
			memset(xb, 0, head_dim * sizeof(float));
			for (int tp = 0; tp <= pos; tp++)
			{
				float *vt = s->value_cache + loff + (uint64_t)tp * kv_dim +
							(uint64_t)(h / kv_mul) * head_dim;
				float a = att[tp];
				for (int i = 0; i < head_dim; i++)
					xb[i] += a * vt[i];
			}
		}

		quantize(s->xq, s->xq_s, s->xb, all_heads_dim);
		matmul(s->xb, s->xq, s->xq_s, &w->wo[l], all_heads_dim, dim);
		for (int i = 0; i < dim; i++)
			s->x[i] += s->xb[i];

		// ffn: self.w2(F.silu(self.w1(x)) * self.w3(x))
		rmsnorm(s->xb, s->x, w->rms_ffn_weight + l * dim, dim);
		quantize(s->xq, s->xq_s, s->xb, dim);
		matmul(s->hb, s->xq, s->xq_s, &w->w1[l], dim, hidden_dim);
		matmul(s->hb2, s->xq, s->xq_s, &w->w3[l], dim, hidden_dim);
		for (int i = 0; i < hidden_dim; i++)
		{
			float val = s->hb[i];
			val *= (1.0f / (1.0f + expf_k(-val))); // silu
			s->hb[i] = val * s->hb2[i];
		}
		quantize(s->hq, s->hq_s, s->hb, hidden_dim);
		matmul(s->xb, s->hq, s->hq_s, &w->w2[l], hidden_dim, dim);
		for (int i = 0; i < dim; i++)
			s->x[i] += s->xb[i];

		// a 0.6B token takes seconds under TCG: keep the UI alive per layer
		if (sched_active())
			sched_yield();
	}

	rmsnorm(s->x, s->x, w->rms_final_weight, dim);
	quantize(s->xq, s->xq_s, s->x, dim);
	matmul(s->logits, s->xq, s->xq_s, &w->wcls, dim, p->vocab_size);
	return s->logits;
}

// ---------------- byte-level BPE ----------------

static void encode(Tokenizer *t, const char *text, int *tokens, int *n_tokens)
{
	char *str_buffer = malloc((uint64_t)t->max_token_length * 2 + 3);
	*n_tokens = 0;
	if (!str_buffer)
		return;

	for (const char *c = text; *c != '\0'; c++)
	{
		int id = -1;

		// special tokens like <|im_start|> are looked up whole
		if (*c == '<')
		{
			int end_pos = -1;
			for (int k = 1; c[k] != '\0' && k < 64; k++)
				if (c[k] == '>')
				{
					end_pos = k;
					break;
				}
			if (end_pos != -1)
			{
				memcpy(str_buffer, c, end_pos + 1);
				str_buffer[end_pos + 1] = '\0';
				id = tok_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
				if (id != -1)
					c += end_pos;
			}
		}

		if (id == -1)
		{
			str_buffer[0] = *c;
			str_buffer[1] = '\0';
			id = tok_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
		}
		if (id != -1)
			tokens[(*n_tokens)++] = id;
		// byte-level BPE has a token for every byte; a miss drops the byte
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
			int id = tok_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
			if (id != -1 && t->merge_scores[id] > best_score)
			{
				best_score = t->merge_scores[id];
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

	free(str_buffer);
}

// ---------------- public entry points ----------------

const char *qwen_describe(void)
{
	return load_qwen() ? qwen.describe : NULL;
}

int qwen_run(const char *prompt, int max_tokens, int temp_centi,
			 int topp_centi, unsigned long seed, llm_emit_fn emit, void *ud)
{
	if (!load_qwen())
		return LLM_ERR_NO_MODEL;

	Tokenizer *t = &qwen.tokenizer;

	// render the chat template around the user's message
	size_t rlen = sizeof(TMPL_PRE) + strlen(prompt) + sizeof(TMPL_POST);
	char *rendered = malloc(rlen);
	int *prompt_tokens = malloc((rlen + 4) * sizeof(int));
	Sampler sampler = {
		.vocab_size = qwen.config.vocab_size,
		.temperature = temp_centi / 100.0f,
		.topp = topp_centi / 100.0f,
		.rng_state = seed ? seed : 42,
		.probindex = malloc(qwen.config.vocab_size * sizeof(ProbIndex)),
	};
	if (!rendered || !prompt_tokens || !sampler.probindex)
	{
		free(rendered);
		free(prompt_tokens);
		free(sampler.probindex);
		return LLM_ERR_OOM;
	}
	snprintf(rendered, rlen, "%s%s%s", TMPL_PRE, prompt, TMPL_POST);

	int num_prompt_tokens = 0;
	encode(t, rendered, prompt_tokens, &num_prompt_tokens);
	if (num_prompt_tokens < 1)
	{
		free(rendered);
		free(prompt_tokens);
		free(sampler.probindex);
		return LLM_ERR_NO_MODEL;
	}

	int steps = qwen.config.seq_len;
	if (max_tokens > 0 && num_prompt_tokens + max_tokens < steps)
		steps = num_prompt_tokens + max_tokens;

	int token = prompt_tokens[0];
	int pos = 0;
	int generated = 0;

	while (pos < steps)
	{
		float *logits = forward(token, pos);

		int next;
		bool sampled = false;
		if (pos < num_prompt_tokens - 1)
			next = prompt_tokens[pos + 1];
		else
		{
			next = llm_sample(&sampler, logits);
			sampled = true;
		}
		pos++;

		if (sampled)
		{
			if (next == (int)t->eos_token_id || next == (int)t->bos_token_id)
				break;
			generated++;
			const char *piece = t->vocab[next];
			// never print special tokens the sampler may cough up
			if (piece[0] != '\0' && !(piece[0] == '<' && piece[1] == '|'))
				emit_sanitized(piece, emit, ud);
		}
		token = next;
	}

	free(rendered);
	free(prompt_tokens);
	free(sampler.probindex);
	return generated;
}
