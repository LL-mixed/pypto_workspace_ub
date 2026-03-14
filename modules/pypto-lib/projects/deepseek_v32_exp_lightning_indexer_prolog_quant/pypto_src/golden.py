# Copyright (c) PyPTO Contributors.
# Lightning Indexer Prolog Quant — PyPTO v3 self-contained golden program.
#
# Fusion note: The PTO/CCE backend requires each InCore function to use only ONE
# pipe type (CUBE for matmul, VECTOR for elementwise/row_*). So we cannot fuse
# matmul with mul/layernorm/softmax in the same incore. Within the same type we
# already maximize fusion: softmax (row_max→row_expand_sub→exp→row_sum→row_expand_div),
# layernorm (full norm in one kernel), symmetric_quant (abs→row_max→scale→div→cast)
# are each one incore. To fuse across matmul and vector ops would require backend
# support for mixed pipe types or a different execution model.
#
# Algorithm:
#   Q: x @ w_qb.T → dequant(scale) → RoPE → Hadamard → symmetric_quant
#   K: x @ wk.T → LayerNorm → RoPE → Hadamard → symmetric_quant
#   Weights: x @ w_proj.T → softmax

import pypto.language as pl
import numpy as np
import torch

# --- run_example.py interface (simpler) ---
ALL_CASES = {"Default": {"epsilon": 1e-5}}
DEFAULT_CASE = "Default"
__outputs__ = ["q_int8_out", "q_scale_out", "k_int8_out", "k_scale_out", "weights_out"]
RTOL = 1e-2
ATOL = 1e-2

# Order of args passed to orchestration (must match LightningIndexerPrologQuant.cpp)
TENSOR_ORDER = [
    "x", "w_qb", "w_qb_scale", "wk", "ln_gamma_k", "ln_beta_k", "w_proj",
    "cos", "sin", "hadamard_q", "hadamard_k",
    "q_int8_out", "q_scale_out", "k_int8_out", "k_scale_out", "weights_out", "weights_norm",
]


def generate_inputs(params):
    """Generate input and output tensors for run_example. Returns dict keyed by TENSOR_ORDER."""
    np.random.seed(42)
    tensors = {
        "x": torch.from_numpy(np.random.randn(192, 128).astype(np.float32)),
        "w_qb": torch.from_numpy(np.random.randn(128, 64).astype(np.float32)),
        "w_qb_scale": torch.from_numpy(np.ones((192, 64), dtype=np.float32) * 0.01),
        "wk": torch.from_numpy(np.random.randn(128, 64).astype(np.float32)),
        "ln_gamma_k": torch.from_numpy(np.ones((1, 64), dtype=np.float32)),
        "ln_beta_k": torch.from_numpy(np.zeros((1, 64), dtype=np.float32)),
        "w_proj": torch.from_numpy(np.random.randn(128, 64).astype(np.float32)),
        "cos": torch.from_numpy(np.random.randn(192, 64).astype(np.float32)),
        "sin": torch.from_numpy(np.random.randn(192, 64).astype(np.float32)),
        "hadamard_q": torch.from_numpy(np.eye(64, dtype=np.float32)),
        "hadamard_k": torch.from_numpy(np.eye(64, dtype=np.float32)),
        "q_int8_out": torch.zeros(192, 64, dtype=torch.int8),
        "q_scale_out": torch.zeros(192, 64, dtype=torch.float32),
        "k_int8_out": torch.zeros(192, 64, dtype=torch.int8),
        "k_scale_out": torch.zeros(192, 64, dtype=torch.float32),
        "weights_out": torch.zeros(192, 64, dtype=torch.float32),
        "weights_norm": torch.zeros(192, 64, dtype=torch.float32),
    }
    return tensors


def compute_golden(tensors, params):
    """Reference implementation: compute expected outputs in-place."""
    epsilon = params.get("epsilon", 1e-5)
    x = tensors["x"].numpy()
    w_qb = tensors["w_qb"].numpy()
    w_qb_scale = tensors["w_qb_scale"].numpy()
    wk = tensors["wk"].numpy()
    ln_gamma_k = tensors["ln_gamma_k"].numpy()
    ln_beta_k = tensors["ln_beta_k"].numpy()
    w_proj = tensors["w_proj"].numpy()
    cos = tensors["cos"].numpy()
    hadamard_q = tensors["hadamard_q"].numpy()
    hadamard_k = tensors["hadamard_k"].numpy()

    # Q path: x @ w_qb.T -> mul scale -> mul cos -> @ hadamard_q -> quant
    q_raw = x @ w_qb
    q_deq = q_raw * w_qb_scale
    q_roped = q_deq * cos
    q_had = q_roped @ hadamard_q
    inv_127 = 1.0 / 127.0
    abs_q = np.abs(q_had)
    amax = np.max(abs_q, axis=1, keepdims=True)
    scale_q = np.where(amax > 0, amax * inv_127, np.ones_like(amax))
    q_quant = np.clip(np.round(q_had / scale_q).astype(np.float32), -127, 127).astype(np.int8)
    tensors["q_int8_out"][:] = torch.from_numpy(q_quant)
    tensors["q_scale_out"][:] = torch.from_numpy(scale_q)

    # K path: x @ wk.T -> layernorm -> mul cos -> @ hadamard_k -> quant
    k_raw = x @ wk
    mean_k = k_raw.mean(axis=1, keepdims=True)
    var_k = k_raw.var(axis=1, keepdims=True) + epsilon
    rstd = 1.0 / np.sqrt(var_k)
    k_normed = (k_raw - mean_k) * rstd * ln_gamma_k + ln_beta_k
    k_roped = k_normed * cos
    k_had = k_roped @ hadamard_k
    amax_k = np.max(np.abs(k_had), axis=1, keepdims=True)
    scale_k = np.where(amax_k > 0, amax_k * inv_127, np.ones_like(amax_k))
    k_quant = np.clip(np.round(k_had / scale_k).astype(np.float32), -127, 127).astype(np.int8)
    tensors["k_int8_out"][:] = torch.from_numpy(k_quant)
    tensors["k_scale_out"][:] = torch.from_numpy(scale_k)

    # Weights: x @ w_proj -> softmax
    weights_raw = x @ w_proj
    row_max = np.max(weights_raw, axis=1, keepdims=True)
    exp_v = np.exp(weights_raw - row_max)
    row_sum = np.sum(exp_v, axis=1, keepdims=True)
    weights_norm = exp_v / row_sum
    tensors["weights_out"][:] = torch.from_numpy(weights_norm)
    tensors["weights_norm"][:] = torch.from_numpy(weights_norm)


# Tile: VECTOR kernels use [TM, TN]; matmul uses [TM, TK_MATMUL] @ [TK_MATMUL, TN] for higher Vec/Acc.
TM = 192
TN = 64
TK_MATMUL = 128  # matmul-only: larger K tile -> [192,128]@[128,64], raises matmul SRAM
BATCH_TILES = 1


@pl.program
class LightningIndexerPrologQuantProgram:
    """Self-contained Lightning Indexer Prolog. InCore kernels respect one-pipe-type-per-function."""

    # ---------- CUBE (Matrix) kernels: matmul uses TM x TK_MATMUL @ TK_MATMUL x TN ----------
    @pl.function(type=pl.FunctionType.InCore)
    def incore_matmul(
        self,
        a: pl.Tensor[[TM, TK_MATMUL], pl.FP32],
        b: pl.Tensor[[TK_MATMUL, TN], pl.FP32],
        output: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
    ) -> pl.Tensor[[TM, TN], pl.FP32]:
        """Single tile matmul (CUBE): [192,128] @ [128,64] -> [192,64]. Larger TK_MATMUL raises SRAM."""
        a_t: pl.Tile[[TM, TK_MATMUL], pl.FP32] = pl.load(a, [0, 0], [TM, TK_MATMUL])
        b_t: pl.Tile[[TK_MATMUL, TN], pl.FP32] = pl.load(b, [0, 0], [TK_MATMUL, TN])
        out_t: pl.Tile[[TM, TN], pl.FP32] = pl.matmul(a_t, b_t)
        out_tensor: pl.Tensor[[TM, TN], pl.FP32] = pl.store(out_t, [0, 0], [TM, TN], output)
        return out_tensor

    @pl.function(type=pl.FunctionType.InCore)
    def incore_matmul_nn(
        self,
        a: pl.Tensor[[TM, TN], pl.FP32],
        b: pl.Tensor[[TN, TN], pl.FP32],
        output: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
    ) -> pl.Tensor[[TM, TN], pl.FP32]:
        """Matmul (CUBE) for Hadamard path: [192,64] @ [64,64] -> [192,64]. Same tile as VECTOR (TN)."""
        a_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(a, [0, 0], [TM, TN])
        b_t: pl.Tile[[TN, TN], pl.FP32] = pl.load(b, [0, 0], [TN, TN])
        out_t: pl.Tile[[TM, TN], pl.FP32] = pl.matmul(a_t, b_t)
        out_tensor: pl.Tensor[[TM, TN], pl.FP32] = pl.store(out_t, [0, 0], [TM, TN], output)
        return out_tensor

    # ---------- VECTOR kernels: single tile [TM, TN] = [192, 64] ----------
    @pl.function(type=pl.FunctionType.InCore)
    def incore_mul(
        self,
        a: pl.Tensor[[TM, TN], pl.FP32],
        b: pl.Tensor[[TM, TN], pl.FP32],
        output: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
    ) -> pl.Tensor[[TM, TN], pl.FP32]:
        """Element-wise mul (VECTOR). Single tile 192x64."""
        a_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(a, [0, 0], [TM, TN])
        b_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(b, [0, 0], [TM, TN])
        out_t: pl.Tile[[TM, TN], pl.FP32] = pl.mul(a_t, b_t)
        out_tensor: pl.Tensor[[TM, TN], pl.FP32] = pl.store(out_t, [0, 0], [TM, TN], output)
        return out_tensor

    @pl.function(type=pl.FunctionType.InCore)
    def incore_softmax(
        self,
        x: pl.Tensor[[TM, TN], pl.FP32],
        tmp: pl.Tensor[[TM, TN], pl.FP32],
        output: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
    ) -> pl.Tensor[[TM, TN], pl.FP32]:
        """Fused softmax (VECTOR). Single tile 192x64."""
        x_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(x, [0, 0], [TM, TN])
        tmp_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(tmp, [0, 0], [TM, TN])
        max_t: pl.Tile[[TM, TN], pl.FP32] = pl.row_max(x_t, tmp_t)
        shifted: pl.Tile[[TM, TN], pl.FP32] = pl.row_expand_sub(x_t, max_t)
        exp_t: pl.Tile[[TM, TN], pl.FP32] = pl.exp(shifted)
        sum_t: pl.Tile[[TM, TN], pl.FP32] = pl.row_sum(exp_t, tmp_t)
        out_t: pl.Tile[[TM, TN], pl.FP32] = pl.row_expand_div(exp_t, sum_t)
        out_tensor: pl.Tensor[[TM, TN], pl.FP32] = pl.store(out_t, [0, 0], [TM, TN], output)
        return out_tensor

    @pl.function(type=pl.FunctionType.InCore)
    def incore_layernorm(
        self,
        x: pl.Tensor[[TM, TN], pl.FP32],
        gamma: pl.Tensor[[1, TN], pl.FP32],
        beta: pl.Tensor[[1, TN], pl.FP32],
        tmp: pl.Tensor[[TM, TN], pl.FP32],
        output: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
        inv_n: pl.Scalar[pl.FP32],
        eps: pl.Scalar[pl.FP32],
    ) -> pl.Tensor[[TM, TN], pl.FP32]:
        """Fused LayerNorm over last dim (VECTOR). Single tile 192x64."""
        x_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(x, [0, 0], [TM, TN])
        g_t: pl.Tile[[1, TN], pl.FP32] = pl.load(gamma, [0, 0], [1, TN])
        beta_t: pl.Tile[[1, TN], pl.FP32] = pl.load(beta, [0, 0], [1, TN])
        tmp_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(tmp, [0, 0], [TM, TN])
        sum_t: pl.Tile[[TM, TN], pl.FP32] = pl.row_sum(x_t, tmp_t)
        mean_t: pl.Tile[[TM, TN], pl.FP32] = pl.muls(sum_t, inv_n)
        centered: pl.Tile[[TM, TN], pl.FP32] = pl.row_expand_sub(x_t, mean_t)
        sq_t: pl.Tile[[TM, TN], pl.FP32] = pl.mul(centered, centered)
        var_sum_t: pl.Tile[[TM, TN], pl.FP32] = pl.row_sum(sq_t, tmp_t)
        var_t: pl.Tile[[TM, TN], pl.FP32] = pl.muls(var_sum_t, inv_n)
        var_eps_t: pl.Tile[[TM, TN], pl.FP32] = pl.adds(var_t, eps)
        rstd_t: pl.Tile[[TM, TN], pl.FP32] = pl.rsqrt(var_eps_t)
        normed: pl.Tile[[TM, TN], pl.FP32] = pl.row_expand_mul(centered, rstd_t)
        scaled: pl.Tile[[TM, TN], pl.FP32] = pl.mul(normed, g_t)
        out_t: pl.Tile[[TM, TN], pl.FP32] = pl.add(scaled, beta_t)
        out_tensor: pl.Tensor[[TM, TN], pl.FP32] = pl.store(out_t, [0, 0], [TM, TN], output)
        return out_tensor

    @pl.function(type=pl.FunctionType.InCore)
    def incore_symmetric_quant(
        self,
        x: pl.Tensor[[TM, TN], pl.FP32],
        tmp: pl.Tensor[[TM, TN], pl.FP32],
        quant_out: pl.Out[pl.Tensor[[TM, TN], pl.INT8]],
        scale_out: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
        inv_127: pl.Scalar[pl.FP32],
    ) -> pl.Tensor[[TM, TN], pl.INT8]:
        """Fused symmetric quant (VECTOR). Single tile 192x64."""
        x_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(x, [0, 0], [TM, TN])
        tmp_t: pl.Tile[[TM, TN], pl.FP32] = pl.load(tmp, [0, 0], [TM, TN])
        abs_t: pl.Tile[[TM, TN], pl.FP32] = pl.abs(x_t)
        amax_t: pl.Tile[[TM, TN], pl.FP32] = pl.row_max(abs_t, tmp_t)
        scale_t: pl.Tile[[TM, TN], pl.FP32] = pl.muls(amax_t, inv_127)
        quant_t: pl.Tile[[TM, TN], pl.FP32] = pl.row_expand_div(x_t, scale_t)
        quant_i8: pl.Tile[[TM, TN], pl.INT8] = pl.cast(quant_t, target_type=pl.INT8)
        pl.store(scale_t, [0, 0], [TM, TN], scale_out)
        out_tensor: pl.Tensor[[TM, TN], pl.INT8] = pl.store(quant_i8, [0, 0], [TM, TN], quant_out)
        return out_tensor

    @pl.function(type=pl.FunctionType.Orchestration)
    def LightningIndexerPrologQuant(
        self,
        x: pl.Tensor[[TM, TK_MATMUL], pl.FP32],
        w_qb: pl.Tensor[[TK_MATMUL, TN], pl.FP32],
        w_qb_scale: pl.Tensor[[TM, TN], pl.FP32],
        wk: pl.Tensor[[TK_MATMUL, TN], pl.FP32],
        ln_gamma_k: pl.Tensor[[1, TN], pl.FP32],
        ln_beta_k: pl.Tensor[[1, TN], pl.FP32],
        w_proj: pl.Tensor[[TK_MATMUL, TN], pl.FP32],
        cos: pl.Tensor[[TM, TN], pl.FP32],
        sin: pl.Tensor[[TM, TN], pl.FP32],
        hadamard_q: pl.Tensor[[TN, TN], pl.FP32],
        hadamard_k: pl.Tensor[[TN, TN], pl.FP32],
        q_int8_out: pl.Out[pl.Tensor[[TM, TN], pl.INT8]],
        q_scale_out: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
        k_int8_out: pl.Out[pl.Tensor[[TM, TN], pl.INT8]],
        k_scale_out: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
        weights_out: pl.Out[pl.Tensor[[TM, TN], pl.FP32]],
        epsilon: pl.Scalar[pl.FP32],
    ) -> pl.Tensor[[TM, TN], pl.FP32]:
        """Orchestration: matmul [TM,TK_MATMUL]@[TK_MATMUL,TN]; vector ops [TM,TN]=[192,64]."""
        q_raw: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        q_deq: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        q_roped: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        q_had: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        k_raw: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        k_normed: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        k_roped: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        k_had: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        weights_raw: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        weights_norm: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)
        tmp: pl.Tensor[[TM, TN], pl.FP32] = pl.create_tensor([TM, TN], dtype=pl.FP32)

        # Q path
        q_raw = self.incore_matmul(x, w_qb, q_raw)
        q_deq = self.incore_mul(q_raw, w_qb_scale, q_deq)
        q_roped = self.incore_mul(q_deq, cos, q_roped)
        q_had = self.incore_matmul_nn(q_roped, hadamard_q, q_had)
        self.incore_symmetric_quant(q_had, tmp, q_int8_out, q_scale_out, 1.0 / 127.0)

        # K path (inv_n = 1/TN for LayerNorm over last dim)
        k_raw = self.incore_matmul(x, wk, k_raw)
        k_normed = self.incore_layernorm(k_raw, ln_gamma_k, ln_beta_k, tmp, k_normed, 1.0 / 64.0, epsilon)
        k_roped = self.incore_mul(k_normed, cos, k_roped)
        k_had = self.incore_matmul_nn(k_roped, hadamard_k, k_had)
        self.incore_symmetric_quant(k_had, tmp, k_int8_out, k_scale_out, 1.0 / 127.0)

        # Weights path
        weights_raw = self.incore_matmul(x, w_proj, weights_raw)
        weights_norm = self.incore_softmax(weights_raw, tmp, weights_norm)
        pl.assemble(weights_out, weights_norm, [0, 0])
        return weights_norm
