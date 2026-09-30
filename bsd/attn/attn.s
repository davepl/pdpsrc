.globl _main
.globl _write
.globl _exit

/ BSD user-mode entry point for the ATTN/11 training sample.
/ This keeps the model/math code in PDP-11 assembly but runs as a
/ normal 2.11BSD process instead of a fixed-address bare-metal image.

_main:
	jmp	main

/ ===== sample/nn11/FXMATH.MAC =====
/ 
/ FXMATH.MAC - Fixed-point arithmetic primitives
/ NN11 Level 0 - Scalar primitives
/ 
/ Format Q7.8: 16-bit signed, 8 fractional bits
/ value = integer_representation / 256
/ range: [-128.0, +127.996]
/ resolution: 1/256 ~ 0.004
/ 
/ Requires: EIS (KE11-F) for MUL, DIV, ASH, ASHC
/ 

/ ============================================================
/ FXMUL - Q8 multiply (macro, inline)
/ 
/ Multiplies two Q8 values: result = a * b
/ Uses: MUL (32-bit result in Rd:Rd+1), ASHC to shift back
/ 
/ Usage: FXMUL Ra, Rb
/ Ra = first operand (Q8), modified: receives result
/ Rb = second operand (Q8), preserved
/ Clobbers: Ra, Ra+1 (register pair)
/ 
/ Note: Ra must be even (R0, R2, R4) for MUL/ASHC
/ ============================================================


/ ============================================================
/ FXMQ15 - Q15 * Q8 -> Q8 (macro, inline)
/ 
/ For multiplying softmax weights (Q15) by values (Q8)
/ 
/ Usage: FXMQ15 Ra, Rb
/ Ra = Q15 operand (even register)
/ Rb = Q8 operand
/ Result in Ra
/ ============================================================


/ ============================================================
/ FXABS - Absolute value (macro, inline)
/ 
/ Usage: FXABS Rn
/ ============================================================


/ ============================================================
/ FXDIV - Q8 division: R0 = R0 / R1
/ 
/ Input:  R0 = dividend (Q8)
/ R1 = divisor  (Q8)
/ Output: R0 = quotient (Q8)
/ Clobbers: R0, R1, R2, R3
/ 
/ Method: shift dividend left 8 to get Q16, then DIV by
/ divisor to get Q8 quotient.
/ R0:R1(Q16) / R1(Q8) -> Q8
/ ============================================================

        .globl  fxdiv

/ save divisor in R3
fxdiv:  mov     r1, r3
/ R2 = dividend (Q8)
        mov     r0, r2
/ Build 32-bit Q16 dividend in R0:R1
/ = original Q8 value << 8
/ R0 = high word
        clr     r0
/ R1 = low word = original Q8
        mov     r2, r1
/ shift left 8: R0:R1 <<= 8
        ashc    $8., r0
/ Now R0:R1 = dividend in Q16
/ R0 = quotient (Q8), R1 = remainder
        div     r3, r0
        rts     pc

/ end of FXMATH.MAC

/ ===== sample/nn11/VECOP.MAC =====
/ 
/ VECOP.MAC - Vector operations
/ NN11 Level 1 - Vector operations
/ 
/ All values are Q7.8 fixed-point (16-bit signed)
/ Requires: FXMATH.MAC (for FXMUL macro)
/ 

/ ============================================================
/ VDOT - Dot product with 32-bit accumulation
/ 
/ Computes sum(x[i] * y[i]) in Q8
/ Accumulates products in 32-bit Q16 to avoid overflow,
/ then shifts result back to Q8.
/ 
/ Input:  R0 = ptr to vector x
/ R1 = ptr to vector y
/ R2 = length (number of elements)
/ Output: R0 = dot product (Q8)
/ Clobbers: R0-R4
/ ============================================================

        .globl  vdot

/ save R3, R4
vdot:   mov     r3, -(sp)
        mov     r4, -(sp)
/ 32-bit accumulator = 0
        clr     dthi
        clr     dtlo
/ R3 = ptr x
        mov     r0, r3
/ R4 = ptr y
        mov     r1, r4
/ R2 = loop counter
/ R0 = x[i] (even reg for MUL)
vdot_l1:     mov     (r3)+, r0
/ R0:R1 = x[i] * y[i] (Q16, 32-bit)
        mul     (r4)+, r0
/ accumulate low word
        add     r1, dtlo
/ propagate carry
        adc     dthi
/ accumulate high word
        add     r0, dthi
        sob     r2, vdot_l1
/ Convert 32-bit Q16 result to Q8
        mov     dthi, r0
        mov     dtlo, r1
/ shift right 8: Q16 -> Q8
        ashc    $-8., r0
/ Clamp: if R0 != 0 and R0 != -1, result overflows 16 bits
        tst     r0
/ R0=0: positive result fits
        beq     vd_ok
        cmp     r0, $-1
/ R0=-1: negative result fits
        beq     vd_ok
        bpl     vd_po
/ clamp to -32768
        mov     $100000, r1
        br      vd_ok
/ clamp to +32767
vd_po:  mov     $077777, r1
/ result in R0
vd_ok:  mov     r1, r0
/ restore
        mov     (sp)+, r4
        mov     (sp)+, r3
        rts     pc

/ accumulator high word
dthi:  0
/ accumulator low word
dtlo:  0

/ ============================================================
/ VADD - Vector addition: z[i] = x[i] + y[i]
/ 
/ Input:  R0 = ptr x, R1 = ptr y, R2 = ptr z, R3 = length
/ Output: pointers advanced, z[] filled
/ Clobbers: R0-R3
/ ============================================================

        .globl  vadd

vadd:
/ z[i] = x[i]
vadd_l1:     mov     (r0)+, (r2)
/ z[i] += y[i], advance z
        add     (r1)+, (r2)+
        sob     r3, vadd_l1
        rts     pc

/ ============================================================
/ VSUB - Vector subtraction: z[i] = x[i] - y[i]
/ 
/ Input:  R0 = ptr x, R1 = ptr y, R2 = ptr z, R3 = length
/ Output: pointers advanced, z[] filled
/ Clobbers: R0-R3
/ ============================================================

        .globl  vsub

vsub:
/ z[i] = x[i]
vsub_l1:     mov     (r0)+, (r2)
/ z[i] -= y[i], advance z
        sub     (r1)+, (r2)+
        sob     r3, vsub_l1
        rts     pc

/ ============================================================
/ VSCL - Scalar-vector multiply: y[i] = alpha * x[i]  (Q8)
/ 
/ Input:  R0 = ptr x, R1 = ptr y, R2 = length, R3 = alpha (Q8)
/ Output: y[] filled with scaled values
/ Clobbers: R0-R5
/ ============================================================

        .globl  vscl

/ save R4, R5
vscl:   mov     r4, -(sp)
        mov     r5, -(sp)
/ R4 = ptr x
        mov     r0, r4
/ R5 = ptr y
        mov     r1, r5
/ R2 = counter, R3 = alpha
/ R0 = x[i] (even reg)
vscl_l1:     mov     (r4)+, r0
/ R0:R1 = alpha * x[i] (Q16)
        mul     r3, r0
/ >> 8 -> Q8 in R1
        ashc    $-8., r0
/ y[i] = result
        mov     r1, (r5)+
        sob     r2, vscl_l1
/ restore
        mov     (sp)+, r5
        mov     (sp)+, r4
        rts     pc

/ ============================================================
/ VMAX - Find maximum element and its index
/ 
/ Input:  R0 = ptr to vector, R1 = length
/ Output: R0 = max value (Q8), R1 = index of max (0-based)
/ Clobbers: R0-R4
/ ============================================================

        .globl  vmax

/ R2 = current max = vec[0]
vmax:   mov     (r0)+, r2
/ R3 = index of max = 0
        clr     r3
/ R4 = current index
        mov     $1, r4
/ remaining elements
        dec     r1
/ single element, done
        beq     vmax_l2
/ vec[i] vs current max
vmax_l1:     cmp     (r0)+, r2
/ not greater, skip
        ble     vmax_l3
/ new max (re-read after autoincr)
        mov     -2(r0), r2
/ new best index
        mov     r4, r3
vmax_l3:     inc     r4
        sob     r1, vmax_l1
/ R0 = max value
vmax_l2:     mov     r2, r0
/ R1 = max index
        mov     r3, r1
        rts     pc

/ ============================================================
/ VCPY - Vector copy: dst[i] = src[i]
/ 
/ Input:  R0 = ptr src, R1 = ptr dst, R2 = length
/ Clobbers: R0-R2
/ ============================================================

        .globl  vcpy

vcpy:
vcpy_l1:     mov     (r0)+, (r1)+
        sob     r2, vcpy_l1
        rts     pc

/ ============================================================
/ VCLR - Clear vector: vec[i] = 0
/ 
/ Input:  R0 = ptr vec, R1 = length
/ Clobbers: R0, R1
/ ============================================================

        .globl  vclr

vclr:
vclr_l1:     clr     (r0)+
        sob     r1, vclr_l1
        rts     pc

/ end of VECOP.MAC

/ ===== sample/nn11/MATOP.MAC =====
/ 
/ MATOP.MAC - Matrix operations
/ NN11 Level 2 - Matrix-vector operations
/ 
/ All values Q7.8 fixed-point. Matrices are row-major.
/ All routines use the JSR R5 inline parameter convention.
/ 
/ Requires: FXMATH.MAC (for Q8 format definitions)
/ 

/ ============================================================
/ MVMUL - Matrix-vector multiply: vout = mat * vin
/ 
/ vout[i] = sum_j( mat[i][j] * vin[j] )  for i=0..rows-1
/ Uses 32-bit Q16 accumulation per output element.
/ 
/ Call:   JSR  R5, MVMUL
/ .WORD mat, vin, vout, rows, cols
/ 
/ vout has 'rows' elements, vin has 'cols' elements.
/ Clobbers: R0-R4
/ ============================================================

        .globl  mvmul

mvmul:  mov     (r5)+, mp_mat
        mov     (r5)+, mp_vin
        mov     (r5)+, mp_out
        mov     (r5)+, mp_row
        mov     (r5)+, mp_col
        mov     r3, -(sp)
        mov     r4, -(sp)
/ R3 = matrix ptr (advances row by row)
        mov     mp_mat, r3
/ R4 = output ptr (advances)
        mov     mp_out, r4
/ --- one row: dot product with vin ---
mvmul_l10:
        clr     mp_ahi
        clr     mp_alo
/ R2 = vin ptr (reset each row)
        mov     mp_vin, r2
/ push column counter
        mov     mp_col, -(sp)
/ R0 = mat[i][j]
mvmul_l20:    mov     (r3)+, r0
/ R0:R1 = mat[i][j] * vin[j] (Q16)
        mul     (r2)+, r0
/ 32-bit accumulate
        add     r1, mp_alo
        adc     mp_ahi
        add     r0, mp_ahi
        dec     (sp)
        bne     mvmul_l20
/ pop counter
        tst     (sp)+
/ Q16 -> Q8, with clamp
        mov     mp_ahi, r0
        mov     mp_alo, r1
        ashc    $-8., r0
        tst     r0
        beq     mvmul_l5
        cmp     r0, $-1
        beq     mvmul_l5
        bpl     mvmul_l6
        mov     $100000, r1
        br      mvmul_l5
mvmul_l6:     mov     $077777, r1
/ vout[i] = result
mvmul_l5:     mov     r1, (r4)+
        dec     mp_row
        bne     mvmul_l10
        mov     (sp)+, r4
        mov     (sp)+, r3
        rts     r5

/ ============================================================
/ MVADD - Matrix-vector multiply-add: vout += mat * vin
/ 
/ Like MVMUL but adds to existing vout values.
/ 
/ Call:   JSR  R5, MVADD
/ .WORD mat, vin, vout, rows, cols
/ Clobbers: R0-R4
/ ============================================================

        .globl  mvadd

mvadd:  mov     (r5)+, mp_mat
        mov     (r5)+, mp_vin
        mov     (r5)+, mp_out
        mov     (r5)+, mp_row
        mov     (r5)+, mp_col
        mov     r3, -(sp)
        mov     r4, -(sp)
        mov     mp_mat, r3
        mov     mp_out, r4
mvadd_l10:    clr     mp_ahi
        clr     mp_alo
        mov     mp_vin, r2
        mov     mp_col, -(sp)
mvadd_l20:    mov     (r3)+, r0
        mul     (r2)+, r0
        add     r1, mp_alo
        adc     mp_ahi
        add     r0, mp_ahi
        dec     (sp)
        bne     mvadd_l20
        tst     (sp)+
        mov     mp_ahi, r0
        mov     mp_alo, r1
        ashc    $-8., r0
/ Clamp R1 if overflow
        tst     r0
        beq     mvadd_l37
        cmp     r0, $-1
        beq     mvadd_l37
        bpl     mvadd_l38
        mov     $100000, r1
        br      mvadd_l37
mvadd_l38:    mov     $077777, r1
/ ADD instead of MOV
mvadd_l37:    add     r1, (r4)
        bvc     mvadd_l35
        tst     r1
        bmi     mvadd_l36
        mov     $077777, (r4)
        br      mvadd_l35
mvadd_l36:    mov     $100000, (r4)
mvadd_l35:    add     $2, r4
        dec     mp_row
        bne     mvadd_l10
        mov     (sp)+, r4
        mov     (sp)+, r3
        rts     r5

/ ============================================================
/ VTMUL - Transpose-vector multiply: vout = mat^T * vin
/ 
/ vout[j] = sum_i( mat[i][j] * vin[i] )  for j=0..cols-1
/ Clears vout first, then accumulates row by row:
/ for each row i, adds vin[i] * row_i to vout.
/ Per-product Q8 rounding (acceptable for d_model=16).
/ 
/ Call:   JSR  R5, VTMUL
/ .WORD mat, vin, vout, rows, cols
/ 
/ vin has 'rows' elements, vout has 'cols' elements.
/ Clobbers: R0-R4
/ ============================================================

        .globl  vtmul

vtmul:  mov     (r5)+, mp_mat
        mov     (r5)+, mp_vin
        mov     (r5)+, mp_out
        mov     (r5)+, mp_row
        mov     (r5)+, mp_col
        mov     r3, -(sp)
        mov     r4, -(sp)
/ Clear output vector
        mov     mp_out, r0
        mov     mp_col, r1
vtmul_l5:     clr     (r0)+
        sob     r1, vtmul_l5
/ Process rows
/ R3 = matrix ptr (advances through all)
        mov     mp_mat, r3
/ R2 = vin ptr (advances per row)
        mov     mp_vin, r2
/ --- one row: vout += vin[i] * row_i ---
vtmul_l10:
/ scalar = vin[i]
        mov     (r2)+, mp_scl
/ R4 = output ptr (reset each row)
        mov     mp_out, r4
/ column counter
        mov     mp_col, -(sp)
/ R0 = mat[i][j]
vtmul_l20:    mov     (r3)+, r0
/ R0:R1 = mat[i][j] * vin[i] (Q16)
        mul     mp_scl, r0
/ >> 8 -> Q8 in R1
        ashc    $-8., r0
/ Clamp R1
        tst     r0
        beq     vtmul_l42
        cmp     r0, $-1
        beq     vtmul_l42
        bpl     vtmul_l43
        mov     $100000, r1
        br      vtmul_l42
vtmul_l43:    mov     $077777, r1
/ vout[j] += result
vtmul_l42:    add     r1, (r4)
        bvc     vtmul_l40
        tst     r1
        bmi     vtmul_l41
        mov     $077777, (r4)
        br      vtmul_l40
vtmul_l41:    mov     $100000, (r4)
vtmul_l40:    add     $2, r4
        dec     (sp)
        bne     vtmul_l20
        tst     (sp)+
        dec     mp_row
        bne     vtmul_l10
        mov     (sp)+, r4
        mov     (sp)+, r3
        rts     r5

/ ============================================================
/ OUTER - Outer product: mat += vx (x) vy
/ 
/ mat[i][j] += vx[i] * vy[j]
/ For backward pass: weight gradient accumulation.
/ 
/ Call:   JSR  R5, OUTER
/ .WORD mat, vx, vy, rows, cols
/ 
/ rows = len(vx), cols = len(vy)
/ Clobbers: R0-R4
/ ============================================================

        .globl  outer

outer:  mov     (r5)+, mp_mat
/ vx
        mov     (r5)+, mp_vin
/ vy
        mov     (r5)+, mp_out
        mov     (r5)+, mp_row
        mov     (r5)+, mp_col
        mov     r3, -(sp)
        mov     r4, -(sp)
/ R3 = matrix ptr (advances)
        mov     mp_mat, r3
/ R2 = vx ptr (advances per row)
        mov     mp_vin, r2
/ --- one row: mat[i] += vx[i] * vy ---
outer_l10:
/ scalar = vx[i]
        mov     (r2)+, mp_scl
/ R4 = vy ptr (reset each row)
        mov     mp_out, r4
        mov     mp_col, -(sp)
/ R0 = vy[j]
outer_l20:    mov     (r4)+, r0
/ R0:R1 = vx[i] * vy[j] (Q16)
        mul     mp_scl, r0
/ >> 8 -> Q8 in R1
        ashc    $-8., r0
/ Clamp R1 after ASHC
        tst     r0
        beq     outer_l26
        cmp     r0, $-1
        beq     outer_l26
        bpl     outer_l27
        mov     $100000, r1
        br      outer_l26
outer_l27:    mov     $077777, r1
/ mat[i][j] += result
outer_l26:    add     r1, (r3)
/ no overflow, skip clamp
        bvc     outer_l30
        tst     r1
        bmi     outer_l25
/ clamp to +32767
        mov     $077777, (r3)
        br      outer_l30
/ clamp to -32768
outer_l25:    mov     $100000, (r3)
/ advance ptr
outer_l30:    add     $2, r3
        dec     (sp)
        bne     outer_l20
        tst     (sp)+
        dec     mp_row
        bne     outer_l10
        mov     (sp)+, r4
        mov     (sp)+, r3
        rts     r5

/ ============================================================
/ Shared private storage for all matrix operations
/ ============================================================
/ matrix pointer
mp_mat:  0
/ input vector / vx pointer
mp_vin:  0
/ output vector / vy pointer
mp_out:  0
/ row count
mp_row:  0
/ column count
mp_col:  0
/ accumulator high (MVMUL/MVADD)
mp_ahi:  0
/ accumulator low
mp_alo:  0
/ scalar temp (VTMUL/OUTER)
mp_scl:  0

/ end of MATOP.MAC

/ ===== sample/nn11/ACTFN.MAC =====
/ 
/ ACTFN.MAC - Activation functions for NN11
/ Part of NN11: Neural Network library for PDP-11
/ 
/ Includes:
/ RELU   - ReLU macro (single register)
/ DRELU  - ReLU derivative macro (for backward pass)
/ VRELU  - ReLU on vector (subroutine)
/ SFTMX  - Softmax on vector (subroutine, LUT-based)
/ EXPTBL - exp() lookup table (256 entries, Q8)
/ 
/ Requires: FXMATH.MAC (FXDIV), VECOP.MAC (VMAX)
/ 

/ ============================================================
/ RELU - ReLU activation (macro, inline)
/ 
/ Usage: RELU Rn
/ Sets Rn = max(0, Rn)
/ ============================================================


/ ============================================================
/ DRELU - ReLU derivative mask (macro, for backward pass)
/ 
/ Usage: DRELU Rgrad, Ract
/ Rgrad = gradient register (zeroed if activation was <= 0)
/ Ract  = original pre-activation value (tested, not modified)
/ ============================================================


/ ============================================================
/ VRELU - Apply ReLU to vector in-place
/ 
/ Input:  R0 = ptr to vector, R1 = length
/ Output: negative elements zeroed
/ Clobbers: R0, R1
/ ============================================================

        .globl  vrelu

vrelu:
/ test element
vrelu_l1:     tst     (r0)
/ positive: keep
        bpl     vrelu_l2
/ negative: zero
        clr     (r0)
/ next word
vrelu_l2:     add     $2, r0
        sob     r1, vrelu_l1
        rts     pc

/ ============================================================
/ SFTMX - Softmax on vector in-place
/ 
/ Computes softmax(x_i) = exp(x_i - max) / sum(exp(x_j - max))
/ Uses EXPTBL lookup for exp(), FXDIV for division.
/ 
/ Input:  R0 = ptr to vector (Q8), R1 = length
/ Output: vector replaced with softmax probabilities (Q8)
/ Values in [0, 1.0], should sum to ~1.0 (256 Q8)
/ Clobbers: R0-R5
/ ============================================================

        .globl  sftmx

/ save vector ptr
sftmx:  mov     r0, sf_vec
/ save length
        mov     r1, sf_len

/ Step 1: Find max value (for numerical stability)
/ R0=max, R1=index (clobbers R0-R4)
        jsr     pc, vmax
        mov     r0, sf_max

/ Step 2: exp(x_i - max) via LUT, accumulate sum
/ R5 = running sum of exp values
        clr     r5
/ R3 = vector ptr
        mov     sf_vec, r3
/ R4 = counter
        mov     sf_len, r4
/ R0 = x_i (Q8)
sftmx_l10:    mov     (r3), r0
/ R0 = x_i - max (<= 0)
        sub     sf_max, r0
/ R0 = |x_i - max| (>= 0)
        neg     r0
/ divide by 8 -> table index
        ash     $-3., r0
/ clamp to table bounds
        cmp     r0, $255.
        ble     sftmx_l15
        mov     $255., r0
/ word offset (index * 2)
sftmx_l15:    asl     r0
/ lookup exp value (Q8)
        mov     exptbl(r0), r0
/ store in-place
        mov     r0, (r3)+
/ sum += exp_i
        add     r0, r5
        sob     r4, sftmx_l10

/ Step 3: Normalize - divide each exp value by sum
/ reset vector ptr
        mov     sf_vec, r3
/ reset counter
        mov     sf_len, r4
/ R5 = sum (FXDIV only clobbers R0-R3, R5 is safe)
/ R0 = exp_i (Q8)
sftmx_l30:    mov     (r3), r0
/ save ptr (FXDIV clobbers R3)
        mov     r3, -(sp)
/ R1 = sum (divisor)
        mov     r5, r1
/ R0 = exp_i / sum (Q8)
        jsr     pc, fxdiv
/ restore ptr
        mov     (sp)+, r3
/ store normalized value
        mov     r0, (r3)+
        sob     r4, sftmx_l30

        rts     pc

/ Private storage
sf_vec:  0
sf_len:  0
sf_max:  0

/ ============================================================
/ EXPTBL - exp() lookup table for softmax
/ Index i -> exp(-i/32) in Q8 format
/ 256 entries, covers x in [-7.97, 0]
/ 
/ Key values:
/ [0]   = exp(0)    = 1.000 = 256
/ [16]  = exp(-0.5) = 0.607 = 155
/ [32]  = exp(-1.0) = 0.368 = 94
/ [64]  = exp(-2.0) = 0.135 = 35
/ [128] = exp(-4.0) = 0.018 = 5
/ [255] = exp(-7.97)= 0.000 = 0
/ ============================================================

exptbl:
        / [0]-[7]
         256.
                 248.
                 240.
                 233.
                 226.
                 219.
                 212.
                 206.
        / [8]-[15]
         199.
                 193.
                 187.
                 182.
                 176.
                 171.
                 165.
                 160.
        / [16]-[23]
         155.
                 150.
                 146.
                 141.
                 137.
                 133.
                 129.
                 125.
        / [24]-[31]
         121.
                 117.
                 114.
                 110.
                 107.
                 103.
                 100.
                 97.
        / [32]-[39]
         94.
                 91.
                 88.
                 86.
                 83.
                 81.
                 78.
                 76.
        / [40]-[47]
         73.
                 71.
                 69.
                 67.
                 65.
                 63.
                 61.
                 59.
        / [48]-[55]
         57.
                 55.
                 54.
                 52.
                 50.
                 49.
                 47.
                 46.
        / [56]-[63]
         44.
                 43.
                 42.
                 41.
                 39.
                 38.
                 37.
                 36.
        / [64]-[71]
         35.
                 34.
                 33.
                 32.
                 31.
                 30.
                 29.
                 28.
        / [72]-[79]
         27.
                 26.
                 25.
                 25.
                 24.
                 23.
                 22.
                 22.
        / [80]-[87]
         21.
                 20.
                 20.
                 19.
                 19.
                 18.
                 17.
                 17.
        / [88]-[95]
         16.
                 16.
                 15.
                 15.
                 14.
                 14.
                 14.
                 13.
        / [96]-[103]
         13.
                 12.
                 12.
                 12.
                 11.
                 11.
                 11.
                 10.
        / [104]-[111]
         10.
                 10.
                 9.
                 9.
                 9.
                 8.
                 8.
                 8.
        / [112]-[119]
         8.
                 7.
                 7.
                 7.
                 7.
                 7.
                 6.
                 6.
        / [120]-[127]
         6.
                 6.
                 6.
                 5.
                 5.
                 5.
                 5.
                 5.
        / [128]-[135]
         5.
                 5.
                 4.
                 4.
                 4.
                 4.
                 4.
                 4.
        / [136]-[143]
         4.
                 4.
                 3.
                 3.
                 3.
                 3.
                 3.
                 3.
        / [144]-[151]
         3.
                 3.
                 3.
                 3.
                 3.
                 2.
                 2.
                 2.
        / [152]-[159]
         2.
                 2.
                 2.
                 2.
                 2.
                 2.
                 2.
                 2.
        / [160]-[167]
         2.
                 2.
                 2.
                 2.
                 2.
                 1.
                 1.
                 1.
        / [168]-[175]
         1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
        / [176]-[183]
         1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
        / [184]-[191]
         1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
        / [192]-[199]
         1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
                 1.
        / [200]-[207]
         0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
        / [208]-[215]
         0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
        / [216]-[223]
         0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
        / [224]-[231]
         0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
        / [232]-[239]
         0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
        / [240]-[247]
         0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
        / [248]-[255]
         0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.
                 0.

/ end of ACTFN.MAC

/ ===== sample/nn11/LAYER.MAC =====
/ 
/ LAYER.MAC - Neural network layer operations for NN11
/ Part of NN11: Neural Network library for PDP-11
/ 
/ Includes:
/ EMBED  - Token + position embedding lookup
/ PROJ   - Output projection (Y -> logits via VTMUL)
/ 
/ Requires: FXMATH.MAC, VECOP.MAC, MATOP.MAC, ACTFN.MAC
/ 

/ ============================================================
/ EMBED - Token + position embedding lookup
/ 
/ For each position i in the sequence:
/ output[i] = token_embed[tokens[i]] + pos_embed[i]
/ 
/ Call:   JSR  R5, EMBED
/ .WORD tokens   ; ptr to token ID array (seq_len words)
/ .WORD tkemb    ; ptr to token embed table (vocab x d_model)
/ .WORD psemb    ; ptr to pos embed table (seq_len x d_model)
/ .WORD xout     ; ptr to output (seq_len x d_model)
/ .WORD seq_len
/ .WORD d_model
/ 
/ Clobbers: R0-R4
/ ============================================================

        .globl  embed

embed:  mov     (r5)+, em_tok
        mov     (r5)+, em_tke
        mov     (r5)+, em_pse
        mov     (r5)+, em_out
        mov     (r5)+, em_seq
        mov     (r5)+, em_dim
        mov     r3, -(sp)
        mov     r4, -(sp)
/ Row size in bytes
        mov     em_dim, r0
        asl     r0
/ d_model * 2
        mov     r0, em_rsz
/ R3 = token ID ptr (advances)
        mov     em_tok, r3
/ R4 = pos embed ptr (advances)
        mov     em_pse, r4
/ --- one position ---
embed_l10:
/ R0 = token ID
        mov     (r3)+, r0
/ R0:R1 = tok_id * row_size
        mul     em_rsz, r0
/ R1 = &tkemb[tok_id][0]
        add     em_tke, r1
/ R2 = output ptr
        mov     em_out, r2
/ column counter
        mov     em_dim, -(sp)
/ out[j] = tkemb[tok][j]
embed_l20:    mov     (r1)+, (r2)
/ out[j] += psemb[pos][j]
        add     (r4)+, (r2)+
        dec     (sp)
        bne     embed_l20
        tst     (sp)+
/ advance output to next row
        add     em_rsz, em_out
        dec     em_seq
        bne     embed_l10
        mov     (sp)+, r4
        mov     (sp)+, r3
        rts     r5

/ EMBED private storage
em_tok:  0
em_tke:  0
em_pse:  0
em_out:  0
em_seq:  0
em_dim:  0
em_rsz:  0

/ ============================================================
/ PROJ - Output projection: logits[i] = Y[i] . Wout
/ 
/ For each position, projects d_model -> vocab via VTMUL.
/ Uses self-modifying code to patch VTMUL inline params.
/ 
/ Call:   JSR  R5, PROJ
/ .WORD yin      ; ptr to input (seq_len x d_model)
/ .WORD wout     ; ptr to weight matrix (d_model x vocab)
/ .WORD logits   ; ptr to output (seq_len x vocab)
/ .WORD seq_len
/ .WORD d_model
/ .WORD vocab
/ 
/ Clobbers: R0-R4
/ ============================================================

        .globl  proj

proj:   mov     (r5)+, pj_yin
        mov     (r5)+, pj_wot
        mov     (r5)+, pj_log
        mov     (r5)+, pj_seq
        mov     (r5)+, pj_dim
        mov     (r5)+, pj_voc
/ Compute row sizes
        mov     pj_dim, r0
        asl     r0
/ input row = d_model * 2
        mov     r0, pj_ysz
        mov     pj_voc, r0
        asl     r0
/ output row = vocab * 2
        mov     r0, pj_lsz
/ Patch VTMUL param block (self-modifying code)
pj_lp:
        mov     pj_wot, pj_p1
        mov     pj_yin, pj_p2
        mov     pj_log, pj_p3
        mov     pj_dim, pj_p4
        mov     pj_voc, pj_p5
/ Call VTMUL: logits[i] = Wout^T * Y[i]
        jsr     r5, vtmul
/ mat = Wout
pj_p1:  0
/ vin = Y[i]
pj_p2:  0
/ vout = logits[i]
pj_p3:  0
/ rows = d_model
pj_p4:  0
/ cols = vocab
pj_p5:  0
/ Advance pointers
        add     pj_ysz, pj_yin
        add     pj_lsz, pj_log
        dec     pj_seq
        bne     pj_lp
        rts     r5

/ PROJ private storage
pj_yin:  0
pj_wot:  0
pj_log:  0
pj_seq:  0
pj_dim:  0
pj_voc:  0
pj_ysz:  0
pj_lsz:  0

/ ============================================================
/ ATTN - Self-attention forward pass
/ 
/ 1. Q=X.Wq, K=X.Wk, V=X.Wv   (linear projections)
/ 2. S[i][j] = Q[i].K[j] / sqrt(d)  (scores)
/ 3. A[i] = softmax(S[i])       (per-row)
/ 4. O[i] = sum_j A[i][j]*V[j]  (weighted sum)
/ 5. Y[i] = O[i] + X[i]         (residual)
/ 
/ Call:   JSR  R5, ATTN
/ .WORD xin, wq, wk, wv, yout, work
/ .WORD seq_len, d_model, sqrtsh
/ 
/ work >= 3*seq*dim + seq*seq words
/ sqrtsh = ASR shifts for 1/sqrt(d) (1 for d=4, 2 for d=16)
/ Clobbers: R0-R5
/ ============================================================

        .globl  attn

attn:   mov     (r5)+, at_xin
        mov     (r5)+, at_wq
        mov     (r5)+, at_wk
        mov     (r5)+, at_wv
        mov     (r5)+, at_yot
        mov     (r5)+, at_wrk
        mov     (r5)+, at_seq
        mov     (r5)+, at_dim
        mov     (r5)+, at_shf
/ save R5 (SFTMX clobbers it)
        mov     r5, -(sp)
/ Compute sizes
        mov     at_dim, r0
        asl     r0
/ d_model * 2
        mov     r0, at_rsz
        mov     at_seq, r0
        asl     r0
/ seq_len * 2
        mov     r0, at_ssz
/ Workspace: Q | K | V | S
        mov     at_wrk, at_qq
        mov     at_seq, r0
/ R1 = seq * dim * 2
        mul     at_rsz, r0
        mov     at_qq, r2
        add     r1, r2
        mov     r2, at_kk
        add     r1, r2
        mov     r2, at_vv
        add     r1, r2
        mov     r2, at_ss
/ Negate shift for ASH
        neg     at_shf

/ --- Step 1: Q = X . Wq ---
        mov     at_xin, at_bi
        mov     at_wq, at_bw
        mov     at_qq, at_bo
        jsr     pc, at_bpr
/ --- Step 2: K = X . Wk ---
        mov     at_xin, at_bi
        mov     at_wk, at_bw
        mov     at_kk, at_bo
        jsr     pc, at_bpr
/ --- Step 3: V = X . Wv ---
        mov     at_xin, at_bi
        mov     at_wv, at_bw
        mov     at_vv, at_bo
        jsr     pc, at_bpr

/ --- Step 4: S[i][j] = Q[i].K[j] / sqrt(d) ---
        mov     at_qq, at_qi
        mov     at_ss, at_si
        mov     at_seq, at_oc
at_s1:  mov     at_kk, at_kj
        mov     at_seq, at_ic
at_s2:  mov     at_qi, r0
        mov     at_kj, r1
        mov     at_dim, r2
        jsr     pc, vdot
/ /= sqrt(d)
        ash     at_shf, r0
        mov     at_si, r1
        mov     r0, (r1)+
        mov     r1, at_si
        add     at_rsz, at_kj
        dec     at_ic
        bne     at_s2
        add     at_rsz, at_qi
        dec     at_oc
        bne     at_s1

/ --- Step 5: softmax per row ---
        mov     at_ss, at_si
        mov     at_seq, at_oc
at_s3:  mov     at_si, r0
        mov     at_seq, r1
        jsr     pc, sftmx
        add     at_ssz, at_si
        dec     at_oc
        bne     at_s3

/ --- Step 6: Y[i] = V^T . S[i] ---
        mov     at_ss, at_si
        mov     at_yot, at_yi
        mov     at_seq, at_oc
at_s4:  mov     at_vv, at_t1
        mov     at_si, at_t2
        mov     at_yi, at_t3
        mov     at_seq, at_t4
        mov     at_dim, at_t5
        jsr     r5, vtmul
at_t1:  0
at_t2:  0
at_t3:  0
at_t4:  0
at_t5:  0
        add     at_ssz, at_si
        add     at_rsz, at_yi
        dec     at_oc
        bne     at_s4

/ --- Step 7: Y += X (residual) ---
        mov     at_yot, r0
        mov     at_xin, r1
        mov     at_seq, r2
/ R2:R3 = total elems
        mul     at_dim, r2
at_s5:  add     (r1)+, (r0)+
        sob     r3, at_s5

        mov     (sp)+, r5
        rts     r5

/ --- AT.BPR: batch project out[i] = W^T . in[i] ---
at_bpr: mov     at_seq, at_bc
at_bl:  mov     at_bw, at_v1
        mov     at_bi, at_v2
        mov     at_bo, at_v3
        mov     at_dim, at_v4
        mov     at_dim, at_v5
        jsr     r5, vtmul
at_v1:  0
at_v2:  0
at_v3:  0
at_v4:  0
at_v5:  0
        add     at_rsz, at_bi
        add     at_rsz, at_bo
        dec     at_bc
        bne     at_bl
        rts     pc

/ ATTN private storage
at_xin:  0
at_wq:  0
at_wk:  0
at_wv:  0
at_yot:  0
at_wrk:  0
at_seq:  0
at_dim:  0
at_shf:  0
at_rsz:  0
at_ssz:  0
at_qq:  0
at_kk:  0
at_vv:  0
at_ss:  0
at_qi:  0
at_kj:  0
at_si:  0
at_yi:  0
at_oc:  0
at_ic:  0
at_bw:  0
at_bi:  0
at_bo:  0
at_bc:  0

/ end of LAYER.MAC

/ ===== attn/io_bsd replacement =====

putc:
	movb	r0, putc_ch
	mov	$1,-(sp)
	mov	$putc_ch,-(sp)
	mov	$1,-(sp)
	jsr	pc,_write
	add	$6,sp
	rts	pc

	.globl	puts
puts:
	mov	r1,-(sp)
	mov	r0,r1
puts_l1:
	tstb	(r1)+
	bne	puts_l1
	sub	r0,r1
	dec	r1
	mov	r1,-(sp)
	mov	r0,-(sp)
	mov	$1,-(sp)
	jsr	pc,_write
	add	$6,sp
	mov	(sp)+,r1
	rts	pc

	.globl	newln
newln:
	mov	$1,-(sp)
	mov	$newline_str,-(sp)
	mov	$1,-(sp)
	jsr	pc,_write
	add	$6,sp
	rts	pc

	.globl	putoct
putoct:
	mov	r0,r2
	mov	$6.,r1
	ash	$-15.,r0
	br	putoct_l3
putoct_l3:
	mov	r2,r0
	mov	$6.,r1
putoct_l2:
	mov	r0,-(sp)
	bic	$177770,(sp)
	add	$'0,(sp)
	ash	$-3.,r0
	sob	r1,putoct_l2
	mov	$6.,r1
putoct_l4:
	mov	(sp)+,r0
	jsr	pc,putc
	sob	r1,putoct_l4
	rts	pc

	.globl	putdec
putdec:
	tst	r0
	bpl	putdec_l1
	mov	r0,-(sp)
	mov	$'-,r0
	jsr	pc,putc
	mov	(sp)+,r0
	neg	r0
putdec_l1:
	clr	r3
putdec_l2:
	clr	r1
	mov	r0,r1
	clr	r0
	div	$10.,r0
	add	$'0,r1
	mov	r1,-(sp)
	inc	r3
	tst	r0
	bne	putdec_l2
putdec_l3:
	mov	(sp)+,r0
	jsr	pc,putc
	sob	r3,putdec_l3
	rts	pc

	.globl	putq8
putq8:
	mov	r0,r2
	tst	r0
	bpl	putq8_l1
	mov	$'-,r0
	jsr	pc,putc
	mov	r2,r0
	neg	r0
	mov	r0,r2
putq8_l1:
	mov	r2,r0
	ash	$-8.,r0
	jsr	pc,putdec
	mov	$'.,r0
	jsr	pc,putc
	mov	r2,r0
	bic	$177400,r0
	mul	$1000.,r0
	ashc	$-8.,r0
	mov	r1,r2
	mov	r2,r1
	clr	r0
	div	$100.,r0
	mov	r1,r2
	add	$'0,r0
	jsr	pc,putc
	mov	r2,r1
	clr	r0
	div	$10.,r0
	mov	r1,r2
	add	$'0,r0
	jsr	pc,putc
	mov	r2,r0
	add	$'0,r0
	jsr	pc,putc
	rts	pc

	.globl	putspc
putspc:
	mov	$40,r0
	jsr	pc,putc
	rts	pc

	.globl	putvec
putvec:
	mov	r4,-(sp)
	mov	r5,-(sp)
	mov	r0,r4
	mov	r1,r5
	mov	$'[,r0
	jsr	pc,putc
putvec_l1:
	mov	(r4)+,r0
	jsr	pc,putq8
	dec	r5
	beq	putvec_l2
	mov	$54,r0
	jsr	pc,putc
	mov	$40,r0
	jsr	pc,putc
	br	putvec_l1
putvec_l2:
	mov	$'],r0
	jsr	pc,putc
	mov	(sp)+,r5
	mov	(sp)+,r4
	rts	pc

	.data
newline_str:	.byte 10.
putc_ch:	.byte 0.
	.even
	.text

/ ===== sample/model/FORWRD.MAC =====
/ 
/ FORWRD.MAC - Forward pass and Q16->Q8 weight conversion
/ 

/ ============================================================
/ FORWRD - Full forward pass
/ ============================================================

forwrd:
/ 1. Embedding: X = tok_emb[token] + pos_emb
        jsr     r5, embed
         tokens
                 tkeq8
                 pseq8
                 xx
                 8.
                 16.

/ 2. Self-attention: Y = attn(X, Wq, Wk, Wv)
        jsr     r5, attn
         xx
                 wqq8
                 wkq8
                 wvq8
                 yy
                 work
         8.
                 16.
                 2.

/ 3. Output projection: logits = Y . Wout
        jsr     r5, proj
         yy
                 wotq8
                 logits
                 8.
                 16.
                 10.

        rts     pc

/ ============================================================
/ CVT16 - Convert Q16 weights to Q8 for forward/backward
/ ============================================================

cvt16:  mov     $tkeh, r2
        mov     $tkel, r3
        mov     $tkeq8, r4
        mov     $10.*16., r1
        jsr     pc, cv_do
        mov     $pseh, r2
        mov     $psel, r3
        mov     $pseq8, r4
        mov     $8.*16., r1
        jsr     pc, cv_do
        mov     $wqh, r2
        mov     $wql, r3
        mov     $wqq8, r4
        mov     $16.*16., r1
        jsr     pc, cv_do
        mov     $wkh, r2
        mov     $wkl, r3
        mov     $wkq8, r4
        mov     $16.*16., r1
        jsr     pc, cv_do
        mov     $wvh, r2
        mov     $wvl, r3
        mov     $wvq8, r4
        mov     $16.*16., r1
        jsr     pc, cv_do
        mov     $woth, r2
        mov     $wotl, r3
        mov     $wotq8, r4
        mov     $16.*10., r1
        jsr     pc, cv_do
        rts     pc

/ CV.DO: Convert R1 Q16 pairs (R2=hi, R3=lo) -> Q8 at R4
/ Uses R5 as counter (saved/restored)
cv_do:  mov     r5, -(sp)
/ R5 = counter
        mov     r1, r5
/ high word
cv_dl:  mov     (r2)+, r0
/ low word
        mov     (r3)+, r1
/ Q16 >> 8 = Q8, result in R1
        ashc    $-8., r0
/ store Q8
        mov     r1, (r4)+
        sob     r5, cv_dl
        mov     (sp)+, r5
        rts     pc

/ end of FORWRD.MAC

/ ===== sample/model/BKWRD.MAC =====
/ 
/ BKWRD.MAC - Backward pass (Q15 gradients)
/ Follows prototype.py backward() exactly.
/ Q8 forward values x Q15 gradients -> Q15 results.
/ Same MUL+ASHC#-8 as forward (Q8*Q15=Q23, >>8=Q15).
/ 

/ Workspace pointers into ATTN WORK area (set by BK.SET)
/ -> cached Q (8x16 Q8)
bk_qp:  0
/ -> cached K
bk_kp:  0
/ -> cached V
bk_vp:  0
/ -> cached A (8x8 Q8, post-softmax)
bk_ap:  0
/ Loop vars
/ outer loop index
bk_i:  0
/ inner loop index
bk_j:  0
/ outer loop counter
bk_ic:  0
/ inner loop counter
bk_jc:  0
/ dot(A,dA) temp
bk_dad:  0
/ position byte offset temp
bk_off:  0

/ setup workspace ptrs
bkwrd:  jsr     pc, bk_set

/ ==========================================
/ Step 1: dLogits, dWout, dY
/ For each position i:
/ dL = softmax(logits[i]) - one_hot(target[i]), then <<7 -> Q15
/ dWout += OUTER(Y[i], dL)
/ dY[i] = MVMUL(Wout, dL)
/ ==========================================

/ Clear dY
        mov     $dy, r0
        mov     $8.*16., r1
        jsr     pc, vclr

        mov     $8., bk_ic
        clr     bk_i
/ -- Compute dL for position i --
bk_l1:
/ Copy logits[i] to DL
        mov     bk_i, r0
/ R1 = i * vocab * 2
        mul     $10.*2, r0
        add     $logits, r1
/ R2 = &logits[i]
        mov     r1, r2
        mov     $dl, r3
        mov     $10., r4
bk_c1:  mov     (r2)+, (r3)+
        sob     r4, bk_c1
/ Softmax(DL) in-place
        mov     $dl, r0
        mov     $10., r1
        jsr     pc, sftmx
/ DL[target[i]] -= 256 (1.0 Q8)
        mov     bk_i, r0
        asl     r0
        add     $target, r0
/ target digit
        mov     (r0), r0
/ word offset
        asl     r0
        add     $dl, r0
        sub     $256., (r0)
/ Shift DL to Q15: <<7 (use ASH)
        mov     $dl, r2
        mov     $10., r3
bk_s7:  mov     (r2), r0
        ash     $7., r0
        mov     r0, (r2)+
        sob     r3, bk_s7

/ -- dWout += OUTER(Y[i], DL, D.MODL, VOCAB) --
        mov     bk_i, r0
/ R1 = i*d_model*2
        mul     $32., r0
        add     $yy, r1
/ Y[i] ptr
        mov     r1, bk_p2
        mov     $dwout, bk_p1
        mov     $dl, bk_p3
        jsr     r5, outer
/ mat = DWOUT
bk_p1:  0
/ vx = Y[i]
bk_p2:  0
/ vy = DL
bk_p3:  0
/ rows
bk_p4:  16.
/ cols
bk_p5:  10.

/ -- dY[i] = MVMUL(Wout, DL, D.MODL, VOCAB) --
        mov     bk_i, r0
        mul     $32., r0
        add     $dy, r1
/ dY[i] ptr
        mov     r1, bk_q3
        mov     $wotq8, bk_q1
        mov     $dl, bk_q2
        jsr     r5, mvmul
/ mat = Wout_q8
bk_q1:  0
/ vin = DL
bk_q2:  0
/ vout = dY[i]
bk_q3:  0
/ rows
bk_q4:  16.
/ cols
bk_q5:  10.

        inc     bk_i
        dec     bk_ic
        bne     bk_l1

/ ==========================================
/ Step 2: Backward O = A.V -> dA, dV
/ dO = dY (residual, no copy needed)
/ For each i,j:
/ dA[i][j] = VDOT(V[j], dY[i], D)
/ dV[j] += A[i][j] * dY[i] (VSADD)
/ ==========================================

/ Clear dV
        mov     $dvv, r0
        mov     $8.*16., r1
        jsr     pc, vclr

        mov     $8., bk_ic
        clr     bk_i
bk_l2:  mov     $8., bk_jc
        clr     bk_j
/ dA[i][j] = VDOT(V[j], dY[i], D)
bk_l2j:
/ V[j] ptr
        mov     bk_j, r0
        mul     $32., r0
        add     bk_vp, r1
/ R0 = &V[j]
        mov     r1, r0
/ dY[i] ptr
        mov     bk_i, r2
        mul     $32., r2
        add     $dy, r3
/ R1 = &dY[i]
        mov     r3, r1
        mov     $16., r2
/ R0 = dot product (Q15)
        jsr     pc, vdot
/ Store dA[i][j]
        mov     bk_i, r1
/ R1... MUL puts result in R0:R1 for even reg
        mul     $8.*2, r1
/ Fix: i*S*2 for byte offset into DA
        mov     bk_i, r2
/ R3 = i*S*2
        mul     $16., r2
        add     bk_j, r3
/ +j*2
        add     bk_j, r3
        add     $da, r3
/ DA[i][j] = dot result
        mov     r0, (r3)

/ dV[j][k] += A[i][j] * dY[i][k] >> 8  (VSADD)
/ scalar = A[i][j]
        mov     bk_i, r2
/ R3 = i*S*2
        mul     $16., r2
        add     bk_j, r3
        add     bk_j, r3
        add     bk_ap, r3
/ R0 = A[i][j] (Q8 scalar)
        mov     (r3), r0
/ src = dY[i]
        mov     bk_i, r2
        mul     $32., r2
        add     $dy, r3
/ R1 = &dY[i]
        mov     r3, r1
/ dst = dV[j]
        mov     bk_j, r2
        mul     $32., r2
        add     $dvv, r3
/ R2 = &dV[j]
        mov     r3, r2
        mov     $16., r3
/ dV[j] += A[i][j] * dY[i]
        jsr     pc, vsadd

        inc     bk_j
        dec     bk_jc
        bne     bk_l2j
        inc     bk_i
        dec     bk_ic
        bne     bk_l2

/ ==========================================
/ Step 3: Backward softmax -> dSc (stored in DA)
/ dSc[i][j] = A[i][j] * (dA[i][j] - dot(A[i], dA[i])) >> 8
/ Then dSc >>= sqrt_shift
/ ==========================================

        mov     $8., bk_ic
        clr     bk_i
/ dot_ad = VDOT(A[i], dA[i], S)
bk_l3:
        mov     bk_i, r0
        mul     $16., r0
        add     bk_ap, r1
/ R0 = &A[i]
        mov     r1, r0
        mov     bk_i, r2
        mul     $16., r2
        add     $da, r3
/ R1 = &dA[i]
        mov     r3, r1
        mov     $8., r2
/ R0 = dot_ad (Q15)
        jsr     pc, vdot
/ save dot_ad
        mov     r0, bk_dad

/ dSc[i][j] = A[i][j] * (dA[i][j] - dot_ad) >> 8
/ Then >>= sqrt_shift
        mov     bk_i, r0
        mul     $16., r0
        add     $da, r1
/ R3 = &DA[i][0] (read/write)
        mov     r1, r3
        mov     bk_i, r0
        mul     $16., r0
        add     bk_ap, r1
/ R4 = &A[i][0]
        mov     r1, r4
        mov     $8., r5
/ dA[i][j] (Q15)
bk_sm:  mov     (r3), r0
/ dA[i][j] - dot_ad (Q15)
        sub     bk_dad, r0
/ no overflow
        bvc     bk_nv
/ result positive -> was negative overflow
        bpl     bk_np
/ clamp +32767
        mov     $077777, r0
        br      bk_nv
/ clamp -32768
bk_np:  mov     $100000, r0
/ A[i][j] * (dA-dot) Q8*Q15=Q23
bk_nv:  mul     (r4)+, r0
/ >>8 -> Q15
        ashc    $-8., r0
/ >>sqrt_shift (scaling)
        ash     $-2., r1
/ store dSc[i][j]
        mov     r1, (r3)+
        sob     r5, bk_sm

        inc     bk_i
        dec     bk_ic
        bne     bk_l3

/ ==========================================
/ Step 4: Backward Q.K^T -> dQ, dK
/ dQ[i] = VTMUL(K, dSc[i], S, D)
/ dK[j] = VTMUL(Q, dSc_col_j, S, D)
/ ==========================================

/ -- dQ: for each i, dQ[i] = K^T . dSc[i] --
        mov     $8., bk_ic
        clr     bk_i
bk_l4q: mov     bk_i, r0
        mul     $16., r0
        add     $da, r1
/ &dSc[i] (vin)
        mov     r1, bk_r2
        mov     bk_i, r0
        mul     $32., r0
        add     $dqq, r1
/ &dQ[i] (vout)
        mov     r1, bk_r3
/ K (mat)
        mov     bk_kp, bk_r1
        jsr     r5, vtmul
/ mat = K
bk_r1:  0
/ vin = dSc[i]
bk_r2:  0
/ vout = dQ[i]
bk_r3:  0
/ rows = S
bk_r4:  8.
/ cols = D
bk_r5:  16.

        inc     bk_i
        dec     bk_ic
        bne     bk_l4q

/ -- dK: for each j, extract column j of dSc, then VTMUL --
        mov     $8., bk_ic
        clr     bk_j
/ Extract column BK.J from DA (S x S) into DTMP
bk_l4k:
        mov     bk_j, r0
        asl     r0
/ R0 = &DA[0][j]
        add     $da, r0
        mov     $dtmp, r1
        mov     $8., r2
bk_gc:  mov     (r0), (r1)+
/ stride = S*2 bytes
        add     $16., r0
        sob     r2, bk_gc

/ dK[j] = VTMUL(Q, DTMP, S, D)
        mov     bk_j, r0
        mul     $32., r0
        add     $dkk, r1
/ &dK[j]
        mov     r1, bk_s3
/ Q (mat)
        mov     bk_qp, bk_s1
/ col (vin)
        mov     $dtmp, bk_s2
        jsr     r5, vtmul
bk_s1:  0
bk_s2:  0
bk_s3:  0
bk_s4:  8.
bk_s5:  16.

        inc     bk_j
        dec     bk_ic
        bne     bk_l4k

/ ==========================================
/ Step 5: Backward projections + dX
/ dX[i] = dY[i]  (residual copy)
/ For each i:
/ dX[i] += MVADD(Wq, dQ[i])
/ dWq   += OUTER(X[i], dQ[i])
/ dX[i] += MVADD(Wk, dK[i])
/ dWk   += OUTER(X[i], dK[i])
/ dX[i] += MVADD(Wv, dV[i])
/ dWv   += OUTER(X[i], dV[i])
/ ==========================================

/ Copy dY -> dX
        mov     $dy, r0
        mov     $dxx, r1
        mov     $8.*16., r2
        jsr     pc, vcpy

        mov     $8., bk_ic
        clr     bk_i
/ Compute byte offsets for position i
bk_l5:
        mov     bk_i, r0
        mul     $32., r0
/ i * d_model * 2
        mov     r1, bk_off

/ --- Q path ---
/ dX[i] += Wq * dQ[i]
        mov     $wqq8, bk_t1
        mov     bk_off, r0
        add     $dqq, r0
/ &dQ[i]
        mov     r0, bk_t2
        mov     bk_off, r0
        add     $dxx, r0
/ &dX[i]
        mov     r0, bk_t3
        jsr     r5, mvadd
bk_t1:  0
bk_t2:  0
bk_t3:  0
bk_t4:  16.
bk_t5:  16.
/ dWq += OUTER(X[i], dQ[i])
        mov     $dwq, bk_u1
        mov     bk_off, r0
        add     $xx, r0
/ &X[i]
        mov     r0, bk_u2
        mov     bk_off, r0
        add     $dqq, r0
/ &dQ[i]
        mov     r0, bk_u3
        jsr     r5, outer
bk_u1:  0
bk_u2:  0
bk_u3:  0
bk_u4:  16.
bk_u5:  16.

/ --- K path ---
        mov     $wkq8, bk_v1
        mov     bk_off, r0
        add     $dkk, r0
        mov     r0, bk_v2
        mov     bk_off, r0
        add     $dxx, r0
        mov     r0, bk_v3
        jsr     r5, mvadd
bk_v1:  0
bk_v2:  0
bk_v3:  0
bk_v4:  16.
bk_v5:  16.
        mov     $dwk, bk_w1
        mov     bk_off, r0
        add     $xx, r0
        mov     r0, bk_w2
        mov     bk_off, r0
        add     $dkk, r0
        mov     r0, bk_w3
        jsr     r5, outer
bk_w1:  0
bk_w2:  0
bk_w3:  0
bk_w4:  16.
bk_w5:  16.

/ --- V path ---
        mov     $wvq8, bk_x1
        mov     bk_off, r0
        add     $dvv, r0
        mov     r0, bk_x2
        mov     bk_off, r0
        add     $dxx, r0
        mov     r0, bk_x3
        jsr     r5, mvadd
bk_x1:  0
bk_x2:  0
bk_x3:  0
bk_x4:  16.
bk_x5:  16.
        mov     $dwv, bk_y1
        mov     bk_off, r0
        add     $xx, r0
        mov     r0, bk_y2
        mov     bk_off, r0
        add     $dvv, r0
        mov     r0, bk_y3
        jsr     r5, outer
bk_y1:  0
bk_y2:  0
bk_y3:  0
bk_y4:  16.
bk_y5:  16.

        inc     bk_i
        dec     bk_ic
        beq     bk_d5
/ too far for BNE
        jmp     bk_l5
bk_d5:

/ ==========================================
/ Step 6: Backward embedding
/ d_tok[token[i]] += dX[i]
/ d_pos[i] += dX[i]
/ ==========================================

        mov     $8., bk_ic
        clr     bk_i
bk_l6:  mov     bk_i, r0
        mul     $32., r0
        add     $dxx, r1
/ R3 = &dX[i]
        mov     r1, r3

/ d_tok[token[i]] += dX[i]
        mov     bk_i, r0
        asl     r0
        add     $tokens, r0
/ token id
        mov     (r0), r0
        mul     $32., r0
/ R1 = &d_tok[token[i]]
        add     $dtke, r1
        mov     $16., r4
/ R2 = &dX[i] (src, will advance)
        mov     r3, r2
bk_te:  mov     (r2)+, r0
        add     r0, (r1)
        bvc     bk_te_l71
        tst     r0
        bmi     bk_te_l72
        mov     $077777, (r1)
        br      bk_te_l71
bk_te_l72:    mov     $100000, (r1)
bk_te_l71:    add     $2, r1
        sob     r4, bk_te

/ d_pos[i] += dX[i]
        mov     bk_i, r0
        mul     $32., r0
/ R1 = &d_pos[i]
        add     $dpse, r1
/ R2 back to &dX[i]
        sub     $32., r2
        mov     $16., r4
bk_pe:  mov     (r2)+, r0
        add     r0, (r1)
        bvc     bk_pe_l73
        tst     r0
        bmi     bk_pe_l74
        mov     $077777, (r1)
        br      bk_pe_l73
bk_pe_l74:    mov     $100000, (r1)
bk_pe_l73:    add     $2, r1
        sob     r4, bk_pe

        inc     bk_i
        dec     bk_ic
        bne     bk_l6

        rts     pc

/ BK.SET: compute cached workspace pointers
/ ATTN stores Q|K|V|A contiguously in WORK
bk_set: mov     $work, r0
        mov     r0, bk_qp
        add     $8.*32., r0
        mov     r0, bk_kp
        add     $8.*32., r0
        mov     r0, bk_vp
        add     $8.*32., r0
        mov     r0, bk_ap
        rts     pc

/ ============================================================
/ VSADD - Vector scale-add: dst[k] += (scalar * src[k]) >> 8
/ R0 = scalar, R1 = ptr src, R2 = ptr dst, R3 = count
/ Clobbers R0-R5
/ ============================================================

        .globl  vsadd

vsadd:  mov     r4, -(sp)
        mov     r5, -(sp)
/ R4 = scalar
        mov     r0, r4
/ R5 = ptr src
        mov     r1, r5
/ R2 = ptr dst, R3 = count
/ R0 = src[k]
vs_lp:  mov     (r5)+, r0
/ R0:R1 = scalar * src[k]
        mul     r4, r0
/ >> 8, result in R1
        ashc    $-8., r0
/ Clamp R1
        tst     r0
        beq     vs_ck
        cmp     r0, $-1
        beq     vs_ck
        bpl     vs_cp
        mov     $100000, r1
        br      vs_ck
vs_cp:  mov     $077777, r1
/ dst[k] += result
vs_ck:  add     r1, (r2)
        bvc     vs_nc
        tst     r1
        bmi     vs_ng
        mov     $077777, (r2)
        br      vs_nc
vs_ng:  mov     $100000, (r2)
vs_nc:  add     $2, r2
        sob     r3, vs_lp
        mov     (sp)+, r5
        mov     (sp)+, r4
        rts     pc

/ end of BKWRD.MAC

/ ===== sample/model/UPDAT.MAC =====
/ 
/ UPDAT.MAC - Weight initialization, SGD update, gradient zeroing
/ 

/ ============================================================
/ RAND - 15-bit LCG pseudo-random number generator
/ Output: R0 = random value [0, 32767]
/ ============================================================

rand:   mov     rn_sed, r0
/ R0:R1 = seed * 25173
        mul     $25173., r0
        add     $13849., r1
/ keep 15 bits
        bic     $100000, r1
        mov     r1, rn_sed
        mov     r1, r0
        rts     pc

/ seed
rn_sed:  887.

/ ============================================================
/ INITW - Initialize weights with random Q16 values
/ ============================================================

/ tok_emb (10 x 16 = 160 words)
initw:
        mov     $tkeh, r4
        mov     $tkel, r5
        mov     $10.*16., r3
        jsr     pc, in_fil
/ pos_emb (8 x 16 = 128)
        mov     $pseh, r4
        mov     $psel, r5
        mov     $8.*16., r3
        jsr     pc, in_fil
/ Wq (16 x 16 = 256)
        mov     $wqh, r4
        mov     $wql, r5
        mov     $16.*16., r3
        jsr     pc, in_fil
/ Wk
        mov     $wkh, r4
        mov     $wkl, r5
        mov     $16.*16., r3
        jsr     pc, in_fil
/ Wv
        mov     $wvh, r4
        mov     $wvl, r5
        mov     $16.*16., r3
        jsr     pc, in_fil
/ Wout (16 x 10 = 160)
        mov     $woth, r4
        mov     $wotl, r5
        mov     $16.*10., r3
        jsr     pc, in_fil
        rts     pc

/ IN.FIL: Fill R3 words of Q16 weights at R4(hi)/R5(lo)
/ Random Q8 in [-128,127], convert to Q16
in_fil: jsr     pc, rand
/ R0 = [0, 32767]
/ keep 8 bits [0, 255]
        bic     $177400, r0
/ [-128, 127] in Q8
        sub     $128., r0
/ Convert Q8 -> Q16 (32-bit): Q16 = Q8 << 8
/ R1 = Q8 value (low word)
        mov     r0, r1
/ R0 = sign extension
        ash     $-15., r0
/ R0:R1 <<= 8 -> Q16
        ashc    $8., r0
/ store high
        mov     r0, (r4)+
/ store low
        mov     r1, (r5)+
        sob     r3, in_fil
        rts     pc

/ ============================================================
/ UPDAT - SGD weight update with Q16 accumulators
/ ============================================================

updat:
/ For each weight group: w_q16 -= grad_q15 >> (lr_shift - 1)
/ Then zero gradients

/ tok_emb: lr_shift = LR.EMB (4)
        mov     $tkeh, r2
        mov     $tkel, r3
        mov     $dtke, r4
        mov     $10.*16., r1
        mov     $4., r0
        jsr     pc, up_do
/ pos_emb
        mov     $pseh, r2
        mov     $psel, r3
        mov     $dpse, r4
        mov     $8.*16., r1
        mov     $4., r0
        jsr     pc, up_do
/ Wq: lr_shift = LR.ATN (1)
        mov     $wqh, r2
        mov     $wql, r3
        mov     $dwq, r4
        mov     $16.*16., r1
        mov     $1., r0
        jsr     pc, up_do
/ Wk
        mov     $wkh, r2
        mov     $wkl, r3
        mov     $dwk, r4
        mov     $16.*16., r1
        mov     $1., r0
        jsr     pc, up_do
/ Wv
        mov     $wvh, r2
        mov     $wvl, r3
        mov     $dwv, r4
        mov     $16.*16., r1
        mov     $1., r0
        jsr     pc, up_do
/ Wout: lr_shift = LR.OUT (6)
        mov     $woth, r2
        mov     $wotl, r3
        mov     $dwout, r4
        mov     $16.*10., r1
        mov     $6., r0
        jsr     pc, up_do

/ Gradients already zeroed by UP.DO (CLR after read)
        rts     pc

/ UP.DO: Update R1 weights. R2=hi, R3=lo, R4=grad, R0=lr_shift
/ w_q16 -= grad_q15 >> (shift-1)
/ Uses ADD of negated delta to avoid PDP-11 SUB carry issues
up_do:  mov     r5, -(sp)
        mov     r0, up_shf
/ shift-1
        dec     up_shf
/ negate for ASH (right shift)
        neg     up_shf
/ R5 = counter
        mov     r1, r5
/ R0 = grad (Q15)
up_dlp: mov     (r4), r0
/ zero grad after reading
        clr     (r4)+
/ R0 = delta = grad >> (shift-1)
        ash     up_shf, r0
/ R0 = -delta
        neg     r0
/ Add sign-extended -delta to whi:wlo
/ lo: ADD -delta to wlo
        add     r0, (r3)
/ hi: add carry + sign extension
/ whi += carry from low add
        adc     (r2)
/ test -delta
        tst     r0
/ >= 0: sign ext is 0, done
        bpl     up_nx
/ < 0: sign ext is -1, add -1 to hi
        dec     (r2)
/ advance hi ptr
up_nx:  add     $2, r2
/ advance lo ptr
        add     $2, r3
        sob     r5, up_dlp
        mov     (sp)+, r5
        rts     pc

up_shf:  0

/ ============================================================
/ ZEROG - Zero all gradient arrays
/ ============================================================

zerog:  mov     $dtke, r0
        mov     $10.*16., r1
        jsr     pc, vclr
        mov     $dpse, r0
        mov     $8.*16., r1
        jsr     pc, vclr
        mov     $dwq, r0
        mov     $16.*16., r1
        jsr     pc, vclr
        mov     $dwk, r0
        mov     $16.*16., r1
        jsr     pc, vclr
        mov     $dwv, r0
        mov     $16.*16., r1
        jsr     pc, vclr
        mov     $dwout, r0
        mov     $16.*10., r1
        jsr     pc, vclr
        rts     pc

/ end of UPDAT.MAC

/ ===== sample/TRAIN.MAC =====
/ 
/ TRAIN.MAC - Train a 1-layer, 1-head transformer to reverse
/ an 8-digit sequence. "Paper Tape is All You Need."
/ 
/ Q8 forward, Q15 backward, Q16 weight accumulators.
/ PDP-11/34 bare metal with EIS.
/ 
/ Memory map:
/ 001000  Code start
/ ~010000 Data (weights, workspace, gradients)
/ ~034000 Stack top
/ 

/ === Constants ===

/ Size constants (words)

/ Learning rate shifts: (emb=4, attn=1, out=6)


/ --- Jump over subroutines ---
        jmp     main

/ --- Include libraries ---

/ --- Include model modules ---

/ ============================================================
/ MAIN - Training entry point
/ ============================================================

main:
	/ use the BSD process stack; do not reset sp into static data

/ Banner
        mov     $s_bnr, r0
        jsr     pc, puts
        jsr     pc, newln
        mov     $s_bn2, r0
        jsr     pc, puts
        jsr     pc, newln
        jsr     pc, newln
        mov     $s_trn, r0
        jsr     pc, puts
        jsr     pc, newln

/ Initialize weights
        jsr     pc, initw

/ Training loop
        mov     $1, tr_stp
        clr     tr_hit
        clr     tr_tot

/ --- One training step ---
tr_lp:
/ generate sample -> TOKENS, TARGET
        jsr     pc, gensm
/ convert Q16 weights -> Q8
        jsr     pc, cvt16
/ forward pass
        jsr     pc, forwrd
/ backward pass
        jsr     pc, bkwrd
/ weight update
        jsr     pc, updat

/ Count correct predictions
        jsr     pc, count

/ Report every RPRT steps
        clr     r0
        mov     tr_stp, r1
/ R0=quot, R1 = step % RPRT
        div     $50., r0
        tst     r1
        bne     tr_nr
        jsr     pc, report
tr_nr:  inc     tr_stp
        cmp     tr_stp, $350.+1
        blo     tr_lp

/ Final test
        jsr     pc, newln
        mov     $s_tst, r0
        jsr     pc, puts
        jsr     pc, newln
        jsr     pc, test
	clr	-(sp)
	jsr	pc,_exit

/ Training state
/ current step
tr_stp:  0
/ correct predictions
tr_hit:  0
/ total predictions
tr_tot:  0

/ ============================================================
/ GENSM - Generate random sample (digit reversal)
/ ============================================================

gensm:  mov     $tokens, r2
        mov     $8., r3
gs_lp:  jsr     pc, rand
/ R0 = random 15-bit value
/ digit = R0 mod 10
        mov     r0, r1
        clr     r0
/ R1 = remainder
        div     $10., r0
        mov     r1, (r2)+
        sob     r3, gs_lp
/ Reverse into TARGET
/ source index (last)
        mov     $8.-1, r3
        mov     $target, r2
        mov     $8., r4
gs_rv:  mov     r3, r0
/ byte offset
        asl     r0
        add     $tokens, r0
        mov     (r0), (r2)+
        dec     r3
        sob     r4, gs_rv
        rts     pc

/ ============================================================
/ COUNT - Count correct predictions from logits
/ ============================================================

count:  mov     $logits, cn_lp
        mov     $target, cn_tp
        mov     $8., cn_cnt
cn_lop: mov     cn_lp, r0
        mov     $10., r1
/ R0=max, R1=argmax
        jsr     pc, vmax
        mov     cn_tp, r2
/ compare with target
        cmp     r1, (r2)
        bne     cn_no
        inc     tr_hit
cn_no:  inc     tr_tot
/ next logits row
        add     $20., cn_lp
/ next target
        add     $2, cn_tp
        dec     cn_cnt
        bne     cn_lop
        rts     pc
cn_lp:  0
cn_tp:  0
cn_cnt:  0

/ ============================================================
/ CLOSS - Compute cross-entropy loss for current sample
/ Returns R0 = average per-position loss (Q8)
/ Uses DL as temp buffer
/ ============================================================

closs:  clr     cl_smh
        clr     cl_sml
        mov     $8., cl_cnt
        clr     cl_i
/ Copy logits[i] to DL
cl_lp:
        mov     cl_i, r0
        mul     $20., r0
        add     $logits, r1
/ R2 = &logits[i]
        mov     r1, r2
        mov     $dl, r3
        mov     $10., r4
cl_cp:  mov     (r2)+, (r3)+
        sob     r4, cl_cp
/ Softmax(DL) in-place
        mov     $dl, r0
        mov     $10., r1
        jsr     pc, sftmx
/ Look up -ln(softmax[target[i]])
        mov     cl_i, r0
        asl     r0
        add     $target, r0
/ target digit
        mov     (r0), r0
        asl     r0
        add     $dl, r0
/ softmax value [0,256] Q8
        mov     (r0), r0
        cmp     r0, $256.
/ p=1.0 -> loss=0
        bhis    cl_zr
/ word offset
        asl     r0
        add     $logtbl, r0
/ -ln(p) in Q12
        mov     (r0), r0
/ 32-bit accumulate
        add     r0, cl_sml
        adc     cl_smh
cl_zr:  inc     cl_i
        dec     cl_cnt
        bne     cl_lp
/ Average: 32-bit sum / 8 (ASHC #-3)
        mov     cl_smh, r0
        mov     cl_sml, r1
/ R0:R1 >>= 3
        ashc    $-3., r0
/ result fits 16 bits (Q12)
        mov     r1, r0
        rts     pc

cl_smh:  0
cl_sml:  0
cl_i:  0
cl_cnt:  0

/ ============================================================
/ PUTLSS - Print R0 as Q12 fixed-point "i.dddd"
/ Input: R0 = Q12 value (positive)
/ Clobbers: R0-R3
/ ============================================================

/ save
putlss: mov     r0, r2
/ integer part
        ash     $-12., r0
        jsr     pc, putdec
        mov     $'., r0
        jsr     pc, putc
/ fraction: (low 12 bits) * 10000 / 4096
        mov     r2, r0
/ keep low 12 bits
        bic     $170000, r0
/ R0:R1 = frac * 10000
        mul     $10000., r0
/ / 4096, result in R1
        ashc    $-12., r0
/ R2 = decimal (0-9999)
        mov     r1, r2
/ digit 1 (thousands)
        clr     r0
        mov     r2, r1
        div     $1000., r0
        mov     r1, r2
        add     $'0, r0
        jsr     pc, putc
/ digit 2 (hundreds)
        clr     r0
        mov     r2, r1
        div     $100., r0
        mov     r1, r2
        add     $'0, r0
        jsr     pc, putc
/ digit 3 (tens)
        clr     r0
        mov     r2, r1
        div     $10., r0
        mov     r1, r2
        add     $'0, r0
        jsr     pc, putc
/ digit 4 (ones)
        mov     r2, r0
        add     $'0, r0
        jsr     pc, putc
        rts     pc

/ ============================================================
/ REPORT - Print step, loss, accuracy
/ ============================================================

report: mov     $s_stp, r0
        jsr     pc, puts
/ Print step with padding to 4 digits
        mov     tr_stp, r0
        cmp     r0, $1000.
        bhis    rp_np
        jsr     pc, putspc
        cmp     r0, $100.
        bhis    rp_np
        jsr     pc, putspc
rp_np:  mov     tr_stp, r0
        jsr     pc, putdec
/ Loss
        mov     $s_lss, r0
        jsr     pc, puts
/ R0 = loss Q12
        jsr     pc, closs
        jsr     pc, putlss
        mov     $s_acc, r0
        jsr     pc, puts
/ acc = hit * 1000 / tot  -> print as 0.xxx
        mov     tr_hit, r0
/ R0:R1 = hit*1000
        mul     $1000., r0
/ R0 = permille (0-1000)
        div     tr_tot, r0
/ save permille
        mov     r0, r2
/ Print "0." or "1."
        cmp     r2, $1000.
        blo     rp_z
        mov     $'1, r0
        jsr     pc, putc
        mov     $'., r0
        jsr     pc, putc
        mov     $s_zzz, r0
        jsr     pc, puts
        br      rp_dn
rp_z:   mov     $'0, r0
        jsr     pc, putc
        mov     $'., r0
        jsr     pc, putc
/ Print 3 digits with leading zeros
        mov     r2, r1
        clr     r0
/ R0=hundreds, R1=rest
        div     $100., r0
        mov     r1, r2
        add     $'0, r0
        jsr     pc, putc
        mov     r2, r1
        clr     r0
        div     $10., r0
        mov     r1, r2
        add     $'0, r0
        jsr     pc, putc
        mov     r2, r0
        add     $'0, r0
        jsr     pc, putc
rp_dn:  jsr     pc, newln
/ Reset counters
        clr     tr_hit
        clr     tr_tot
        rts     pc

/ ============================================================
/ TEST - Final test: 20 samples
/ ============================================================

test:   clr     te_sok
        mov     $10., te_cnt
te_lp:  jsr     pc, gensm
        jsr     pc, cvt16
        jsr     pc, forwrd
/ Store predictions in TE.PRD
        mov     $logits, te_lg
        mov     $te_prd, te_pp
        mov     $8., te_pos
te_gp:  mov     te_lg, r0
        mov     $10., r1
/ R1 = argmax
        jsr     pc, vmax
        mov     te_pp, r2
        mov     r1, (r2)+
        mov     r2, te_pp
        add     $20., te_lg
        dec     te_pos
        bne     te_gp
/ Print: " i i i i i i i i -> p p p p p p p p  OK/FAIL"
/ Print input tokens
        jsr     pc, putspc
        mov     $tokens, r3
        mov     $8., r4
te_pt:  mov     (r3)+, r0
        add     $'0, r0
        jsr     pc, putc
        jsr     pc, putspc
        sob     r4, te_pt
        mov     $s_dsh, r0
        jsr     pc, puts
/ Print predictions
        mov     $te_prd, r3
        mov     $8., r4
te_pp2: mov     (r3)+, r0
        add     $'0, r0
        jsr     pc, putc
        jsr     pc, putspc
        sob     r4, te_pp2
/ Check if all correct
        mov     $1, te_all
        mov     $target, r3
        mov     $te_prd, r2
        mov     $8., r4
te_cmp: cmp     (r3)+, (r2)+
        beq     te_eq
        clr     te_all
te_eq:  sob     r4, te_cmp
        tst     te_all
        beq     te_fl
        inc     te_sok
        mov     $s_ok, r0
        jsr     pc, puts
        br      te_nx
te_fl:  mov     $s_fail, r0
        jsr     pc, puts
te_nx:  jsr     pc, newln
        dec     te_cnt
        bne     te_lp
/ Print score
        jsr     pc, newln
        mov     $s_scr, r0
        jsr     pc, puts
        mov     te_sok, r0
        cmp     r0, $10.
        bhis    te_np
        jsr     pc, putspc
te_np:  jsr     pc, putdec
        mov     $s_of, r0
        jsr     pc, puts
        mov     $10., r0
        jsr     pc, putdec
        jsr     pc, newln
        rts     pc

te_sok:  0
te_cnt:  0
te_pos:  0
te_all:  0
te_lg:  0
te_pp:  0
te_prd:
/ predicted digits
.=.+16.

/ ============================================================
/ Strings
/ ============================================================

        .even
s_bnr: .byte 97.
        .byte 116.
        .byte 116.
        .byte 110.
        .byte 47.
        .byte 49.
        .byte 49.
        .byte 32.
        .byte 45.
        .byte 32.
        .byte 112.
        .byte 97.
        .byte 112.
        .byte 101.
        .byte 114.
        .byte 32.
        .byte 116.
        .byte 97.
        .byte 112.
        .byte 101.
        .byte 32.
        .byte 105.
        .byte 115.
        .byte 32.
        .byte 97.
        .byte 108.
        .byte 108.
        .byte 32.
        .byte 121.
        .byte 111.
        .byte 117.
        .byte 32.
        .byte 110.
        .byte 101.
        .byte 101.
        .byte 100.
        .byte 0
s_bn2: .byte 100.
        .byte 61.
        .byte 49.
        .byte 54.
        .byte 32.
        .byte 115.
        .byte 101.
        .byte 113.
        .byte 61.
        .byte 56.
        .byte 32.
        .byte 118.
        .byte 61.
        .byte 49.
        .byte 48.
        .byte 32.
        .byte 112.
        .byte 97.
        .byte 114.
        .byte 97.
        .byte 109.
        .byte 115.
        .byte 61.
        .byte 49.
        .byte 50.
        .byte 49.
        .byte 54.
        .byte 32.
        .byte 113.
        .byte 56.
        .byte 47.
        .byte 113.
        .byte 49.
        .byte 53.
        .byte 47.
        .byte 113.
        .byte 49.
        .byte 54.
        .byte 0
s_trn: .byte 116.
        .byte 114.
        .byte 97.
        .byte 105.
        .byte 110.
        .byte 105.
        .byte 110.
        .byte 103.
        .byte 46.
        .byte 46.
        .byte 46.
        .byte 0
        .even
s_stp: .byte 32.
        .byte 115.
        .byte 116.
        .byte 101.
        .byte 112.
        .byte 32.
        .byte 0
s_lss: .byte 32.
        .byte 108.
        .byte 111.
        .byte 115.
        .byte 115.
        .byte 61.
        .byte 0
s_acc: .byte 32.
        .byte 97.
        .byte 99.
        .byte 99.
        .byte 61.
        .byte 0
s_zzz: .byte 48.
        .byte 48.
        .byte 48.
        .byte 0
s_tst: .byte 0
        .even
s_dsh: .byte 45.
        .byte 62.
        .byte 32.
        .byte 0
s_ok: .byte 32.
        .byte 111.
        .byte 107.
        .byte 0
s_fail: .byte 32.
        .byte 102.
        .byte 97.
        .byte 105.
        .byte 108.
        .byte 0
        .even
s_scr: .byte 32.
        .byte 97.
        .byte 99.
        .byte 99.
        .byte 117.
        .byte 114.
        .byte 97.
        .byte 99.
        .byte 121.
        .byte 32.
        .byte 32.
        .byte 0
s_of: .byte 47.
        .byte 0
        .even

/ ============================================================
/ DATA SECTION
/ ============================================================

/ --- Log table: LOGTBL[x] = -ln(x/256)*4096 Q12, x=0..256 ---
logtbl:  22713.
         22713.
         19874.
         18213.
         17035.
         16121.
         15374.
         14743.
         14196.
                 13713.
                 13282.
                 12891.
                 12535.
                 12207.
                 11903.
                 11621.
         11357.
                 11108.
                 10874.
                 10653.
                 10443.
                 10243.
                 10052.
                 9870.
         9696.
                 9529.
                 9368.
                 9213.
                 9064.
                 8921.
                 8782.
                 8647.
         8517.
                 8391.
                 8269.
                 8150.
                 8035.
                 7923.
                 7813.
                 7707.
         7603.
                 7502.
                 7404.
                 7307.
                 7213.
                 7121.
                 7031.
                 6943.
         6857.
                 6772.
                 6689.
                 6608.
                 6529.
                 6451.
                 6374.
                 6299.
         6225.
                 6153.
                 6081.
                 6011.
                 5943.
                 5875.
                 5808.
                 5743.
         5678.
                 5615.
                 5552.
                 5491.
                 5430.
                 5370.
                 5311.
                 5253.
         5196.
                 5139.
                 5084.
                 5029.
                 4974.
                 4921.
                 4868.
                 4816.
         4764.
                 4713.
                 4663.
                 4613.
                 4564.
                 4516.
                 4468.
                 4421.
         4374.
                 4328.
                 4282.
                 4237.
                 4192.
                 4148.
                 4104.
                 4060.
         4017.
                 3975.
                 3933.
                 3891.
                 3850.
                 3810.
                 3769.
                 3729.
         3690.
                 3650.
                 3612.
                 3573.
                 3535.
                 3497.
                 3460.
                 3423.
         3386.
                 3350.
                 3314.
                 3278.
                 3242.
                 3207.
                 3172.
                 3138.
         3103.
                 3069.
                 3036.
                 3002.
                 2969.
                 2936.
                 2904.
                 2871.
         2839.
                 2807.
                 2776.
                 2744.
                 2713.
                 2682.
                 2651.
                 2621.
         2591.
                 2561.
                 2531.
                 2501.
                 2472.
                 2443.
                 2414.
                 2385.
         2357.
                 2328.
                 2300.
                 2272.
                 2244.
                 2217.
                 2189.
                 2162.
         2135.
                 2108.
                 2082.
                 2055.
                 2029.
                 2003.
                 1977.
                 1951.
         1925.
                 1900.
                 1874.
                 1849.
                 1824.
                 1799.
                 1774.
                 1750.
         1725.
                 1701.
                 1677.
                 1653.
                 1629.
                 1605.
                 1582.
                 1558.
         1535.
                 1512.
                 1488.
                 1466.
                 1443.
                 1420.
                 1397.
                 1375.
         1353.
                 1330.
                 1308.
                 1286.
                 1265.
                 1243.
                 1221.
                 1200.
         1178.
                 1157.
                 1136.
                 1115.
                 1094.
                 1073.
                 1052.
                 1032.
         1011.
                 991.
                 970.
                 950.
                 930.
                 910.
                 890.
                 870.
         850.
                 831.
                 811.
                 792.
                 772.
                 753.
                 734.
                 715.
         696.
                 677.
                 658.
                 639.
                 621.
                 602.
                 584.
                 565.
         547.
                 529.
                 511.
                 492.
                 474.
                 457.
                 439.
                 421.
         403.
                 386.
                 368.
                 351.
                 333.
                 316.
                 299.
                 281.
         264.
                 247.
                 230.
                 213.
                 197.
                 180.
                 163.
                 147.
         130.
                 114.
                 97.
                 81.
                 65.
                 48.
                 32.
                 16.
         0.

/ --- Training data ---
tokens:
/ input tokens
.=.+16.
target:
/ target (reversed)
.=.+16.

/ --- Q16 weight accumulators (32-bit per weight) ---
/ High words
tkeh:
/ tok_emb hi (160)
.=.+320.
pseh:
/ pos_emb hi (128)
.=.+256.
wqh:
/ Wq hi (256)
.=.+512.
wkh:
/ Wk hi
.=.+512.
wvh:
/ Wv hi
.=.+512.
woth:
/ Wout hi (160)
.=.+320.
/ Low words
tkel:
.=.+320.
psel:
.=.+256.
wql:
.=.+512.
wkl:
.=.+512.
wvl:
.=.+512.
wotl:
.=.+320.

/ --- Q8 weight copies (for forward/backward) ---
tkeq8:
.=.+320.
pseq8:
.=.+256.
wqq8:
.=.+512.
wkq8:
.=.+512.
wvq8:
.=.+512.
wotq8:
.=.+320.

/ --- Gradient accumulators (Q15, 16-bit) ---
dtke:
.=.+320.
dpse:
.=.+256.
dwq:
.=.+512.
dwk:
.=.+512.
dwv:
.=.+512.
dwout:
.=.+320.

/ --- Forward cache ---
xx:
/ embeddings (input to attention)
.=.+256.
yy:
/ attention output (Y = O + X)
.=.+256.
logits:
/ output logits
.=.+160.
work:
/ ATTN workspace: Q|K|V|S
.=.+896.

/ --- Backward workspace ---
dl:
/ dLogits (one position, Q15)
.=.+20.
dy:
/ dY (Q15)
.=.+256.
da:
/ dA (Q15), reused as dSc
.=.+128.
dqq:
/ dQ (Q15)
.=.+256.
dkk:
/ dK (Q15)
.=.+256.
dvv:
/ dV (Q15)
.=.+256.
dxx:
/ dX (Q15)
.=.+256.
dtmp:
/ temp vector (d_model)
.=.+32.

/ --- Stack ---
        / stack space
        .=.+512.
/ stack grows downward


