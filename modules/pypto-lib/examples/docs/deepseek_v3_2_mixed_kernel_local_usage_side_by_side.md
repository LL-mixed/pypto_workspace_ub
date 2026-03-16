# DeepSeek v3.2 Mixed Kernel Local Tensor Usage (AIC vs AIV)

统计口径：基于 `08_after_ExpandMixedKernel.py` 中每个 `*_aic/*_aiv` 函数里 `create_tensor(...)` 的显式本地张量字节数。

- 这是 **AIC/AIV 并排同口径** 的 local tensor 统计。
- 不再只看 AIV，也不做 front/back 跨函数“总 usage%”混算。

## decode_front

| Mixed Kernel | AIC local tensor used | AIV local tensor used |
|---|---:|---:|
| `deepseek_v3_2_decode_front_layer_incore_0` | 132.00 KB | 1.00 KB |
| `deepseek_v3_2_decode_front_layer_incore_1` | 129.00 KB | 4.00 KB |
| `deepseek_v3_2_decode_front_layer_incore_2` | 192.00 KB | 1.51 KB |

## prefill_front

| Mixed Kernel | AIC local tensor used | AIV local tensor used |
|---|---:|---:|
| `deepseek_v3_2_prefill_front_layer_incore_0` | 197.00 KB | 2.00 KB |
| `deepseek_v3_2_prefill_front_layer_incore_1` | 192.00 KB | 1.51 KB |

## decode_back

| Mixed Kernel | AIC local tensor used | AIV local tensor used |
|---|---:|---:|
| `deepseek_v3_2_decode_back_layer_incore_0` | 132.00 KB | 1.00 KB |
| `deepseek_v3_2_decode_back_layer_incore_1` | 128.00 KB | 0 B |

## prefill_back

| Mixed Kernel | AIC local tensor used | AIV local tensor used |
|---|---:|---:|
| `deepseek_v3_2_prefill_back_layer_incore_0` | 132.00 KB | 1.00 KB |
| `deepseek_v3_2_prefill_back_layer_incore_1` | 128.00 KB | 0 B |

