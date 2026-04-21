/*
 * WiiR600ISA.h — minimal R600 / R700 shader ISA encoder for userland.
 *
 * The R600 ISA is a 64-bit word machine with three instruction classes:
 *   - CF (Control Flow) : 2-dword words at the head of a program.
 *   - ALU               : 2-dword clause words referenced by a CF_ALU.
 *   - VTX / TEX         : 4-dword clause words referenced by CF_FETCH.
 *
 * The encoders here cover enough to build:
 *   - Pass-through VS that fetches position + colour and exports them.
 *   - PS that exports an interpolated input.
 *   - Simple ALU sequences (MOV, ADD, MUL, DP4) sufficient for GLSL 1.20
 *     vertex transforms and single-texture fragment shaders.
 *
 * All encoders are pure — they fill out dwords in a caller-supplied array
 * and return the number of dwords written. On a big-endian PowerPC host
 * the output is written host-endian; the vertex fetch word sets
 * BUF_SWAP_32BIT so the GPU byte-swaps on its side.
 *
 * References:
 *   - Linux drivers/gpu/drm/radeon/r600_blit_shaders.c (pre-compiled blobs)
 *   - AMD R600 ISA public documentation
 *   - Mesa src/gallium/drivers/r600/sfn/sfn_instr_*.cpp for reference
 *
 * Copyright (c) 2026 John Davis. All rights reserved.
 */

#ifndef WIIGX2_R600_ISA_H
#define WIIGX2_R600_ISA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Register / operand encoding ------------------------------------ */

/* Source / destination GPR channels. */
#define WIIR600_CHAN_X  0
#define WIIR600_CHAN_Y  1
#define WIIR600_CHAN_Z  2
#define WIIR600_CHAN_W  3

/* CF instruction types. */
#define WIIR600_CF_INST_NOP          0x00
#define WIIR600_CF_INST_ALU          0x08
#define WIIR600_CF_INST_JUMP         0x0A
#define WIIR600_CF_INST_ELSE         0x0D
#define WIIR600_CF_INST_CALL_FS      0x13  /* fetch-shader call */
#define WIIR600_CF_INST_RETURN       0x14
#define WIIR600_CF_INST_VTX          0x01
#define WIIR600_CF_INST_TEX          0x02

/* CF EXPORT types (used by CF_ALLOC_EXPORT_WORD0/1). */
#define WIIR600_EXPORT_TYPE_PIXEL    0
#define WIIR600_EXPORT_TYPE_POS      1
#define WIIR600_EXPORT_TYPE_PARAM    2

/* CF EXPORT instruction codes. */
#define WIIR600_CF_INST_EXPORT       0x28
#define WIIR600_CF_INST_EXPORT_DONE  0x29

/* ALU OP2 / OP3 opcodes (subset). */
#define WIIR600_ALU_OP2_MOV          0x19
#define WIIR600_ALU_OP2_ADD          0x00
#define WIIR600_ALU_OP2_MUL          0x01
#define WIIR600_ALU_OP2_DOT4         0x50  /* DP4 in Mesa terms */
#define WIIR600_ALU_OP2_NOP          0x1A
#define WIIR600_ALU_OP3_MULADD       0x10

/* ALU src_sel special values (R600 ISA table 8.6). */
#define WIIR600_ALU_SRC_1_0_F        0xF9
#define WIIR600_ALU_SRC_0_0_F        0xF8
#define WIIR600_ALU_SRC_LITERAL      0xFD

/* --- Utility ----------------------------------------------------------
 * All encoders fill a little-endian bitfield layout into uint32_t words.
 * The ring / IB memory is mapped such that the GPU reads these as LE on
 * a PowerPC host when the 8IN32 swap is in play. We emit them in the
 * host's endianness and let the write path swap.
 * -------------------------------------------------------------------- */

/* Pack helper — force field into range and shift. */
static __inline__ uint32_t
wiir600_field(uint32_t v, uint32_t width, uint32_t shift)
{
    return (v & ((1u << width) - 1u)) << shift;
}

/* --- CF word encoders ------------------------------------------------ */

/* CF_ALU word: dispatch an ALU clause at `kcache_bank0` * 2 DW offset
 * into the program's ALU pool, `alu_count` instructions, last-of-program
 * flag `eop`. `addr` is the DW offset of the referenced ALU clause. */
static __inline__ void
wiir600_cf_alu(uint32_t out[2], uint32_t aluClauseDwordOffset,
               uint32_t aluCount, int eop)
{
    /* DW0: ADDR[21:0] */
    out[0] = wiir600_field(aluClauseDwordOffset, 22, 0);
    /* DW1: COUNT[6:0] at [10:4], CF_INST[9:0] at [13:7]? We follow
     * Mesa's layout: COUNT at bits [10:4], CF_INST at bits [29:23],
     * END_OF_PROGRAM at bit [21], BARRIER at bit [31]. */
    out[1] = wiir600_field(aluCount ? (aluCount - 1) : 0, 7, 0) |   /* count - 1 */
             (eop ? (1u << 21) : 0u) |                              /* end_of_program */
             wiir600_field(WIIR600_CF_INST_ALU, 7, 23) |            /* CF_INST */
             (1u << 31);                                             /* barrier */
}

/* CF_FETCH (VTX): run `count` VTX clauses starting at DW offset `addr`. */
static __inline__ void
wiir600_cf_vtx(uint32_t out[2], uint32_t vtxClauseDwordOffset,
               uint32_t count, int eop)
{
    out[0] = wiir600_field(vtxClauseDwordOffset, 22, 0);
    out[1] = wiir600_field(count ? (count - 1) : 0, 3, 0) |
             (eop ? (1u << 21) : 0u) |
             wiir600_field(WIIR600_CF_INST_VTX, 7, 23) |
             (1u << 31);
}

/* CF_ALLOC_IMP_EXPORT — export to pixel/pos/param.
 *   dstGpr     = source GPR index holding the 4-component export value
 *   arrayBase  = semantic/destination slot:
 *                  type=POS:   0 = POSITION_0
 *                  type=PIXEL: 0 = PIX0 (color 0)
 *                  type=PARAM: semantic index 0..31
 *   srcSelXYZW = 0..3 (choose channel from dstGpr), or 4=0.0, 5=1.0, 7=mask
 *   exportType = WIIR600_EXPORT_TYPE_*
 *   lastExport = 1 if this is the final export in the program
 */
static __inline__ void
wiir600_cf_export(uint32_t out[2],
                  uint32_t dstGpr,
                  uint32_t arrayBase,
                  uint32_t srcSelX, uint32_t srcSelY,
                  uint32_t srcSelZ, uint32_t srcSelW,
                  uint32_t exportType,
                  int lastExport)
{
    /* DW0: ARRAY_BASE[12:0], TYPE[14:13], RW_GPR[21:15],
     *      RW_REL[22], INDEX_GPR[29:23]. */
    out[0] = wiir600_field(arrayBase, 13, 0) |
             wiir600_field(exportType, 2, 13) |
             wiir600_field(dstGpr, 7, 15);
    /* DW1: SRC_SEL_X[2:0], SRC_SEL_Y[5:3], SRC_SEL_Z[8:6], SRC_SEL_W[11:9],
     *      BARRIER[31], CF_INST[29:23]. */
    out[1] = wiir600_field(srcSelX, 3, 0) |
             wiir600_field(srcSelY, 3, 3) |
             wiir600_field(srcSelZ, 3, 6) |
             wiir600_field(srcSelW, 3, 9) |
             wiir600_field(lastExport ? WIIR600_CF_INST_EXPORT_DONE
                                      : WIIR600_CF_INST_EXPORT, 7, 23) |
             (1u << 31);
}

/* --- ALU instruction encoder ---------------------------------------- */

/* Minimal OP2 encoder. Source operands are (src_sel, chan, neg).
 * `writeMask` controls which of the 4 channels this ALU writes — pass
 * the logical channel (X/Y/Z/W) in the low 2 bits. `last` marks the
 * end of an ALU instruction group (a vec4 "bundle"). */
static __inline__ void
wiir600_alu_op2(uint32_t out[2],
                uint32_t op,
                uint32_t src0Sel, uint32_t src0Chan, int src0Neg,
                uint32_t src1Sel, uint32_t src1Chan, int src1Neg,
                uint32_t dstGpr, uint32_t dstChan,
                int writeMask, int last)
{
    /* DW0: SRC0_SEL[8:0], SRC0_REL[9], SRC0_CHAN[11:10], SRC0_NEG[12],
     *      SRC1_SEL[21:13], SRC1_REL[22], SRC1_CHAN[24:23], SRC1_NEG[25],
     *      INDEX_MODE[28:26], PRED_SEL[30:29], LAST[31].
     */
    out[0] = wiir600_field(src0Sel, 9, 0) |
             wiir600_field(src0Chan, 2, 10) |
             (src0Neg ? (1u << 12) : 0u) |
             wiir600_field(src1Sel, 9, 13) |
             wiir600_field(src1Chan, 2, 23) |
             (src1Neg ? (1u << 25) : 0u) |
             (last ? (1u << 31) : 0u);
    /* DW1: ENCODING=0 for OP2, ALU_INST[17:7], BANK_SWIZZLE[20:18],
     *      DST_GPR[27:21], DST_REL[28], DST_CHAN[30:29], CLAMP[31],
     *      WRITE_MASK[4] inside OMOD, OMOD[6:5], SRC0_ABS[0], SRC1_ABS[1],
     *      UPDATE_EXEC_MASK[3], UPDATE_PRED[2]. */
    out[1] = (writeMask ? (1u << 4) : 0u) |
             wiir600_field(op, 11, 7) |
             wiir600_field(dstGpr, 7, 21) |
             wiir600_field(dstChan, 2, 29);
}

/* --- VTX clause (fetch) ---------------------------------------------
 * 4-DW VTX fetch that reads from buffer resource `bufferId` at offset
 * (srcGpr.srcChan * stride + offset) and writes DST_SEL swizzle into
 * the 4 channels of `dstGpr`. On big-endian host BUF_SWAP_32BIT is set.
 * -------------------------------------------------------------------- */
static __inline__ void
wiir600_vtx_fetch(uint32_t out[4],
                  uint32_t bufferId,
                  uint32_t srcGpr, uint32_t srcChan,
                  uint32_t dstGpr,
                  uint32_t dstSelX, uint32_t dstSelY,
                  uint32_t dstSelZ, uint32_t dstSelW,
                  uint32_t offsetBytes,
                  int lastInClause)
{
    /* DW0: VTX_INST[4:0]=0 (FETCH), FETCH_TYPE[6:5]=0 (vertex),
     *      BUFFER_ID[14:8], SRC_GPR[21:15], SRC_SEL_X[25:24], MEGA_FETCH[31:26]. */
    out[0] = wiir600_field(0, 5, 0) |
             wiir600_field(bufferId, 7, 8) |
             wiir600_field(srcGpr, 7, 15) |
             wiir600_field(srcChan, 2, 24) |
             wiir600_field(16, 6, 26);                /* mega-fetch count-1 = 15 */

    /* DW1: DST_GPR[6:0], DST_REL[7], DST_SEL_X[11:9], DST_SEL_Y[14:12],
     *      DST_SEL_Z[17:15], DST_SEL_W[20:18], USE_CONST_FIELDS[21],
     *      DATA_FORMAT[27:22], NUM_FORMAT[29:28], FORMAT_COMP[30],
     *      SRF_MODE[31]. For 4x32-bit float we want FMT=47, NUM=SCALED(0),
     *      SRF_MODE=1. */
    out[1] = wiir600_field(dstGpr, 7, 0) |
             wiir600_field(dstSelX, 3, 9) |
             wiir600_field(dstSelY, 3, 12) |
             wiir600_field(dstSelZ, 3, 15) |
             wiir600_field(dstSelW, 3, 18) |
             wiir600_field(47, 6, 22) |    /* FMT_32_32_32_32_FLOAT */
             (0u << 28) |                  /* NUM_FORMAT=NORM */
             (1u << 31);                   /* SRF_MODE_ALL */

    /* DW2: OFFSET[15:0], ENDIAN_SWAP[17:16], CONST_BUF_NO_STRIDE[18],
     *      MEGA_FETCH[19], ALT_CONST[20]. 2 = 8IN32 (big-endian host). */
    out[2] = wiir600_field(offsetBytes, 16, 0) |
             wiir600_field(2, 2, 16);                 /* BUF_SWAP_32BIT */
    out[3] = lastInClause ? 0u : 0u;                  /* reserved */
    (void) lastInClause;                              /* future use */
}

/* --- High-level helper: emit a pass-through VS ----------------------
 * Emits an R600 program that copies the first two vec4s from the vertex
 * buffer at resource slot `vbSlot` (position + colour) into R1/R2, then
 * exports them as POS0 and PARAM0. The emitted program layout:
 *   [CF]   CF_CALL_FS (implicit)
 *   [CF 0] CF_VTX  addr=vtxClauseOff, count=2
 *   [CF 1] CF_EXPORT POS0  (R1.xyzw)
 *   [CF 2] CF_EXPORT_DONE PARAM0 (R2.xyzw) -- END
 *   [VTX] 2 x 4-DW fetch
 *
 * Returns total dword count written to `prog`.
 * -------------------------------------------------------------------- */
static __inline__ uint32_t
wiir600_build_passthrough_vs(uint32_t *prog, uint32_t vbSlot)
{
    uint32_t cursor = 0;
    uint32_t vtxClauseOff;
    uint32_t alignFill;

    /* CF words are 2 DWs each; VTX clause words are 4 DWs each.
     * The VTX clause must be 16-DW aligned for R600. We'll lay out:
     *   CF 0..5 (3 CF words, 6 DWs) then pad to 16, then 2 VTX fetches. */
    uint32_t cf[6];

    /* CF[0]: CF_VTX addr=?, count=2 */
    /* CF[1]: CF_EXPORT POS0  */
    /* CF[2]: CF_EXPORT_DONE PARAM0 */
    /* Compute the VTX clause DW offset placeholder. */
    vtxClauseOff = 16u; /* aligned offset after 3 CF words + padding */

    wiir600_cf_vtx(&cf[0], vtxClauseOff / 2u, 2, 0);
    wiir600_cf_export(&cf[2], 1, 0,
                      WIIR600_CHAN_X, WIIR600_CHAN_Y,
                      WIIR600_CHAN_Z, WIIR600_CHAN_W,
                      WIIR600_EXPORT_TYPE_POS, 0);
    wiir600_cf_export(&cf[4], 2, 0,
                      WIIR600_CHAN_X, WIIR600_CHAN_Y,
                      WIIR600_CHAN_Z, WIIR600_CHAN_W,
                      WIIR600_EXPORT_TYPE_PARAM, 1);

    prog[cursor++] = cf[0];  prog[cursor++] = cf[1];
    prog[cursor++] = cf[2];  prog[cursor++] = cf[3];
    prog[cursor++] = cf[4];  prog[cursor++] = cf[5];

    /* Pad to 16-DW alignment before VTX clause. */
    alignFill = (16u - (cursor & 15u)) & 15u;
    while (alignFill--) {
        prog[cursor++] = 0u;
    }

    /* VTX[0]: fetch position into R1. */
    {
        uint32_t w[4];
        wiir600_vtx_fetch(w, vbSlot, 0 /* srcGpr=R0.x = VertexID index */,
                          WIIR600_CHAN_X, 1,
                          WIIR600_CHAN_X, WIIR600_CHAN_Y,
                          WIIR600_CHAN_Z, WIIR600_CHAN_W,
                          0 /* offset bytes */, 0);
        prog[cursor++] = w[0]; prog[cursor++] = w[1];
        prog[cursor++] = w[2]; prog[cursor++] = w[3];
    }
    /* VTX[1]: fetch colour into R2 at offset 16 bytes. */
    {
        uint32_t w[4];
        wiir600_vtx_fetch(w, vbSlot, 0,
                          WIIR600_CHAN_X, 2,
                          WIIR600_CHAN_X, WIIR600_CHAN_Y,
                          WIIR600_CHAN_Z, WIIR600_CHAN_W,
                          16, 1);
        prog[cursor++] = w[0]; prog[cursor++] = w[1];
        prog[cursor++] = w[2]; prog[cursor++] = w[3];
    }

    return cursor;
}

#ifdef __cplusplus
}
#endif

#endif /* WIIGX2_R600_ISA_H */
