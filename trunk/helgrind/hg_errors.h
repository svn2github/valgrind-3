
/*--------------------------------------------------------------------*/
/*--- Error management for Helgrind.                               ---*/
/*---                                                  hg_errors.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Helgrind, a Valgrind tool for detecting errors
   in threaded programs.

   Copyright (C) 2007-2008 OpenWorks Ltd
      info@open-works.co.uk

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

#ifndef __HG_ERRORS_H
#define __HG_ERRORS_H


/* The standard bundle of error management functions that we are
required to present to the core/tool interface at startup. */
Bool  HG_(eq_Error) ( VgRes not_used, Error* e1, Error* e2 );
void  HG_(pp_Error) ( Error* err );
UInt  HG_(update_extra) ( Error* err );
Bool  HG_(recognised_suppression) ( Char* name, Supp *su );
Bool  HG_(read_extra_suppression_info) ( Int fd, Char* buf, Int nBuf,
                                         Supp* su );
Bool  HG_(error_matches_suppression) ( Error* err, Supp* su );
Char* HG_(get_error_name) ( Error* err );
void  HG_(print_extra_suppression_info) ( Error* err );

/* Functions for recording various kinds of errors. */
void HG_(record_error_Race) ( Thread* thr, 
                              Addr data_addr, Bool isWrite, Int szB,
                              ExeContext* mb_lastlock );
void HG_(record_error_FreeMemLock)    ( Thread* thr, Lock* lk );
void HG_(record_error_UnlockUnlocked) ( Thread*, Lock* );
void HG_(record_error_UnlockForeign)  ( Thread*, Thread*, Lock* );
void HG_(record_error_UnlockBogus)    ( Thread*, Addr );
void HG_(record_error_PthAPIerror)    ( Thread*, HChar*, Word, HChar* );
void HG_(record_error_LockOrder)      ( Thread*, Addr, Addr,
                                        ExeContext*, ExeContext* );
void HG_(record_error_Misc)           ( Thread*, HChar* );

/* Statistics pertaining to error management. */
extern ULong HG_(stats__LockN_to_P_queries);
extern ULong HG_(stats__LockN_to_P_get_map_size) ( void );
extern ULong HG_(stats__string_table_queries);
extern ULong HG_(stats__string_table_get_map_size) ( void );

#endif /* ! __HG_ERRORS_H */

/*--------------------------------------------------------------------*/
/*--- end                                              hg_errors.h ---*/
/*--------------------------------------------------------------------*/
