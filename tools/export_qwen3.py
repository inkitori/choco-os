# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "jinja2"]
# ///
"""Export a HuggingFace Qwen3 checkpoint to the qwen3.c Q8_0 format.

Reads config.json / tokenizer.json / model.safetensors from a local HF
snapshot directory and writes <out>.bin (256-byte header + fp32 norms +
Q8_0 weights) and <out>.tokenizer, the format consumed by kernel/src/qwen.c
(a port of adriancable/qwen3.c, MIT).

Usage: uv run tools/export_qwen3.py <hf_snapshot_dir> <out_prefix>
"""

import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

GROUP_SIZE = 64
MAGIC = 0x616A6331  # "ajc1"
VERSION = 1


def load_safetensors(path):
    """Return {name: np.float32 array} without torch. bf16 handled manually."""
    tensors = {}
    with open(path, "rb") as f:
        (hlen,) = struct.unpack("<Q", f.read(8))
        header = json.loads(f.read(hlen))
        base = 8 + hlen
        data = np.memmap(path, dtype=np.uint8, mode="r", offset=base)
        for name, info in header.items():
            if name == "__metadata__":
                continue
            start, end = info["data_offsets"]
            raw = np.asarray(data[start:end])
            if info["dtype"] == "BF16":
                u16 = raw.view(np.uint16).astype(np.uint32) << 16
                arr = u16.view(np.float32)
            elif info["dtype"] == "F32":
                arr = raw.view(np.float32)
            else:
                raise ValueError(f"unhandled dtype {info['dtype']} for {name}")
            tensors[name] = arr.reshape(info["shape"]).astype(np.float32)
    return tensors


def quantize_q80(w):
    """Symmetric int8 groupwise quantization; returns (int8, fp32 scales, err)."""
    flat = w.reshape(-1, GROUP_SIZE)
    wmax = np.abs(flat).max(axis=1)
    scale = wmax / 127.0
    safe = np.where(scale == 0.0, 1.0, scale)
    q = np.rint(flat / safe[:, None]).astype(np.int8)
    err = np.abs(q.astype(np.float32) * scale[:, None] - flat).max()
    return q, scale.astype(np.float32), float(err)


def write_fp32(f, arr):
    f.write(np.ascontiguousarray(arr, dtype=np.float32).tobytes())


def write_q80(f, name, arr):
    q, s, err = quantize_q80(arr)
    f.write(q.tobytes())
    f.write(s.tobytes())
    print(f"  {name}: {arr.shape} q8_0 maxerr {err:.6f}")


def export_model(cfg, tensors, out_bin):
    n_layers = cfg["num_hidden_layers"]
    shared = 1 if cfg.get("tie_word_embeddings") else 0

    with open(out_bin, "wb") as f:
        f.write(struct.pack("<Ii", MAGIC, VERSION))
        f.write(
            struct.pack(
                "<10i",
                cfg["hidden_size"],
                cfg["intermediate_size"],
                n_layers,
                cfg["num_attention_heads"],
                cfg["num_key_value_heads"],
                cfg["vocab_size"],
                cfg["max_position_embeddings"],
                cfg["head_dim"],
                shared,
                GROUP_SIZE,
            )
        )
        f.write(b"\0" * (256 - f.tell()))

        L = "model.layers.{}.{}"
        for i in range(n_layers):
            write_fp32(f, tensors[L.format(i, "input_layernorm.weight")])
        for i in range(n_layers):
            write_fp32(f, tensors[L.format(i, "post_attention_layernorm.weight")])
        write_fp32(f, tensors["model.norm.weight"])
        for i in range(n_layers):
            write_fp32(f, tensors[L.format(i, "self_attn.q_norm.weight")])
        for i in range(n_layers):
            write_fp32(f, tensors[L.format(i, "self_attn.k_norm.weight")])

        write_q80(f, "embed", tensors["model.embed_tokens.weight"])
        for part in ("q_proj", "k_proj", "v_proj", "o_proj"):
            for i in range(n_layers):
                key = L.format(i, f"self_attn.{part}.weight")
                write_q80(f, key, tensors[key])
        for part in ("gate_proj", "down_proj", "up_proj"):
            for i in range(n_layers):
                key = L.format(i, f"mlp.{part}.weight")
                write_q80(f, key, tensors[key])
        if not shared:
            write_q80(f, "lm_head", tensors["lm_head.weight"])
    print(f"wrote {out_bin} ({Path(out_bin).stat().st_size / 1e6:.1f} MB)")


def bytes_to_unicode():
    """Reference GPT-2 byte -> unicode-char map."""
    bs = (
        list(range(ord("!"), ord("~") + 1))
        + list(range(ord("\xa1"), ord("\xac") + 1))
        + list(range(ord("\xae"), ord("\xff") + 1))
    )
    cs = bs[:]
    n = 0
    for b in range(256):
        if b not in bs:
            bs.append(b)
            cs.append(256 + n)
            n += 1
    return dict(zip(bs, map(chr, cs)))


def export_tokenizer(cfg, tok_json, out_tok):
    u2b = {u: b for b, u in bytes_to_unicode().items()}

    def to_bytes(token):
        return b"".join(
            bytes([u2b[ch]]) if ch in u2b else ch.encode("utf-8") for ch in token
        )

    vocab = dict(tok_json["model"]["vocab"])
    merges = tok_json["model"]["merges"]
    specials = {t["content"]: t["id"] for t in tok_json.get("added_tokens", [])}

    id_to_token = {}
    for tok, i in vocab.items():
        id_to_token[i] = (to_bytes(tok), tok)
    for tok, i in specials.items():
        id_to_token[i] = (tok.encode("utf-8"), tok)

    merge_rank = {
        "".join(m if isinstance(m, list) else m.split()): r
        for r, m in enumerate(merges)
    }

    vocab_size = cfg["vocab_size"]
    max_len = max(len(b) for b, _ in id_to_token.values())
    with open(out_tok, "wb") as f:
        f.write(struct.pack("<III", max_len, cfg["bos_token_id"], cfg["eos_token_id"]))
        for i in range(vocab_size):
            raw, internal = id_to_token.get(i, (b"", None))
            rank = merge_rank.get(internal) if internal else None
            score = -math.log(rank + 1) if rank is not None else -1e6
            f.write(struct.pack("<fI", score, len(raw)))
            f.write(raw)
    print(f"wrote {out_tok} ({Path(out_tok).stat().st_size / 1e6:.1f} MB)")


def show_template(hf_dir):
    from jinja2 import Template

    tc = json.loads((hf_dir / "tokenizer_config.json").read_text())
    rendered = Template(tc["chat_template"]).render(
        messages=[{"role": "user", "content": "%s"}],
        add_generation_prompt=True,
        enable_thinking=False,
    )
    print("chat template (non-thinking), for kernel/src/qwen.c:")
    print(repr(rendered))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    hf_dir = Path(sys.argv[1])
    out = sys.argv[2]

    cfg = json.loads((hf_dir / "config.json").read_text())
    tok_json = json.loads((hf_dir / "tokenizer.json").read_text())
    export_tokenizer(cfg, tok_json, out + ".tokenizer")
    show_template(hf_dir)
    tensors = load_safetensors(hf_dir / "model.safetensors")
    export_model(cfg, tensors, out + ".bin")


if __name__ == "__main__":
    main()
