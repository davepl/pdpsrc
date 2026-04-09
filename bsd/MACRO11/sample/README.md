# ATTN/11 - Paper Tape Is All You Need
**A single-layer, single-head transformer written in PDP-11 assembly language.**

![PXL_20260304_185637663 MP](https://github.com/user-attachments/assets/91b92356-3a51-4c95-a549-1b7fc12d6e4f)

This project is the spiritual successor of [Xortran](https://github.com/dbrll/xortran), a neural network that learns XOR with backpropagation in Fortran IV on the IBM 1130 (1965) and PDP-11/20 (1970).

The natural next step was to see if those machines could successfully train a small transformer in an acceptable amount of time (a few hours).

Architecturally, a transformer is actually a fairly modest extension of a basic neural network. The building blocks such as matrix multiplies, backpropagation, SGD, and cross-entropy are already there.

The three new components are:

- Self-attention: dot-product score between projected queries and keys
- Positional encoding: learned position embeddings, added to the input
- Softmax: to turn scores into a probability distribution

The goal is to train the Transformer to reverse a sequence of digits.
Despite its apparent simplicity, reversal is not a trivial task for a neural network: the model must learn to route each token to a position that depends only on its index, with no content-based shortcut. This is the kind of problem that self-attention is designed for, and is in fact one of the algorithmic benchmarks included in Tensor2Tensor, Google's reference implementation of the original transformer in 2017.

## Architecture

The data path is straightforward: tokens are embedded, passed through self-attention with a residual connection, then projected back to the vocabulary and softmaxed into a prediction:

Tokens -> Embedding -> Self-Attention -> Residual -> Projection -> Softmax

| Hyperparameter | Value |
| --------------- | --------------------------- |
| Layers          | 1                           |
| Heads           | 1                           |
| d_model         | 16                          |
| Sequence length | 8                           |
| Vocabulary      | 10 (digits 0–9)             |
| Parameters      | 1,216                       |

The model is an encoder-only transformer: embedding, self-attention with residual connection, and output projection. It's a genuine Transformer with self-attention, but not BERT or a GPT either: it has no layer norm, no feed-forward network, and no decoder. The task requires no transformation of the token representations, so attention and the residual connection are sufficient. Layer normalization, useful in deeper networks to prevent activation drift, is unnecessary with a single layer.

## Optimizing for 1970 Hardware

The first implementation followed Xortran and was written in Fortran IV. With a uniform learning rate of 0.01, the model took 25mn for 100 steps and needed 1,500 training steps to reach 100% accuracy, which on real hardware would have translated to about 6.5 hours of training, and possibly a whole week on the IBM 1130.

This was unacceptably long even by 1970s standards, as those machines were time-shared and computing time was very valuable.

A first improvement was the switch to hand-tuned, per-layer learning rates:

| Layer                       | Learning Rate |
| --------------------------- | ------------- |
| Wq, Wk, Wv (attention)      | 0.08          |
| Token & position embeddings | 0.01          |
| Wout (output projection)    | 0.0025        |

Attention weights, which encode the reversal pattern, benefit from a high learning rate, while the output projection converges better with a smaller one. With this tuning, training dropped to 600 steps and an estimated 2.5 hours.

The optimizer is plain stochastic gradient descent (SGD). Adam would automatically adapt the step size per parameter, but at the cost of two extra state vectors per weight, tripling the memory devoted to parameters. It would also add a square root and division per update, both expensive on the PDP-11 even with the EIS.
The per-layer learning rates achieve a similar effect at no additional cost, and the model is small enough for the three rates to be tuned by hand. It also allows the Transformer to fit in 32KB of core memory instead of 64KB, which was important in the 1970s.

Side note: since it's bare-metal assembly, ATTN/11 doesn't use more memory than Xortran, which pays the cost of RT-11 V3 and the Fortran runtime. The resulting binary is also fairly compact, at exactly 6,179 bytes.

### NN11

The core arithmetic operations use NN11, a minimal fixed-point neural network stack designed for ATTN/11 and the PDP-11.

NN11 is organized in levels not unlike BLAS: scalar primitives (FXMATH), vector operations such as dot product and scaling (VECOP), then matrix–vector operations (MATOP), each level building on the one below.

Two additional modules extend the stack beyond linear algebra: activation functions and their lookup tables (ACTFN), and layer-level routines (LAYER) that compose the previous operations into embedding, projection, and attention. 

The arithmetic is adapted to each pass:

| Pass                | Format | Precision                    |
| ------------------- | ------ | ---------------------------- |
| Forward             | Q8     | 8 fractional bits (1/256)    |
| Backward            | Q15    | 15 fractional bits (1/32768) |
| Weight accumulators | Q16    | 32-bit (16.16 fixed-point)   |

The choice of Q8 forward and Q15 backward pairs well on the PDP-11: multiplying a Q8 value by a Q15 value yields Q23 in a 32-bit register pair, and a single `ASHC #-8` brings it back to Q15. The backward pass multiply thus costs no more than the forward pass one, while giving gradients 128 times the precision of the activations.

The optimized model converges in 350 steps, bringing the total training time to only 5.5 minutes on my PDP-11/34A.

![PXL_20260302_155049106](https://github.com/user-attachments/assets/2a107b17-528a-442d-9ffb-7404e27d11e5)

I don't have an actual paper tape reader, so the object code is directly deposited in memory through the console.

Here is the result after running the Transformer:

```
 ATTN/11 - PAPER TAPE IS ALL YOU NEED
 D=16 SEQ=8 V=10 Q8/Q15/Q16

TRAINING...
 STEP    50 LOSS=1.6113 ACC=0.217
 STEP   100 LOSS=2.1865 ACC=0.255
 STEP   150 LOSS=2.1511 ACC=0.267
 STEP   200 LOSS=1.3874 ACC=0.395
 STEP   250 LOSS=0.0500 ACC=0.662
 STEP   300 LOSS=0.0019 ACC=0.982
 STEP   350 LOSS=0.0009 ACC=1.000

 4 7 4 9 6 3 6 5 -> 5 6 3 6 9 4 7 4  OK
 0 5 6 1 6 7 0 3 -> 3 0 7 6 1 6 5 0  OK
 0 9 4 5 6 5 4 1 -> 1 4 5 6 5 4 9 0  OK
 4 5 4 7 2 5 2 7 -> 7 2 5 2 7 4 5 4  OK
 ...

 ACCURACY  10/10
```

## Prototype

Before committing to assembly, correctness had to be proven. The floating then fixed-point arithmetic were prototyped and validated in [Sheaf](https://github.com/sheaf-lang/sheaf), my functional ML framework with built-in observability.

Sheaf offers several advantages over Python for this kind of ML work: about a third less code, stronger correctness guarantees from its purely functional semantics, and built-in tracing of every intermediate tensor with its shape, range, and timing. This is invaluable when developing fixed-point arithmetic.

For instance, a range guard on `vtmul` immediately catches a missing `>>8` shift:

```
sheaf ./tools/prototype.shf --guard vtmul:range:-5000:5000 --trace vtmul
Input:  [3. 1. 4. 1. 5. 9. 2. 6.]
Running forward pass...
├─ [vtmul] f64[16x16] [min:-1.72e2 max:1.86e2] (2.0KB), f64[16] [min:-2.38e2 max:2.55e2] (128B)
└─ ← f64[16] [min:-3.10e4 max:3.28e4] (128B) (0.03ms)
/!\ Guard Breached: Range { lo: -5000.0, hi: 5000.0 }
Function: vtmul
Value range [-3.10e4, 3.28e4] outside allowed [-5000, 5000]

Backtrace (last 2 operations):

├─ [vtmul] f64[16x16] [min:-1.72e2 max:1.86e2] (2.0KB), f64[16] [min:-2.38e2 max:2.55e2] (128B)
└─ ← f64[16] [min:-3.10e4 max:3.28e4] (128B) (0.03ms)

--- End of Backtrace ---
```

Both the floating and fixed-point prototypes can be found in the `proto` folder, along with the initial Fortran versions.

## Implementation Details

Without a floating-point unit, transcendental functions (exp, log) are replaced by precomputed lookup tables. A table read costs a single `MOV` instruction on the PDP-11, making it far cheaper than polynomial approximations or CORDIC algorithms.

### Softmax

The softmax uses a 256-entry table (**EXPTBL**, Q8) mapping each index _i_ to exp(−*i*/32). The computation proceeds in three steps:

1. The maximum of the input vector is found and subtracted from each element for numerical stability.
2. The difference (max − _x_i_), divided by 8, serves as the table index, clamped to [0, 255].
3. Each resulting exp value is divided by their sum (via FXDIV) to produce a probability distribution.

The table covers roughly 8 units of input before reaching zero, which is more than sufficient for a 10-class vocabulary.

### Cross-Entropy Loss

The loss is computed every 50 steps for reporting purposes. It relies on a second table (**LOGTBL**, 257 entries, Q12) mapping each value _x_ ∈ [0, 256] to −ln(_x_/256) × 4096.

The computation follows the standard path: softmax of the logits, read the probability assigned to the target token, then look up −ln(_p_) in the table.
The eight contributions (one per sequence position) are accumulated in a 32-bit register pair (necessary because the sum of eight Q12 values can exceed 16 bits) then divided by 8 via `ASHC #-3`.
Q12 precision (1/4096 ~= 0.0002) provides four decimal places, which is sufficient to monitor convergence.

### Lookup Tables

ATTN/11 uses two lookup tables. The first one maps each index to exp(−i/32) in Q8, and replaces the exponential in softmax with a single `MOV` instruction.
The other table is merely a convenience and maps each value to −ln(x/256) in Q12. It is used every 50 steps to compute the cross-entropy loss for monitoring convergence.

| Table  | Entries | Format | Function            | Location       |
| ------ | ------- | ------ | ------------------- | -------------- |
| EXPTBL | 256     | Q8     | exp(−*i*/32)        | `nn11/ACTFN.MAC` |
| LOGTBL | 257     | Q12    | −ln(_x_/256) × 4096 | `TRAIN.MAC`      |

Both tables are generated offline with a Sheaf script and stored as `.WORD` constants in the source.

### Cross-Entropy Gradient

The backward pass takes advantage of a well-known property of the softmax–cross-entropy pair: the gradient of the logits reduces to

```
dL[i] = softmax(logits[i]) - one_hot(target[i])
```

which eliminates any logarithm computation during training. The result, initially in Q8, is shifted left by 7 bits to Q15 - the format used throughout the backward pass. The same SFTMX routine serves both the forward and backward passes, with no separate backpropagation variant.

One last note: AI tools were used as assistants during development of those algorithms. All design decisions, scaling strategy and numerical validation were manually verified on hardware.

### Memory Map

ATTN/11 uses 19.2 KB of memory. The following table shows the memory map, adapted from the MACRO-11 assembler listing:

```
Address   Section                        Size
─────────────────────────────────────────────────
000000    Interrupt vectors               512 B
001000    Code (NN11 + model + I/O)       5.1 KB
          incl. EXPTBL (256 entries)
013112    Strings                         142 B
013330    LOGTBL (257 entries, Q12)       514 B
014332    Tokens + Target                 32 B
014372    Q16 weight accumulators         4.8 KB
          -> High words (1216)            2.4 KB
          -> Low words (1216)             2.4 KB
025772    Q8 weight copies                2.4 KB
032572    Gradient accumulators (Q15)     2.4 KB
037372    Forward cache                   1.5 KB
          -> X, Y, logits, WORK (Q,K,V,S)
042432    Backward workspace              1.4 KB
          -> dL, dY, dA, dQ, dK, dV, dX
045316    Stack                           512 B
046316    Free                            12.8 KB
077777    End of memory (32 KB)
```

The 1,216 parameters are replicated three times, by necessity: Q16 accumulators (4.8 KB), Q8 for the forward pass (2.4 KB), and Q15 for the gradients (2.4 KB). The model alone uses 9.6 KB, by far the largest share of the total memory usage.

## Building

The two requirements are the [MACRO11](https://github.com/j-hoppe/MACRO11) assembler and [obj2bin](https://github.com/AK6DN/obj2bin) to convert the object code into a loadable binary.

```
$ macro11 TRAIN.MAC -o ATTN-11.obj
$ obj2bin.pl ATTN-11.obj --binary --outfile=ATTN-11.bin
$ ls -l ATTN-11.bin
-rw-r--r--  1 damien  damien  6179 Mar  4 21:53 ATTN-11.bin
```

## Running

You will either need:
- A physical PDP/11 with a CPU supporting the EIS instructions, and 32K of core or MOS memory.
- [ll-34](https://github.com/dbrll/ll-34): a circuit-level, microcycle-accurate PDP-11/34 emulator I designed as a digital replica of the real hardware.

ll-34 is probably as close to owning an actual 11/34 as can be. Start ATTN/11 with:
```sh
ll-34 --lda ATTN-11.bin
```

Or for a quick demo, use the WebAssembly version available here: https://dbrll.github.io/ll-34/.

SIMH also works, but it simulates the high level behavior (not the circuit) of a PDP-11 and runs at host CPU speed.
It can be slowed down artificially, however the timing is not cycle-accurate, making it less suitable for performance tuning or for an authentic experience.
