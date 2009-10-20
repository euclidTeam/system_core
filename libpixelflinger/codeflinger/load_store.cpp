/* libs/pixelflinger/codeflinger/load_store.cpp
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#include <assert.h>
#include <stdio.h>
#include <cutils/log.h>

#include "codeflinger/GGLAssembler.h"

namespace android {

// ----------------------------------------------------------------------------

void GGLAssembler::store(const pointer_t& addr, const pixel_t& s, uint32_t flags)
{    
    const int bits = addr.size;
    const int inc = (flags & WRITE_BACK)?1:0;
    switch (bits) {
    case 32:
#ifdef ARCH_ARM
        if (inc)    STR(AL, s.reg, addr.reg, immed12_post(4));
        else        STR(AL, s.reg, addr.reg);
#elif defined(ARCH_SH)
        MOV_ST_L(s.reg, addr.reg);
        if (inc)
            ADD_IMM(4, addr.reg);
#endif
        break;
    case 24:
        // 24 bits formats are a little special and used only for RGB
        // 0x00BBGGRR is unpacked as R,G,B
#ifdef ARCH_ARM
        STRB(AL, s.reg, addr.reg, immed12_pre(0));
        MOV(AL, 0, s.reg, reg_imm(s.reg, ROR, 8));
        STRB(AL, s.reg, addr.reg, immed12_pre(1));
        MOV(AL, 0, s.reg, reg_imm(s.reg, ROR, 8));
        STRB(AL, s.reg, addr.reg, immed12_pre(2));
        if (!(s.flags & CORRUPTIBLE)) {
            MOV(AL, 0, s.reg, reg_imm(s.reg, ROR, 16));
        }
        if (inc)
            ADD(AL, 0, addr.reg, addr.reg, imm(3));
#elif defined(ARCH_SH)
        MOV_ST_B(s.reg, addr.reg);      // 0x00BBGGRR

        SWAP_B(s.reg, s.reg);

        IMM(1, R0);
        MOV_ST_B_R0(s.reg, addr.reg);   // 0x00BBRRGG

        SWAP_B(s.reg, s.reg);
        SWAP_W(s.reg, s.reg);

        IMM(2, R0);
        MOV_ST_B_R0(s.reg, addr.reg);   // 0xGGRR00BB

        if (!(s.flags & CORRUPTIBLE)) {
            SWAP_W(s.reg, s.reg);
        }
        if (inc)
            ADD_IMM(3, addr.reg);
#endif
        break;
    case 16:
#ifdef ARCH_ARM
        if (inc)    STRH(AL, s.reg, addr.reg, immed8_post(2));
        else        STRH(AL, s.reg, addr.reg);
#elif defined(ARCH_SH)
        MOV_ST_W(s.reg, addr.reg);
        if (inc)
            ADD_IMM(2, addr.reg);
#endif
        break;
    case  8:
#ifdef ARCH_ARM
        if (inc)    STRB(AL, s.reg, addr.reg, immed12_post(1));
        else        STRB(AL, s.reg, addr.reg);
#elif defined(ARCH_SH)
        MOV_ST_B(s.reg, addr.reg);
        if (inc)
            ADD_IMM(1, addr.reg);
#endif
        break;
    }
}

void GGLAssembler::load(const pointer_t& addr, const pixel_t& s, uint32_t flags)
{    
    Scratch scratches(registerFile());    
    int s0;

    const int bits = addr.size;
    const int inc = (flags & WRITE_BACK)?1:0;
    switch (bits) {
    case 32:
#ifdef ARCH_ARM
        if (inc)    LDR(AL, s.reg, addr.reg, immed12_post(4));
        else        LDR(AL, s.reg, addr.reg);
#elif defined(ARCH_SH)
        if (inc)    MOV_LD_L_POSTINC(addr.reg, s.reg);
        else        MOV_LD_L(addr.reg, s.reg);
#endif
        break;
    case 24:
        // 24 bits formats are a little special and used only for RGB
        // R,G,B is packed as 0x00BBGGRR 
        s0 = scratches.obtain();
#ifdef ARCH_ARM
        if (s.reg != addr.reg) {
            LDRB(AL, s.reg, addr.reg, immed12_pre(0));      // R
            LDRB(AL, s0, addr.reg, immed12_pre(1));         // G
            ORR(AL, 0, s.reg, s.reg, reg_imm(s0, LSL, 8));
            LDRB(AL, s0, addr.reg, immed12_pre(2));         // B
            ORR(AL, 0, s.reg, s.reg, reg_imm(s0, LSL, 16));
        } else {
            int s1 = scratches.obtain();
            LDRB(AL, s1, addr.reg, immed12_pre(0));         // R
            LDRB(AL, s0, addr.reg, immed12_pre(1));         // G
            ORR(AL, 0, s1, s1, reg_imm(s0, LSL, 8));
            LDRB(AL, s0, addr.reg, immed12_pre(2));         // B
            ORR(AL, 0, s.reg, s1, reg_imm(s0, LSL, 16));
        }
        if (inc)
            ADD(AL, 0, addr.reg, addr.reg, imm(3));
#elif defined(ARCH_SH)
        if (s.reg != addr.reg) {
            MOV_LD_B(addr.reg, s.reg);  // R  (sign extension)
            EXTU_B(s.reg, s.reg);       //    cut sign bits.

            IMM(1, R0);
            MOV_LD_B_R0(addr.reg, s0);  // G  (sign extension)
            EXTU_B(s0, s0);             //    cut sign bits.
            SHLL8(s0);
            OR(s0, s.reg);

            IMM(2, R0);
            MOV_LD_B_R0(addr.reg, s0);  // B  (sign extension)
            EXTU_B(s0, s0);             //    cut sign bits.
            SHLL16(s0);
            OR(s0, s.reg);
        } else {
            int s1 = scratches.obtain();
            MOV_LD_B(addr.reg, s1);     // R  (sign extension)
            EXTU_B(s1, s1);             //    cut sign bits.

            IMM(1, R0);
            MOV_LD_B_R0(addr.reg, s0);  // G  (sign extension)
            EXTU_B(s0, s0);             //    cut sign bits.
            SHLL8(s0);
            OR(s0, s1);

            IMM(2, R0);
            MOV_LD_B_R0(addr.reg, s0);  // B  (sign extension)
            EXTU_B(s0, s0);             //    cut sign bits.
            SHLL16(s0);
            OR(s0, s1);
            MOV(s1, s.reg);
        }
        if (inc)
            ADD_IMM(3, addr.reg);
#endif
        break;        
    case 16:
#ifdef ARCH_ARM
        if (inc)    LDRH(AL, s.reg, addr.reg, immed8_post(2));
        else        LDRH(AL, s.reg, addr.reg);
#elif defined(ARCH_SH)
        if (inc)    MOV_LD_W_POSTINC(addr.reg, s.reg); // sign extention
        else        MOV_LD_W(addr.reg, s.reg);
        EXTU_W(s.reg, s.reg);
#endif
        break;
    case  8:
#ifdef ARCH_ARM
        if (inc)    LDRB(AL, s.reg, addr.reg, immed12_post(1));
        else        LDRB(AL, s.reg, addr.reg);
#elif defined(ARCH_SH)
        if (inc)    MOV_LD_B_POSTINC(addr.reg, s.reg);
        else        MOV_LD_B(addr.reg, s.reg);
        EXTU_B(s.reg, s.reg);
#endif
        break;
    }
}

void GGLAssembler::extract(integer_t& d, int s, int h, int l, int bits)
{
    const int maskLen = h-l;

    assert(maskLen<=8);
    assert(h);
    
#if defined(ARCH_SH)
    Scratch scratches(registerFile());
#endif
    if (h != bits) {
        const int mask = ((1<<maskLen)-1) << l;
#ifdef ARCH_ARM
        if (isValidImmediate(mask)) {
            AND(AL, 0, d.reg, s, imm(mask));    // component = packed & mask;
        } else if (isValidImmediate(~mask)) {
            BIC(AL, 0, d.reg, s, imm(~mask));   // component = packed & mask;
        } else {
            MOV(AL, 0, d.reg, reg_imm(s, LSL, 32-h));
            l += 32-h;
            h = 32;
        }
#elif defined(ARCH_SH)
        MOV(s, d.reg);
        SHLL(32-h, d.reg);
        l += 32-h;
        h = 32;
#endif
        s = d.reg;
    }
    
    if (l) {
#ifdef ARCH_ARM
        MOV(AL, 0, d.reg, reg_imm(s, LSR, l));  // component = packed >> l;
#elif defined(ARCH_SH)
        MOV(s, d.reg);
        SHLR(l, d.reg);
#endif
        s = d.reg;
    }
    
    if (s != d.reg) {
#ifdef ARCH_ARM
        MOV(AL, 0, d.reg, s);
#elif defined(ARCH_SH)
        MOV(s, d.reg);
#endif
    }

    d.s = maskLen;
}

void GGLAssembler::extract(integer_t& d, const pixel_t& s, int component)
{
    extract(d,  s.reg,
                s.format.c[component].h,
                s.format.c[component].l,
                s.size());
}

void GGLAssembler::extract(component_t& d, const pixel_t& s, int component)
{
    integer_t r(d.reg, 32, d.flags);
    extract(r,  s.reg,
                s.format.c[component].h,
                s.format.c[component].l,
                s.size());
    d = component_t(r);
}


void GGLAssembler::expand(integer_t& d, const component_t& s, int dbits)
{
    if (s.l || (s.flags & CLEAR_HI)) {
        extract(d, s.reg, s.h, s.l, 32);
        expand(d, d, dbits);
    } else {
        expand(d, integer_t(s.reg, s.size(), s.flags), dbits);
    }
}

void GGLAssembler::expand(component_t& d, const component_t& s, int dbits)
{
    integer_t r(d.reg, 32, d.flags);
    expand(r, s, dbits);
    d = component_t(r);
}

void GGLAssembler::expand(integer_t& dst, const integer_t& src, int dbits)
{
    assert(src.size());

    int sbits = src.size();
    int s = src.reg;
    int d = dst.reg;
#if defined(ARCH_SH)
    Scratch scratches(registerFile());
#endif

    // be sure to set 'dst' after we read 'src' as they may be identical
    dst.s = dbits;
    dst.flags = 0;

    if (dbits<=sbits) {
        if (s != d) {
#ifdef ARCH_ARM
            MOV(AL, 0, d, s);
#elif defined(ARCH_SH)
            MOV(s, d);
#endif
        }
        return;
    }

    if (sbits == 1) {
#ifdef ARCH_ARM
        RSB(AL, 0, d, s, reg_imm(s, LSL, dbits));
            // d = (s<<dbits) - s;
#elif defined(ARCH_SH)
        int Rn = scratches.obtain();
        MOV(s, Rn);
        SHLL(dbits, Rn);
        SUB(s, Rn);
        MOV(Rn, d);
#endif
        return;
    }

    if (dbits % sbits) {
#ifdef ARCH_ARM
        MOV(AL, 0, d, reg_imm(s, LSL, dbits-sbits));
#elif defined(ARCH_SH)
        MOV(s, d);
        SHLL(dbits-sbits, d);
#endif
            // d = s << (dbits-sbits);
        dbits -= sbits;
#ifdef ARCH_ARM
        do {
            ORR(AL, 0, d, d, reg_imm(d, LSR, sbits));
                // d |= d >> sbits;
            dbits -= sbits;
            sbits *= 2;
        } while(dbits>0);
#elif defined(ARCH_SH)
        int Rn = scratches.obtain();
        do {
            MOV(d, Rn);
            SHLR(sbits, Rn);
            OR(Rn, d);
                // d |= d >> sbits;
            dbits -= sbits;
            sbits *= 2;
        } while(dbits>0);
#endif
        return;
    }
    
    dbits -= sbits;
#ifdef ARCH_ARM
    do {
        ORR(AL, 0, d, s, reg_imm(s, LSL, sbits));
            // d |= d<<sbits;
        s = d;        
        dbits -= sbits;
        if (sbits*2 < dbits) {
            sbits *= 2;
        }
    } while(dbits>0);
#elif defined(ARCH_SH)
    int Rn = scratches.obtain();
    do {
        MOV(s, Rn);
        SHLL(sbits, Rn);
        OR(Rn, d);
            // d |= d<<sbits;
        s = d;
        dbits -= sbits;
        if (sbits*2 < dbits) {
            sbits *= 2;
        }
    } while(dbits>0);
#endif
}

void GGLAssembler::downshift(
        pixel_t& d, int component, component_t s, const reg_t& dither)
{
    const needs_t& needs = mBuilderContext.needs;
    Scratch scratches(registerFile());

    int sh = s.h;
    int sl = s.l;
    int maskHiBits = (sh!=32) ? ((s.flags & CLEAR_HI)?1:0) : 0;
    int maskLoBits = (sl!=0)  ? ((s.flags & CLEAR_LO)?1:0) : 0;
    int sbits = sh - sl;

    int dh = d.format.c[component].h;
    int dl = d.format.c[component].l;
    int dbits = dh - dl;
    int dithering = 0;
    
    LOGE_IF(sbits<dbits, "sbits (%d) < dbits (%d) in downshift", sbits, dbits);

    if (sbits>dbits) {
        // see if we need to dither
        dithering = mDithering;
    }
    
    int ireg = d.reg;
    if (!(d.flags & FIRST)) {
        if (s.flags & CORRUPTIBLE)  {
            ireg = s.reg;
        } else {
            ireg = scratches.obtain();
        }
    }
    d.flags &= ~FIRST;

    if (maskHiBits) {
        // we need to mask the high bits (and possibly the lowbits too)
        // and we might be able to use immediate mask.
        if (!dithering) {
            // we don't do this if we only have maskLoBits because we can
            // do it more efficiently below (in the case where dl=0)
            const int offset = sh - dbits;
            if (dbits<=8 && offset >= 0) {
                const uint32_t mask = ((1<<dbits)-1) << offset;
                if (isValidImmediate(mask) || isValidImmediate(~mask)) {
                    build_and_immediate(ireg, s.reg, mask, 32);
                    sl = offset;
                    s.reg = ireg; 
                    sbits = dbits;
                    maskLoBits = maskHiBits = 0;
                }
            }
        } else {
            // in the dithering case though, we need to preserve the lower bits
            const uint32_t mask = ((1<<sbits)-1) << sl;
            if (isValidImmediate(mask) || isValidImmediate(~mask)) {
                build_and_immediate(ireg, s.reg, mask, 32);
                s.reg = ireg; 
                maskLoBits = maskHiBits = 0;
            }
        }
    }

    // XXX: we could special case (maskHiBits & !maskLoBits)
    // like we do for maskLoBits below, but it happens very rarely
    // that we have maskHiBits only and the conditions necessary to lead
    // to better code (like doing d |= s << 24)

    if (maskHiBits) {
#ifdef ARCH_ARM
        MOV(AL, 0, ireg, reg_imm(s.reg, LSL, 32-sh));
#elif defined(ARCH_SH)
        MOV(s.reg, ireg);
        SHLL(32-sh, ireg);
#endif
        sl += 32-sh;
        sh = 32;
        s.reg = ireg;
        maskHiBits = 0;
    }

    //	Downsampling should be performed as follows:
    //  V * ((1<<dbits)-1) / ((1<<sbits)-1)
    //	V * [(1<<dbits)/((1<<sbits)-1)	-	1/((1<<sbits)-1)]
    //	V * [1/((1<<sbits)-1)>>dbits	-	1/((1<<sbits)-1)]
    //	V/((1<<(sbits-dbits))-(1>>dbits))	-	(V>>sbits)/((1<<sbits)-1)>>sbits
    //	V/((1<<(sbits-dbits))-(1>>dbits))	-	(V>>sbits)/(1-(1>>sbits))
    //
    //	By approximating (1>>dbits) and (1>>sbits) to 0:
    //
    //		V>>(sbits-dbits)	-	V>>sbits
    //
	//  A good approximation is V>>(sbits-dbits),
    //  but better one (needed for dithering) is:
    //
    //		(V>>(sbits-dbits)<<sbits	-	V)>>sbits
    //		(V<<dbits	-	V)>>sbits
    //		(V	-	V>>dbits)>>(sbits-dbits)

    // Dithering is done here
    if (dithering) {
        comment("dithering");
        if (sl) {
#ifdef ARCH_ARM
            MOV(AL, 0, ireg, reg_imm(s.reg, LSR, sl));
#elif defined(ARCH_SH)
            MOV(s.reg, ireg);
            SHLR(sl, ireg);
#endif
            sh -= sl;
            sl = 0;
            s.reg = ireg; 
        }
        // scaling (V-V>>dbits)
#ifdef ARCH_ARM
        SUB(AL, 0, ireg, s.reg, reg_imm(s.reg, LSR, dbits));
        const int shift = (GGL_DITHER_BITS - (sbits-dbits));
        if (shift>0)        ADD(AL, 0, ireg, ireg, reg_imm(dither.reg, LSR, shift));
        else if (shift<0)   ADD(AL, 0, ireg, ireg, reg_imm(dither.reg, LSL,-shift));
        else                ADD(AL, 0, ireg, ireg, dither.reg);
#elif defined(ARCH_SH)
        int Rn = scratches.obtain();
        MOV(s.reg, Rn);
        SHLR(dbits, Rn);
        MOV(s.reg, ireg);
        SUB(Rn, ireg);
        const int shift = (GGL_DITHER_BITS - (sbits-dbits));
        if (shift>0) {
            MOV(dither.reg, Rn);
            SHLR(shift, Rn);
            ADD(Rn, ireg);
        } else if (shift<0) {
            MOV(dither.reg, Rn);
            SHLL(-shift, Rn);
            ADD(Rn, ireg);
        } else {
            ADD(dither.reg, ireg);
        }
        scratches.recycle(Rn);
#endif
        s.reg = ireg; 
    }

#ifdef ARCH_ARM
    if ((maskLoBits|dithering) && (sh > dbits)) {
        int shift = sh-dbits;
        if (dl) {
            MOV(AL, 0, ireg, reg_imm(s.reg, LSR, shift));
            if (ireg == d.reg) {
                MOV(AL, 0, d.reg, reg_imm(ireg, LSL, dl));
            } else {
                ORR(AL, 0, d.reg, d.reg, reg_imm(ireg, LSL, dl));
            }
        } else {
            if (ireg == d.reg) {
                MOV(AL, 0, d.reg, reg_imm(s.reg, LSR, shift));
            } else {
                ORR(AL, 0, d.reg, d.reg, reg_imm(s.reg, LSR, shift));
            }
        }
    } else {
        int shift = sh-dh;
        if (shift>0) {
            if (ireg == d.reg) {
                MOV(AL, 0, d.reg, reg_imm(s.reg, LSR, shift));
            } else {
                ORR(AL, 0, d.reg, d.reg, reg_imm(s.reg, LSR, shift));
            }
        } else if (shift<0) {
            if (ireg == d.reg) {
                MOV(AL, 0, d.reg, reg_imm(s.reg, LSL, -shift));
            } else {
                ORR(AL, 0, d.reg, d.reg, reg_imm(s.reg, LSL, -shift));
            }
        } else {
            if (ireg == d.reg) {
                if (s.reg != d.reg) {
                    MOV(AL, 0, d.reg, s.reg);
                }
            } else {
                ORR(AL, 0, d.reg, d.reg, s.reg);
            }
        }
    }
#elif defined(ARCH_SH)
    if ((maskLoBits|dithering) && (sh > dbits)) {
        int shift = sh-dbits;
        if (dl) {
            MOV(s.reg, ireg);
            SHLR(shift, ireg);
            if (ireg == d.reg) {
                MOV(ireg, d.reg);
                SHLL(dl, d.reg);
            } else {
                int Rn = scratches.obtain();
                MOV(ireg, Rn);
                SHLL(dl, Rn);
                OR(Rn, d.reg);
                scratches.recycle(Rn);
            }
        } else {
            if (ireg == d.reg) {
                MOV(s.reg, d.reg);
                SHLR(shift, d.reg);
            } else {
                int Rn = scratches.obtain();
                MOV(s.reg, Rn);
                SHLR(shift, Rn);
                OR(Rn, d.reg);
                scratches.recycle(Rn);
            }
        }
    } else {
        int shift = sh-dh;
        if (shift>0) {
            if (ireg == d.reg) {
                MOV(s.reg, d.reg);
                SHLR(shift, d.reg);
            } else {
                int Rn = scratches.obtain();
                MOV(s.reg, Rn);
                SHLR(shift, Rn);
                OR(Rn, d.reg);
                scratches.recycle(Rn);
            }
        } else if (shift<0) {
            if (ireg == d.reg) {
                MOV(s.reg, d.reg);
                SHLL(-shift, d.reg);
            } else {
                int Rn = scratches.obtain();
                MOV(s.reg, Rn);
                SHLL(-shift, Rn);
                OR(Rn, d.reg);
                scratches.recycle(Rn);
            }
        } else {
            if (ireg == d.reg) {
                if (s.reg != d.reg) {
                    MOV(s.reg, d.reg);
                }
            } else {
                OR(s.reg, d.reg);
            }
        }
    }
#endif
}

}; // namespace android
