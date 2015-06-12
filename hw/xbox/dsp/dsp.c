/*
    DSP56300 emulator

    Copyright (c) 2015 espes

    Adapted from Hatari DSP M56001 emulation
    (C) 2001-2008 ARAnyM developer team
    Adaption to Hatari (C) 2008 by Thomas Huth

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#include <stddef.h>

#include "dsp_cpu.h"
#include "dsp_dma.h"

#include "dsp.h"

/* Defines */
#define BITMASK(x)  ((1<<(x))-1)
#define ARRAYSIZE(x) (int)(sizeof(x)/sizeof(x[0]))

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member));})
#endif


#define DPRINTF(s, ...) fprintf(stderr, s, ## __VA_ARGS__)

struct DSPState {
    dsp_core_t core;
    DSPDMAState dma;
    int save_cycles;
};

static uint32_t read_peripheral(dsp_core_t* core, uint32_t address);
static void write_peripheral(dsp_core_t* core, uint32_t address, uint32_t value);

DSPState* dsp_init(void)
{
    DPRINTF("dsp_init\n");

    DSPState* dsp = (DSPState*)malloc(sizeof(DSPState));
    memset(dsp, 0, sizeof(*dsp));

    dsp->core.read_peripheral = read_peripheral;
    dsp->core.write_peripheral = write_peripheral;

    dsp_reset(dsp);
    return dsp;
}

void dsp_reset(DSPState* dsp)
{
    dsp56k_reset_cpu(&dsp->core);
    dsp->save_cycles = 0;
}

void dsp_destroy(DSPState* dsp)
{
    free(dsp);
}

static uint32_t read_peripheral(dsp_core_t* core, uint32_t address) {
    DSPState* dsp = container_of(core, DSPState, core);

    printf("read_peripheral 0x%06x\n", address);

    switch(address) {
    // case 0xFFFFC5: // interrupt
    case 0xFFFFD6:
        return dsp_dma_read(&dsp->dma, DMA_CONTROL);
    }

    return 0xababa;
}

static void write_peripheral(dsp_core_t* core, uint32_t address, uint32_t value) {
    DSPState* dsp = container_of(core, DSPState, core);

    printf("write_peripheral [0x%06x] = 0x%06x\n", address, value);

    switch(address) {
    // case 0xFFFFC5: // interrupt
    case 0xFFFFD6:
        return dsp_dma_write(&dsp->dma, DMA_CONTROL, value);
    }
}


void dsp_step(DSPState* dsp)
{
    dsp56k_execute_instruction(&dsp->core);
}

void dsp_run(DSPState* dsp, int cycles)
{
    dsp->save_cycles += cycles;

    if (dsp->save_cycles <= 0) return;

    // if (unlikely(bDspDebugging)) {
    //     while (dsp->core.save_cycles > 0)
    //     {
    //         dsp56k_execute_instruction();
    //         dsp->core.save_cycles -= dsp->core.instr_cycle;
    //         DebugDsp_Check();
    //     }
    // } else {
    //  fprintf(stderr, "--> %d\n", dsp->core.save_cycles);
    while (dsp->save_cycles > 0)
    {
        dsp56k_execute_instruction(&dsp->core);
        dsp->save_cycles -= dsp->core.instr_cycle;
    }

} 

void dsp_bootstrap(DSPState* dsp, const uint32_t* pram, size_t len)
{
    assert(sizeof(dsp->core.pram) >= len);
    memcpy(dsp->core.pram, pram, len);
}


/**
 * Disassemble DSP code between given addresses, return next PC address
 */
uint32_t dsp_disasm_address(DSPState* dsp, FILE *out, uint32_t lowerAdr, uint32_t UpperAdr)
{
    uint32_t dsp_pc;

    for (dsp_pc=lowerAdr; dsp_pc<=UpperAdr; dsp_pc++) {
        dsp_pc += dsp56k_execute_one_disasm_instruction(&dsp->core, out, dsp_pc);
    }
    return dsp_pc;
}


/**
 * Get the value from the given (16-bit) DSP memory address / space
 * exactly the same way as in dsp_cpu.c::read_memory() (except for
 * the host/transmit peripheral register values which access has
 * side-effects). Set the mem_str to suitable string for that
 * address / space.
 * Return the value at given address. For valid values AND the return
 * value with BITMASK(24).
 */
uint32_t dsp_read_memory(DSPState* dsp, uint32_t address, char space_id, const char **mem_str)
{
    int space;

    switch (space_id) {
    case 'X':
        space = DSP_SPACE_X;
        if (address >= DSP_PERIPH_BASE) {
            *mem_str = "X periph";
        } else {
            *mem_str = "X ram";
        }
        break;
    case 'Y':
        space = DSP_SPACE_Y;
        *mem_str = "Y ram";
        break;
    case 'P':
        space = DSP_SPACE_P;
        *mem_str = "P ram";
        break;
    default:
        assert(false);
    }

    return dsp56k_read_memory(&dsp->core, space, address);
}


/**
 * Output memory values between given addresses in given DSP address space.
 * Return next DSP address value.
 */
uint32_t dsp_disasm_memory(DSPState* dsp, uint32_t dsp_memdump_addr, uint32_t dsp_memdump_upper, char space)
{
    uint32_t mem, value;
    const char *mem_str;

    for (mem = dsp_memdump_addr; mem <= dsp_memdump_upper; mem++) {
        value = dsp_read_memory(dsp, mem, space, &mem_str);
        fprintf(stderr,"%s:%04x  %06x\n", mem_str, mem, value);
    }
    return dsp_memdump_upper+1;
}

/**
 * Show information on DSP core state which isn't
 * shown by any of the other commands (dd, dm, dr).
 */
void dsp_info(DSPState* dsp)
{
    int i, j;
    const char *stackname[] = { "SSH", "SSL" };

    fputs("DSP core information:\n", stderr);

    for (i = 0; i < ARRAYSIZE(stackname); i++) {
        fprintf(stderr, "- %s stack:", stackname[i]);
        for (j = 0; j < ARRAYSIZE(dsp->core.stack[0]); j++) {
            fprintf(stderr, " %04x", dsp->core.stack[i][j]);
        }
        fputs("\n", stderr);
    }

    fprintf(stderr, "- Interrupt IPL:");
    for (i = 0; i < ARRAYSIZE(dsp->core.interrupt_ipl); i++) {
        fprintf(stderr, " %04x", dsp->core.interrupt_ipl[i]);
    }
    fputs("\n", stderr);

    fprintf(stderr, "- Pending ints: ");
    for (i = 0; i < ARRAYSIZE(dsp->core.interrupt_is_pending); i++) {
        fprintf(stderr, " %04hx", dsp->core.interrupt_is_pending[i]);
    }
    fputs("\n", stderr);
}

/**
 * Show DSP register contents
 */
void dsp_print_registers(DSPState* dsp)
{
    uint32_t i;

    fprintf(stderr,"A: A2: %02x  A1: %06x  A0: %06x\n",
        dsp->core.registers[DSP_REG_A2], dsp->core.registers[DSP_REG_A1], dsp->core.registers[DSP_REG_A0]);
    fprintf(stderr,"B: B2: %02x  B1: %06x  B0: %06x\n",
        dsp->core.registers[DSP_REG_B2], dsp->core.registers[DSP_REG_B1], dsp->core.registers[DSP_REG_B0]);
    
    fprintf(stderr,"X: X1: %06x  X0: %06x\n", dsp->core.registers[DSP_REG_X1], dsp->core.registers[DSP_REG_X0]);
    fprintf(stderr,"Y: Y1: %06x  Y0: %06x\n", dsp->core.registers[DSP_REG_Y1], dsp->core.registers[DSP_REG_Y0]);

    for (i=0; i<8; i++) {
        fprintf(stderr,"R%01x: %04x   N%01x: %04x   M%01x: %04x\n", 
            i, dsp->core.registers[DSP_REG_R0+i],
            i, dsp->core.registers[DSP_REG_N0+i],
            i, dsp->core.registers[DSP_REG_M0+i]);
    }

    fprintf(stderr,"LA: %04x   LC: %04x   PC: %04x\n", dsp->core.registers[DSP_REG_LA], dsp->core.registers[DSP_REG_LC], dsp->core.pc);
    fprintf(stderr,"SR: %04x  OMR: %02x\n", dsp->core.registers[DSP_REG_SR], dsp->core.registers[DSP_REG_OMR]);
    fprintf(stderr,"SP: %02x    SSH: %04x  SSL: %04x\n", 
        dsp->core.registers[DSP_REG_SP], dsp->core.registers[DSP_REG_SSH], dsp->core.registers[DSP_REG_SSL]);
}


/**
 * Get given DSP register address and required bit mask.
 * Works for A0-2, B0-2, LA, LC, M0-7, N0-7, R0-7, X0-1, Y0-1, PC, SR, SP,
 * OMR, SSH & SSL registers, but note that the SP, SSH & SSL registers
 * need special handling (in DSP*SetRegister()) when they are set.
 * Return the register width in bits or zero for an error.
 */
int dsp_get_register_address(DSPState* dsp, const char *regname, uint32_t **addr, uint32_t *mask)
{
#define MAX_REGNAME_LEN 4
    typedef struct {
        const char name[MAX_REGNAME_LEN];
        uint32_t *addr;
        size_t bits;
        uint32_t mask;
    } reg_addr_t;
    
    /* sorted by name so that this can be bisected */
    const reg_addr_t registers[] = {

        /* 56-bit A register */
        { "A0",  &dsp->core.registers[DSP_REG_A0],  32, BITMASK(24) },
        { "A1",  &dsp->core.registers[DSP_REG_A1],  32, BITMASK(24) },
        { "A2",  &dsp->core.registers[DSP_REG_A2],  32, BITMASK(8) },

        /* 56-bit B register */
        { "B0",  &dsp->core.registers[DSP_REG_B0],  32, BITMASK(24) },
        { "B1",  &dsp->core.registers[DSP_REG_B1],  32, BITMASK(24) },
        { "B2",  &dsp->core.registers[DSP_REG_B2],  32, BITMASK(8) },

        /* 16-bit LA & LC registers */
        { "LA",  &dsp->core.registers[DSP_REG_LA],  32, BITMASK(16) },
        { "LC",  &dsp->core.registers[DSP_REG_LC],  32, BITMASK(16) },

        /* 16-bit M registers */
        { "M0",  &dsp->core.registers[DSP_REG_M0],  32, BITMASK(16) },
        { "M1",  &dsp->core.registers[DSP_REG_M1],  32, BITMASK(16) },
        { "M2",  &dsp->core.registers[DSP_REG_M2],  32, BITMASK(16) },
        { "M3",  &dsp->core.registers[DSP_REG_M3],  32, BITMASK(16) },
        { "M4",  &dsp->core.registers[DSP_REG_M4],  32, BITMASK(16) },
        { "M5",  &dsp->core.registers[DSP_REG_M5],  32, BITMASK(16) },
        { "M6",  &dsp->core.registers[DSP_REG_M6],  32, BITMASK(16) },
        { "M7",  &dsp->core.registers[DSP_REG_M7],  32, BITMASK(16) },

        /* 16-bit N registers */
        { "N0",  &dsp->core.registers[DSP_REG_N0],  32, BITMASK(16) },
        { "N1",  &dsp->core.registers[DSP_REG_N1],  32, BITMASK(16) },
        { "N2",  &dsp->core.registers[DSP_REG_N2],  32, BITMASK(16) },
        { "N3",  &dsp->core.registers[DSP_REG_N3],  32, BITMASK(16) },
        { "N4",  &dsp->core.registers[DSP_REG_N4],  32, BITMASK(16) },
        { "N5",  &dsp->core.registers[DSP_REG_N5],  32, BITMASK(16) },
        { "N6",  &dsp->core.registers[DSP_REG_N6],  32, BITMASK(16) },
        { "N7",  &dsp->core.registers[DSP_REG_N7],  32, BITMASK(16) },

        { "OMR", &dsp->core.registers[DSP_REG_OMR], 32, 0x5f },

        /* 16-bit program counter */
        { "PC",  (uint32_t*)(&dsp->core.pc),  24, BITMASK(24) },

        /* 16-bit DSP R (address) registers */
        { "R0",  &dsp->core.registers[DSP_REG_R0],  32, BITMASK(16) },
        { "R1",  &dsp->core.registers[DSP_REG_R1],  32, BITMASK(16) },
        { "R2",  &dsp->core.registers[DSP_REG_R2],  32, BITMASK(16) },
        { "R3",  &dsp->core.registers[DSP_REG_R3],  32, BITMASK(16) },
        { "R4",  &dsp->core.registers[DSP_REG_R4],  32, BITMASK(16) },
        { "R5",  &dsp->core.registers[DSP_REG_R5],  32, BITMASK(16) },
        { "R6",  &dsp->core.registers[DSP_REG_R6],  32, BITMASK(16) },
        { "R7",  &dsp->core.registers[DSP_REG_R7],  32, BITMASK(16) },

        { "SSH", &dsp->core.registers[DSP_REG_SSH], 32, BITMASK(16) },
        { "SSL", &dsp->core.registers[DSP_REG_SSL], 32, BITMASK(16) },
        { "SP",  &dsp->core.registers[DSP_REG_SP],  32, BITMASK(6) },

        /* 16-bit status register */
        { "SR",  &dsp->core.registers[DSP_REG_SR],  32, 0xefff },

        /* 48-bit X register */
        { "X0",  &dsp->core.registers[DSP_REG_X0],  32, BITMASK(24) },
        { "X1",  &dsp->core.registers[DSP_REG_X1],  32, BITMASK(24) },

        /* 48-bit Y register */
        { "Y0",  &dsp->core.registers[DSP_REG_Y0],  32, BITMASK(24) },
        { "Y1",  &dsp->core.registers[DSP_REG_Y1],  32, BITMASK(24) }
    };
    /* left, right, middle, direction */
    int l, r, m, dir = 0;
    unsigned int i, len;
    char reg[MAX_REGNAME_LEN];

    for (i = 0; i < sizeof(reg) && regname[i]; i++) {
        reg[i] = toupper(regname[i]);
    }
    if (i < 2 || regname[i]) {
        /* too short or longer than any of the names */
        return 0;
    }
    len = i;
    
    /* bisect */
    l = 0;
    r = ARRAYSIZE(registers) - 1;
    do {
        m = (l+r) >> 1;
        for (i = 0; i < len; i++) {
            dir = (int)reg[i] - registers[m].name[i];
            if (dir) {
                break;
            }
        }
        if (dir == 0) {
            *addr = registers[m].addr;
            *mask = registers[m].mask;
            return registers[m].bits;
        }
        if (dir < 0) {
            r = m-1;
        } else {
            l = m+1;
        }
    } while (l <= r);
#undef MAX_REGNAME_LEN
    return 0;
}


/**
 * Set given DSP register value, return false if unknown register given
 */
bool dsp_disasm_set_register(DSPState* dsp, const char *arg, uint32_t value)
{
    uint32_t *addr, mask, sp_value;
    int bits;

    /* first check registers needing special handling... */
    if (arg[0]=='S' || arg[0]=='s') {
        if (arg[1]=='P' || arg[1]=='p') {
            dsp->core.registers[DSP_REG_SP] = value & BITMASK(6);
            value &= BITMASK(4); 
            dsp->core.registers[DSP_REG_SSH] = dsp->core.stack[0][value];
            dsp->core.registers[DSP_REG_SSL] = dsp->core.stack[1][value];
            return true;
        }
        if (arg[1]=='S' || arg[1]=='s') {
            sp_value = dsp->core.registers[DSP_REG_SP] & BITMASK(4);
            if (arg[2]=='H' || arg[2]=='h') {
                if (sp_value == 0) {
                    dsp->core.registers[DSP_REG_SSH] = 0;
                    dsp->core.stack[0][sp_value] = 0;
                } else {
                    dsp->core.registers[DSP_REG_SSH] = value & BITMASK(16);
                    dsp->core.stack[0][sp_value] = value & BITMASK(16);
                }
                return true;
            }
            if (arg[2]=='L' || arg[2]=='l') {
                if (sp_value == 0) {
                    dsp->core.registers[DSP_REG_SSL] = 0;
                    dsp->core.stack[1][sp_value] = 0;
                } else {
                    dsp->core.registers[DSP_REG_SSL] = value & BITMASK(16);
                    dsp->core.stack[1][sp_value] = value & BITMASK(16);
                }
                return true;
            }
        }
    }

    /* ...then registers where address & mask are enough */
    bits = dsp_get_register_address(dsp, arg, &addr, &mask);
    switch (bits) {
    case 32:
        *addr = value & mask;
        return true;
    case 16:
        *(uint16_t*)addr = value & mask;
        return true;
    }
    return false;
}
