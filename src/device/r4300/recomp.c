/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - recomp.c                                                *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#if defined(__GNUC__)
#include <unistd.h>
#ifndef __MINGW32__
#include <sys/mman.h>
#endif
#endif

#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "device/memory/memory.h"
#include "device/r4300/cached_interp.h"
#include "device/r4300/exception.h"
#include "device/r4300/ops.h"
#include "device/r4300/recomp.h"
#include "device/r4300/recomph.h" //include for function prototypes
#include "device/r4300/tlb.h"
#include "main/main.h"
#include "main/profile.h"

static void *malloc_exec(size_t size);
static void free_exec(void *ptr, size_t length);

static void RSV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.RESERVED;
    g_dev.r4300.recomp.recomp_func = genreserved;
}

static void RFIN_BLOCK(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.FIN_BLOCK;
    g_dev.r4300.recomp.recomp_func = genfin_block;
}

static void RNOTCOMPILED(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NOTCOMPILED;
    g_dev.r4300.recomp.recomp_func = gennotcompiled;
}

static void recompile_standard_i_type(void)
{
    g_dev.r4300.recomp.dst->f.i.rs = r4300_regs(&g_dev.r4300) + ((g_dev.r4300.recomp.src >> 21) & 0x1F);
    g_dev.r4300.recomp.dst->f.i.rt = r4300_regs(&g_dev.r4300) + ((g_dev.r4300.recomp.src >> 16) & 0x1F);
    g_dev.r4300.recomp.dst->f.i.immediate = (int16_t) g_dev.r4300.recomp.src;
}

static void recompile_standard_j_type(void)
{
    g_dev.r4300.recomp.dst->f.j.inst_index = g_dev.r4300.recomp.src & UINT32_C(0x3FFFFFF);
}

static void recompile_standard_r_type(void)
{
    g_dev.r4300.recomp.dst->f.r.rs = r4300_regs(&g_dev.r4300) + ((g_dev.r4300.recomp.src >> 21) & 0x1F);
    g_dev.r4300.recomp.dst->f.r.rt = r4300_regs(&g_dev.r4300) + ((g_dev.r4300.recomp.src >> 16) & 0x1F);
    g_dev.r4300.recomp.dst->f.r.rd = r4300_regs(&g_dev.r4300) + ((g_dev.r4300.recomp.src >> 11) & 0x1F);
    g_dev.r4300.recomp.dst->f.r.sa = (g_dev.r4300.recomp.src >>  6) & 0x1F;
}

static void recompile_standard_lf_type(void)
{
    g_dev.r4300.recomp.dst->f.lf.base = (g_dev.r4300.recomp.src >> 21) & 0x1F;
    g_dev.r4300.recomp.dst->f.lf.ft = (g_dev.r4300.recomp.src >> 16) & 0x1F;
    g_dev.r4300.recomp.dst->f.lf.offset = g_dev.r4300.recomp.src & 0xFFFF;
}

static void recompile_standard_cf_type(void)
{
    g_dev.r4300.recomp.dst->f.cf.ft = (g_dev.r4300.recomp.src >> 16) & 0x1F;
    g_dev.r4300.recomp.dst->f.cf.fs = (g_dev.r4300.recomp.src >> 11) & 0x1F;
    g_dev.r4300.recomp.dst->f.cf.fd = (g_dev.r4300.recomp.src >>  6) & 0x1F;
}

//-------------------------------------------------------------------------
//                                  SPECIAL                                
//-------------------------------------------------------------------------

static void RNOP(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NOP;
    g_dev.r4300.recomp.recomp_func = gennop;
}

static void RSLL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SLL;
    g_dev.r4300.recomp.recomp_func = gensll;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSRL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SRL;
    g_dev.r4300.recomp.recomp_func = gensrl;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSRA(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SRA;
    g_dev.r4300.recomp.recomp_func = gensra;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSLLV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SLLV;
    g_dev.r4300.recomp.recomp_func = gensllv;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSRLV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SRLV;
    g_dev.r4300.recomp.recomp_func = gensrlv;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSRAV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SRAV;
    g_dev.r4300.recomp.recomp_func = gensrav;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RJR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.JR;
    g_dev.r4300.recomp.recomp_func = genjr;
    recompile_standard_i_type();
}

static void RJALR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.JALR;
    g_dev.r4300.recomp.recomp_func = genjalr;
    recompile_standard_r_type();
}

static void RSYSCALL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SYSCALL;
    g_dev.r4300.recomp.recomp_func = gensyscall;
}

static void RBREAK(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RSYNC(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SYNC;
    g_dev.r4300.recomp.recomp_func = gensync;
}

static void RMFHI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MFHI;
    g_dev.r4300.recomp.recomp_func = genmfhi;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RMTHI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MTHI;
    g_dev.r4300.recomp.recomp_func = genmthi;
    recompile_standard_r_type();
}

static void RMFLO(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MFLO;
    g_dev.r4300.recomp.recomp_func = genmflo;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RMTLO(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MTLO;
    g_dev.r4300.recomp.recomp_func = genmtlo;
    recompile_standard_r_type();
}

static void RDSLLV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSLLV;
    g_dev.r4300.recomp.recomp_func = gendsllv;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSRLV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSRLV;
    g_dev.r4300.recomp.recomp_func = gendsrlv;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSRAV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSRAV;
    g_dev.r4300.recomp.recomp_func = gendsrav;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RMULT(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MULT;
    g_dev.r4300.recomp.recomp_func = genmult;
    recompile_standard_r_type();
}

static void RMULTU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MULTU;
    g_dev.r4300.recomp.recomp_func = genmultu;
    recompile_standard_r_type();
}

static void RDIV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DIV;
    g_dev.r4300.recomp.recomp_func = gendiv;
    recompile_standard_r_type();
}

static void RDIVU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DIVU;
    g_dev.r4300.recomp.recomp_func = gendivu;
    recompile_standard_r_type();
}

static void RDMULT(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DMULT;
    g_dev.r4300.recomp.recomp_func = gendmult;
    recompile_standard_r_type();
}

static void RDMULTU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DMULTU;
    g_dev.r4300.recomp.recomp_func = gendmultu;
    recompile_standard_r_type();
}

static void RDDIV(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DDIV;
    g_dev.r4300.recomp.recomp_func = genddiv;
    recompile_standard_r_type();
}

static void RDDIVU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DDIVU;
    g_dev.r4300.recomp.recomp_func = genddivu;
    recompile_standard_r_type();
}

static void RADD(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ADD;
    g_dev.r4300.recomp.recomp_func = genadd;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RADDU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ADDU;
    g_dev.r4300.recomp.recomp_func = genaddu;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSUB(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SUB;
    g_dev.r4300.recomp.recomp_func = gensub;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSUBU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SUBU;
    g_dev.r4300.recomp.recomp_func = gensubu;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RAND(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.AND;
    g_dev.r4300.recomp.recomp_func = genand;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void ROR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.OR;
    g_dev.r4300.recomp.recomp_func = genor;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RXOR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.XOR;
    g_dev.r4300.recomp.recomp_func = genxor;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RNOR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NOR;
    g_dev.r4300.recomp.recomp_func = gennor;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSLT(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SLT;
    g_dev.r4300.recomp.recomp_func = genslt;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSLTU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SLTU;
    g_dev.r4300.recomp.recomp_func = gensltu;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDADD(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DADD;
    g_dev.r4300.recomp.recomp_func = gendadd;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDADDU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DADDU;
    g_dev.r4300.recomp.recomp_func = gendaddu;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSUB(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSUB;
    g_dev.r4300.recomp.recomp_func = gendsub;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSUBU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSUBU;
    g_dev.r4300.recomp.recomp_func = gendsubu;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RTGE(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTGEU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTLT(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTLTU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTEQ(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TEQ;
    g_dev.r4300.recomp.recomp_func = genteq;
    recompile_standard_r_type();
}

static void RTNE(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RDSLL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSLL;
    g_dev.r4300.recomp.recomp_func = gendsll;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSRL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSRL;
    g_dev.r4300.recomp.recomp_func = gendsrl;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSRA(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSRA;
    g_dev.r4300.recomp.recomp_func = gendsra;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSLL32(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSLL32;
    g_dev.r4300.recomp.recomp_func = gendsll32;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSRL32(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSRL32;
    g_dev.r4300.recomp.recomp_func = gendsrl32;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDSRA32(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DSRA32;
    g_dev.r4300.recomp.recomp_func = gendsra32;
    recompile_standard_r_type();
    if (g_dev.r4300.recomp.dst->f.r.rd == r4300_regs(&g_dev.r4300)) RNOP();
}

static void (*const recomp_special[64])(void) =
{
    RSLL , RSV   , RSRL , RSRA , RSLLV   , RSV    , RSRLV  , RSRAV  ,
    RJR  , RJALR , RSV  , RSV  , RSYSCALL, RBREAK , RSV    , RSYNC  ,
    RMFHI, RMTHI , RMFLO, RMTLO, RDSLLV  , RSV    , RDSRLV , RDSRAV ,
    RMULT, RMULTU, RDIV , RDIVU, RDMULT  , RDMULTU, RDDIV  , RDDIVU ,
    RADD , RADDU , RSUB , RSUBU, RAND    , ROR    , RXOR   , RNOR   ,
    RSV  , RSV   , RSLT , RSLTU, RDADD   , RDADDU , RDSUB  , RDSUBU ,
    RTGE , RTGEU , RTLT , RTLTU, RTEQ    , RSV    , RTNE   , RSV    ,
    RDSLL, RSV   , RDSRL, RDSRA, RDSLL32 , RSV    , RDSRL32, RDSRA32
};

//-------------------------------------------------------------------------
//                                   REGIMM                                
//-------------------------------------------------------------------------

static void RBLTZ(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZ;
    g_dev.r4300.recomp.recomp_func = genbltz;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZ_IDLE;
            g_dev.r4300.recomp.recomp_func = genbltz_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZ_OUT;
        g_dev.r4300.recomp.recomp_func = genbltz_out;
    }
}

static void RBGEZ(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZ;
    g_dev.r4300.recomp.recomp_func = genbgez;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZ_IDLE;
            g_dev.r4300.recomp.recomp_func = genbgez_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZ_OUT;
        g_dev.r4300.recomp.recomp_func = genbgez_out;
    }
}

static void RBLTZL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZL;
    g_dev.r4300.recomp.recomp_func = genbltzl;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbltzl_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZL_OUT;
        g_dev.r4300.recomp.recomp_func = genbltzl_out;
    }
}

static void RBGEZL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZL;
    g_dev.r4300.recomp.recomp_func = genbgezl;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbgezl_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZL_OUT;
        g_dev.r4300.recomp.recomp_func = genbgezl_out;
    }
}

static void RTGEI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTGEIU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTLTI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTLTIU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTEQI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RTNEI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
}

static void RBLTZAL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZAL;
    g_dev.r4300.recomp.recomp_func = genbltzal;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZAL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbltzal_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZAL_OUT;
        g_dev.r4300.recomp.recomp_func = genbltzal_out;
    }
}

static void RBGEZAL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZAL;
    g_dev.r4300.recomp.recomp_func = genbgezal;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZAL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbgezal_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZAL_OUT;
        g_dev.r4300.recomp.recomp_func = genbgezal_out;
    }
}

static void RBLTZALL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZALL;
    g_dev.r4300.recomp.recomp_func = genbltzall;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZALL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbltzall_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLTZALL_OUT;
        g_dev.r4300.recomp.recomp_func = genbltzall_out;
    }
}

static void RBGEZALL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZALL;
    g_dev.r4300.recomp.recomp_func = genbgezall;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZALL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbgezall_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGEZALL_OUT;
        g_dev.r4300.recomp.recomp_func = genbgezall_out;
    }
}

static void (*const recomp_regimm[32])(void) =
{
    RBLTZ  , RBGEZ  , RBLTZL  , RBGEZL  , RSV  , RSV, RSV  , RSV,
    RTGEI  , RTGEIU , RTLTI   , RTLTIU  , RTEQI, RSV, RTNEI, RSV,
    RBLTZAL, RBGEZAL, RBLTZALL, RBGEZALL, RSV  , RSV, RSV  , RSV,
    RSV    , RSV    , RSV     , RSV     , RSV  , RSV, RSV  , RSV
};

//-------------------------------------------------------------------------
//                                     TLB                                 
//-------------------------------------------------------------------------

static void RTLBR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TLBR;
    g_dev.r4300.recomp.recomp_func = gentlbr;
}

static void RTLBWI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TLBWI;
    g_dev.r4300.recomp.recomp_func = gentlbwi;
}

static void RTLBWR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TLBWR;
    g_dev.r4300.recomp.recomp_func = gentlbwr;
}

static void RTLBP(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TLBP;
    g_dev.r4300.recomp.recomp_func = gentlbp;
}

static void RERET(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ERET;
    g_dev.r4300.recomp.recomp_func = generet;
}

static void (*const recomp_tlb[64])(void) =
{
    RSV  , RTLBR, RTLBWI, RSV, RSV, RSV, RTLBWR, RSV, 
    RTLBP, RSV  , RSV   , RSV, RSV, RSV, RSV   , RSV, 
    RSV  , RSV  , RSV   , RSV, RSV, RSV, RSV   , RSV, 
    RERET, RSV  , RSV   , RSV, RSV, RSV, RSV   , RSV, 
    RSV  , RSV  , RSV   , RSV, RSV, RSV, RSV   , RSV, 
    RSV  , RSV  , RSV   , RSV, RSV, RSV, RSV   , RSV, 
    RSV  , RSV  , RSV   , RSV, RSV, RSV, RSV   , RSV, 
    RSV  , RSV  , RSV   , RSV, RSV, RSV, RSV   , RSV
};

//-------------------------------------------------------------------------
//                                    COP0                                 
//-------------------------------------------------------------------------

static void RMFC0(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MFC0;
    g_dev.r4300.recomp.recomp_func = genmfc0;
    recompile_standard_r_type();
    g_dev.r4300.recomp.dst->f.r.rd = (int64_t*) (r4300_cp0_regs(&g_dev.r4300.cp0) + ((g_dev.r4300.recomp.src >> 11) & 0x1F));
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
    if (g_dev.r4300.recomp.dst->f.r.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RMTC0(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MTC0;
    g_dev.r4300.recomp.recomp_func = genmtc0;
    recompile_standard_r_type();
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
}

static void RTLB(void)
{
    recomp_tlb[(g_dev.r4300.recomp.src & 0x3F)]();
}

static void (*const recomp_cop0[32])(void) =
{
    RMFC0, RSV, RSV, RSV, RMTC0, RSV, RSV, RSV,
    RSV  , RSV, RSV, RSV, RSV  , RSV, RSV, RSV,
    RTLB , RSV, RSV, RSV, RSV  , RSV, RSV, RSV,
    RSV  , RSV, RSV, RSV, RSV  , RSV, RSV, RSV
};

//-------------------------------------------------------------------------
//                                     BC                                  
//-------------------------------------------------------------------------

static void RBC1F(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1F;
    g_dev.r4300.recomp.recomp_func = genbc1f;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1F_IDLE;
            g_dev.r4300.recomp.recomp_func = genbc1f_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1F_OUT;
        g_dev.r4300.recomp.recomp_func = genbc1f_out;
    }
}

static void RBC1T(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1T;
    g_dev.r4300.recomp.recomp_func = genbc1t;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1T_IDLE;
            g_dev.r4300.recomp.recomp_func = genbc1t_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1T_OUT;
        g_dev.r4300.recomp.recomp_func = genbc1t_out;
    }
}

static void RBC1FL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1FL;
    g_dev.r4300.recomp.recomp_func = genbc1fl;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1FL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbc1fl_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1FL_OUT;
        g_dev.r4300.recomp.recomp_func = genbc1fl_out;
    }
}

static void RBC1TL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1TL;
    g_dev.r4300.recomp.recomp_func = genbc1tl;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1TL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbc1tl_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BC1TL_OUT;
        g_dev.r4300.recomp.recomp_func = genbc1tl_out;
    }
}

static void (*const recomp_bc[4])(void) =
{
    RBC1F , RBC1T ,
    RBC1FL, RBC1TL
};

//-------------------------------------------------------------------------
//                                     S                                   
//-------------------------------------------------------------------------

static void RADD_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ADD_S;
    g_dev.r4300.recomp.recomp_func = genadd_s;
    recompile_standard_cf_type();
}

static void RSUB_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SUB_S;
    g_dev.r4300.recomp.recomp_func = gensub_s;
    recompile_standard_cf_type();
}

static void RMUL_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MUL_S;
    g_dev.r4300.recomp.recomp_func = genmul_s;
    recompile_standard_cf_type();
}

static void RDIV_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DIV_S;
    g_dev.r4300.recomp.recomp_func = gendiv_s;
    recompile_standard_cf_type();
}

static void RSQRT_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SQRT_S;
    g_dev.r4300.recomp.recomp_func = gensqrt_s;
    recompile_standard_cf_type();
}

static void RABS_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ABS_S;
    g_dev.r4300.recomp.recomp_func = genabs_s;
    recompile_standard_cf_type();
}

static void RMOV_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MOV_S;
    g_dev.r4300.recomp.recomp_func = genmov_s;
    recompile_standard_cf_type();
}

static void RNEG_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NEG_S;
    g_dev.r4300.recomp.recomp_func = genneg_s;
    recompile_standard_cf_type();
}

static void RROUND_L_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ROUND_L_S;
    g_dev.r4300.recomp.recomp_func = genround_l_s;
    recompile_standard_cf_type();
}

static void RTRUNC_L_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TRUNC_L_S;
    g_dev.r4300.recomp.recomp_func = gentrunc_l_s;
    recompile_standard_cf_type();
}

static void RCEIL_L_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CEIL_L_S;
    g_dev.r4300.recomp.recomp_func = genceil_l_s;
    recompile_standard_cf_type();
}

static void RFLOOR_L_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.FLOOR_L_S;
    g_dev.r4300.recomp.recomp_func = genfloor_l_s;
    recompile_standard_cf_type();
}

static void RROUND_W_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ROUND_W_S;
    g_dev.r4300.recomp.recomp_func = genround_w_s;
    recompile_standard_cf_type();
}

static void RTRUNC_W_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TRUNC_W_S;
    g_dev.r4300.recomp.recomp_func = gentrunc_w_s;
    recompile_standard_cf_type();
}

static void RCEIL_W_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CEIL_W_S;
    g_dev.r4300.recomp.recomp_func = genceil_w_s;
    recompile_standard_cf_type();
}

static void RFLOOR_W_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.FLOOR_W_S;
    g_dev.r4300.recomp.recomp_func = genfloor_w_s;
    recompile_standard_cf_type();
}

static void RCVT_D_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_D_S;
    g_dev.r4300.recomp.recomp_func = gencvt_d_s;
    recompile_standard_cf_type();
}

static void RCVT_W_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_W_S;
    g_dev.r4300.recomp.recomp_func = gencvt_w_s;
    recompile_standard_cf_type();
}

static void RCVT_L_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_L_S;
    g_dev.r4300.recomp.recomp_func = gencvt_l_s;
    recompile_standard_cf_type();
}

static void RC_F_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_F_S;
    g_dev.r4300.recomp.recomp_func = genc_f_s;
    recompile_standard_cf_type();
}

static void RC_UN_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_UN_S;
    g_dev.r4300.recomp.recomp_func = genc_un_s;
    recompile_standard_cf_type();
}

static void RC_EQ_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_EQ_S;
    g_dev.r4300.recomp.recomp_func = genc_eq_s;
    recompile_standard_cf_type();
}

static void RC_UEQ_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_UEQ_S;
    g_dev.r4300.recomp.recomp_func = genc_ueq_s;
    recompile_standard_cf_type();
}

static void RC_OLT_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_OLT_S;
    g_dev.r4300.recomp.recomp_func = genc_olt_s;
    recompile_standard_cf_type();
}

static void RC_ULT_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_ULT_S;
    g_dev.r4300.recomp.recomp_func = genc_ult_s;
    recompile_standard_cf_type();
}

static void RC_OLE_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_OLE_S;
    g_dev.r4300.recomp.recomp_func = genc_ole_s;
    recompile_standard_cf_type();
}

static void RC_ULE_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_ULE_S;
    g_dev.r4300.recomp.recomp_func = genc_ule_s;
    recompile_standard_cf_type();
}

static void RC_SF_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_SF_S;
    g_dev.r4300.recomp.recomp_func = genc_sf_s;
    recompile_standard_cf_type();
}

static void RC_NGLE_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGLE_S;
    g_dev.r4300.recomp.recomp_func = genc_ngle_s;
    recompile_standard_cf_type();
}

static void RC_SEQ_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_SEQ_S;
    g_dev.r4300.recomp.recomp_func = genc_seq_s;
    recompile_standard_cf_type();
}

static void RC_NGL_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGL_S;
    g_dev.r4300.recomp.recomp_func = genc_ngl_s;
    recompile_standard_cf_type();
}

static void RC_LT_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_LT_S;
    g_dev.r4300.recomp.recomp_func = genc_lt_s;
    recompile_standard_cf_type();
}

static void RC_NGE_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGE_S;
    g_dev.r4300.recomp.recomp_func = genc_nge_s;
    recompile_standard_cf_type();
}

static void RC_LE_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_LE_S;
    g_dev.r4300.recomp.recomp_func = genc_le_s;
    recompile_standard_cf_type();
}

static void RC_NGT_S(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGT_S;
    g_dev.r4300.recomp.recomp_func = genc_ngt_s;
    recompile_standard_cf_type();
}

static void (*const recomp_s[64])(void) =
{
    RADD_S    , RSUB_S    , RMUL_S   , RDIV_S    , RSQRT_S   , RABS_S    , RMOV_S   , RNEG_S    , 
    RROUND_L_S, RTRUNC_L_S, RCEIL_L_S, RFLOOR_L_S, RROUND_W_S, RTRUNC_W_S, RCEIL_W_S, RFLOOR_W_S, 
    RSV       , RSV       , RSV      , RSV       , RSV       , RSV       , RSV      , RSV       , 
    RSV       , RSV       , RSV      , RSV       , RSV       , RSV       , RSV      , RSV       , 
    RSV       , RCVT_D_S  , RSV      , RSV       , RCVT_W_S  , RCVT_L_S  , RSV      , RSV       , 
    RSV       , RSV       , RSV      , RSV       , RSV       , RSV       , RSV      , RSV       , 
    RC_F_S    , RC_UN_S   , RC_EQ_S  , RC_UEQ_S  , RC_OLT_S  , RC_ULT_S  , RC_OLE_S , RC_ULE_S  , 
    RC_SF_S   , RC_NGLE_S , RC_SEQ_S , RC_NGL_S  , RC_LT_S   , RC_NGE_S  , RC_LE_S  , RC_NGT_S
};

//-------------------------------------------------------------------------
//                                     D                                   
//-------------------------------------------------------------------------

static void RADD_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ADD_D;
    g_dev.r4300.recomp.recomp_func = genadd_d;
    recompile_standard_cf_type();
}

static void RSUB_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SUB_D;
    g_dev.r4300.recomp.recomp_func = gensub_d;
    recompile_standard_cf_type();
}

static void RMUL_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MUL_D;
    g_dev.r4300.recomp.recomp_func = genmul_d;
    recompile_standard_cf_type();
}

static void RDIV_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DIV_D;
    g_dev.r4300.recomp.recomp_func = gendiv_d;
    recompile_standard_cf_type();
}

static void RSQRT_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SQRT_D;
    g_dev.r4300.recomp.recomp_func = gensqrt_d;
    recompile_standard_cf_type();
}

static void RABS_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ABS_D;
    g_dev.r4300.recomp.recomp_func = genabs_d;
    recompile_standard_cf_type();
}

static void RMOV_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MOV_D;
    g_dev.r4300.recomp.recomp_func = genmov_d;
    recompile_standard_cf_type();
}

static void RNEG_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NEG_D;
    g_dev.r4300.recomp.recomp_func = genneg_d;
    recompile_standard_cf_type();
}

static void RROUND_L_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ROUND_L_D;
    g_dev.r4300.recomp.recomp_func = genround_l_d;
    recompile_standard_cf_type();
}

static void RTRUNC_L_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TRUNC_L_D;
    g_dev.r4300.recomp.recomp_func = gentrunc_l_d;
    recompile_standard_cf_type();
}

static void RCEIL_L_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CEIL_L_D;
    g_dev.r4300.recomp.recomp_func = genceil_l_d;
    recompile_standard_cf_type();
}

static void RFLOOR_L_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.FLOOR_L_D;
    g_dev.r4300.recomp.recomp_func = genfloor_l_d;
    recompile_standard_cf_type();
}

static void RROUND_W_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ROUND_W_D;
    g_dev.r4300.recomp.recomp_func = genround_w_d;
    recompile_standard_cf_type();
}

static void RTRUNC_W_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.TRUNC_W_D;
    g_dev.r4300.recomp.recomp_func = gentrunc_w_d;
    recompile_standard_cf_type();
}

static void RCEIL_W_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CEIL_W_D;
    g_dev.r4300.recomp.recomp_func = genceil_w_d;
    recompile_standard_cf_type();
}

static void RFLOOR_W_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.FLOOR_W_D;
    g_dev.r4300.recomp.recomp_func = genfloor_w_d;
    recompile_standard_cf_type();
}

static void RCVT_S_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_S_D;
    g_dev.r4300.recomp.recomp_func = gencvt_s_d;
    recompile_standard_cf_type();
}

static void RCVT_W_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_W_D;
    g_dev.r4300.recomp.recomp_func = gencvt_w_d;
    recompile_standard_cf_type();
}

static void RCVT_L_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_L_D;
    g_dev.r4300.recomp.recomp_func = gencvt_l_d;
    recompile_standard_cf_type();
}

static void RC_F_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_F_D;
    g_dev.r4300.recomp.recomp_func = genc_f_d;
    recompile_standard_cf_type();
}

static void RC_UN_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_UN_D;
    g_dev.r4300.recomp.recomp_func = genc_un_d;
    recompile_standard_cf_type();
}

static void RC_EQ_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_EQ_D;
    g_dev.r4300.recomp.recomp_func = genc_eq_d;
    recompile_standard_cf_type();
}

static void RC_UEQ_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_UEQ_D;
    g_dev.r4300.recomp.recomp_func = genc_ueq_d;
    recompile_standard_cf_type();
}

static void RC_OLT_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_OLT_D;
    g_dev.r4300.recomp.recomp_func = genc_olt_d;
    recompile_standard_cf_type();
}

static void RC_ULT_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_ULT_D;
    g_dev.r4300.recomp.recomp_func = genc_ult_d;
    recompile_standard_cf_type();
}

static void RC_OLE_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_OLE_D;
    g_dev.r4300.recomp.recomp_func = genc_ole_d;
    recompile_standard_cf_type();
}

static void RC_ULE_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_ULE_D;
    g_dev.r4300.recomp.recomp_func = genc_ule_d;
    recompile_standard_cf_type();
}

static void RC_SF_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_SF_D;
    g_dev.r4300.recomp.recomp_func = genc_sf_d;
    recompile_standard_cf_type();
}

static void RC_NGLE_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGLE_D;
    g_dev.r4300.recomp.recomp_func = genc_ngle_d;
    recompile_standard_cf_type();
}

static void RC_SEQ_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_SEQ_D;
    g_dev.r4300.recomp.recomp_func = genc_seq_d;
    recompile_standard_cf_type();
}

static void RC_NGL_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGL_D;
    g_dev.r4300.recomp.recomp_func = genc_ngl_d;
    recompile_standard_cf_type();
}

static void RC_LT_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_LT_D;
    g_dev.r4300.recomp.recomp_func = genc_lt_d;
    recompile_standard_cf_type();
}

static void RC_NGE_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGE_D;
    g_dev.r4300.recomp.recomp_func = genc_nge_d;
    recompile_standard_cf_type();
}

static void RC_LE_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_LE_D;
    g_dev.r4300.recomp.recomp_func = genc_le_d;
    recompile_standard_cf_type();
}

static void RC_NGT_D(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.C_NGT_D;
    g_dev.r4300.recomp.recomp_func = genc_ngt_d;
    recompile_standard_cf_type();
}

static void (*const recomp_d[64])(void) =
{
    RADD_D    , RSUB_D    , RMUL_D   , RDIV_D    , RSQRT_D   , RABS_D    , RMOV_D   , RNEG_D    ,
    RROUND_L_D, RTRUNC_L_D, RCEIL_L_D, RFLOOR_L_D, RROUND_W_D, RTRUNC_W_D, RCEIL_W_D, RFLOOR_W_D,
    RSV       , RSV       , RSV      , RSV       , RSV       , RSV       , RSV      , RSV       ,
    RSV       , RSV       , RSV      , RSV       , RSV       , RSV       , RSV      , RSV       ,
    RCVT_S_D  , RSV       , RSV      , RSV       , RCVT_W_D  , RCVT_L_D  , RSV      , RSV       ,
    RSV       , RSV       , RSV      , RSV       , RSV       , RSV       , RSV      , RSV       ,
    RC_F_D    , RC_UN_D   , RC_EQ_D  , RC_UEQ_D  , RC_OLT_D  , RC_ULT_D  , RC_OLE_D , RC_ULE_D  ,
    RC_SF_D   , RC_NGLE_D , RC_SEQ_D , RC_NGL_D  , RC_LT_D   , RC_NGE_D  , RC_LE_D  , RC_NGT_D
};

//-------------------------------------------------------------------------
//                                     W                                   
//-------------------------------------------------------------------------

static void RCVT_S_W(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_S_W;
    g_dev.r4300.recomp.recomp_func = gencvt_s_w;
    recompile_standard_cf_type();
}

static void RCVT_D_W(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_D_W;
    g_dev.r4300.recomp.recomp_func = gencvt_d_w;
    recompile_standard_cf_type();
}

static void (*const recomp_w[64])(void) =
{
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RCVT_S_W, RCVT_D_W, RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV
};

//-------------------------------------------------------------------------
//                                     L                                   
//-------------------------------------------------------------------------

static void RCVT_S_L(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_S_L;
    g_dev.r4300.recomp.recomp_func = gencvt_s_l;
    recompile_standard_cf_type();
}

static void RCVT_D_L(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CVT_D_L;
    g_dev.r4300.recomp.recomp_func = gencvt_d_l;
    recompile_standard_cf_type();
}

static void (*const recomp_l[64])(void) =
{
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV,
    RCVT_S_L, RCVT_D_L, RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
    RSV     , RSV     , RSV, RSV, RSV, RSV, RSV, RSV, 
};

//-------------------------------------------------------------------------
//                                    COP1                                 
//-------------------------------------------------------------------------

static void RMFC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MFC1;
    g_dev.r4300.recomp.recomp_func = genmfc1;
    recompile_standard_r_type();
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
    if (g_dev.r4300.recomp.dst->f.r.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDMFC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DMFC1;
    g_dev.r4300.recomp.recomp_func = gendmfc1;
    recompile_standard_r_type();
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
    if (g_dev.r4300.recomp.dst->f.r.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RCFC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CFC1;
    g_dev.r4300.recomp.recomp_func = gencfc1;
    recompile_standard_r_type();
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
    if (g_dev.r4300.recomp.dst->f.r.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RMTC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.MTC1;
    recompile_standard_r_type();
    g_dev.r4300.recomp.recomp_func = genmtc1;
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
}

static void RDMTC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DMTC1;
    recompile_standard_r_type();
    g_dev.r4300.recomp.recomp_func = gendmtc1;
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
}

static void RCTC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CTC1;
    recompile_standard_r_type();
    g_dev.r4300.recomp.recomp_func = genctc1;
    g_dev.r4300.recomp.dst->f.r.nrd = (g_dev.r4300.recomp.src >> 11) & 0x1F;
}

static void RBC(void)
{
    recomp_bc[((g_dev.r4300.recomp.src >> 16) & 3)]();
}

static void RS(void)
{
    recomp_s[(g_dev.r4300.recomp.src & 0x3F)]();
}

static void RD(void)
{
    recomp_d[(g_dev.r4300.recomp.src & 0x3F)]();
}

static void RW(void)
{
    recomp_w[(g_dev.r4300.recomp.src & 0x3F)]();
}

static void RL(void)
{
    recomp_l[(g_dev.r4300.recomp.src & 0x3F)]();
}

static void (*const recomp_cop1[32])(void) =
{
    RMFC1, RDMFC1, RCFC1, RSV, RMTC1, RDMTC1, RCTC1, RSV,
    RBC  , RSV   , RSV  , RSV, RSV  , RSV   , RSV  , RSV,
    RS   , RD    , RSV  , RSV, RW   , RL    , RSV  , RSV,
    RSV  , RSV   , RSV  , RSV, RSV  , RSV   , RSV  , RSV
};

//-------------------------------------------------------------------------
//                                   R4300                                 
//-------------------------------------------------------------------------

static void RSPECIAL(void)
{
    recomp_special[(g_dev.r4300.recomp.src & 0x3F)]();
}

static void RREGIMM(void)
{
    recomp_regimm[((g_dev.r4300.recomp.src >> 16) & 0x1F)]();
}

static void RJ(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.J;
    g_dev.r4300.recomp.recomp_func = genj;
    recompile_standard_j_type();
    target = (g_dev.r4300.recomp.dst->f.j.inst_index<<2) | (g_dev.r4300.recomp.dst->addr & UINT32_C(0xF0000000));
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.J_IDLE;
            g_dev.r4300.recomp.recomp_func = genj_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.J_OUT;
        g_dev.r4300.recomp.recomp_func = genj_out;
    }
}

static void RJAL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.JAL;
    g_dev.r4300.recomp.recomp_func = genjal;
    recompile_standard_j_type();
    target = (g_dev.r4300.recomp.dst->f.j.inst_index<<2) | (g_dev.r4300.recomp.dst->addr & UINT32_C(0xF0000000));
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.JAL_IDLE;
            g_dev.r4300.recomp.recomp_func = genjal_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.JAL_OUT;
        g_dev.r4300.recomp.recomp_func = genjal_out;
    }
}

static void RBEQ(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BEQ;
    g_dev.r4300.recomp.recomp_func = genbeq;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BEQ_IDLE;
            g_dev.r4300.recomp.recomp_func = genbeq_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BEQ_OUT;
        g_dev.r4300.recomp.recomp_func = genbeq_out;
    }
}

static void RBNE(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BNE;
    g_dev.r4300.recomp.recomp_func = genbne;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BNE_IDLE;
            g_dev.r4300.recomp.recomp_func = genbne_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BNE_OUT;
        g_dev.r4300.recomp.recomp_func = genbne_out;
    }
}

static void RBLEZ(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLEZ;
    g_dev.r4300.recomp.recomp_func = genblez;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLEZ_IDLE;
            g_dev.r4300.recomp.recomp_func = genblez_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLEZ_OUT;
        g_dev.r4300.recomp.recomp_func = genblez_out;
    }
}

static void RBGTZ(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGTZ;
    g_dev.r4300.recomp.recomp_func = genbgtz;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGTZ_IDLE;
            g_dev.r4300.recomp.recomp_func = genbgtz_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGTZ_OUT;
        g_dev.r4300.recomp.recomp_func = genbgtz_out;
    }
}

static void RADDI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ADDI;
    g_dev.r4300.recomp.recomp_func = genaddi;
    recompile_standard_i_type();
    if(g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RADDIU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ADDIU;
    g_dev.r4300.recomp.recomp_func = genaddiu;
    recompile_standard_i_type();
    if(g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSLTI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SLTI;
    g_dev.r4300.recomp.recomp_func = genslti;
    recompile_standard_i_type();
    if(g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSLTIU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SLTIU;
    g_dev.r4300.recomp.recomp_func = gensltiu;
    recompile_standard_i_type();
    if(g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RANDI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ANDI;
    g_dev.r4300.recomp.recomp_func = genandi;
    recompile_standard_i_type();
    if(g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RORI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.ORI;
    g_dev.r4300.recomp.recomp_func = genori;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RXORI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.XORI;
    g_dev.r4300.recomp.recomp_func = genxori;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLUI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LUI;
    g_dev.r4300.recomp.recomp_func = genlui;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RCOP0(void)
{
    recomp_cop0[((g_dev.r4300.recomp.src >> 21) & 0x1F)]();
}

static void RCOP1(void)
{
    recomp_cop1[((g_dev.r4300.recomp.src >> 21) & 0x1F)]();
}

static void RBEQL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BEQL;
    g_dev.r4300.recomp.recomp_func = genbeql;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BEQL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbeql_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BEQL_OUT;
        g_dev.r4300.recomp.recomp_func = genbeql_out;
    }
}

static void RBNEL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BNEL;
    g_dev.r4300.recomp.recomp_func = genbnel;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BNEL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbnel_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BNEL_OUT;
        g_dev.r4300.recomp.recomp_func = genbnel_out;
    }
}

static void RBLEZL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLEZL;
    g_dev.r4300.recomp.recomp_func = genblezl;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLEZL_IDLE;
            g_dev.r4300.recomp.recomp_func = genblezl_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BLEZL_OUT;
        g_dev.r4300.recomp.recomp_func = genblezl_out;
    }
}

static void RBGTZL(void)
{
    uint32_t target;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGTZL;
    g_dev.r4300.recomp.recomp_func = genbgtzl;
    recompile_standard_i_type();
    target = g_dev.r4300.recomp.dst->addr + g_dev.r4300.recomp.dst->f.i.immediate*4 + 4;
    if (target == g_dev.r4300.recomp.dst->addr)
    {
        if (g_dev.r4300.recomp.check_nop)
        {
            g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGTZL_IDLE;
            g_dev.r4300.recomp.recomp_func = genbgtzl_idle;
        }
    }
    else if (target < g_dev.r4300.recomp.dst_block->start || target >= g_dev.r4300.recomp.dst_block->end || g_dev.r4300.recomp.dst->addr == (g_dev.r4300.recomp.dst_block->end-4))
    {
        g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.BGTZL_OUT;
        g_dev.r4300.recomp.recomp_func = genbgtzl_out;
    }
}

static void RDADDI(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DADDI;
    g_dev.r4300.recomp.recomp_func = gendaddi;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RDADDIU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.DADDIU;
    g_dev.r4300.recomp.recomp_func = gendaddiu;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLDL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LDL;
    g_dev.r4300.recomp.recomp_func = genldl;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLDR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LDR;
    g_dev.r4300.recomp.recomp_func = genldr;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLB(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LB;
    g_dev.r4300.recomp.recomp_func = genlb;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLH(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LH;
    g_dev.r4300.recomp.recomp_func = genlh;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLWL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LWL;
    g_dev.r4300.recomp.recomp_func = genlwl;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLW(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LW;
    g_dev.r4300.recomp.recomp_func = genlw;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLBU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LBU;
    g_dev.r4300.recomp.recomp_func = genlbu;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLHU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LHU;
    g_dev.r4300.recomp.recomp_func = genlhu;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLWR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LWR;
    g_dev.r4300.recomp.recomp_func = genlwr;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLWU(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LWU;
    g_dev.r4300.recomp.recomp_func = genlwu;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSB(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SB;
    g_dev.r4300.recomp.recomp_func = gensb;
    recompile_standard_i_type();
}

static void RSH(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SH;
    g_dev.r4300.recomp.recomp_func = gensh;
    recompile_standard_i_type();
}

static void RSWL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SWL;
    g_dev.r4300.recomp.recomp_func = genswl;
    recompile_standard_i_type();
}

static void RSW(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SW;
    g_dev.r4300.recomp.recomp_func = gensw;
    recompile_standard_i_type();
}

static void RSDL(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SDL;
    g_dev.r4300.recomp.recomp_func = gensdl;
    recompile_standard_i_type();
}

static void RSDR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SDR;
    g_dev.r4300.recomp.recomp_func = gensdr;
    recompile_standard_i_type();
}

static void RSWR(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SWR;
    g_dev.r4300.recomp.recomp_func = genswr;
    recompile_standard_i_type();
}

static void RCACHE(void)
{
    g_dev.r4300.recomp.recomp_func = gencache;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.CACHE;
}

static void RLL(void)
{
    g_dev.r4300.recomp.recomp_func = genll;
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LL;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RLWC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LWC1;
    g_dev.r4300.recomp.recomp_func = genlwc1;
    recompile_standard_lf_type();
}

static void RLLD(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
    recompile_standard_i_type();
}

static void RLDC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LDC1;
    g_dev.r4300.recomp.recomp_func = genldc1;
    recompile_standard_lf_type();
}

static void RLD(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.LD;
    g_dev.r4300.recomp.recomp_func = genld;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSC(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SC;
    g_dev.r4300.recomp.recomp_func = gensc;
    recompile_standard_i_type();
    if (g_dev.r4300.recomp.dst->f.i.rt == r4300_regs(&g_dev.r4300)) RNOP();
}

static void RSWC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SWC1;
    g_dev.r4300.recomp.recomp_func = genswc1;
    recompile_standard_lf_type();
}

static void RSCD(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.NI;
    g_dev.r4300.recomp.recomp_func = genni;
    recompile_standard_i_type();
}

static void RSDC1(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SDC1;
    g_dev.r4300.recomp.recomp_func = gensdc1;
    recompile_standard_lf_type();
}

static void RSD(void)
{
    g_dev.r4300.recomp.dst->ops = g_dev.r4300.current_instruction_table.SD;
    g_dev.r4300.recomp.recomp_func = gensd;
    recompile_standard_i_type();
}

static void (*const recomp_ops[64])(void) =
{
    RSPECIAL, RREGIMM, RJ   , RJAL  , RBEQ , RBNE , RBLEZ , RBGTZ ,
    RADDI   , RADDIU , RSLTI, RSLTIU, RANDI, RORI , RXORI , RLUI  ,
    RCOP0   , RCOP1  , RSV  , RSV   , RBEQL, RBNEL, RBLEZL, RBGTZL,
    RDADDI  , RDADDIU, RLDL , RLDR  , RSV  , RSV  , RSV   , RSV   ,
    RLB     , RLH    , RLWL , RLW   , RLBU , RLHU , RLWR  , RLWU  ,
    RSB     , RSH    , RSWL , RSW   , RSDL , RSDR , RSWR  , RCACHE,
    RLL     , RLWC1  , RSV  , RSV   , RLLD , RLDC1, RSV   , RLD   ,
    RSC     , RSWC1  , RSV  , RSV   , RSCD , RSDC1, RSV   , RSD
};

static int get_block_length(const struct precomp_block *block)
{
    return (block->end-block->start)/4;
}

static size_t get_block_memsize(const struct precomp_block *block)
{
    int length = get_block_length(block);
    return ((length+1)+(length>>2)) * sizeof(struct precomp_instr);
}

/**********************************************************************
 ******************** initialize an empty block ***********************
 **********************************************************************/
void init_block(struct r4300_core* r4300, struct precomp_block* block)
{
    int i, length, already_exist = 1;
    timed_section_start(TIMED_SECTION_COMPILER);
#ifdef DBG
    DebugMessage(M64MSG_INFO, "init block %" PRIX32 " - %" PRIX32, block->start, block->end);
#endif

    length = get_block_length(block);

    if (!block->block)
    {
        size_t memsize = get_block_memsize(block);
        if (r4300->emumode == EMUMODE_DYNAREC) {
            block->block = (struct precomp_instr *) malloc_exec(memsize);
            if (!block->block) {
                DebugMessage(M64MSG_ERROR, "Memory error: couldn't allocate executable memory for dynamic recompiler. Try to use an interpreter mode.");
                return;
            }
        }
        else {
            block->block = (struct precomp_instr *) malloc(memsize);
            if (!block->block) {
                DebugMessage(M64MSG_ERROR, "Memory error: couldn't allocate memory for cached interpreter.");
                return;
            }
        }

        memset(block->block, 0, memsize);
        already_exist = 0;
    }

    if (r4300->emumode == EMUMODE_DYNAREC)
    {
        if (!block->code)
        {
#if defined(PROFILE_R4300)
            r4300->recomp.max_code_length = 524288; /* allocate so much code space that we'll never have to realloc(), because this may */
            /* cause instruction locations to move, and break our profiling data                */
#else
            r4300->recomp.max_code_length = 32768;
#endif
            block->code = (unsigned char *) malloc_exec(r4300->recomp.max_code_length);
        }
        else
        {
            r4300->recomp.max_code_length = block->max_code_length;
        }
        r4300->recomp.code_length = 0;
        r4300->recomp.inst_pointer = &block->code;

        if (block->jumps_table)
        {
            free(block->jumps_table);
            block->jumps_table = NULL;
        }
        if (block->riprel_table)
        {
            free(block->riprel_table);
            block->riprel_table = NULL;
        }
        init_assembler(NULL, 0, NULL, 0);
        init_cache(block->block);
    }

    if (!already_exist)
    {
#if defined(PROFILE_R4300)
        r4300->recomp.pfProfile = fopen("instructionaddrs.dat", "ab");
        long x86addr = (long) block->code;
        int mipsop = -2; /* -2 == NOTCOMPILED block at beginning of x86 code */
        if (fwrite(&mipsop, 1, 4, r4300->recomp.pfProfile) != 4 || // write 4-byte MIPS opcode
                fwrite(&x86addr, 1, sizeof(char *), r4300->recomp.pfProfile) != sizeof(char *)) // write pointer to dynamically generated x86 code for this MIPS instruction
            DebugMessage(M64MSG_ERROR, "Error writing R4300 instruction address profiling data");
#endif

        for (i=0; i<length; i++)
        {
            r4300->recomp.dst = block->block + i;
            r4300->recomp.dst->addr = block->start + i*4;
            r4300->recomp.dst->reg_cache_infos.need_map = 0;
            r4300->recomp.dst->local_addr = r4300->recomp.code_length;
#ifdef COMPARE_CORE
            if (r4300->emumode == EMUMODE_DYNAREC) gendebug();
#endif
            RNOTCOMPILED();
            if (r4300->emumode == EMUMODE_DYNAREC) r4300->recomp.recomp_func();
        }
#if defined(PROFILE_R4300)
        fclose(r4300->recomp.pfProfile);
        r4300->recomp.pfProfile = NULL;
#endif
        r4300->recomp.init_length = r4300->recomp.code_length;
    }
    else
    {
#if defined(PROFILE_R4300)
        r4300->recomp.code_length = block->code_length; /* leave old instructions in their place */
#else
        r4300->recomp.code_length = r4300->recomp.init_length; /* recompile everything, overwrite old recompiled instructions */
#endif
        for (i=0; i<length; i++)
        {
            r4300->recomp.dst = block->block + i;
            r4300->recomp.dst->reg_cache_infos.need_map = 0;
            r4300->recomp.dst->local_addr = i * (r4300->recomp.init_length / length);
            r4300->recomp.dst->ops = r4300->current_instruction_table.NOTCOMPILED;
        }
    }

    if (r4300->emumode == EMUMODE_DYNAREC)
    {
        free_all_registers();
        /* calling pass2 of the assembler is not necessary here because all of the code emitted by
           gennotcompiled() and gendebug() is position-independent and contains no jumps . */
        block->code_length = r4300->recomp.code_length;
        block->max_code_length = r4300->recomp.max_code_length;
        free_assembler(&block->jumps_table, &block->jumps_number, &block->riprel_table, &block->riprel_number);
    }

    /* here we're marking the block as a valid code even if it's not compiled
     * yet as the game should have already set up the code correctly.
     */
    r4300->cached_interp.invalid_code[block->start>>12] = 0;
    if (block->end < UINT32_C(0x80000000) || block->start >= UINT32_C(0xc0000000))
    {
        uint32_t paddr = virtual_to_physical_address(r4300, block->start, 2);
        r4300->cached_interp.invalid_code[paddr>>12] = 0;
        if (!r4300->cached_interp.blocks[paddr>>12])
        {
            r4300->cached_interp.blocks[paddr>>12] = (struct precomp_block *) malloc(sizeof(struct precomp_block));
            r4300->cached_interp.blocks[paddr>>12]->code = NULL;
            r4300->cached_interp.blocks[paddr>>12]->block = NULL;
            r4300->cached_interp.blocks[paddr>>12]->jumps_table = NULL;
            r4300->cached_interp.blocks[paddr>>12]->riprel_table = NULL;
            r4300->cached_interp.blocks[paddr>>12]->start = paddr & ~UINT32_C(0xFFF);
            r4300->cached_interp.blocks[paddr>>12]->end = (paddr & ~UINT32_C(0xFFF)) + UINT32_C(0x1000);
        }
        init_block(r4300, r4300->cached_interp.blocks[paddr>>12]);

        paddr += block->end - block->start - 4;
        r4300->cached_interp.invalid_code[paddr>>12] = 0;
        if (!r4300->cached_interp.blocks[paddr>>12])
        {
            r4300->cached_interp.blocks[paddr>>12] = (struct precomp_block *) malloc(sizeof(struct precomp_block));
            r4300->cached_interp.blocks[paddr>>12]->code = NULL;
            r4300->cached_interp.blocks[paddr>>12]->block = NULL;
            r4300->cached_interp.blocks[paddr>>12]->jumps_table = NULL;
            r4300->cached_interp.blocks[paddr>>12]->riprel_table = NULL;
            r4300->cached_interp.blocks[paddr>>12]->start = paddr & ~UINT32_C(0xFFF);
            r4300->cached_interp.blocks[paddr>>12]->end = (paddr & ~UINT32_C(0xFFF)) + UINT32_C(0x1000);
        }
        init_block(r4300, r4300->cached_interp.blocks[paddr>>12]);
    }
    else
    {
        uint32_t alt_addr = block->start ^ UINT32_C(0x20000000);

        if (r4300->cached_interp.invalid_code[alt_addr>>12])
        {
            if (!r4300->cached_interp.blocks[alt_addr>>12])
            {
                r4300->cached_interp.blocks[alt_addr>>12] = (struct precomp_block *) malloc(sizeof(struct precomp_block));
                r4300->cached_interp.blocks[alt_addr>>12]->code = NULL;
                r4300->cached_interp.blocks[alt_addr>>12]->block = NULL;
                r4300->cached_interp.blocks[alt_addr>>12]->jumps_table = NULL;
                r4300->cached_interp.blocks[alt_addr>>12]->riprel_table = NULL;
                r4300->cached_interp.blocks[alt_addr>>12]->start = alt_addr & ~UINT32_C(0xFFF);
                r4300->cached_interp.blocks[alt_addr>>12]->end = (alt_addr & ~UINT32_C(0xFFF)) + UINT32_C(0x1000);
            }
            init_block(r4300, r4300->cached_interp.blocks[alt_addr>>12]);
        }
    }
    timed_section_end(TIMED_SECTION_COMPILER);
}

void free_block(struct r4300_core* r4300, struct precomp_block* block)
{
    size_t memsize = get_block_memsize(block);

    if (block->block) {
        if (r4300->emumode == EMUMODE_DYNAREC) {
            free_exec(block->block, memsize);
        }
        else {
            free(block->block);
        }
        block->block = NULL;
    }
    if (block->code) { free_exec(block->code, block->max_code_length); block->code = NULL; }
    if (block->jumps_table) { free(block->jumps_table); block->jumps_table = NULL; }
    if (block->riprel_table) { free(block->riprel_table); block->riprel_table = NULL; }
}

/**********************************************************************
 ********************* recompile a block of code **********************
 **********************************************************************/
void recompile_block(struct r4300_core* r4300, const uint32_t* source, struct precomp_block* block, uint32_t func)
{
    uint32_t i;
    int length, finished = 0;
    timed_section_start(TIMED_SECTION_COMPILER);
    length = (block->end-block->start)/4;
    r4300->recomp.dst_block = block;

    //for (i=0; i<16; i++) block->md5[i] = 0;
    block->adler32 = 0;

    if (r4300->emumode == EMUMODE_DYNAREC)
    {
        r4300->recomp.code_length = block->code_length;
        r4300->recomp.max_code_length = block->max_code_length;
        r4300->recomp.inst_pointer = &block->code;
        init_assembler(block->jumps_table, block->jumps_number, block->riprel_table, block->riprel_number);
        init_cache(block->block + (func & 0xFFF) / 4);
    }

#if defined(PROFILE_R4300)
    r4300->recomp.pfProfile = fopen("instructionaddrs.dat", "ab");
#endif

    for (i = (func & 0xFFF) / 4; finished != 2; i++)
    {
        if (block->start < UINT32_C(0x80000000) || UINT32_C(block->start >= 0xc0000000))
        {
            uint32_t address2 = virtual_to_physical_address(r4300, block->start + i*4, 0);
            if (r4300->cached_interp.blocks[address2>>12]->block[(address2&UINT32_C(0xFFF))/4].ops == r4300->current_instruction_table.NOTCOMPILED) {
                r4300->cached_interp.blocks[address2>>12]->block[(address2&UINT32_C(0xFFF))/4].ops = r4300->current_instruction_table.NOTCOMPILED2;
            }
        }

        r4300->recomp.SRC = source + i;
        r4300->recomp.src = source[i];
        r4300->recomp.check_nop = source[i+1] == 0;
        r4300->recomp.dst = block->block + i;
        r4300->recomp.dst->addr = block->start + i*4;
        r4300->recomp.dst->reg_cache_infos.need_map = 0;
        r4300->recomp.dst->local_addr = r4300->recomp.code_length;
#ifdef COMPARE_CORE
        if (r4300->emumode == EMUMODE_DYNAREC) { gendebug(); }
#endif
#if defined(PROFILE_R4300)
        long x86addr = (long) (block->code + block->block[i].local_addr);

        /* write 4-byte MIPS opcode, followed by a pointer to dynamically generated x86 code for
         * this MIPS instruction. */
        if (fwrite(source + i, 1, 4, r4300->recomp.pfProfile) != 4
        || fwrite(&x86addr, 1, sizeof(char *), r4300->recomp.pfProfile) != sizeof(char *)) {
            DebugMessage(M64MSG_ERROR, "Error writing R4300 instruction address profiling data");
        }
#endif
        r4300->recomp.recomp_func = NULL;
        recomp_ops[((r4300->recomp.src >> 26) & 0x3F)]();
        if (r4300->emumode == EMUMODE_DYNAREC) { r4300->recomp.recomp_func(); }
        r4300->recomp.dst = block->block + i;

        /*if ((r4300->recomp.dst+1)->ops != NOTCOMPILED && !r4300->recomp.delay_slot_compiled &&
          i < length)
          {
          if (r4300->emumode == EMUMODE_DYNAREC) genlink_subblock();
          finished = 2;
          }*/
        if (r4300->recomp.delay_slot_compiled)
        {
            r4300->recomp.delay_slot_compiled--;
            free_all_registers();
        }

        if (i >= length-2+(length>>2)) { finished = 2; }
        if (i >= (length-1) && (block->start == UINT32_C(0xa4000000) ||
                    block->start >= UINT32_C(0xc0000000) ||
                    block->end   <  UINT32_C(0x80000000))) { finished = 2; }
        if (r4300->recomp.dst->ops == r4300->current_instruction_table.ERET || finished == 1) { finished = 2; }
        if (/*i >= length && */
                (r4300->recomp.dst->ops == r4300->current_instruction_table.J ||
                 r4300->recomp.dst->ops == r4300->current_instruction_table.J_OUT ||
                 r4300->recomp.dst->ops == r4300->current_instruction_table.JR) &&
                !(i >= (length-1) && (block->start >= UINT32_C(0xc0000000) ||
                        block->end   <  UINT32_C(0x80000000)))) {
            finished = 1;
        }
    }

#if defined(PROFILE_R4300)
    long x86addr = (long) (block->code + r4300->recomp.code_length);
    int mipsop = -3; /* -3 == block-postfix */
    /* write 4-byte MIPS opcode, followed by a pointer to dynamically generated x86 code for
     * this MIPS instruction. */
    if (fwrite(&mipsop, 1, 4, r4300->recomp.pfProfile) != 4
    || fwrite(&x86addr, 1, sizeof(char *), r4300->recomp.pfProfile) != sizeof(char *)) {
        DebugMessage(M64MSG_ERROR, "Error writing R4300 instruction address profiling data");
    }
#endif

    if (i >= length)
    {
        r4300->recomp.dst = block->block + i;
        r4300->recomp.dst->addr = block->start + i*4;
        r4300->recomp.dst->reg_cache_infos.need_map = 0;
        r4300->recomp.dst->local_addr = r4300->recomp.code_length;
#ifdef COMPARE_CORE
        if (r4300->emumode == EMUMODE_DYNAREC) { gendebug(); }
#endif
        RFIN_BLOCK();
        if (r4300->emumode == EMUMODE_DYNAREC) { r4300->recomp.recomp_func(); }
        i++;
        if (i < length-1+(length>>2)) // useful when last opcode is a jump
        {
            r4300->recomp.dst = block->block + i;
            r4300->recomp.dst->addr = block->start + i*4;
            r4300->recomp.dst->reg_cache_infos.need_map = 0;
            r4300->recomp.dst->local_addr = r4300->recomp.code_length;
#ifdef COMPARE_CORE
            if (r4300->emumode == EMUMODE_DYNAREC) { gendebug(); }
#endif
            RFIN_BLOCK();
            if (r4300->emumode == EMUMODE_DYNAREC) { r4300->recomp.recomp_func(); }
            i++;
        }
    }
    else if (r4300->emumode == EMUMODE_DYNAREC) { genlink_subblock(); }

    if (r4300->emumode == EMUMODE_DYNAREC)
    {
        free_all_registers();
        passe2(block->block, (func&0xFFF)/4, i, block);
        block->code_length = r4300->recomp.code_length;
        block->max_code_length = r4300->recomp.max_code_length;
        free_assembler(&block->jumps_table, &block->jumps_number, &block->riprel_table, &block->riprel_number);
    }
#ifdef DBG
    DebugMessage(M64MSG_INFO, "block recompiled (%" PRIX32 "-%" PRIX32 ")", func, block->start+i*4);
#endif
#if defined(PROFILE_R4300)
    fclose(r4300->recomp.pfProfile);
    r4300->recomp.pfProfile = NULL;
#endif
    timed_section_end(TIMED_SECTION_COMPILER);
}

static int is_jump(const struct r4300_core* r4300)
{
    return
        (r4300->recomp.dst->ops == r4300->current_instruction_table.J ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.J_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.J_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.JAL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.JAL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.JAL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BEQ ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BEQ_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BEQ_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BNE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BNE_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BNE_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLEZ ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLEZ_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLEZ_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGTZ ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGTZ_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGTZ_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BEQL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BEQL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BEQL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BNEL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BNEL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BNEL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLEZL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLEZL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLEZL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGTZL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGTZL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGTZL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.JR ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.JALR ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZ ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZ_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZ_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZ ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZ_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZ_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZAL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZAL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZAL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZAL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZAL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZAL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZALL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZALL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BLTZALL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZALL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZALL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BGEZALL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1F ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1F_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1F_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1T ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1T_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1T_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1FL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1FL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1FL_IDLE ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1TL ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1TL_OUT ||
         r4300->recomp.dst->ops == r4300->current_instruction_table.BC1TL_IDLE);
}

/**********************************************************************
 ************ recompile only one opcode (use for delay slot) **********
 **********************************************************************/
void recompile_opcode(struct r4300_core* r4300)
{
    r4300->recomp.SRC++;
    r4300->recomp.src = *r4300->recomp.SRC;
    r4300->recomp.dst++;
    r4300->recomp.dst->addr = (r4300->recomp.dst-1)->addr + 4;
    r4300->recomp.dst->reg_cache_infos.need_map = 0;

    recomp_ops[((r4300->recomp.src >> 26) & 0x3F)]();
    if (!is_jump(r4300))
    {
#if defined(PROFILE_R4300)
        long x86addr = (long) ((*r4300->recomp.inst_pointer) + r4300->recomp.code_length);

        /* write 4-byte MIPS opcode, followed by a pointer to dynamically generated x86 code for
         * this MIPS instruction. */
        if (fwrite(&r4300->recomp.src, 1, 4, r4300->recomp.pfProfile) != 4
        || fwrite(&x86addr, 1, sizeof(char *), r4300->recomp.pfProfile) != sizeof(char *)) {
            DebugMessage(M64MSG_ERROR, "Error writing R4300 instruction address profiling data");
        }
#endif
        r4300->recomp.recomp_func = NULL;
        recomp_ops[((r4300->recomp.src >> 26) & 0x3F)]();
        if (r4300->emumode == EMUMODE_DYNAREC) { r4300->recomp.recomp_func(); }
    }
    else
    {
        RNOP();
        if (r4300->emumode == EMUMODE_DYNAREC) { r4300->recomp.recomp_func(); }
    }
    r4300->recomp.delay_slot_compiled = 2;
}

#if defined(PROFILE_R4300)
void profile_write_end_of_code_blocks(struct r4300_core* r4300)
{
    size_t i;

    r4300->recomp.pfProfile = fopen("instructionaddrs.dat", "ab");

    for (i = 0; i < 0x100000; ++i) {
        if (r4300->cached_interp.invalid_code[i] == 0 && r4300->cached_interp.blocks[i] != NULL && r4300->cached_interp.blocks[i]->code != NULL && r4300->cached_interp.blocks[i]->block != NULL)
        {
            unsigned char *x86addr;
            int mipsop;
            // store final code length for this block
            mipsop = -1; /* -1 == end of x86 code block */
            x86addr = r4300->cached_interp.blocks[i]->code + r4300->cached_interp.blocks[i]->code_length;
            if (fwrite(&mipsop, 1, 4, r4300->recomp.pfProfile) != 4 ||
                    fwrite(&x86addr, 1, sizeof(char *), r4300->recomp.pfProfile) != sizeof(char *))
                DebugMessage(M64MSG_ERROR, "Error writing R4300 instruction address profiling data");
        }
    }

    fclose(r4300->recomp.pfProfile);
    r4300->recomp.pfProfile = NULL;
}
#endif


/* Parameterless version of cached_interpreter_dynarec_jump_to to ease usage in dynarec. */
void dynarec_jump_to_address(void)
{
    cached_interpreter_dynarec_jump_to(&g_dev.r4300, g_dev.r4300.recomp.jump_to_address);
}

/* Parameterless version of exception_general to ease usage in dynarec. */
void dynarec_exception_general(void)
{
    exception_general(&g_dev.r4300);
}

/* Parameterless version of check_cop1_unusable to ease usage in dynarec. */
int dynarec_check_cop1_unusable(void)
{
    return check_cop1_unusable(&g_dev.r4300);
}


/* Parameterless version of cp0_update_count to ease usage in dynarec. */
void dynarec_cp0_update_count(void)
{
    cp0_update_count(&g_dev.r4300);
}

/**********************************************************************
 ************** allocate memory with executable bit set ***************
 **********************************************************************/
static void *malloc_exec(size_t size)
{
#if defined(WIN32)
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#elif defined(__GNUC__)

#ifndef  MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

    void *block = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED)
    { DebugMessage(M64MSG_ERROR, "Memory error: couldn't allocate %zi byte block of aligned RWX memory.", size); return NULL; }

    return block;
#else
    return malloc(size);
#endif
}

/**********************************************************************
 ************* reallocate memory with executable bit set **************
 **********************************************************************/
void *realloc_exec(void *ptr, size_t oldsize, size_t newsize)
{
    void* block = malloc_exec(newsize);
    if (block != NULL)
    {
        size_t copysize;
        copysize = (oldsize < newsize)
            ? oldsize
            : newsize;
        memcpy(block, ptr, copysize);
    }
    free_exec(ptr, oldsize);
    return block;
}

/**********************************************************************
 **************** frees memory with executable bit set ****************
 **********************************************************************/
static void free_exec(void *ptr, size_t length)
{
#if defined(WIN32)
    VirtualFree(ptr, 0, MEM_RELEASE);
#elif defined(__GNUC__)
    munmap(ptr, length);
#else
    free(ptr);
#endif
}
