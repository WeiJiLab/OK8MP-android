// Auto-generated file. Do not edit!
//   Template: src/f32-igemm/4x8-aarch32-neon-cortex-a75.S.in
//   Generator: tools/xngen
//
// Copyright 2019 Google LLC
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include <xnnpack/assembly.h>

.syntax unified

// void xnn_f32_igemm_ukernel_4x8__aarch32_neon_pld_cortex_a75(
//     size_t mr,                            r0
//     size_t nc,                            r1
//     size_t kc,                            r2 -> r5 -> sp + 64
//     size_t ks,                            r3 -> sp + 68 -> r14
//     const float**restrict a,  sp + 108 -> r2
//     const void*restrict w,    sp + 112 -> r9
//     uint8_t*restrict c,       sp + 116 -> r11
//     size_t cm_stride,         sp + 120 -> (r6)
//     size_t cn_stride,         sp + 124 -> (r7)
//     size_t a_offset,          sp + 128 -> (r5)
//     const float* zero,        sp + 132 -> (r7)
//     output_params*params,     sp + 136 -> (r5)

// inner loop registers

// A0   r3  d0
// A1  r12  d1
// A2  r10  d2
// A3   r0  d3

// B    r9  d8,  d9, d10, d11
// B       d12, d13, d14, d15

// C0  r11 d16-d17  q8  d18-d19  q9
// C1   r4 d20-d21 q10  d22-d23 q11
// C2   r8 d24-d25 q12  d26-d27 q13
// C3   r6 d28-d29 q14  d30-d31 q15

// Clamp (r5) d4 d5 d6 d7

BEGIN_FUNCTION xnn_f32_igemm_ukernel_4x8__aarch32_neon_pld_cortex_a75
        .arm
#ifndef __APPLE__
        .arch armv7-a
        .fpu neon
#endif
        // Push 108 bytes
        // r2 will be reloaded in outer loop.  r3 is ks
        PUSH   {r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r14}  // +44
        VPUSH  {d8-d15}                                         // +64 = 108

        LDR         r2, [sp, 108]    // a
        LDR         r9, [sp, 112]    // w
        LDR        r11, [sp, 116]    // c
        LDR         r6, [sp, 120]    // cm_stride
        LDR        r14, [sp, 68]     // p = ks

        // Clamp C pointers
        CMP    r0, 2                 // if mr >= 2
        ADD    r4, r11, r6           //   c1 = c0 + cm_stride
        MOVLO  r4, r11               // c1
                                     // if mr > 2
        ADD    r8, r4, r6            //   c2 = c1 + cm_stride
        MOVLS  r8, r4                // c2
        CMP    r0, 4                 // if mr >=4
        ADD    r6, r8, r6            //   c3 = c2 + cm_stride
        MOVLO  r6, r8                // c3

        .p2align 3
0:
        # Load initial bias from w into accumulators
        VLDM       r9!, {d16-d19}   // Bias
        VMOV       q10, q8
        VMOV       q11, q9
        VMOV       q12, q8
        VMOV       q13, q9
        VMOV       q14, q8
        VMOV       q15, q9

        PLD         [r9,   0]  // Prefetch B
        PLD         [r9,  64]
        PLD         [r9, 128]
        PLD         [r9, 192]
        PLD         [r9, 256]
        PLD         [r9, 320]

1:
        # Load next 4 A pointers
        LDR         r3, [r2,  0]
        LDR        r12, [r2,  4]
        LDR        r10, [r2,  8]
        LDR         r0, [r2, 12]
        ADD         r2, r2, 16

        // Add a_offset
        LDR         r5, [sp, 128]    // a_offset
        LDR         r7, [sp, 132]    // zero
        CMP         r3,  r7          // if a0 == zero
        ADD         r3,  r3, r5      // a0 += a_offset
        MOVEQ       r3,  r7          //   a0 = zero, else += a0 + a_offset
        CMP        r12,  r7          // if a1 == zero
        ADD        r12, r12, r5      // a1 += a_offset
        MOVEQ      r12,  r7          //   a1 = zero, else += a1 + a_offset
        CMP        r10,  r7          // if a2 == zero
        ADD        r10, r10, r5      // a2 += a_offset
        MOVEQ      r10,  r7          //   a2 = zero, else += a2 + a_offset
        CMP         r0,  r7          // if a3 == zero
        ADD         r0,  r0, r5      // a3 += a_offset
        LDR         r5, [sp, 64]     // kc
        MOVEQ       r0,  r7          //   a3 = zero, else += a3 + a_offset

        PLD         [r3,  0]    // Prefetch A
        PLD         [r3, 64]
        PLD        [r12,  0]
        PLD        [r12, 64]
        PLD        [r10,  0]
        PLD        [r10, 64]
        PLD         [r0,  0]
        PLD         [r0, 64]

        SUBS        r5, r5, 16       // kc - 16
        BLO         4f               // less than 4 channels?

        // Prologue
        VLD1.32    {d0}, [r3]!       // A0
        VLDM        r9!, {d8-d11}    // B0
        VLD1.32    {d1}, [r12]!      // A1
        VLD1.32    {d2}, [r10]!      // A2
        VLD1.32    {d3}, [ r0]!      // A3

        SUBS        r5, r5, 16
        BLO         3f               // less than 4 channels?  skip main loop

        .p2align 3

        // Main loop - 4 floats of A (16 bytes)
2:
        VMLA.F32     q8, q4, d0[0]
        VLDM        r9!, {d12-d15}   // B1
        VMLA.F32    q10, q4, d1[0]
        VMLA.F32    q12, q4, d2[0]
        VLD1.32    {d4}, [r3]!       // A0
        VMLA.F32    q14, q4, d3[0]
        VMLA.F32     q9, q5, d0[0]
        VLD1.32    {d5}, [r12]!      // A1
        VMLA.F32    q11, q5, d1[0]
        VMLA.F32    q13, q5, d2[0]
        VMLA.F32    q15, q5, d3[0]
        VLD1.32    {d6}, [r10]!      // A2
        VMLA.F32     q8, q6, d0[1]
        VMLA.F32    q10, q6, d1[1]
        VLD1.32    {d7}, [ r0]!      // A3
        VMLA.F32    q12, q6, d2[1]
        VMLA.F32    q14, q6, d3[1]
        VLDM        r9!, {d8-d11}    // B0
        VMLA.F32     q9, q7, d0[1]
        VMLA.F32    q11, q7, d1[1]
        VMLA.F32    q13, q7, d2[1]
        VMLA.F32    q15, q7, d3[1]

        VMLA.F32     q8, q4, d4[0]
        VLDM        r9!, {d12-d15}   // B1
        VMLA.F32    q10, q4, d5[0]
        PLD         [r3, 128]        // Prefetch A0
        VMLA.F32    q12, q4, d6[0]
        VLD1.32    {d0}, [r3]!       // A0
        VMLA.F32    q14, q4, d7[0]
        PLD         [r12, 128]       // Prefetch A1
        VMLA.F32     q9, q5, d4[0]
        VLD1.32    {d1}, [r12]!      // A1
        VMLA.F32    q11, q5, d5[0]
        PLD         [r10, 128]       // Prefetch A2
        VMLA.F32    q13, q5, d6[0]
        VLD1.32    {d2}, [r10]!      // A2
        VMLA.F32    q15, q5, d7[0]
        PLD         [r0, 128]        // Prefetch A3
        VMLA.F32     q8, q6, d4[1]
        VLD1.32    {d3}, [ r0]!      // A3
        VMLA.F32    q10, q6, d5[1]
        PLD         [r9, 384]        // Prefetch B
        VMLA.F32    q12, q6, d6[1]
        PLD         [r9, 448]        // Prefetch B
        VMLA.F32    q14, q6, d7[1]
        VLDM        r9!, {d8-d11}    // B0
        VMLA.F32     q9, q7, d4[1]
        VMLA.F32    q11, q7, d5[1]
        SUBS         r5, r5, 16
        VMLA.F32    q13, q7, d6[1]
        VMLA.F32    q15, q7, d7[1]
        BHS         2b

        // Epilogue
3:
        VMLA.F32     q8, q4, d0[0]
        VLDM        r9!, {d12-d15}   // B1
        VMLA.F32    q10, q4, d1[0]
        VMLA.F32    q12, q4, d2[0]
        VLD1.32    {d4}, [r3]!       // A0
        VMLA.F32    q14, q4, d3[0]
        VMLA.F32     q9, q5, d0[0]
        VLD1.32    {d5}, [r12]!      // A1
        VMLA.F32    q11, q5, d1[0]
        VMLA.F32    q13, q5, d2[0]
        VMLA.F32    q15, q5, d3[0]
        VLD1.32    {d6}, [r10]!      // A2
        VMLA.F32     q8, q6, d0[1]
        VMLA.F32    q10, q6, d1[1]
        VLD1.32    {d7}, [ r0]!      // A3
        VMLA.F32    q12, q6, d2[1]
        VMLA.F32    q14, q6, d3[1]
        VLDM        r9!, {d8-d11}    // B0
        VMLA.F32     q9, q7, d0[1]
        VMLA.F32    q11, q7, d1[1]
        VMLA.F32    q13, q7, d2[1]
        VMLA.F32    q15, q7, d3[1]

        VMLA.F32     q8, q4, d4[0]
        VLDM        r9!, {d12-d15}   // B1
        VMLA.F32    q10, q4, d5[0]
        VMLA.F32    q12, q4, d6[0]
        VMLA.F32    q14, q4, d7[0]
        VMLA.F32     q9, q5, d4[0]
        VMLA.F32    q11, q5, d5[0]
        VMLA.F32    q13, q5, d6[0]
        VMLA.F32    q15, q5, d7[0]
        VMLA.F32     q8, q6, d4[1]
        VMLA.F32    q10, q6, d5[1]
        VMLA.F32    q12, q6, d6[1]
        VMLA.F32    q14, q6, d7[1]
        VMLA.F32     q9, q7, d4[1]
        VMLA.F32    q11, q7, d5[1]
        VMLA.F32    q13, q7, d6[1]
        VMLA.F32    q15, q7, d7[1]

4:
        // Is there a remainder?- 1 to 3 floats of A (4, 8 or 12 bytes)
        TST         r5, 12
        BNE         7f

        .p2align 3
5:
        # ks loop
        SUBS r14, r14, 16  // ks -= MR * sizeof(void*)
        BNE 1b

        // Load params pointer
        LDR          r5, [sp, 136]   // clamping_params
        LDR          r7, [sp, 124]   // cn_stride
        LDR         r14, [sp, 68]    // p = ks

        // Load clamping_params values
        VLD1.32     {d4[],d5[]}, [r5]!
        SUBS        r1, r1, 8
        VLD1.32     {d6[],d7[]}, [r5]

        // Clamp
        VMIN.F32     q8,  q8, q2
        VMIN.F32     q9,  q9, q2
        VMIN.F32    q10, q10, q2
        VMIN.F32    q11, q11, q2
        VMIN.F32    q12, q12, q2
        VMIN.F32    q13, q13, q2
        VMIN.F32    q14, q14, q2
        VMIN.F32    q15, q15, q2
        VMAX.F32     q8,  q8, q3
        VMAX.F32     q9,  q9, q3
        VMAX.F32    q10, q10, q3
        VMAX.F32    q11, q11, q3
        VMAX.F32    q12, q12, q3
        VMAX.F32    q13, q13, q3
        VMAX.F32    q14, q14, q3
        VMAX.F32    q15, q15, q3

        // Store full 4 x 8
        BLO         10f
        VST1.32     {d28-d31}, [r6], r7
        VST1.32     {d24-d27}, [r8], r7
        VST1.32     {d20-d23}, [r4], r7
        VST1.32     {d16-d19}, [r11], r7
        SUB         r2, r2, r14    // a -= ks
        BHI         0b

6:
        VPOP        {d8-d15}
        ADD         sp, sp, 8  // skip r2, r3
        POP         {r4, r5, r6, r7, r8, r9, r10, r11, pc}

        .p2align 3
7:
        // Is there a remainder?- 2 floats of A (8 bytes)
        TST         r5, 8
        BEQ         8f

        // Remainder - 2 floats of A (8 bytes)
        VLD1.32    {d0}, [r3]!       // A0
        VLDM        r9!, {d8-d11}    // B0
        VLD1.32    {d1}, [r12]!      // A1
        VLD1.32    {d2}, [r10]!      // A2
        VLD1.32    {d3}, [ r0]!      // A3

        VMLA.F32     q8, q4, d0[0]
        VMLA.F32     q9, q5, d0[0]
        VMLA.F32    q10, q4, d1[0]
        VMLA.F32    q11, q5, d1[0]
        VLDM        r9!, {d12-d15}   // B1
        VMLA.F32    q12, q4, d2[0]
        VMLA.F32    q13, q5, d2[0]
        VMLA.F32    q14, q4, d3[0]
        VMLA.F32    q15, q5, d3[0]
        VMLA.F32     q8, q6, d0[1]
        VMLA.F32     q9, q7, d0[1]
        VMLA.F32    q10, q6, d1[1]
        VMLA.F32    q11, q7, d1[1]
        VMLA.F32    q12, q6, d2[1]
        VMLA.F32    q13, q7, d2[1]
        VMLA.F32    q14, q6, d3[1]
        VMLA.F32    q15, q7, d3[1]
8:
        // Is there a remainder?- 1 floats of A (4 bytes)
        TST         r5, 4
        BEQ         5b

9:
        // Remainder- 1 floats of A (4 bytes)
        VLDM        r3!, {s0}       // A0
        VLDM        r9!, {d8-d11}   // B0
        VLDM       r12!, {s2}       // A1
        VLDM       r10!, {s4}       // A2
        VLDM        r0!, {s6}       // A3
        VMLA.F32     q8, q4, d0[0]
        VMLA.F32     q9, q5, d0[0]
        VMLA.F32    q10, q4, d1[0]
        VMLA.F32    q11, q5, d1[0]
        VMLA.F32    q12, q4, d2[0]
        VMLA.F32    q13, q5, d2[0]
        VMLA.F32    q14, q4, d3[0]
        VMLA.F32    q15, q5, d3[0]
        B           5b

        // Store odd width
10:
        TST         r1, 4
        BEQ         11f
        VST1.32    {d28-d29},  [r6]!
        VMOV        q14, q15
        VST1.32    {d24-d25},  [r8]!
        VMOV        q12, q13
        VST1.32    {d20-d21},  [r4]!
        VMOV        q10, q11
        VST1.32    {d16-d17}, [r11]!
        VMOV         q8,  q9

11:
        TST        r1, 2
        BEQ        12f
        VST1.32    {d28},  [r6]!
        VMOV        d28, d29
        VST1.32    {d24},  [r8]!
        VMOV        d24, d25
        VST1.32    {d20},  [r4]!
        VMOV        d20, d21
        VST1.32    {d16}, [r11]!
        VMOV        d16, d17

12:
        TST         r1, 1
        BEQ         13f
        VST1.32    {d28[0]},  [r6]!
        VST1.32    {d24[0]},  [r8]!
        VST1.32    {d20[0]},  [r4]!
        VST1.32    {d16[0]}, [r11]!

13:
        VPOP        {d8-d15}
        ADD         sp, sp, 8  // skip r2, r3
        POP         {r4, r5, r6, r7, r8, r9, r10, r11, pc}

END_FUNCTION xnn_f32_igemm_ukernel_4x8__aarch32_neon_pld_cortex_a75

#ifdef __ELF__
.section ".note.GNU-stack","",%progbits
#endif



