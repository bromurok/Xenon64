#include <stdio.h>

// Input plugin stubs
void readController(int Control, unsigned char *Command) { }
void controllerCommand(int Control, unsigned char *Command) { }

// Audio plugin stubs
void romOpen_audio(void) { }
void romClosed_audio(void) { }
void processAList(void) { }
void initiateAudio(void *AudioInfo) { }
void aiLenChanged(void) { }
void aiDacrateChanged(int SystemType) { }

// Misc core stubs
int failsafeRec = 0;
unsigned char inc_about[1] = {0};
int fast_memory = 1;
char ucodeNames_GBI1[256] = "GBI1";
char ucodeNames_GBI2[256] = "GBI2";
void messagebox(const char *msg) { }

// Libc compat wrappers para XDK
#include <string.h>
#include <math.h>
#include <time.h>

int strcasecmp(const char *a, const char *b) { return _stricmp(a, b); }
void *memalign(size_t alignment, size_t size) {
    extern void *malloc(size_t);
    return malloc(size);
}
struct tm *localtime_r(const time_t *timer, struct tm *buf) { struct tm *t = localtime(timer); if (t) *buf = *t; return buf; }
int my_isnan(double x) { return _isnan(x); }


void dyna_start(void) { }


#include "r4300/r4300.h"

extern void (*interp_ops[64])(void);
extern unsigned int op;

void prefetch_opcode(unsigned long instr)
{
    unsigned int op_code = (instr >> 26) & 0x3F;
    unsigned int rs = (instr >> 21) & 0x1F;
    unsigned int rt = (instr >> 16) & 0x1F;
    unsigned int rd = (instr >> 11) & 0x1F;
    unsigned int sa = (instr >> 6) & 0x1F;
    unsigned int sub;

    op = instr;

    switch (op_code)
    {
    case 0: // SPECIAL (R-type)
        PC->f.r.rs = &reg[rs];
        PC->f.r.rt = &reg[rt];
        PC->f.r.rd = &reg[rd];
        PC->f.r.sa = (unsigned char)sa;
        PC->f.r.nrd = (unsigned char)rd;
        break;

    case 2: case 3: // J, JAL
        PC->f.j.inst_index = instr & 0x3FFFFFF;
        break;

    case 16: // COP0
        PC->f.r.rs = &reg[rs];
        PC->f.r.rt = &reg[rt];
        PC->f.r.rd = &reg[rd];
        PC->f.r.nrd = (unsigned char)rd;
        break;

    case 17: // COP1
        sub = (instr >> 21) & 0x1F;
        if (sub == 8) // BC1F, BC1T, BC1FL, BC1TL
        {
            PC->f.i.immediate = (short)(instr & 0xFFFF);
        }
        else if (sub <= 6) // MFC1, DMFC1, CFC1, MTC1, DMTC1, CTC1
        {
            PC->f.r.rt = &reg[rt];
            PC->f.r.nrd = (unsigned char)rd;
        }
        else // COP1 S, D, W, L
        {
            PC->f.cf.ft = (unsigned char)rt;
            PC->f.cf.fs = (unsigned char)rd;
            PC->f.cf.fd = (unsigned char)sa;
        }
        break;

    case 0x31: // LWC1
    case 0x35: // LDC1
    case 0x39: // SWC1
    case 0x3D: // SDC1
        PC->f.lf.base = (unsigned char)rs;
        PC->f.lf.ft = (unsigned char)rt;
        PC->f.lf.offset = (short)(instr & 0xFFFF);
        break;

    default: // All I-type (ADDI, LW, SW, BEQ, etc.)
        PC->f.i.rs = &reg[rs];
        PC->f.i.rt = &reg[rt];
        PC->f.i.immediate = (short)(instr & 0xFFFF);
        break;
    }
}

PowerPC_func *recompile_block(PowerPC_block *ppc_block, unsigned int addr) { return NULL; }
void init_block(PowerPC_block *ppc_block) { }
