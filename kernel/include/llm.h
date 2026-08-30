#ifndef LLM_H
#define LLM_H

// In-kernel LLM inference (llama2.c port). The API deliberately avoids
// float parameters: llm.c is compiled hard-float (SSE) while the rest of
// the kernel is soft-float, so floats may not cross this boundary.
// Temperatures etc. are passed in centi-units (80 => 0.80).

typedef void (*llm_emit_fn)(const char *piece, void *ud);

// Returns number of tokens generated, or a negative error code.
// model: "stories15M", "stories260K", or "qwen3" (Qwen3-0.6B instruct;
// the prompt is wrapped in its chat template and answered, see qwen.c).
// max_tokens: 0 means the model's full context length.
// echo_prompt: also emit the prompt tokens during prefill (llama only).
int llm_generate(const char *model, const char *prompt, int max_tokens,
				 int temp_centi, int topp_centi, unsigned long seed,
				 int echo_prompt, llm_emit_fn emit, void *ud);

// Human-readable info about a model ("dim=288 layers=6 ..."), or NULL if
// the model/module is unavailable.
const char *llm_describe(const char *model);

#define LLM_ERR_NO_MODEL -1
#define LLM_ERR_NO_TOKENIZER -2
#define LLM_ERR_OOM -3
#define LLM_ERR_BUSY -4

#endif
