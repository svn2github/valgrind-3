
/*--------------------------------------------------------------------*/
/*--- A header file for all parts of the MemCheck tool.            ---*/
/*---                                                 mc_include.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of MemCheck, a heavyweight Valgrind tool for
   detecting memory errors.

   Copyright (C) 2000-2005 Julian Seward 
      jseward@acm.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

/* Note: this header should contain declarations that are for use by
   Memcheck only -- declarations shared with Addrcheck go in mac_shared.h.
*/

#ifndef __MC_INCLUDE_H
#define __MC_INCLUDE_H

#include "mac_shared.h"

#define MC_(str)    VGAPPEND(vgMemCheck_,str)

/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

/* When instrumenting, omit some checks if tell-tale literals for
   inlined strlen() are visible in the basic block.  default: YES */
extern Bool MC_(clo_avoid_strlen_errors);


/*------------------------------------------------------------*/
/*--- Functions                                            ---*/
/*------------------------------------------------------------*/

/* Functions defined in mc_main.c */
extern VG_REGPARM(1) void MC_(helperc_complain_undef) ( HWord );
extern void MC_(helperc_value_check8_fail) ( void );
extern void MC_(helperc_value_check4_fail) ( void );
extern void MC_(helperc_value_check1_fail) ( void );
extern void MC_(helperc_value_check0_fail) ( void );

extern VG_REGPARM(1) void MC_(helperc_STOREV8be) ( Addr, ULong );
extern VG_REGPARM(1) void MC_(helperc_STOREV8le) ( Addr, ULong );
extern VG_REGPARM(2) void MC_(helperc_STOREV4be) ( Addr, UWord );
extern VG_REGPARM(2) void MC_(helperc_STOREV4le) ( Addr, UWord );
extern VG_REGPARM(2) void MC_(helperc_STOREV2be) ( Addr, UWord );
extern VG_REGPARM(2) void MC_(helperc_STOREV2le) ( Addr, UWord );
extern VG_REGPARM(2) void MC_(helperc_STOREV1)   ( Addr, UWord );

extern VG_REGPARM(1) ULong MC_(helperc_LOADV8be) ( Addr );
extern VG_REGPARM(1) ULong MC_(helperc_LOADV8le) ( Addr );
extern VG_REGPARM(1) UWord MC_(helperc_LOADV4be) ( Addr );
extern VG_REGPARM(1) UWord MC_(helperc_LOADV4le) ( Addr );
extern VG_REGPARM(1) UWord MC_(helperc_LOADV2be) ( Addr );
extern VG_REGPARM(1) UWord MC_(helperc_LOADV2le) ( Addr );
extern VG_REGPARM(1) UWord MC_(helperc_LOADV1)   ( Addr );

extern void MC_(helperc_MAKE_STACK_UNINIT) ( Addr base, UWord len );

/* Functions defined in mc_translate.c */
extern IRBB* MC_(instrument) ( IRBB* bb_in, VexGuestLayout* layout,
                               IRType gWordTy, IRType hWordTy );


#endif /* ndef __MC_INCLUDE_H */

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

