
/*--------------------------------------------------------------------*/
/*--- A simple parser for /proc/self/maps on Linux 2.4.X           ---*/
/*---                                            vg_procselfmaps.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2003 Julian Seward 
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


#include "vg_include.h"


/* static ... to keep it out of the stack frame. */
static Char procmap_buf[M_PROCMAP_BUF];

/* Records length of /proc/self/maps read into procmap_buf. */
static Int  buf_n_tot;


/* Helper fns. */

static Int hexdigit ( Char c )
{
   if (c >= '0' && c <= '9') return (Int)(c - '0');
   if (c >= 'a' && c <= 'f') return 10 + (Int)(c - 'a');
   if (c >= 'A' && c <= 'F') return 10 + (Int)(c - 'A');
   return -1;
}

static Int readchar ( Char* buf, Char* ch )
{
   if (*buf == 0) return 0;
   *ch = *buf;
   return 1;
}

static Int readhex ( Char* buf, UInt* val )
{
   Int n = 0;
   *val = 0;
   while (hexdigit(*buf) >= 0) {
      *val = (*val << 4) + hexdigit(*buf);
      n++; buf++;
   }
   return n;
}


/* Read /proc/self/maps, store the contents in a static buffer.  If there's
   a syntax error or other failure, just abort. */
void VG_(read_procselfmaps_contents)(void)
{
   Int n_chunk, fd;
   
   /* Read the initial memory mapping from the /proc filesystem. */
   fd = VG_(open) ( "/proc/self/maps", VKI_O_RDONLY, 0 );
   if (fd == -1) {
      VG_(message)(Vg_UserMsg, "FATAL: can't open /proc/self/maps");
      VG_(exit)(1);
   }
   buf_n_tot = 0;
   do {
      n_chunk = VG_(read) ( fd, &procmap_buf[buf_n_tot], 
                            M_PROCMAP_BUF - buf_n_tot );
      buf_n_tot += n_chunk;
   } while ( n_chunk > 0 && buf_n_tot < M_PROCMAP_BUF );
   VG_(close)(fd);
   if (buf_n_tot >= M_PROCMAP_BUF-5) {
      VG_(message)(Vg_UserMsg, "FATAL: M_PROCMAP_BUF is too small; "
                               "increase it and recompile");
       VG_(exit)(1);
   }
   if (buf_n_tot == 0) {
      VG_(message)(Vg_UserMsg, "FATAL: I/O error on /proc/self/maps" );
       VG_(exit)(1);
   }
   procmap_buf[buf_n_tot] = 0;
}

/* Parse /proc/self/maps.  For each map entry, call
   record_mapping, passing it, in this order:

      start address in memory
      length
      r permissions char; either - or r
      w permissions char; either - or w
      x permissions char; either - or x
      offset in file, or zero if no file
      filename, zero terminated, or NULL if no file

   So the sig of the called fn might be

      void (*record_mapping)( Addr start, UInt size, 
                              Char r, Char w, Char x, 
                              UInt foffset, UChar* filename )

   Note that the supplied filename is transiently stored; record_mapping 
   should make a copy if it wants to keep it.

   Nb: it is important that this function does not alter the contents of
       procmap_buf!
*/
void VG_(read_procselfmaps) (
   void (*record_mapping)( Addr, UInt, Char, Char, Char, UInt, UChar* ),
   Bool read_from_file )
{
   Int    i, j, i_eol;
   Addr   start, endPlusOne;
   UChar* filename;
   UInt   foffset;
   UChar  rr, ww, xx, pp, ch, tmp;

   if (read_from_file) {
      VG_(read_procselfmaps_contents)();
   }

   if (0)
      VG_(message)(Vg_DebugMsg, "raw:\n%s", procmap_buf );

   /* Ok, it's safely aboard.  Parse the entries. */
   i = 0;
   while (True) {
      if (i >= buf_n_tot) break;

      /* Read (without fscanf :) the pattern %8x-%8x %c%c%c%c %8x */
      j = readhex(&procmap_buf[i], &start);
      if (j > 0) i += j; else goto syntaxerror;
      j = readchar(&procmap_buf[i], &ch);
      if (j == 1 && ch == '-') i += j; else goto syntaxerror;
      j = readhex(&procmap_buf[i], &endPlusOne);
      if (j > 0) i += j; else goto syntaxerror;

      j = readchar(&procmap_buf[i], &ch);
      if (j == 1 && ch == ' ') i += j; else goto syntaxerror;

      j = readchar(&procmap_buf[i], &rr);
      if (j == 1 && (rr == 'r' || rr == '-')) i += j; else goto syntaxerror;
      j = readchar(&procmap_buf[i], &ww);
      if (j == 1 && (ww == 'w' || ww == '-')) i += j; else goto syntaxerror;
      j = readchar(&procmap_buf[i], &xx);
      if (j == 1 && (xx == 'x' || xx == '-')) i += j; else goto syntaxerror;
      /* I haven't a clue what this last field means. */
      j = readchar(&procmap_buf[i], &pp);
      if (j == 1 && (pp == 'p' || pp == '-' || pp == 's')) 
                                              i += j; else goto syntaxerror;

      j = readchar(&procmap_buf[i], &ch);
      if (j == 1 && ch == ' ') i += j; else goto syntaxerror;

      j = readhex(&procmap_buf[i], &foffset);
      if (j > 0) i += j; else goto syntaxerror;
      
      goto read_line_ok;

    syntaxerror:
      VG_(message)(Vg_UserMsg, "FATAL: syntax error reading /proc/self/maps");
      { Int k;
        VG_(printf)("last 50 chars: `");
        for (k = i-50; k <= i; k++) VG_(printf)("%c", procmap_buf[k]);
        VG_(printf)("'\n");
      }
       VG_(exit)(1);

    read_line_ok:

      /* Try and find the name of the file mapped to this segment, if
         it exists. */
      while (procmap_buf[i] != '\n' && i < M_PROCMAP_BUF-1) i++;
      i_eol = i;
      i--;
      while (!VG_(isspace)(procmap_buf[i]) && i >= 0) i--;
      i++;
      if (i < i_eol-1 && procmap_buf[i] == '/') {
         /* Minor hack: put a '\0' at the filename end for the call to
            `record_mapping', then restore the old char with `tmp'. */
         filename = &procmap_buf[i];
         tmp = filename[i_eol - i];
         filename[i_eol - i] = '\0';
      } else {
         tmp = '\0';
         filename = NULL;
         foffset = 0;
      }

      (*record_mapping) ( start, endPlusOne-start, 
                          rr, ww, xx, 
                          foffset, filename );

      if ('\0' != tmp) {
         filename[i_eol - i] = tmp;
      }

      i = i_eol + 1;
   }
}

/*--------------------------------------------------------------------*/
/*--- end                                        vg_procselfmaps.c ---*/
/*--------------------------------------------------------------------*/
