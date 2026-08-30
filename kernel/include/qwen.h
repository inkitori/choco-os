#ifndef QWEN_H
#define QWEN_H

// Qwen3 Q8_0 engine, called only through llm.c's dispatcher (which owns
// the busy flag and the public llm.h API). Same float-boundary rules as
// llm.h: qwen.c is hard-float, callers are not, so no float crosses this.

#include "llm.h"

int qwen_run(const char *prompt, int max_tokens, int temp_centi,
			 int topp_centi, unsigned long seed, llm_emit_fn emit, void *ud);

// Model info string, or NULL if the qwen3 modules are unavailable.
const char *qwen_describe(void);

#endif
