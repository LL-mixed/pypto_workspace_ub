#!/usr/bin/env python3
"""Generate source-side capacity budget markdown for DeepSeek front kernels."""

from __future__ import annotations

import ast
from pathlib import Path


ROOT = Path("/data/liaoheng/pypto_workspace/pypto-lib/examples")


def _read_constants(py_file: Path) -> dict[str, int]:
    tree = ast.parse(py_file.read_text(encoding="utf-8"))
    values: dict[str, int] = {}
    wanted = {
        "K_CHUNK",
        "Q_OUT_CHUNK",
        "KV_OUT_CHUNK",
        "LORA_CHUNK",
        "TOK_TILE",
        "BATCH_TILE",
        "INDEX_TOPK",
        "KV_LORA_RANK",
        "QK_ROPE_HEAD_DIM",
        "V_HEAD_DIM",
        "UB_SOFT_LIMIT_BYTES",
        "LOCAL_PAD_WIDTH",
        "K_CHUNK",
        "LORA_CHUNK",
        "Q_OUT_CHUNK",
        "TOK_TILE",
        "BATCH_TILE",
    }
    def _eval_int_expr(node: ast.AST) -> int:
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -_eval_int_expr(node.operand)
        if isinstance(node, ast.BinOp):
            lhs = _eval_int_expr(node.left)
            rhs = _eval_int_expr(node.right)
            if isinstance(node.op, ast.Add):
                return lhs + rhs
            if isinstance(node.op, ast.Sub):
                return lhs - rhs
            if isinstance(node.op, ast.Mult):
                return lhs * rhs
            if isinstance(node.op, ast.FloorDiv):
                return lhs // rhs
        raise ValueError("unsupported int expr")

    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name in wanted:
                try:
                    values[name] = _eval_int_expr(node.value)
                except ValueError:
                    pass
    return values


def _calc(kind: str, c: dict[str, int]) -> tuple[int, int, int, int, int]:
    lane = c["TOK_TILE"] if kind == "prefill" else c["BATCH_TILE"]
    stage1 = lane * c["K_CHUNK"] * 4 + lane * c["LORA_CHUNK"] * 4 + lane * c["Q_OUT_CHUNK"] * 4 + lane * c["KV_OUT_CHUNK"] * 4
    stage1 += lane * c["LOCAL_PAD_WIDTH"] * 2
    stage2 = 2 * (1 + 2) * c["INDEX_TOPK"] * 4 + c["KV_LORA_RANK"] * 4 + c["QK_ROPE_HEAD_DIM"] * 4 + c["V_HEAD_DIM"] * 4
    peak = max(stage1, stage2)

    # Cube tile working-set estimate for dominant matmul tile:
    # A[lane, K_CHUNK] bf16 + B[K_CHUNK, max(LORA_CHUNK,Q_OUT_CHUNK)] bf16 + C[lane, N] fp32
    n_chunk = max(c["LORA_CHUNK"], c["Q_OUT_CHUNK"])
    cube_a = lane * c["K_CHUNK"] * 2
    cube_b = c["K_CHUNK"] * n_chunk * 2
    cube_c = lane * n_chunk * 4
    cube_tile_bytes = cube_a + cube_b + cube_c
    cube_soft_limit_bytes = 1024 * 1024
    return stage1, stage2, peak, cube_tile_bytes, cube_soft_limit_bytes


def _emit_md(kind: str, src: Path, out: Path) -> None:
    c = _read_constants(src)
    stage1, stage2, peak, cube_tile_bytes, cube_soft_limit_bytes = _calc(kind, c)
    limit = c["UB_SOFT_LIMIT_BYTES"]
    lane_name = "TOK_TILE" if kind == "prefill" else "BATCH_TILE"
    md = f"""# DeepSeek Front Capacity Budget

## Source
- File: `{src}`
- Kernel kind: `{kind}`

## Formula
- `stage1_est_bytes = {lane_name}*K_CHUNK*4 + {lane_name}*LORA_CHUNK*4 + {lane_name}*Q_OUT_CHUNK*4 + {lane_name}*KV_OUT_CHUNK*4`
- `stage2_est_bytes = 2*(1+2)*INDEX_TOPK*4 + KV_LORA_RANK*4 + QK_ROPE_HEAD_DIM*4 + V_HEAD_DIM*4`
- `peak_est_bytes = max(stage1_est_bytes, stage2_est_bytes)`

## Current Config
- `{lane_name} = {c[lane_name]}`
- `K_CHUNK = {c["K_CHUNK"]}`
- `LORA_CHUNK = {c["LORA_CHUNK"]}`
- `Q_OUT_CHUNK = {c["Q_OUT_CHUNK"]}`
- `KV_OUT_CHUNK = {c["KV_OUT_CHUNK"]}`
- `LOCAL_PAD_WIDTH = {c["LOCAL_PAD_WIDTH"]}`
- `INDEX_TOPK = {c["INDEX_TOPK"]}`
- `KV_LORA_RANK = {c["KV_LORA_RANK"]}`
- `QK_ROPE_HEAD_DIM = {c["QK_ROPE_HEAD_DIM"]}`
- `V_HEAD_DIM = {c["V_HEAD_DIM"]}`
- `UB_SOFT_LIMIT_BYTES = {limit}`

## Result
- `stage1_est_bytes = {stage1} B`
- `stage2_est_bytes = {stage2} B`
- `peak_est_bytes = {peak} B`
- `peak / UB_SOFT_LIMIT = {peak / limit:.2%}`
- Status: {"PASS" if peak <= limit else "FAIL"}

## Cube Usage (Estimated)
- `cube_tile_est_bytes = {cube_tile_bytes} B`
- `cube_soft_limit_bytes = {cube_soft_limit_bytes} B`
- `cube_usage_est = {cube_tile_bytes / cube_soft_limit_bytes:.2%}`
"""
    out.write_text(md, encoding="utf-8")


def main() -> None:
    prefill_src = ROOT / "deepseek_v3_2_prefill_front.py"
    decode_src = ROOT / "deepseek_v3_2_decode_front.py"
    prefill_out = ROOT / "deepseek_v3_2_prefill_front_dump/report/deepseek_v3_2_prefill_front_capacity_budget.md"
    decode_out = ROOT / "deepseek_v3_2_decode_front_dump/report/deepseek_v3_2_decode_front_capacity_budget.md"

    _emit_md("prefill", prefill_src, prefill_out)
    _emit_md("decode", decode_src, decode_out)
    print(prefill_out)
    print(decode_out)


if __name__ == "__main__":
    main()

