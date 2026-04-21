/*
 * WiiGX2PM4.h — userland PM4 command stream builder for GPU7 / R700.
 *
 * Emits Type-3 PM4 packets into a dword array. All helpers take
 * a cursor (uint32_t *ptr, uint32_t *count) — callers are responsible
 * for bounds checking against the owning WiiGX2Buffer.
 *
 * Register numbering follows the AMD / Linux radeon convention:
 *   SET_CONFIG_REG  base 0x00008000, range 0x0000..0x2BFF
 *   SET_CONTEXT_REG base 0x00028000, range 0x0000..0x4000
 *   SET_RESOURCE    base 0x00038000 (word index, each resource is 7 DWs)
 *
 * Header-only, C89-compatible.
 *
 * Copyright (c) 2026 John Davis. All rights reserved.
 */

#ifndef WIIGX2_PM4_H
#define WIIGX2_PM4_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Packet header helpers ---------------- */

#define WIIGX2_PACKET3(op, count_minus_1) \
    (((uint32_t) 3u << 30) | \
     (((uint32_t) (op) & 0xFFu) << 8) | \
     (((uint32_t) (count_minus_1) & 0x3FFFu) << 16))

#define WIIGX2_PACKET3_NOP  0x10
#define WIIGX2_PACKET3_INDIRECT_BUFFER       0x32
#define WIIGX2_PACKET3_SET_CONFIG_REG        0x68
#define WIIGX2_PACKET3_SET_CONTEXT_REG       0x69
#define WIIGX2_PACKET3_SET_RESOURCE          0x6D
#define WIIGX2_PACKET3_SET_ALU_CONST         0x6A
#define WIIGX2_PACKET3_SET_SAMPLER           0x6E
#define WIIGX2_PACKET3_SURFACE_SYNC          0x43
#define WIIGX2_PACKET3_EVENT_WRITE           0x46
#define WIIGX2_PACKET3_EVENT_WRITE_EOP       0x47
#define WIIGX2_PACKET3_DRAW_INDEX_AUTO       0x2D
#define WIIGX2_PACKET3_DRAW_INDEX            0x2B
#define WIIGX2_PACKET3_NUM_INSTANCES         0x2F
#define WIIGX2_PACKET3_INDEX_TYPE            0x2A

#define WIIGX2_CFG_REG_BASE         0x00008000u
#define WIIGX2_CTX_REG_BASE         0x00028000u
#define WIIGX2_RESOURCE_BASE        0x00038000u
#define WIIGX2_SAMPLER_BASE         0x0003C000u

/* Resource-slot layout (decaf-emu latte_enum_sq.h SQ_RES_OFFSET). Each
 * slot is 7 DWORDs; the index written into PM4 SET_RESOURCE is
 * (slot_index * 7). */
#define WIIGX2_RESSLOT_PS_TEX_BASE   0x000u   /* 0..17   (PS textures) */
#define WIIGX2_RESSLOT_PS_BUF_BASE   0x080u   /* 0..15   */
#define WIIGX2_RESSLOT_VS_TEX_BASE   0x0A0u   /* 0..17   */
#define WIIGX2_RESSLOT_VS_BUF_BASE   0x120u   /* 0..15   */
#define WIIGX2_RESSLOT_VS_ATTR_BASE  0x140u   /* 0..15 (VS fetch-shader) */
#define WIIGX2_RESSLOT_GS_TEX_BASE   0x150u
#define WIIGX2_RESSLOT_GS_BUF_BASE   0x1D0u

/* Sampler-slot layout. Each sampler slot is 3 DWORDs; the index written
 * into PM4 SET_SAMPLER is (slot_index * 3). */
#define WIIGX2_SAMPSLOT_PS_BASE      0u        /* 0..17   */
#define WIIGX2_SAMPSLOT_VS_BASE      18u       /* 18..35  */
#define WIIGX2_SAMPSLOT_GS_BASE      36u       /* 36..53  */

/* SURFACE_SYNC CP_COHER_CNTL bits (R6xx/R7xx, matches Linux r600d.h). */
#define WIIGX2_SURFACE_SYNC_VC_ACTION_ENA     (1u << 24)
#define WIIGX2_SURFACE_SYNC_TC_ACTION_ENA     (1u << 23)
#define WIIGX2_SURFACE_SYNC_CB_ACTION_ENA     (1u << 25)
#define WIIGX2_SURFACE_SYNC_DB_ACTION_ENA     (1u << 26)
#define WIIGX2_SURFACE_SYNC_SH_ACTION_ENA     (1u << 27)
#define WIIGX2_SURFACE_SYNC_FULL_CACHE_ENA    (1u << 31)

/* VGT primitive types (used when emitting a DRAW directly — normally
 * written through SET_CONFIG_REG of VGT_PRIMITIVE_TYPE = 0x2256). */
#define WIIGX2_VGT_PRIM_POINTLIST     1
#define WIIGX2_VGT_PRIM_LINELIST      2
#define WIIGX2_VGT_PRIM_LINESTRIP     3
#define WIIGX2_VGT_PRIM_TRILIST       4
#define WIIGX2_VGT_PRIM_TRIFAN        5
#define WIIGX2_VGT_PRIM_TRISTRIP      6
#define WIIGX2_VGT_PRIM_QUADLIST      13
#define WIIGX2_VGT_PRIM_RECTLIST      17

/* DRAW_INDEX_AUTO / _INDEX DW1 flag: source=AutoIndex. */
#define WIIGX2_DRAW_SOURCE_AUTO   (2u << 0)

/* ---------------- Cursor-based emitters ---------------- */

/* Low-level: write a single DW and bump *count. */
static __inline__ void
wiigx2_pm4_dw(uint32_t *cmd, uint32_t *count, uint32_t value)
{
    cmd[(*count)++] = value;
}

/* PACKET3 header with payload size = bodyDwords. */
static __inline__ void
wiigx2_pm4_header3(uint32_t *cmd, uint32_t *count,
                   uint32_t opcode, uint32_t bodyDwords)
{
    wiigx2_pm4_dw(cmd, count,
                  WIIGX2_PACKET3(opcode, bodyDwords - 1));
}

/* SET_CONFIG_REG: regByte must be in range [0x8000, 0xAC00). */
static __inline__ void
wiigx2_pm4_set_config_reg(uint32_t *cmd, uint32_t *count,
                          uint32_t regByte, uint32_t value)
{
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_SET_CONFIG_REG, 2);
    wiigx2_pm4_dw(cmd, count, (regByte - WIIGX2_CFG_REG_BASE) >> 2);
    wiigx2_pm4_dw(cmd, count, value);
}

/* SET_CONTEXT_REG: regByte must be in range [0x28000, 0x28000+ctx). */
static __inline__ void
wiigx2_pm4_set_context_reg(uint32_t *cmd, uint32_t *count,
                           uint32_t regByte, uint32_t value)
{
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_SET_CONTEXT_REG, 2);
    wiigx2_pm4_dw(cmd, count, (regByte - WIIGX2_CTX_REG_BASE) >> 2);
    wiigx2_pm4_dw(cmd, count, value);
}

static __inline__ void
wiigx2_pm4_set_context_regs(uint32_t *cmd, uint32_t *count,
                            uint32_t regByte,
                            const uint32_t *values, uint32_t n)
{
    uint32_t i;
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_SET_CONTEXT_REG, 1 + n);
    wiigx2_pm4_dw(cmd, count, (regByte - WIIGX2_CTX_REG_BASE) >> 2);
    for (i = 0; i < n; i++) {
        wiigx2_pm4_dw(cmd, count, values[i]);
    }
}

/* SURFACE_SYNC. `size` is in bytes (0xFFFFFFFF = everything), `baseShifted`
 * is base >> 8 (0 when FULL_CACHE_ENA is set). */
static __inline__ void
wiigx2_pm4_surface_sync(uint32_t *cmd, uint32_t *count,
                        uint32_t coherCntl, uint32_t size,
                        uint32_t baseShifted)
{
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_SURFACE_SYNC, 4);
    wiigx2_pm4_dw(cmd, count, coherCntl);
    wiigx2_pm4_dw(cmd, count, size);
    wiigx2_pm4_dw(cmd, count, baseShifted);
    wiigx2_pm4_dw(cmd, count, 10u);   /* poll interval, matches Linux */
}

/* SET_RESOURCE — writes a 7-DW resource descriptor at `slot`. `slot` is
 * the R6xx/R7xx resource index (0..175 on RV710 — VS uses the low slots,
 * PS uses the high slots; see Linux r600d.h R_03xxxx_SQ_RESOURCE_*). */
static __inline__ void
wiigx2_pm4_set_resource(uint32_t *cmd, uint32_t *count,
                        uint32_t slot, const uint32_t words[7])
{
    uint32_t i;
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_SET_RESOURCE, 1 + 7);
    wiigx2_pm4_dw(cmd, count, slot * 7u);
    for (i = 0; i < 7; i++) {
        wiigx2_pm4_dw(cmd, count, words[i]);
    }
}

/* Fill out a VS vertex-buffer resource (7 DWs). Mirrors
 * WiiGX2::pm4SetVTXResource but leaves emission to the caller. */
static __inline__ void
wiigx2_build_vtx_resource(uint32_t out[7],
                          uint64_t basePhys,
                          uint32_t sizeBytes,
                          uint32_t stride)
{
    out[0] = (uint32_t) (basePhys & 0xFFFFFFFFu);
    out[1] = sizeBytes ? (sizeBytes - 1) : 0;
    /* DW2: stride[10:0] | FMT_32_32_32_32_FLOAT (47) << 16 |
     *      NUM_FORMAT=NORM(0)<<12 is default | 8IN32 endian swap (2<<30). */
    out[2] = ((uint32_t) stride & 0x7FFu) |
             (47u << 16) |
             (2u << 30);
    /* DW3: mega-fetch count — set to stride like the Linux r600 blit.
     * (Some bring-up paths prefer 1 — see kernel memory note.) */
    out[3] = (stride & 0xFFFu);
    out[4] = 0;
    out[5] = 0;
    /* DW6 type: TYPE_VERTEX_BUFFER = 3 in top 2 bits. */
    out[6] = (3u << 30);
}

/* EVENT_WRITE — fires a non-timestamp event (no payload write). */
static __inline__ void
wiigx2_pm4_event_write(uint32_t *cmd, uint32_t *count,
                       uint32_t eventType, uint32_t eventIndex)
{
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_EVENT_WRITE, 1);
    wiigx2_pm4_dw(cmd, count,
                  (eventType & 0x3Fu) | ((eventIndex & 0xFu) << 8));
}

/* DRAW_INDEX_AUTO: DW1=vertex count, DW2=source select (auto). */
static __inline__ void
wiigx2_pm4_draw_auto(uint32_t *cmd, uint32_t *count, uint32_t vertexCount)
{
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_DRAW_INDEX_AUTO, 2);
    wiigx2_pm4_dw(cmd, count, vertexCount);
    wiigx2_pm4_dw(cmd, count, WIIGX2_DRAW_SOURCE_AUTO);
}

static __inline__ void
wiigx2_pm4_num_instances(uint32_t *cmd, uint32_t *count, uint32_t n)
{
    wiigx2_pm4_header3(cmd, count, WIIGX2_PACKET3_NUM_INSTANCES, 1);
    wiigx2_pm4_dw(cmd, count, n);
}

/* Pad ring-safe IB trailing NOPs so IB dword count is aligned.
 * R6xx CP requires IB dword count be a multiple of 4. */
static __inline__ void
wiigx2_pm4_pad_to_multiple_of_4(uint32_t *cmd, uint32_t *count)
{
    while ((*count & 3u) != 0u) {
        wiigx2_pm4_dw(cmd, count,
                      WIIGX2_PACKET3(WIIGX2_PACKET3_NOP, 0));
        wiigx2_pm4_dw(cmd, count, 0u);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* WIIGX2_PM4_H */
