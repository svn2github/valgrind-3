/* -*- mode: C; c-basic-offset: 3; -*- */

/*--------------------------------------------------------------------*/
/*--- The address space manager: segment initialisation and        ---*/
/*--- tracking, stack operations                                   ---*/
/*---                                                              ---*/
/*--- Implementation for Linux (and Darwin!)   m_aspacemgr-linux.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2013 Julian Seward 
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

/* *************************************************************
   DO NOT INCLUDE ANY OTHER FILES HERE.
   ADD NEW INCLUDES ONLY TO priv_aspacemgr.h
   AND THEN ONLY AFTER READING DIRE WARNINGS THERE TOO.
   ************************************************************* */

#include "priv_aspacemgr.h"
#include "pub_core_libcbase.h"   // VG_(strcmp), etc
#include "pub_core_vki.h"        // VKI_PROT_EXEC, etc.
#include "pub_core_debuglog.h"   // VG_(debugLog)
#include "pub_core_options.h"    // VG_(clo_sanity_level)
#include "pub_core_syscall.h"    // SysRes
#include "config.h"              // HAVE_REMAP


/*-----------------------------------------------------------------*/
/*---                                                           ---*/
/*--- Overview.                                                 ---*/
/*---                                                           ---*/
/*-----------------------------------------------------------------*/

/* Purpose
   ~~~~~~~
   The purpose of the address space manager (aspacem) is:

   (1) to record the disposition of all parts of the process' address
       space at all times.

   (2) to the extent that it can, influence layout in ways favourable
       to our purposes.

   It is important to appreciate that whilst it can and does attempt
   to influence layout, and usually succeeds, it isn't possible to
   impose absolute control: in the end, the kernel is the final
   arbiter, and can always bounce our requests.

   Strategy
   ~~~~~~~~
   The strategy is therefore as follows: 

   * Track ownership of mappings.  Each one can belong either to
     Valgrind or to the client.

   * Try to place the client's fixed and hinted mappings at the
     requested addresses.  Fixed mappings are allowed anywhere except
     in areas reserved by Valgrind; the client can trash its own
     mappings if it wants.  Hinted mappings are allowed providing they
     fall entirely in free areas; if not, they will be placed by
     aspacem in a free area.

   * Anonymous mappings are allocated so as to keep Valgrind and
     client areas widely separated when possible.  If address space
     runs low, then they may become intermingled: aspacem will attempt
     to use all possible space.  But under most circumstances lack of
     address space is not a problem and so the areas will remain far
     apart.

     Searches for client space start at aspacem_cStart and will wrap
     around the end of the available space if needed.  Searches for
     Valgrind space start at aspacem_vStart and will also wrap around.
     Because aspacem_cStart is approximately at the start of the
     available space and aspacem_vStart is approximately in the
     middle, for the most part the client anonymous mappings will be
     clustered towards the start of available space, and Valgrind ones
     in the middle.

     The available space is delimited by aspacem_minAddr and
     aspacem_maxAddr.  aspacem is flexible and can operate with these
     at any (sane) setting.  For 32-bit Linux, aspacem_minAddr is set
     to some low-ish value at startup (64M) and aspacem_maxAddr is
     derived from the stack pointer at system startup.  This seems a
     reliable way to establish the initial boundaries.
     A command line option allows to change the value of aspacem_minAddr,
     so as to allow memory hungry applications to use the lowest
     part of the memory.

     64-bit Linux is similar except for the important detail that the
     upper boundary is set to 64G.  The reason is so that all
     anonymous mappings (basically all client data areas) are kept
     below 64G, since that is the maximum range that memcheck can
     track shadow memory using a fast 2-level sparse array.  It can go
     beyond that but runs much more slowly.  The 64G limit is
     arbitrary and is trivially changed.  So, with the current
     settings, programs on 64-bit Linux will appear to run out of
     address space and presumably fail at the 64G limit.  Given the
     considerable space overhead of Memcheck, that means you should be
     able to memcheckify programs that use up to about 32G natively.

   Note that the aspacem_minAddr/aspacem_maxAddr limits apply only to
   anonymous mappings.  The client can still do fixed and hinted maps
   at any addresses provided they do not overlap Valgrind's segments.
   This makes Valgrind able to load prelinked .so's at their requested
   addresses on 64-bit platforms, even if they are very high (eg,
   112TB).

   At startup, aspacem establishes the usable limits, and advises
   m_main to place the client stack at the top of the range, which on
   a 32-bit machine will be just below the real initial stack.  One
   effect of this is that self-hosting sort-of works, because an inner
   valgrind will then place its client's stack just below its own
   initial stack.
     
   The segment array and segment kinds
   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   The central data structure is the segment array (segments[0
   .. nsegments_used-1]).  This covers the entire address space in
   order, giving account of every byte of it.  Free spaces are
   represented explicitly as this makes many operations simpler.
   Mergeable adjacent segments are aggressively merged so as to create
   a "normalised" representation.

   There are 7 (mutually-exclusive) segment kinds, the meaning of
   which is important:

   SkFree: a free space, which may be allocated either to Valgrind (V)
      or the client (C).

   SkAnonC: an anonymous mapping belonging to C.  For these, aspacem
      tracks a field indicating what kind of segment this is, conceptually:
      heap, extensible stack, break segment (aka data segment).

   SkFileC: a file mapping belonging to C.

   SkShmC: a shared memory segment belonging to C.

   SkAnonV: an anonymous mapping belonging to V.  These cover all V's
      dynamic memory needs, including non-client malloc/free areas,
      shadow memory, and the translation cache.

   SkFileV: a file mapping belonging to V.  As far as I know these are
      only created transiently for the purposes of reading debug info.

   SkResvn: a reservation segment.

   These are mostly straightforward.  Reservation segments have some
   subtlety, however.

   A reservation segment is unmapped from the kernel's point of view,
   but is an area in which aspacem will not create anonymous maps
   (either Vs or Cs).  The idea is that we will try to keep it clear
   when the choice to do so is ours.  Reservation segments are
   'invisible' from the client's point of view: it may choose to park
   a fixed mapping in the middle of one, and that's just tough -- we
   can't do anything about that.  From the client's perspective
   reservations are semantically equivalent to (although
   distinguishable from, if it makes enquiries) free areas.

   Reservations are a primitive mechanism provided for whatever
   purposes the rest of the system wants.  Currently they are used to
   reserve the expansion space into which a growdown stack is
   expanded, and into which the data segment is extended.  Note,
   though, that reservation segments are an implementation detail that
   should not be visible outside the address space manager.

   Reservations may be shrunk in order that an adjoining anonymous
   mapping may be extended.  This makes dataseg/stack expansion work.
   A reservation may not be shrunk below one page.

   The advise/notify concept
   ~~~~~~~~~~~~~~~~~~~~~~~~~
   All mmap-related calls must be routed via aspacem.  Calling
   sys_mmap directly from the rest of the system is very dangerous
   because aspacem's data structures will become out of date.

   The fundamental mode of operation of aspacem is to support client
   mmaps.  Here's what happens (in ML_(generic_PRE_sys_mmap)):

   * m_syswrap intercepts the mmap call.  It examines the parameters
     and identifies the requested placement constraints.  There are
     three possibilities: no constraint (MAny), hinted (MHint, "I
     prefer X but will accept anything"), and fixed (MFixed, "X or
     nothing").

   * This request is passed to VG_(am_get_advisory).  This decides on
     a placement as described in detail in Strategy above.  It may
     also indicate that the map should fail, because it would trash
     one of Valgrind's areas, which would probably kill the system.

   * Control returns to the wrapper.  If VG_(am_get_advisory) has
     declared that the map should fail, then it must be made to do so.
     Usually, though, the request is considered acceptable, in which
     case an "advised" address is supplied.  The advised address
     replaces the original address supplied by the client, and
     MAP_FIXED is set.

     Note at this point that although aspacem has been asked for
     advice on where to place the mapping, no commitment has yet been
     made by either it or the kernel.

   * The adjusted request is handed off to the kernel.

   * The kernel's result is examined.  If the map succeeded, aspacem
     is told of the outcome (VG_(am_notify_client_mmap)), so it can
     update its records accordingly.

  This then is the central advise-notify idiom for handling client
  mmap/munmap/mprotect/shmat:

  * ask aspacem for an advised placement (or a veto)

  * if not vetoed, hand request to kernel, using the advised placement

  * examine result, and if successful, notify aspacem of the result.

  There are also many convenience functions, eg
  VG_(am_mmap_anon_fixed_client), which do both phases entirely within
  aspacem.

  To debug all this, a sync-checker is provided.  It reads
  /proc/self/maps, compares what it sees with aspacem's records, and
  complains if there is a difference.  --sanity-level=3 runs it before
  and after each syscall, which is a powerful, if slow way of finding
  buggy syscall wrappers.

  Loss of pointercheck
  ~~~~~~~~~~~~~~~~~~~~
  Up to and including Valgrind 2.4.1, x86 segmentation was used to
  enforce seperation of V and C, so that wild writes by C could not
  trash V.  This got called "pointercheck".  Unfortunately, the new
  more flexible memory layout, plus the need to be portable across
  different architectures, means doing this in hardware is no longer
  viable, and doing it in software is expensive.  So at the moment we
  don't do it at all.
*/


/*-----------------------------------------------------------------*/
/*---                                                           ---*/
/*--- The Address Space Manager's state.                        ---*/
/*---                                                           ---*/
/*-----------------------------------------------------------------*/

/* ------ start of STATE for the address-space manager ------ */

/* Limits etc */

Addr VG_(clo_aspacem_minAddr)
#if defined(VGO_darwin)
# if VG_WORDSIZE == 4
   = (Addr) 0x00001000;
# else
   = (Addr) 0x100000000;  // 4GB page zero
# endif
#else
   = (Addr) 0x04000000; // 64M
#endif


// The smallest address that aspacem will try to allocate
static Addr aspacem_minAddr = 0;

// The largest address that aspacem will try to allocate
static Addr aspacem_maxAddr = 0;

// Where aspacem will start looking for client space
static Addr aspacem_cStart = 0;

// Where aspacem will start looking for Valgrind space
static Addr aspacem_vStart = 0;


#define AM_SANITY_CHECK()                                    \
   do {                                                      \
      if (VG_(clo_sanity_level >= 3))                        \
         aspacem_assert(VG_(am_do_sync_check)                \
            (__PRETTY_FUNCTION__,__FILE__,__LINE__));        \
   } while (0) 

/* ------ end of STATE for the address-space manager ------ */

/* ------ Forwards decls ------ */

static void parse_procselfmaps (
      void (*record_mapping)( Addr addr, SizeT len, UInt prot,
                              ULong dev, ULong ino, Off64T offset, 
                              const HChar* filename ),
      void (*record_gap)( Addr addr, SizeT len )
   );

/* ----- Hacks to do with the "commpage" on arm-linux ----- */
/* Not that I have anything against the commpage per se.  It's just
   that it's not listed in /proc/self/maps, which is a royal PITA --
   we have to fake it up, in parse_procselfmaps.

   But note also bug 254556 comment #2: this is now fixed in newer
   kernels -- it is listed as a "[vectors]" entry.  Presumably the
   fake entry made here duplicates the [vectors] entry, and so, if at
   some point in the future, we can stop supporting buggy kernels,
   then this kludge can be removed entirely, since the procmap parser
   below will read that entry in the normal way. */
#if defined(VGP_arm_linux)
#  define ARM_LINUX_FAKE_COMMPAGE_START 0xFFFF0000
#  define ARM_LINUX_FAKE_COMMPAGE_END1  0xFFFF1000
#endif


/* Check the segment array corresponds with the kernel's view of
   memory layout.  sync_check_ok returns True if no anomalies were
   found, else False.  In the latter case the mismatching segments are
   displayed. 

   The general idea is: we get the kernel to show us all its segments
   and also the gaps in between.  For each such interval, try and find
   a sequence of appropriate intervals in our segment array which
   cover or more than cover the kernel's interval, and which all have
   suitable kinds/permissions etc. 

   Although any specific kernel interval is not matched exactly to a
   valgrind interval or sequence thereof, eventually any disagreement
   on mapping boundaries will be detected.  This is because, if for
   example valgrind's intervals cover a greater range than the current
   kernel interval, it must be the case that a neighbouring free-space
   interval belonging to valgrind cannot cover the neighbouring
   free-space interval belonging to the kernel.  So the disagreement
   is detected.

   In other words, we examine each kernel interval in turn, and check
   we do not disagree over the range of that interval.  Because all of
   the address space is examined, any disagreements must eventually be
   detected.
*/

static Bool sync_check_ok = False;

static void sync_check_mapping_callback ( Addr addr, SizeT len, UInt prot,
                                          ULong dev, ULong ino, Off64T offset, 
                                          const HChar* filename )
{
   Bool sloppyXcheck, sloppyRcheck;

   /* If a problem has already been detected, don't continue comparing
      segments, so as to avoid flooding the output with error
      messages. */
#if !defined(VGO_darwin)
   /* GrP fixme not */
   if (!sync_check_ok)
      return;
#endif
   if (len == 0)
      return;

   /* The kernel should not give us wraparounds. */
   aspacem_assert(addr <= addr + len - 1); 

   const NSegment *segLo = ML_(am_find_segment)( addr );
   const NSegment *segHi = ML_(am_find_segment)( addr + len - 1 );

   aspacem_assert(segLo->start <= addr );
   aspacem_assert(segHi->end   >= addr + len - 1 );

   /* x86 doesn't differentiate 'x' and 'r' (at least, all except the
      most recent NX-bit enabled CPUs) and so recent kernels attempt
      to provide execute protection by placing all executable mappings
      low down in the address space and then reducing the size of the
      code segment to prevent code at higher addresses being executed.

      These kernels report which mappings are really executable in
      the /proc/self/maps output rather than mirroring what was asked
      for when each mapping was created. In order to cope with this we
      have a sloppyXcheck mode which we enable on x86 and s390 - in this
      mode we allow the kernel to report execute permission when we weren't
      expecting it but not vice versa. */
#  if defined(VGA_x86) || defined (VGA_s390x)
   sloppyXcheck = True;
#  else
   sloppyXcheck = False;
#  endif

   /* Some kernels on s390 provide 'r' permission even when it was not
      explicitly requested. It seems that 'x' permission implies 'r'. 
      This behaviour also occurs on OS X. */
#  if defined(VGA_s390x) || defined(VGO_darwin)
   sloppyRcheck = True;
#  else
   sloppyRcheck = False;
#  endif

   /* Segments segLo .. segHi inclusive should agree with the
      presented data. */
   const NSegment *seg;
   Int i;
   for (i = 0, seg = segLo; True; seg = ML_(am_next_segment)(seg), ++i) {
      Bool same, cmp_offsets, cmp_devino;
      UInt seg_prot;
   
      /* compare the kernel's offering against ours. */
      same = seg->kind == SkAnonC
             || seg->kind == SkAnonV
             || seg->kind == SkFileC
             || seg->kind == SkFileV
             || seg->kind == SkShmC;

      seg_prot = 0;
      if (seg->hasR) seg_prot |= VKI_PROT_READ;
      if (seg->hasW) seg_prot |= VKI_PROT_WRITE;
      if (seg->hasX) seg_prot |= VKI_PROT_EXEC;

      cmp_offsets = seg->kind == SkFileC || seg->kind == SkFileV;

      cmp_devino = seg->dev != 0 || seg->ino != 0;

      /* Consider other reasons to not compare dev/inode */
#if defined(VGO_linux)
      /* bproc does some godawful hack on /dev/zero at process
         migration, which changes the name of it, and its dev & ino */
      if (filename && 0==VG_(strcmp)(filename, "/dev/zero (deleted)"))
         cmp_devino = False;

      /* hack apparently needed on MontaVista Linux */
      if (filename && VG_(strstr)(filename, "/.lib-ro/"))
         cmp_devino = False;
#endif

#if defined(VGO_darwin)
      // GrP fixme kernel info doesn't have dev/inode
      cmp_devino = False;
      
      // GrP fixme V and kernel don't agree on offsets
      cmp_offsets = False;
#endif
      
      /* If we are doing sloppy execute permission checks then we
         allow segment to have X permission when we weren't expecting
         it (but not vice versa) so if the kernel reported execute
         permission then pretend that this segment has it regardless
         of what we were expecting. */
      if (sloppyXcheck && (prot & VKI_PROT_EXEC) != 0) {
         seg_prot |= VKI_PROT_EXEC;
      }

      if (sloppyRcheck && (prot & (VKI_PROT_EXEC | VKI_PROT_READ)) ==
          (VKI_PROT_EXEC | VKI_PROT_READ)) {
         seg_prot |= VKI_PROT_READ;
      }

      same = same
             && seg_prot == prot
             && (cmp_devino
                   ? (seg->dev == dev && seg->ino == ino)
                   : True)
             && (cmp_offsets 
                   ? seg->start - seg->offset == addr-offset
                   : True);
      if (!same) {
         Addr start = addr;
         Addr end = start + len - 1;
         HChar len_buf[20];
         ML_(am_show_len_concisely)(len_buf, start, end);

         sync_check_ok = False;

         VG_(debugLog)(
            0,"aspacem",
              "segment mismatch: V's seg 1st, kernel's 2nd:\n");
         ML_(am_show_segment_full)( 0, i, seg );
         VG_(debugLog)(0,"aspacem", 
            "...: .... %010llx-%010llx %s %c%c%c.. ....... "
            "d=0x%03llx i=%-7lld o=%-7lld (.) m=. %s\n",
            (ULong)start, (ULong)end, len_buf,
            prot & VKI_PROT_READ  ? 'r' : '-',
            prot & VKI_PROT_WRITE ? 'w' : '-',
            prot & VKI_PROT_EXEC  ? 'x' : '-',
            dev, ino, offset, filename ? filename : "(none)" );

         return;
      }
      if (seg == segHi) break;
   }

   /* Looks harmless.  Keep going. */
   return;
}

static void sync_check_gap_callback ( Addr addr, SizeT len )
{
   /* If a problem has already been detected, don't continue comparing
      segments, so as to avoid flooding the output with error
      messages. */
#if !defined(VGO_darwin)
   /* GrP fixme not */
   if (!sync_check_ok)
      return;
#endif 
   if (len == 0)
      return;

   /* The kernel should not give us wraparounds. */
   aspacem_assert(addr <= addr + len - 1); 

   const NSegment *segLo = ML_(am_find_segment)( addr );
   const NSegment *segHi = ML_(am_find_segment)( addr + len - 1 );

   aspacem_assert(segLo->start <= addr );
   aspacem_assert(segHi->end   >= addr + len - 1 );

   /* Segments segLo .. segHo inclusive should agree with the
      presented data. */
   for (const NSegment *seg = segLo; True; seg = ML_(am_next_segment)(seg)) {
      Bool same;
   
      /* compare the kernel's offering against ours. */
      same = seg->kind == SkFree || seg->kind == SkResvn;

      if (!same) {
         Addr start = addr;
         Addr end = start + len - 1;
         HChar len_buf[20];
         ML_(am_show_len_concisely)(len_buf, start, end);

         sync_check_ok = False;

         VG_(debugLog)(
            0,"aspacem",
              "segment mismatch: V's gap 1st, kernel's 2nd:\n");
         ML_(am_show_segment_full)( 0, -1, seg );
         VG_(debugLog)(0,"aspacem", 
            "   : .... %010llx-%010llx %s\n",
            (ULong)start, (ULong)end, len_buf);
         return;
      }
      if (seg == segHi) break;
   }

   /* Looks harmless.  Keep going. */
   return;
}


/* Sanity check: check that Valgrind and the kernel agree on the
   address space layout.  Prints offending segments and call point if
   a discrepancy is detected, but does not abort the system.  Returned
   Bool is False if a discrepancy was found. */

Bool VG_(am_do_sync_check)( const HChar* fn, const HChar* file, Int line )
{
   sync_check_ok = True;
   parse_procselfmaps( sync_check_mapping_callback, sync_check_gap_callback );

   if (!sync_check_ok) {
      VG_(debugLog)(0,"aspacem", "do_sync_check at %s:%d (%s): FAILED\n",
                      file, line, fn);
      VG_(debugLog)(0,"aspacem", "\n");

#     if 0
      HChar buf[100];   // large enough
      VG_(am_show_nsegments)(0, "post do_sync_check failure");
      ML_(am_sprintf)(buf, "/bin/cat /proc/%d/maps", ML_(am_getpid)());
      VG_(system)(buf);
#     endif
   }
   return sync_check_ok;
}

/* Hook to allow sanity checks to be done from aspacemgr-common.c. */
void ML_(am_do_sanity_check)( void )
{
   AM_SANITY_CHECK();
}


/* Test if a piece of memory is addressable by client or by valgrind with at
   least the "prot" protection permissions by examining the underlying
   segments. The KINDS argument specifies the allowed segments ADDR may
   belong to in order to be considered "valid".
*/
static
Bool is_valid_for( UInt kinds, Addr start, SizeT len, UInt prot )
{
   Bool needR, needW, needX;

   if (len == 0)
      return True; /* somewhat dubious case */
   if (start + len < start)
      return False; /* reject wraparounds */

   needR = toBool(prot & VKI_PROT_READ);
   needW = toBool(prot & VKI_PROT_WRITE);
   needX = toBool(prot & VKI_PROT_EXEC);

   const NSegment *segLo = ML_(am_find_segment)(start);
   const NSegment *segHi;
   aspacem_assert(start >= segLo->start);

   if (start+len-1 <= segLo->end) {
      /* This is a speedup hack which avoids calling ML_(am_find_segment)
         a second time when possible.  It is always correct to just
         use the "else" clause below, but is_valid_for_client is
         called a lot by the leak checker, so avoiding pointless calls
         to ML_(am_find_segment), which can be expensive, is helpful. */
      segHi = segLo;
   } else {
      segHi = ML_(am_find_segment)(start + len - 1);
   }

   for (const NSegment *seg = segLo; True; seg = ML_(am_next_segment)(seg)) {
      if ( (seg->kind & kinds) != 0
           && (needR ? seg->hasR : True)
           && (needW ? seg->hasW : True)
           && (needX ? seg->hasX : True) ) {
         /* ok */
      } else {
         return False;
      }
      if (seg == segHi) break;
   }

   return True;
}

/* Test if a piece of memory is addressable by the client with at
   least the "prot" protection permissions by examining the underlying
   segments. */
Bool VG_(am_is_valid_for_client)( Addr start, SizeT len, UInt prot )
{
   const UInt kinds = SkFileC | SkAnonC | SkShmC;

   return is_valid_for(kinds, start, len, prot);
}

/* Variant of VG_(am_is_valid_for_client) which allows free areas to
   be consider part of the client's addressable space.  It also
   considers reservations to be allowable, since from the client's
   point of view they don't exist. */
Bool VG_(am_is_allowed_for_client)( Addr start, SizeT len, UInt prot )
{
   const UInt kinds = SkFileC | SkAnonC | SkShmC | SkFree | SkResvn;

   return is_valid_for(kinds, start, len, prot);
}


Bool VG_(am_is_valid_for_valgrind) ( Addr start, SizeT len, UInt prot )
{
   const UInt kinds = SkFileV | SkAnonV;

   return is_valid_for(kinds, start, len, prot);
}


/* Returns True if any part of the address range is marked as having
   translations made from it.  This is used to determine when to
   discard code, so if in doubt return True. */

static Bool any_Ts_in_range ( Addr start, SizeT len )
{
   aspacem_assert(len > 0);
   aspacem_assert(start + len > start);
   const NSegment *segLo = ML_(am_find_segment)(start);
   const NSegment *segHi = ML_(am_find_segment)(start + len - 1);

   for (const NSegment *seg = segLo; True; seg = ML_(am_next_segment)(seg)) {
      if (seg->hasT)
         return True;
      if (seg == segHi) break;
   }
   return False;
}


/* Check whether ADDR looks like an address or address-to-be located in an
   extensible client stack segment. Return true if 
   (1) ADDR is located in an already mapped stack segment, OR
   (2) ADDR is located in a reservation segment into which an abutting SkAnonC
       segment can be extended.
   The KIND parameter tells what kind of ADDR is allowed:
   'M' ADDR must be mapped
   'U' ADDR must be unmapped
   '*' ADDR can be mapped or unmapped
*/
Bool VG_(am_addr_is_in_extensible_client_stack)( Addr addr, MapKind kind )
{
   const NSegment *seg = ML_(am_find_segment)(addr);

   switch (seg->kind) {
   case SkFree:
   case SkAnonV:
   case SkFileV:
   case SkFileC:
   case SkShmC:
      return False;

   case SkResvn: {
      if (kind == MkMapped) return False;
      if (seg->smode != SmUpper) return False;
      /* If the the abutting segment towards higher addresses is an SkAnonC
         segment, then ADDR is a future stack pointer. */
      const NSegment *next = ML_(am_next_segment)(seg);
      if (next == NULL || next->kind != SkAnonC) return False;

      /* OK; looks like a stack segment */
      return True;
   }

   case SkAnonC: {
      /* If the abutting segment towards lower addresses is an SkResvn
         segment, then ADDR is a stack pointer into mapped memory. */
      if (kind == MkUnmapped) return False;
      const NSegment *next = ML_(am_prev_segment)(seg);
      if (next == NULL || next->kind != SkResvn || next->smode != SmUpper)
         return False;

      /* OK; looks like a stack segment */
      return True;
   }

   default:
      aspacem_assert(0);   // should never happen
   }
}


/* If ADDR is located in a stack segment return True and set return via
   START and END the address limits of that segment. Otherwise, return
   False and set the limts to 0. */
Bool VG_(am_stack_limits)( Addr addr, /*OUT*/Addr *start, /*OUT*/Addr *end )
{
   const NSegment *seg = ML_(am_find_segment)(addr);

   switch (seg->kind) {
   case SkFree:
      goto bad;

   case SkAnonC:
   case SkFileC:
   case SkShmC:
   case SkAnonV:
   case SkFileV:
      if (!seg->hasR || !seg->hasW) goto bad;
      *start = seg->start;
      *end = seg->end;
      return True;

   case SkResvn:
      /* If ADDR is an unmapped address in an extensible client stack
         then this is OK. */
      if (seg->smode != SmUpper) goto bad;
      *start = seg->start;
      seg = ML_(am_next_segment)(seg);
      if (!seg || seg->kind != SkAnonC || !seg->hasR || !seg->hasW) goto bad;
      *end = seg->end;
      return True;

   default:
      aspacem_assert(0);
   }

 bad:
   *start = *end = 0;
   return False;
}


/*-----------------------------------------------------------------*/
/*---                                                           ---*/
/*--- Startup, including reading /proc/self/maps.               ---*/
/*---                                                           ---*/
/*-----------------------------------------------------------------*/

static void read_maps_callback ( Addr addr, SizeT len, UInt prot,
                                 ULong dev, ULong ino, Off64T offset, 
                                 const HChar* filename )
{
   NSegment seg;
   ML_(am_init_segment)( &seg, SkFree, addr, addr + len - 1 );
   seg.dev    = dev;
   seg.ino    = ino;
   seg.offset = offset;
   seg.hasR   = toBool(prot & VKI_PROT_READ);
   seg.hasW   = toBool(prot & VKI_PROT_WRITE);
   seg.hasX   = toBool(prot & VKI_PROT_EXEC);
   seg.hasT   = False;

   /* Don't use the presence of a filename to decide if a segment in
      the initial /proc/self/maps to decide if the segment is an AnonV
      or FileV segment as some systems don't report the filename. Use
      the device and inode numbers instead. Fixes bug #124528. */
   seg.kind = SkAnonV;
   if (dev != 0 && ino != 0) 
      seg.kind = SkFileV;

#  if defined(VGO_darwin)
   // GrP fixme no dev/ino on darwin
   if (offset != 0) 
      seg.kind = SkFileV;
#  endif // defined(VGO_darwin)

#  if defined(VGP_arm_linux)
   /* The standard handling of entries read from /proc/self/maps will
      cause the faked up commpage segment to have type SkAnonV, which
      is a problem because it contains code we want the client to
      execute, and so later m_translate will segfault the client when
      it tries to go in there.  Hence change the ownership of it here
      to the client (SkAnonC).  The least-worst kludge I could think
      of. */
   if (addr == ARM_LINUX_FAKE_COMMPAGE_START
       && addr + len == ARM_LINUX_FAKE_COMMPAGE_END1
       && seg.kind == SkAnonV)
      seg.kind = SkAnonC;
#  endif // defined(VGP_arm_linux)

   if (filename)
      seg.fnIdx = ML_(am_allocate_segname)( filename );

   ML_(am_add_segment)( &seg );
}

Bool
VG_(am_is_valid_for_aspacem_minAddr)( Addr addr, const HChar **errmsg )
{
   const Addr min = VKI_PAGE_SIZE;
#if VG_WORDSIZE == 4
   const Addr max = 0x40000000;  // 1Gb
#else
   const Addr max = 0x200000000; // 8Gb
#endif
   Bool ok = VG_IS_PAGE_ALIGNED(addr) && addr >= min && addr <= max;

   if (errmsg) {
      *errmsg = "";
      if (! ok) {
         const HChar fmt[] = "Must be a page aligned address between "
                             "0x%lx and 0x%lx";
         static HChar buf[sizeof fmt + 2 * 16];   // large enough
         ML_(am_sprintf)(buf, fmt, min, max);
         *errmsg = buf;
      }
   }
   return ok;
}


/* Overall memory layout and initial segments:

            |<------------ addressable  ----------->|
            |                                       |
 Addr_MIN   |<---- client ---->|<---- valgrind ---->|     Addr_MAX
      +-----+------------------+-+------------------+-----+
      |  R  |                  |R|                  |  R  |
      +-----+------------------+-+------------------+-----+
      0     ^                  ^                    ^     ff....ff
            |                  |                    |
            |                  |                    +--- aspacem_maxAddr
            |                  +--- aspacem_vStart
            +--- aspacem_cStart
            |
            +--- aspacem_minAddr

   The function returns the highest addressable byte in the client stack.
*/
Addr VG_(am_startup) ( Addr sp_at_startup )
{
   NSegment seg;
   Addr     suggested_clstack_end;

   aspacem_assert(sizeof(Word)   == sizeof(void*));
   aspacem_assert(sizeof(Addr)   == sizeof(void*));
   aspacem_assert(sizeof(SizeT)  == sizeof(void*));
   aspacem_assert(sizeof(SSizeT) == sizeof(void*));

   /* Check that we can store the largest imaginable dev, ino and
      offset numbers in an NSegment. */
   aspacem_assert(sizeof(seg.dev)    == 8);
   aspacem_assert(sizeof(seg.ino)    == 8);
   aspacem_assert(sizeof(seg.offset) == 8);
   aspacem_assert(sizeof(seg.mode)   == 4);

   /* Initialise the string table for segment names. */
   ML_(am_segnames_init)();

   /* Initialise the segment structure. */
   ML_(am_segments_init)();

   aspacem_minAddr = VG_(clo_aspacem_minAddr);

#if defined(VGO_darwin)

# if VG_WORDSIZE == 4
   aspacem_maxAddr = (Addr) 0xffffffff;

   aspacem_cStart = aspacem_minAddr;
   aspacem_vStart = 0xf0000000;  // 0xc0000000..0xf0000000 available
# else
   aspacem_maxAddr = (Addr) 0x7fffffffffff;

   aspacem_cStart = aspacem_minAddr;
   aspacem_vStart = 0x700000000000; // 0x7000:00000000..0x7fff:5c000000 avail
   // 0x7fff:5c000000..0x7fff:ffe00000? is stack, dyld, shared cache
# endif

   suggested_clstack_end = -1; // ignored; Mach-O specifies its stack

#else /* !defined(VGO_darwin) */

   /* Establish address limits and block out unusable parts
      accordingly. */

   VG_(debugLog)(2, "aspacem", 
                    "        sp_at_startup = 0x%010llx (supplied)\n", 
                    (ULong)sp_at_startup );

#  if VG_WORDSIZE == 8
     aspacem_maxAddr = (Addr)0x1000000000ULL - 1; // 64G
#    ifdef ENABLE_INNER
     { Addr cse = VG_PGROUNDDN( sp_at_startup ) - 1;
       if (aspacem_maxAddr > cse)
          aspacem_maxAddr = cse;
     }
#    endif
#  else
     aspacem_maxAddr = VG_PGROUNDDN( sp_at_startup ) - 1;
#  endif

   aspacem_cStart = aspacem_minAddr;
   aspacem_vStart = VG_PGROUNDUP(aspacem_minAddr 
                                 + (aspacem_maxAddr - aspacem_minAddr + 1) / 2);
#  ifdef ENABLE_INNER
   aspacem_vStart -= 0x10000000; // 256M
#  endif

   suggested_clstack_end = aspacem_maxAddr - 16*1024*1024ULL
                                           + VKI_PAGE_SIZE;

#endif /* #else of 'defined(VGO_darwin)' */

   aspacem_assert(VG_IS_PAGE_ALIGNED(aspacem_minAddr));
   aspacem_assert(VG_IS_PAGE_ALIGNED(aspacem_maxAddr + 1));
   aspacem_assert(VG_IS_PAGE_ALIGNED(aspacem_cStart));
   aspacem_assert(VG_IS_PAGE_ALIGNED(aspacem_vStart));
   aspacem_assert(VG_IS_PAGE_ALIGNED(suggested_clstack_end + 1));

   VG_(debugLog)(2, "aspacem", 
                    "              minAddr = 0x%010llx (computed)\n", 
                    (ULong)aspacem_minAddr);
   VG_(debugLog)(2, "aspacem", 
                    "              maxAddr = 0x%010llx (computed)\n", 
                    (ULong)aspacem_maxAddr);
   VG_(debugLog)(2, "aspacem", 
                    "               cStart = 0x%010llx (computed)\n", 
                    (ULong)aspacem_cStart);
   VG_(debugLog)(2, "aspacem", 
                    "               vStart = 0x%010llx (computed)\n", 
                    (ULong)aspacem_vStart);
   VG_(debugLog)(2, "aspacem", 
                    "suggested_clstack_end = 0x%010llx (computed)\n", 
                    (ULong)suggested_clstack_end);

   if (aspacem_cStart > Addr_MIN) {
      ML_(am_init_segment)(&seg, SkResvn, Addr_MIN, aspacem_cStart - 1);
      ML_(am_add_segment)(&seg);
   }
   if (aspacem_maxAddr < Addr_MAX) {
      ML_(am_init_segment)(&seg, SkResvn, aspacem_maxAddr + 1, Addr_MAX);
      ML_(am_add_segment)(&seg);
   }

   /* Create a 1-page reservation at the notional initial
      client/valgrind boundary.  This isn't strictly necessary, but
      because the advisor does first-fit and starts searches for
      valgrind allocations at the boundary, this is kind of necessary
      in order to get it to start allocating in the right place. */
   ML_(am_init_segment)( &seg, SkResvn, aspacem_vStart,
                         aspacem_vStart + VKI_PAGE_SIZE - 1 );
   ML_(am_add_segment)(&seg);

   VG_(am_show_nsegments)(2, "Initial layout");

   VG_(debugLog)(2, "aspacem", "Reading /proc/self/maps\n");
   parse_procselfmaps( read_maps_callback, NULL );
   /* NB: on arm-linux, parse_procselfmaps automagically kludges up
      (iow, hands to its callbacks) a description of the ARM Commpage,
      since that's not listed in /proc/self/maps (kernel bug IMO).  We
      have to fake up its existence in parse_procselfmaps and not
      merely add it here as an extra segment, because doing the latter
      causes sync checking to fail: we see we have an extra segment in
      the segments array, which isn't listed in /proc/self/maps.
      Hence we must make it appear that /proc/self/maps contained this
      segment all along.  Sigh. */

   VG_(am_show_nsegments)(2, "With contents of /proc/self/maps");

   AM_SANITY_CHECK();
   return suggested_clstack_end;
}


/*-----------------------------------------------------------------*/
/*---                                                           ---*/
/*--- The core query-notify mechanism.                          ---*/
/*---                                                           ---*/
/*-----------------------------------------------------------------*/

/* Query aspacem to ask where a mapping should go. */

Addr VG_(am_get_advisory) ( const MapRequest*  req, 
                            Bool  forClient, 
                            /*OUT*/Bool* ok )
{
   /* This function implements allocation policy.

      The nature of the allocation request is determined by req, which
      specifies the start and length of the request and indicates
      whether the start address is mandatory, a hint, or irrelevant,
      and by forClient, which says whether this is for the client or
      for V. 

      Return values: the request can be vetoed (*ok is set to False),
      in which case the caller should not attempt to proceed with
      making the mapping.  Otherwise, *ok is set to True, the caller
      may proceed, and the preferred address at which the mapping
      should happen is returned.

      Note that this is an advisory system only: the kernel can in
      fact do whatever it likes as far as placement goes, and we have
      no absolute control over it.

      Allocations will never be granted in a reserved area.

      The Default Policy is:

        Search the address space for two free intervals: one of them
        big enough to contain the request without regard to the
        specified address (viz, as if it was a floating request) and
        the other being able to contain the request at the specified
        address (viz, as if were a fixed request).  Then, depending on
        the outcome of the search and the kind of request made, decide
        whether the request is allowable and what address to advise.

      The Default Policy is overriden by Policy Exception #1:

        If the request is for a fixed client map, we are prepared to
        grant it providing all areas inside the request are either
        free, reservations, or mappings belonging to the client.  In
        other words we are prepared to let the client trash its own
        mappings if it wants to.

      The Default Policy is overriden by Policy Exception #2:

        If the request is for a hinted client map, we are prepared to
        grant it providing all areas inside the request are either
        free or reservations.  In other words we are prepared to let 
        the client have a hinted mapping anywhere it likes provided
        it does not trash either any of its own mappings or any of 
        valgrind's mappings.
   */
   Addr startPoint = forClient ? aspacem_cStart : aspacem_vStart;

   Addr reqStart = req->rkind==MAny ? 0 : req->start;
   Addr reqEnd   = reqStart + req->len - 1;
   Addr reqLen   = req->len;

   if (0) {
      VG_(am_show_nsegments)(0,"getAdvisory");
      VG_(debugLog)(0,"aspacem", "getAdvisory 0x%llx %lld\n", 
                      (ULong)req->start, (ULong)req->len);
   }

   /* Reject zero-length requests */
   if (req->len == 0) {
      *ok = False;
      return 0;
   }

   /* Reject wraparounds */
   if ((req->rkind==MFixed || req->rkind==MHint)
       && req->start + req->len < req->start) {
      *ok = False;
      return 0;
   }

   /* ------ Implement Policy Exception #1 ------ */

   if (forClient && req->rkind == MFixed) {
      const NSegment *segLo = ML_(am_find_segment)(reqStart);
      const NSegment *segHi = ML_(am_find_segment)(reqEnd);
      Bool allow = True;
      for (const NSegment *seg = segLo; True; seg = ML_(am_next_segment)(seg)) {
         if (seg->kind == SkFree
             || seg->kind == SkFileC
             || seg->kind == SkAnonC
             || seg->kind == SkShmC
             || seg->kind == SkResvn) {
            /* ok */
         } else {
            allow = False;
            break;
         }
         if (seg == segHi) break;
      }
      if (allow) {
         /* Acceptable.  Granted. */
         *ok = True;
         return reqStart;
      }
      /* Not acceptable.  Fail. */
      *ok = False;
      return 0;
   }

   /* ------ Implement Policy Exception #2 ------ */

   if (forClient && req->rkind == MHint) {
      const NSegment *segLo = ML_(am_find_segment)(reqStart);
      const NSegment *segHi = ML_(am_find_segment)(reqEnd);
      Bool allow = True;
      for (const NSegment *seg = segLo; True; seg = ML_(am_next_segment)(seg)) {
         if (seg->kind == SkFree || seg->kind == SkResvn) {
            /* ok */
         } else {
            allow = False;
            break;
         }
         if (seg == segHi) break;
      }
      if (allow) {
         /* Acceptable.  Granted. */
         *ok = True;
         return reqStart;
      }
      /* Not acceptable.  Fall through to the default policy. */
   }

   /* ------ Implement the Default Policy ------ */

   /* These are segments found during search, or NULL if not found. */
   const NSegment *floatSeg = NULL;
   const NSegment *fixedSeg = NULL;

   /* Don't waste time looking for a fixed match if not requested to. */
   Bool fixed_not_required = req->rkind == MAny;

   const NSegment *seg = ML_(am_find_segment)(startPoint);
   const NSegment *sentinel = ML_(am_prev_segment)(seg);

   /* Examine holes from index i back round to i-1.  Record the
      index first fixed hole and the first floating hole which would
      satisfy the request. */
   while (True) {
      if (seg->kind == SkFree) {
         Addr holeStart = seg->start;
         Addr holeEnd   = seg->end;

         /* Stay sane .. */
         aspacem_assert(holeStart <= holeEnd);
         aspacem_assert(aspacem_minAddr <= holeStart);
         aspacem_assert(holeEnd <= aspacem_maxAddr);

         /* See if it's any use to us. */
         Addr holeLen = holeEnd - holeStart + 1;  // FIXME: SizeT

         if (fixedSeg == NULL && holeStart <= reqStart && reqEnd <= holeEnd)
            fixedSeg = seg;

         if (floatSeg == NULL && holeLen >= reqLen)
            floatSeg = seg;
  
         /* Don't waste time searching once we've found what we wanted. */
         if ((fixed_not_required || fixedSeg != NULL) && floatSeg != NULL)
            break;
      }
      if (seg == sentinel) break;           // we're done
      seg = ML_(am_next_segment)(seg);
      if (seg == NULL) seg = ML_(am_find_segment)(Addr_MIN);  // wrap around
   }

   if (fixedSeg != NULL) 
      aspacem_assert(fixedSeg->kind == SkFree);

   if (floatSeg != NULL) 
      aspacem_assert(floatSeg->kind == SkFree);

   AM_SANITY_CHECK();

   /* Now see if we found anything which can satisfy the request. */
   switch (req->rkind) {
      case MFixed:
         if (fixedSeg != NULL) {
            *ok = True;
            return req->start;
         } else {
            *ok = False;
            return 0;
         }
         break;
      case MHint:
         if (fixedSeg != NULL) {
            *ok = True;
            return req->start;
         }
         if (floatSeg != NULL) {
            *ok = True;
            return floatSeg->start;
         }
         *ok = False;
         return 0;
      case MAny:
         if (floatSeg != NULL) {
            *ok = True;
            return floatSeg->start;
         }
         *ok = False;
         return 0;
      default: 
         break;
   }

   /*NOTREACHED*/
   ML_(am_barf)("getAdvisory: unknown request kind");
   *ok = False;
   return 0;
}

/* Convenience wrapper for VG_(am_get_advisory) for client floating or
   fixed requests.  If start is zero, a floating request is issued; if
   nonzero, a fixed request at that address is issued.  Same comments
   about return values apply. */

Addr VG_(am_get_advisory_client_simple) ( Addr start, SizeT len, 
                                          /*OUT*/Bool* ok )
{
   MapRequest mreq;
   mreq.rkind = start==0 ? MAny : MFixed;
   mreq.start = start;
   mreq.len   = len;
   return VG_(am_get_advisory)( &mreq, True/*forClient*/, ok );
}

/* Similar to VG_(am_find_nsegment) but only returns free segments. */
static NSegment const * VG_(am_find_free_nsegment) ( Addr a )
{
   const NSegment *seg = ML_(am_find_segment)(a);

   aspacem_assert(seg->start <= a);
   aspacem_assert(a <= seg->end);

   return seg->kind == SkFree ? seg : NULL;
}

Bool VG_(am_covered_by_single_free_segment)
   ( Addr start, SizeT len)
{
   NSegment const* segLo = VG_(am_find_free_nsegment)( start );
   NSegment const* segHi = VG_(am_find_free_nsegment)( start + len - 1 );

   return segLo != NULL && segHi != NULL && segLo == segHi;
}


/* Notifies aspacem that the client completed an mmap successfully.
   The segment array is updated accordingly.  If the returned Bool is
   True, the caller should immediately discard translations from the
   specified address range. */

Bool
VG_(am_notify_client_mmap)( Addr a, SizeT len, UInt prot, UInt flags,
                            Int fd, Off64T offset )
{
   HChar    buf[VKI_PATH_MAX];
   ULong    dev, ino;
   UInt     mode;
   NSegment seg;
   Bool     needDiscard;

   aspacem_assert(len > 0);
   aspacem_assert(VG_IS_PAGE_ALIGNED(a));
   aspacem_assert(VG_IS_PAGE_ALIGNED(len));
   aspacem_assert(VG_IS_PAGE_ALIGNED(offset));

   /* Discard is needed if any of the just-trashed range had T. */
   needDiscard = any_Ts_in_range( a, len );

   ML_(am_init_segment)( &seg, (flags & VKI_MAP_ANONYMOUS) ? SkAnonC : SkFileC,
                         a, a + len - 1 );
   seg.hasR   = toBool(prot & VKI_PROT_READ);
   seg.hasW   = toBool(prot & VKI_PROT_WRITE);
   seg.hasX   = toBool(prot & VKI_PROT_EXEC);
   if (!(flags & VKI_MAP_ANONYMOUS)) {
      // Nb: We ignore offset requests in anonymous mmaps (see bug #126722)
      seg.offset = offset;
      if (ML_(am_get_fd_d_i_m)(fd, &dev, &ino, &mode)) {
         seg.dev = dev;
         seg.ino = ino;
         seg.mode = mode;
      }
      if (ML_(am_resolve_filename)(fd, buf, VKI_PATH_MAX)) {
         seg.fnIdx = ML_(am_allocate_segname)( buf );
      }
   }
   ML_(am_add_segment)( &seg );
   AM_SANITY_CHECK();
   return needDiscard;
}

/* Notifies aspacem that the client completed a shmat successfully.
   The segment array is updated accordingly.  If the returned Bool is
   True, the caller should immediately discard translations from the
   specified address range. */

Bool
VG_(am_notify_client_shmat)( Addr a, SizeT len, UInt prot )
{
   NSegment seg;
   Bool     needDiscard;

   aspacem_assert(len > 0);
   aspacem_assert(VG_IS_PAGE_ALIGNED(a));
   aspacem_assert(VG_IS_PAGE_ALIGNED(len));

   /* Discard is needed if any of the just-trashed range had T. */
   needDiscard = any_Ts_in_range( a, len );

   ML_(am_init_segment)( &seg, SkShmC, a, a + len - 1 );
   seg.hasR   = toBool(prot & VKI_PROT_READ);
   seg.hasW   = toBool(prot & VKI_PROT_WRITE);
   seg.hasX   = toBool(prot & VKI_PROT_EXEC);
   ML_(am_add_segment)( &seg );
   AM_SANITY_CHECK();
   return needDiscard;
}

/* Notifies aspacem that an mprotect was completed successfully.  The
   segment array is updated accordingly.  Note, as with
   VG_(am_notify_munmap), it is not the job of this function to reject
   stupid mprotects, for example the client doing mprotect of
   non-client areas.  Such requests should be intercepted earlier, by
   the syscall wrapper for mprotect.  This function merely records
   whatever it is told.  If the returned Bool is True, the caller
   should immediately discard translations from the specified address
   range. */

Bool VG_(am_notify_mprotect)( Addr start, SizeT len, UInt prot )
{
   aspacem_assert(VG_IS_PAGE_ALIGNED(start));
   aspacem_assert(VG_IS_PAGE_ALIGNED(len));

   if (len == 0)
      return False;

   /* Discard is needed if we're dumping X permission */
   Bool newX = (prot & VKI_PROT_EXEC) != 0;
   Bool needDiscard = any_Ts_in_range( start, len ) && !newX;

   ML_(am_change_permissions)( start, len, prot );

   AM_SANITY_CHECK();
   return needDiscard;
}


static void am_notify_munmap( Addr start, SizeT len )
{
   NSegment seg;
   aspacem_assert(VG_IS_PAGE_ALIGNED(start));
   aspacem_assert(VG_IS_PAGE_ALIGNED(len));
   aspacem_assert(len > 0);

   ML_(am_init_segment)( &seg, SkFree, start, start + len - 1 );

   /* The segment becomes unused (free).  Segments from above
      aspacem_maxAddr were originally SkResvn and so we make them so
      again.  Note, this isn't really right when the segment straddles
      the aspacem_maxAddr boundary - then really it should be split in
      two, the lower part marked as SkFree and the upper part as
      SkResvn.  Ah well. */
   if (start > aspacem_maxAddr 
       && /* check previous comparison is meaningful */
          aspacem_maxAddr < Addr_MAX)
      seg.kind = SkResvn;
   else 
      /* Ditto for segments from below aspacem_minAddr. */
      if (seg.end < aspacem_minAddr && aspacem_minAddr >= Addr_MIN)
         seg.kind = SkResvn;
      else
         seg.kind = SkFree;

   ML_(am_add_segment)( &seg );

   AM_SANITY_CHECK();
}

/* Notifies aspacem that an munmap completed successfully.  The
   segment array is updated accordingly.  As with
   VG_(am_notify_mprotect), we merely record the given info, and don't
   check it for sensibleness.  If the returned Bool is True, the
   caller should immediately discard translations from the specified
   address range. */

Bool VG_(am_notify_munmap)( Addr start, SizeT len )
{
   Bool     needDiscard;
   aspacem_assert(VG_IS_PAGE_ALIGNED(start));
   aspacem_assert(VG_IS_PAGE_ALIGNED(len));

   if (len == 0)
      return False;

   needDiscard = any_Ts_in_range( start, len );

   am_notify_munmap( start, len );

   return needDiscard;
}


/*-----------------------------------------------------------------*/
/*---                                                           ---*/
/*--- Handling mappings which do not arise directly from the    ---*/
/*--- simulation of the client.                                 ---*/
/*---                                                           ---*/
/*-----------------------------------------------------------------*/

/* --- --- --- map, unmap, protect  --- --- --- */

/* Map a file at a fixed address for the client, and update the
   segment array accordingly. */

SysRes VG_(am_mmap_file_fixed_client)
     ( Addr start, SizeT length, UInt prot, Int fd, Off64T offset )
{
   return VG_(am_mmap_named_file_fixed_client)(start, length, prot, fd, offset, NULL);
}

SysRes VG_(am_mmap_named_file_fixed_client)
     ( Addr start, SizeT length, UInt prot, Int fd, Off64T offset, const HChar *name )
{
   SysRes     sres;
   NSegment   seg;
   Addr       advised;
   Bool       ok;
   MapRequest req;
   ULong      dev, ino;
   UInt       mode;
   HChar      buf[VKI_PATH_MAX];

   /* Not allowable. */
   if (length == 0 
       || !VG_IS_PAGE_ALIGNED(start)
       || !VG_IS_PAGE_ALIGNED(offset))
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* Ask for an advisory.  If it's negative, fail immediately. */
   req.rkind = MFixed;
   req.start = start;
   req.len   = length;
   advised = VG_(am_get_advisory)( &req, True/*forClient*/, &ok );
   if (!ok || advised != start)
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* We have been advised that the mapping is allowable at the
      specified address.  So hand it off to the kernel, and propagate
      any resulting failure immediately. */
   // DDD: #warning GrP fixme MAP_FIXED can clobber memory!
   sres = VG_(am_do_mmap_NO_NOTIFY)( 
             start, length, prot, 
             VKI_MAP_FIXED|VKI_MAP_PRIVATE, 
             fd, offset 
          );
   if (sr_isError(sres))
      return sres;

   if (sr_Res(sres) != start) {
      /* I don't think this can happen.  It means the kernel made a
         fixed map succeed but not at the requested location.  Try to
         repair the damage, then return saying the mapping failed. */
      (void)ML_(am_do_munmap_NO_NOTIFY)( sr_Res(sres), length );
      return VG_(mk_SysRes_Error)( VKI_EINVAL );
   }

   /* Ok, the mapping succeeded.  Now notify the interval map. */
   ML_(am_init_segment)( &seg, SkFileC, start,
                         start + VG_PGROUNDUP(length) - 1 );
   seg.offset = offset;
   seg.hasR   = toBool(prot & VKI_PROT_READ);
   seg.hasW   = toBool(prot & VKI_PROT_WRITE);
   seg.hasX   = toBool(prot & VKI_PROT_EXEC);
   if (ML_(am_get_fd_d_i_m)(fd, &dev, &ino, &mode)) {
      seg.dev = dev;
      seg.ino = ino;
      seg.mode = mode;
   }
   if (name) {
      seg.fnIdx = ML_(am_allocate_segname)( name );
   } else if (ML_(am_resolve_filename)(fd, buf, VKI_PATH_MAX)) {
      seg.fnIdx = ML_(am_allocate_segname)( buf );
   }
   ML_(am_add_segment)( &seg );

   AM_SANITY_CHECK();
   return sres;
}


/* Map anonymously at a fixed address for the client, and update
   the segment array accordingly. */

SysRes VG_(am_mmap_anon_fixed_client) ( Addr start, SizeT length, UInt prot )
{
   SysRes     sres;
   NSegment   seg;
   Addr       advised;
   Bool       ok;
   MapRequest req;
 
   /* Not allowable. */
   if (length == 0 || !VG_IS_PAGE_ALIGNED(start))
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* Ask for an advisory.  If it's negative, fail immediately. */
   req.rkind = MFixed;
   req.start = start;
   req.len   = length;
   advised = VG_(am_get_advisory)( &req, True/*forClient*/, &ok );
   if (!ok || advised != start)
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* We have been advised that the mapping is allowable at the
      specified address.  So hand it off to the kernel, and propagate
      any resulting failure immediately. */
   // DDD: #warning GrP fixme MAP_FIXED can clobber memory!
   sres = VG_(am_do_mmap_NO_NOTIFY)( 
             start, length, prot, 
             VKI_MAP_FIXED|VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, 
             0, 0 
          );
   if (sr_isError(sres))
      return sres;

   if (sr_Res(sres) != start) {
      /* I don't think this can happen.  It means the kernel made a
         fixed map succeed but not at the requested location.  Try to
         repair the damage, then return saying the mapping failed. */
      (void)ML_(am_do_munmap_NO_NOTIFY)( sr_Res(sres), length );
      return VG_(mk_SysRes_Error)( VKI_EINVAL );
   }

   /* Ok, the mapping succeeded.  Now notify the interval map. */
   ML_(am_init_segment)( &seg, SkAnonC, start,
                         start + VG_PGROUNDUP(length) - 1 );
   seg.hasR  = toBool(prot & VKI_PROT_READ);
   seg.hasW  = toBool(prot & VKI_PROT_WRITE);
   seg.hasX  = toBool(prot & VKI_PROT_EXEC);
   ML_(am_add_segment)( &seg );

   AM_SANITY_CHECK();
   return sres;
}


/* Map anonymously at an unconstrained address for the client, and
   update the segment array accordingly.  */

SysRes VG_(am_mmap_anon_float_client) ( SizeT length, Int prot )
{
   SysRes     sres;
   NSegment   seg;
   Addr       advised;
   Bool       ok;
   MapRequest req;
 
   /* Not allowable. */
   if (length == 0)
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* Ask for an advisory.  If it's negative, fail immediately. */
   req.rkind = MAny;
   req.start = 0;
   req.len   = length;
   advised = VG_(am_get_advisory)( &req, True/*forClient*/, &ok );
   if (!ok)
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* We have been advised that the mapping is allowable at the
      advised address.  So hand it off to the kernel, and propagate
      any resulting failure immediately. */
   // DDD: #warning GrP fixme MAP_FIXED can clobber memory!
   sres = VG_(am_do_mmap_NO_NOTIFY)( 
             advised, length, prot, 
             VKI_MAP_FIXED|VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, 
             0, 0 
          );
   if (sr_isError(sres))
      return sres;

   if (sr_Res(sres) != advised) {
      /* I don't think this can happen.  It means the kernel made a
         fixed map succeed but not at the requested location.  Try to
         repair the damage, then return saying the mapping failed. */
      (void)ML_(am_do_munmap_NO_NOTIFY)( sr_Res(sres), length );
      return VG_(mk_SysRes_Error)( VKI_EINVAL );
   }

   /* Ok, the mapping succeeded.  Now notify the interval map. */
   ML_(am_init_segment)( &seg, SkAnonC, advised,
                         advised + VG_PGROUNDUP(length) - 1 );
   seg.hasR  = toBool(prot & VKI_PROT_READ);
   seg.hasW  = toBool(prot & VKI_PROT_WRITE);
   seg.hasX  = toBool(prot & VKI_PROT_EXEC);
   ML_(am_add_segment)( &seg );

   AM_SANITY_CHECK();
   return sres;
}


/* Map anonymously at an unconstrained address for V, and update the
   segment array accordingly.  This is fundamentally how V allocates
   itself more address space when needed. */

SysRes VG_(am_mmap_anon_float_valgrind)( SizeT length )
{
   SysRes     sres;
   NSegment   seg;
   Addr       advised;
   Bool       ok;
   MapRequest req;
 
   /* Not allowable. */
   if (length == 0)
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* Ask for an advisory.  If it's negative, fail immediately. */
   req.rkind = MAny;
   req.start = 0;
   req.len   = length;
   advised = VG_(am_get_advisory)( &req, False/*forClient*/, &ok );
   if (!ok)
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

// On Darwin, for anonymous maps you can pass in a tag which is used by
// programs like vmmap for statistical purposes.
#ifndef VM_TAG_VALGRIND
#  define VM_TAG_VALGRIND 0
#endif

   /* We have been advised that the mapping is allowable at the
      specified address.  So hand it off to the kernel, and propagate
      any resulting failure immediately. */
   /* GrP fixme darwin: use advisory as a hint only, otherwise syscall in 
      another thread can pre-empt our spot.  [At one point on the DARWIN
      branch the VKI_MAP_FIXED was commented out;  unclear if this is
      necessary or not given the second Darwin-only call that immediately
      follows if this one fails.  --njn]
      Also, an inner valgrind cannot observe the mmap syscalls done by
      the outer valgrind. The outer Valgrind might make the mmap
      fail here, as the inner valgrind believes that a segment is free,
      while it is in fact used by the outer valgrind.
      So, for an inner valgrind, similarly to DARWIN, if the fixed mmap
      fails, retry the mmap without map fixed.
      This is a kludge which on linux is only activated for the inner.
      The state of the inner aspacemgr is not made correct by this kludge
      and so a.o. VG_(am_do_sync_check) could fail.
      A proper solution implies a better collaboration between the
      inner and the outer (e.g. inner VG_(am_get_advisory) should do
      a client request to call the outer VG_(am_get_advisory). */
   sres = VG_(am_do_mmap_NO_NOTIFY)( 
             advised, length, 
             VKI_PROT_READ|VKI_PROT_WRITE|VKI_PROT_EXEC, 
             VKI_MAP_FIXED|VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, 
             VM_TAG_VALGRIND, 0
          );
#if defined(VGO_darwin) || defined(ENABLE_INNER)
   /* Kludge on Darwin and inner linux if the fixed mmap failed. */
   if (sr_isError(sres)) {
       /* try again, ignoring the advisory */
       sres = VG_(am_do_mmap_NO_NOTIFY)( 
             0, length, 
             VKI_PROT_READ|VKI_PROT_WRITE|VKI_PROT_EXEC, 
             /*VKI_MAP_FIXED|*/VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, 
             VM_TAG_VALGRIND, 0
          );
   }
#endif
   if (sr_isError(sres))
      return sres;

#if defined(VGO_linux) && !defined(ENABLE_INNER)
   /* Doing the check only in linux not inner, as the below
      check can fail when the kludge above has been used. */
   if (sr_Res(sres) != advised) {
      /* I don't think this can happen.  It means the kernel made a
         fixed map succeed but not at the requested location.  Try to
         repair the damage, then return saying the mapping failed. */
      (void)ML_(am_do_munmap_NO_NOTIFY)( sr_Res(sres), length );
      return VG_(mk_SysRes_Error)( VKI_EINVAL );
   }
#endif

   /* Ok, the mapping succeeded.  Now notify the interval map. */
   Addr start = sr_Res(sres);
   ML_(am_init_segment)( &seg, SkAnonV, start,
                         start + VG_PGROUNDUP(length) - 1 );
   seg.hasR  = True;
   seg.hasW  = True;
   seg.hasX  = True;
   ML_(am_add_segment)( &seg );

   AM_SANITY_CHECK();
   return sres;
}

/* Really just a wrapper around VG_(am_mmap_anon_float_valgrind). */

void* VG_(am_shadow_alloc)(SizeT size)
{
   SysRes sres = VG_(am_mmap_anon_float_valgrind)( size );
   return sr_isError(sres) ? NULL : (void*)sr_Res(sres);
}

/* Map a file at an unconstrained address for V, and update the
   segment array accordingly. Use the provided flags */

static SysRes VG_(am_mmap_file_float_valgrind_flags) ( SizeT length, UInt prot,
                                                       UInt flags,
                                                       Int fd, Off64T offset )
{
   SysRes     sres;
   NSegment   seg;
   Addr       advised;
   Bool       ok;
   MapRequest req;
   ULong      dev, ino;
   UInt       mode;
   HChar      buf[VKI_PATH_MAX];
 
   /* Not allowable. */
   if (length == 0 || !VG_IS_PAGE_ALIGNED(offset))
      return VG_(mk_SysRes_Error)( VKI_EINVAL );

   /* Ask for an advisory.  If it's negative, fail immediately. */
   req.rkind = MAny;
   req.start = 0;
   #if defined(VGA_arm) || defined(VGA_arm64) \
      || defined(VGA_mips32) || defined(VGA_mips64)
   aspacem_assert(VKI_SHMLBA >= VKI_PAGE_SIZE);
   #else
   aspacem_assert(VKI_SHMLBA == VKI_PAGE_SIZE);
   #endif
   if ((VKI_SHMLBA > VKI_PAGE_SIZE) && (VKI_MAP_SHARED & flags)) {
      /* arm-linux only. See ML_(generic_PRE_sys_shmat) and bug 290974 */
      req.len = length + VKI_SHMLBA - VKI_PAGE_SIZE;
   } else {
      req.len = length;
   }
   advised = VG_(am_get_advisory)( &req, False/*forClient*/, &ok );
   if (!ok)
      return VG_(mk_SysRes_Error)( VKI_EINVAL );
   if ((VKI_SHMLBA > VKI_PAGE_SIZE) && (VKI_MAP_SHARED & flags))
      advised = VG_ROUNDUP(advised, VKI_SHMLBA);

   /* We have been advised that the mapping is allowable at the
      specified address.  So hand it off to the kernel, and propagate
      any resulting failure immediately. */
   sres = VG_(am_do_mmap_NO_NOTIFY)( 
             advised, length, prot, 
             flags,
             fd, offset 
          );
   if (sr_isError(sres))
      return sres;

   if (sr_Res(sres) != advised) {
      /* I don't think this can happen.  It means the kernel made a
         fixed map succeed but not at the requested location.  Try to
         repair the damage, then return saying the mapping failed. */
      (void)ML_(am_do_munmap_NO_NOTIFY)( sr_Res(sres), length );
      return VG_(mk_SysRes_Error)( VKI_EINVAL );
   }

   /* Ok, the mapping succeeded.  Now notify the interval map. */
   Addr start = sr_Res(sres);
   ML_(am_init_segment)( &seg, SkFileV, start,
                         start + VG_PGROUNDUP(length) - 1 );
   seg.offset = offset;
   seg.hasR   = toBool(prot & VKI_PROT_READ);
   seg.hasW   = toBool(prot & VKI_PROT_WRITE);
   seg.hasX   = toBool(prot & VKI_PROT_EXEC);
   if (ML_(am_get_fd_d_i_m)(fd, &dev, &ino, &mode)) {
      seg.dev  = dev;
      seg.ino  = ino;
      seg.mode = mode;
   }
   if (ML_(am_resolve_filename)(fd, buf, VKI_PATH_MAX)) {
      seg.fnIdx = ML_(am_allocate_segname)( buf );
   }
   ML_(am_add_segment)( &seg );

   AM_SANITY_CHECK();
   return sres;
}
/* Map privately a file at an unconstrained address for V, and update the
   segment array accordingly.  This is used by V for transiently
   mapping in object files to read their debug info.  */

SysRes VG_(am_mmap_file_float_valgrind) ( SizeT length, UInt prot, 
                                          Int fd, Off64T offset )
{
   return VG_(am_mmap_file_float_valgrind_flags) (length, prot,
                                                  VKI_MAP_FIXED|VKI_MAP_PRIVATE,
                                                  fd, offset );
}

SysRes VG_(am_shared_mmap_file_float_valgrind)
   ( SizeT length, UInt prot, Int fd, Off64T offset )
{
   return VG_(am_mmap_file_float_valgrind_flags) (length, prot,
                                                  VKI_MAP_FIXED|VKI_MAP_SHARED,
                                                  fd, offset );
}

/* Convenience wrapper around VG_(am_mmap_anon_float_client) which also
   marks the segment as containing the client heap. This is for the benefit
   of the leak checker which needs to be able to identify such segments
   so as not to use them as sources of roots during leak checks. */
SysRes VG_(am_mmap_client_heap) ( SizeT length, Int prot )
{
   SysRes res = VG_(am_mmap_anon_float_client)(length, prot);

   if (! sr_isError(res)) {
      Addr addr = sr_Res(res);
      NSegment *seg = ML_(am_find_segment)(addr);

      seg->whatsit = WiClientHeap;
   }
   return res;
}

/* --- --- munmap helper --- --- */

static 
SysRes am_munmap_both_wrk ( /*OUT*/Bool* need_discard,
                            Addr start, SizeT len, Bool forClient )
{
   Bool   d;
   SysRes sres;

   *need_discard = False;

   if (!VG_IS_PAGE_ALIGNED(start))
      goto eINVAL;

   if (len == 0) {
      return VG_(mk_SysRes_Success)( 0 );
   }

   if (start + len < len)
      goto eINVAL;

   len = VG_PGROUNDUP(len);
   aspacem_assert(VG_IS_PAGE_ALIGNED(start));
   aspacem_assert(VG_IS_PAGE_ALIGNED(len));

   if (forClient) {
      if (!VG_(am_is_allowed_for_client)( start, len, VKI_PROT_NONE ))
         goto eINVAL;
   } else {
      if (!VG_(am_is_valid_for_valgrind)
            ( start, len, VKI_PROT_NONE ))
         goto eINVAL;
   }

   d = any_Ts_in_range( start, len );

   sres = ML_(am_do_munmap_NO_NOTIFY)( start, len );
   if (sr_isError(sres))
      return sres;

   am_notify_munmap( start, len );

   *need_discard = d;
   return sres;

  eINVAL:
   return VG_(mk_SysRes_Error)( VKI_EINVAL );
}

/* Unmap the given address range and update the segment array
   accordingly.  This fails if the range isn't valid for the client.
   If *need_discard is True after a successful return, the caller
   should immediately discard translations from the specified address
   range. */

SysRes VG_(am_munmap_client)( /*OUT*/Bool* need_discard,
                              Addr start, SizeT len )
{
   return am_munmap_both_wrk( need_discard, start, len, True/*client*/ );
}

/* Unmap the given address range and update the segment array
   accordingly.  This fails if the range isn't valid for valgrind. */

SysRes VG_(am_munmap_valgrind)( Addr start, SizeT len )
{
   Bool need_discard;
   SysRes r = am_munmap_both_wrk( &need_discard, 
                                  start, len, False/*valgrind*/ );
   /* If this assertion fails, it means we allowed translations to be
      made from a V-owned section.  Which shouldn't happen. */
   if (!sr_isError(r))
      aspacem_assert(!need_discard);
   return r;
}

/* Let (start,len) denote an area within a single Valgrind-owned
  segment (anon or file).  Change the ownership of [start, start+len)
  to the client instead.  Fails if (start,len) does not denote a
  suitable segment. */
Bool VG_(am_change_ownership_v_to_c)( Addr start, SizeT len )
{
   if (len == 0)
      return True;
   if (start + len < start)
      return False;
   if (!VG_IS_PAGE_ALIGNED(start) || !VG_IS_PAGE_ALIGNED(len))
      return False;

   return ML_(am_clientise)( start, len );
}

/* Set the 'hasT' bit on the segment containing ADDR indicating that
   translations have or may have been taken from this segment. ADDR is
   expected to belong to a client segment. */
void VG_(am_set_segment_hasT)( Addr addr )
{
   NSegment *seg = ML_(am_find_segment)(addr);
   SegKind kind = seg->kind;
   aspacem_assert(kind == SkAnonC || kind == SkFileC || kind == SkShmC);
   seg->hasT = True;
}


/* --- --- --- reservations --- --- --- */

/* Create a reservation from START .. START+LENGTH-1, with the given
   ShrinkMode.  When checking whether the reservation can be created,
   also ensure that at least abs(EXTRA) extra free bytes will remain
   above (> 0) or below (< 0) the reservation.

   The reservation will only be created if it, plus the extra-zone,
   falls entirely within a single free segment.  The returned Bool
   indicates whether the creation succeeded. */

static Bool create_reservation (Addr start, SizeT length, ShrinkMode smode,
                                SSizeT extra, WhatsIt whatsit)
{
   NSegment seg;

   /* start and end, not taking into account the extra space. */
   Addr start1 = start;
   Addr end1   = start + length - 1;

   /* start and end, taking into account the extra space. */
   Addr start2 = start1;
   Addr end2   = end1;

   if (extra < 0) start2 += extra; // this moves it down :-)
   if (extra > 0) end2 += extra;

   aspacem_assert(VG_IS_PAGE_ALIGNED(start));
   aspacem_assert(VG_IS_PAGE_ALIGNED(start+length));
   aspacem_assert(VG_IS_PAGE_ALIGNED(start2));
   aspacem_assert(VG_IS_PAGE_ALIGNED(end2+1));

   const NSegment *startSeg = ML_(am_find_segment)( start2 );
   const NSegment *endSeg   = ML_(am_find_segment)( end2 );

   /* If the start and end points don't fall within the same (free)
      segment, we're hosed.  This does rely on the assumption that all
      mergeable adjacent segments can be merged, but ML_(am_add_segment)()
      should ensure that. */
   if (startSeg != endSeg)
      return False;

   if (startSeg->kind != SkFree)
      return False;

   /* Looks good - make the reservation. */
   aspacem_assert(startSeg->start <= start2);
   aspacem_assert(end2 <= startSeg->end);

   /* NB: extra space is not included in the reservation. */
   ML_(am_init_segment)( &seg, SkResvn, start1, end1 );
   seg.smode = smode;
   seg.whatsit = whatsit;
   
   ML_(am_add_segment)( &seg );

   AM_SANITY_CHECK();
   return True;
}


/* ADDR is the start address of an anonymous client mapping.  This fn extends
   the mapping by DELTA bytes, taking the space from a reservation section
   which must be adjacent.  If DELTA is positive, the segment is
   extended forwards in the address space, and the reservation must be
   the next one along.  If DELTA is negative, the segment is extended
   backwards in the address space and the reservation must be the
   previous one.  DELTA must be page aligned.  abs(DELTA) must not
   exceed the size of the reservation segment minus one page, that is,
   the reservation segment after the operation must be at least one
   page long. The function returns a pointer to the resized segment. */

static const NSegment *
extend_into_adjacent_reservation_client (Addr addr, SSizeT delta,
                                         Bool *overflow)
{
   const NSegment *segA, *segR;
   UInt   prot;
   SysRes sres;

   *overflow = False;

   segA = ML_(am_find_segment)(addr);
   aspacem_assert(segA->kind == SkAnonC);

   if (delta == 0)
      return segA;

   prot =   (segA->hasR ? VKI_PROT_READ : 0)
          | (segA->hasW ? VKI_PROT_WRITE : 0)
          | (segA->hasX ? VKI_PROT_EXEC : 0);

   aspacem_assert(VG_IS_PAGE_ALIGNED(delta<0 ? -delta : delta));

   if (delta > 0) {

      /* Extending the segment forwards. */
      segR = ML_(am_next_segment)(segA);
      if (segR == NULL || segR->kind != SkResvn || segR->smode != SmLower)
         return NULL;

      if (delta + VKI_PAGE_SIZE 
                > (segR->end - segR->start + 1)) {
         *overflow = True;
         return NULL;
      }
        
      /* Extend the kernel's mapping. */
      // DDD: #warning GrP fixme MAP_FIXED can clobber memory!
      sres = VG_(am_do_mmap_NO_NOTIFY)( 
                segR->start, delta,
                prot,
                VKI_MAP_FIXED|VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, 
                0, 0 
             );
      if (sr_isError(sres))
         return NULL; /* kernel bug if this happens? */
      if (sr_Res(sres) != segR->start) {
         /* kernel bug if this happens? */
        (void)ML_(am_do_munmap_NO_NOTIFY)( sr_Res(sres), delta );
        return NULL;
      }

      /* Ok, success with the kernel.  Update our structures. */
      NSegment seg_copy = *segA;
      seg_copy.end += delta;
      ML_(am_add_segment)( &seg_copy );
   } else {

      /* Extending the segment backwards. */
      delta = -delta;
      aspacem_assert(delta > 0);

      segR = ML_(am_prev_segment)(segA);
      if (segR == NULL || segR->kind != SkResvn || segR->smode != SmUpper)
         return NULL;

      if ((delta + VKI_PAGE_SIZE) > (segR->end - segR->start + 1)) {
         *overflow = True;
         return NULL;
      }
        
      /* Extend the kernel's mapping. */
      // DDD: #warning GrP fixme MAP_FIXED can clobber memory!
      sres = VG_(am_do_mmap_NO_NOTIFY)( 
                segA->start - delta, delta,
                prot,
                VKI_MAP_FIXED|VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, 
                0, 0 
             );
      if (sr_isError(sres))
         return NULL; /* kernel bug if this happens? */
      if (sr_Res(sres) != segA->start - delta) {
         /* kernel bug if this happens? */
        (void)ML_(am_do_munmap_NO_NOTIFY)( sr_Res(sres), delta );
        return NULL;
      }

      /* Ok, success with the kernel.  Update our structures. */
      NSegment seg_copy = *segA;
      seg_copy.start -= delta;
      ML_(am_add_segment)( &seg_copy );
   }

   AM_SANITY_CHECK();
   return segA;
}


/*-----------------------------------------------------------------*/
/*---                                                           ---*/
/*--- Client memory segments                                    ---*/
/*---                                                           ---*/
/*-----------------------------------------------------------------*/

/* Allocate the client data (brk) segment at address BASE. The brk segment
   can be at most MAX_SIZE bytes large. It is represented as an expandable
   anonymous mapping abutted towards higher addresses by a shrinkable 
   reservation segment. The initial size of the anonymous mapping is 1 page.
   BASE is the preferred address for the data segment but cannot be
   guaranteed. Therefore, if successful, the function returns the actual
   base address of the data segment, possibly different from BASE. If the
   data segment could not be allocated the function returns an error. */
SysRes VG_(am_alloc_client_dataseg) ( Addr base, SizeT max_size, UInt prot )
{
   Bool   ok;
   Addr   anon_start  = base;
   SizeT  anon_size   = VKI_PAGE_SIZE;
   Addr   resvn_start = anon_start + anon_size;
   SizeT  resvn_size  = max_size - anon_size;

   aspacem_assert(max_size > anon_size);   // avoid wrap-around for resvn_size
   aspacem_assert(VG_IS_PAGE_ALIGNED(anon_size));
   aspacem_assert(VG_IS_PAGE_ALIGNED(resvn_size));
   aspacem_assert(VG_IS_PAGE_ALIGNED(anon_start));
   aspacem_assert(VG_IS_PAGE_ALIGNED(resvn_start));

   /* Try to create the data seg and associated reservation where
      BASE says. */
   ok = create_reservation(resvn_start, resvn_size, SmLower, anon_size,
                           WiClientBreak);

   if (!ok) {
      /* Hmm, that didn't work.  Well, let aspacem suggest an address
         it likes better, and try again with that. */
      anon_start = VG_(am_get_advisory_client_simple)
                      ( 0/*floating*/, anon_size + resvn_size, &ok );
      if (ok) {
         resvn_start = anon_start + anon_size;
         ok = create_reservation(resvn_start, resvn_size, SmLower, anon_size,
                                 WiClientBreak);
      }
   }

   /* That too might have failed, but if it has, we're hosed: there
      is no Plan C. */
   if (!ok)
      return VG_(mk_SysRes_Error)( VKI_ENOMEM );

   SysRes sres = VG_(am_mmap_anon_fixed_client)( anon_start, anon_size, prot );
   if (! sr_isError(sres)) {
      NSegment *seg = ML_(am_find_segment)(sr_Res(sres));
      seg->whatsit = WiClientBreak;
   }
   return sres;
}

/* Resize the client brk segment from OLDBRK to NEWBRK. Return an error if
   the resize operation could not be completed. A +1 error code means
   "overflow", a -1 error code means "underflow", and 0 means some other
   failure. */
SysRes VG_(am_resize_client_dataseg) ( Addr oldbrk, Addr newbrk )
{
   static Addr brk_base = 0;

   SysRes success = VG_(mk_SysRes_Success)(0);

   if (newbrk == oldbrk) return success;     // nothing to do

   /* We rely on the caller to pass in the correct address here. Ideally,
      we should keep the state (base/limit) in the address space manager and
      not at the call site. */
   if (brk_base == 0) brk_base = oldbrk;

   const NSegment *aseg = VG_(am_find_nsegment)(brk_base);
   aspacem_assert(aseg && aseg->kind == SkAnonC);

   if (newbrk < oldbrk) {
      /* Shrinking the data segment.  Be lazy and don't munmap the
         excess area. */

      /* Test for underflow. */
      if (newbrk < brk_base) return VG_(mk_SysRes_Error)(-1);

      /* Since we're being lazy and not unmapping pages, we have to
         zero out the area, so that if the area later comes back into
         circulation, it will be filled with zeroes, as if it really
         had been unmapped and later remapped.  Be a bit paranoid and
         try hard to ensure we're not going to segfault by doing the
         write - check both ends of the range are in the same segment
         and that segment is writable. */
      const NSegment *nseg = ML_(am_find_segment)(newbrk);
      aspacem_assert(nseg == aseg);
      if (aseg->hasW)
         VG_(memset)( (void*)newbrk, 0, oldbrk - newbrk );

      return success;
   }

   /* Otherwise we're expanding the brk segment. */
   if (newbrk <= aseg->end + 1) {
      /* Still fits within the anon segment. */
      return success;
   }

   Addr newbrkP = VG_PGROUNDUP(newbrk);
   SizeT delta = newbrkP - (aseg->end + 1);
   aspacem_assert(delta > 0);
   aspacem_assert(VG_IS_PAGE_ALIGNED(delta));
   
   Bool overflow;
   if (! extend_into_adjacent_reservation_client(aseg->start, delta,
                                                 &overflow)) {
      if (overflow)
         return VG_(mk_SysRes_Error)(1);
      else
         return VG_(mk_SysRes_Error)(0);
   }

   return success;
}


/* Allocate the client stack segment beginning at STACK_END. The stack segment
   can be at most MAX_SIZE bytes large. It is represented as an expandable
   anonymous mapping abutted towards lower addresses by a shrinkable 
   reservation segment. The initial size of the anonymous mapping is 1 page.
   If the stack could not be allocated the function returns an error. */
SysRes VG_(am_alloc_extensible_client_stack) ( Addr stack_end, SizeT max_size,
                                               UInt prot)
{
   Bool   ok;
   SizeT  anon_size   = VKI_PAGE_SIZE;
   Addr   anon_start  = stack_end  - anon_size;
   SizeT  resvn_size  = max_size   - anon_size;
   Addr   resvn_start = anon_start - resvn_size;

   aspacem_assert(max_size > anon_size);   // avoid wrap-around for resvn_size
   aspacem_assert(VG_IS_PAGE_ALIGNED(anon_size));
   aspacem_assert(VG_IS_PAGE_ALIGNED(resvn_size));
   aspacem_assert(VG_IS_PAGE_ALIGNED(anon_start));
   aspacem_assert(VG_IS_PAGE_ALIGNED(resvn_start));

   if (0)
      VG_(debugLog)(0, "aspacem", "%#lx 0x%lx  %#lx 0x%lx\n",
                    resvn_start, resvn_size, anon_start, anon_size);

   /* Create a shrinkable reservation followed by an anonymous
      segment.  Together these constitute a growdown stack. */
   ok = create_reservation(resvn_start, resvn_size, SmUpper, anon_size,
                           WiClientStack);
   if (ok) {
      SysRes sres = VG_(am_mmap_anon_fixed_client)(anon_start, anon_size, prot);
      if (! sr_isError(sres)) {
         NSegment *seg = ML_(am_find_segment)(sr_Res(sres));
         seg->whatsit = WiClientStack;
      }
      return sres;
   }

   return VG_(mk_SysRes_Error)( VKI_ENOMEM );
}


/* Extend the client stack such that ADDR is mapped. Unless ADDR is already
   mapped in which case nothing happens. Return an error if the stack could
   not be extended. A +1 error code means "overflow" and 0 means some other
   failure. */
SysRes VG_(am_extend_client_stack) ( Addr addr, Addr *new_stack_base )
{
   SizeT udelta;
   SysRes sres = VG_(mk_SysRes_Success)(0);
   Bool overflow = False;

   /* Get the segment containing addr. */
   const NSegment *seg = ML_(am_find_segment)(addr);

   if (seg->kind == SkAnonC) {
      /* Ought to be the extensible stack segment */
      aspacem_assert(seg->whatsit == WiClientStack);
      /* ADDR is already mapped.  Nothing to do. */
      *new_stack_base = seg->start;   // not really "new" :)
      return sres;
   }

   aspacem_assert(seg->kind == SkResvn);
   const NSegment *anon_seg = ML_(am_next_segment)(seg);
   aspacem_assert(anon_seg != NULL);

   udelta = VG_PGROUNDUP(anon_seg->start - addr);
   *new_stack_base = anon_seg->start - udelta;

   VG_(debugLog)(1, "signals", 
                    "extending a stack base 0x%lx down by %lu\n",
                    anon_seg->start, udelta);
   if (! extend_into_adjacent_reservation_client(anon_seg->start,
                                                 -(SSizeT)udelta, &overflow)) {
      if (overflow)
         sres = VG_(mk_SysRes_Error)(1);
      else
         sres = VG_(mk_SysRes_Error)(0);
   }
   return sres;
}


/* --- --- --- resizing/move a mapping --- --- --- */

#if HAVE_MREMAP

/* This function grows a client mapping in place into an adjacent free segment.
   ADDR is the client mapping's start address and DELTA, which must be page
   aligned, is the growth amount. The function returns a pointer to the
   resized segment. The function is used in support of mremap. */
const NSegment *VG_(am_extend_map_client)( Addr addr, SizeT delta )
{
   Addr     xStart;
   SysRes   sres;

   if (0)
      VG_(am_show_nsegments)(0, "VG_(am_extend_map_client) BEFORE");

   /* Get the client segment */
   const NSegment *seg = ML_(am_find_segment)(addr);

   aspacem_assert(seg->kind == SkFileC || seg->kind == SkAnonC ||
                  seg->kind == SkShmC);
   aspacem_assert(delta > 0 && VG_IS_PAGE_ALIGNED(delta)) ;

   xStart = seg->end+1;
   aspacem_assert(xStart + delta >= delta);   // no wrap-around

   /* The segment following the client segment must be a free segment and
      it must be large enough to cover the additional memory. */
   const NSegment *segf = ML_(am_next_segment)(seg);
   aspacem_assert(segf && segf->kind == SkFree);
   aspacem_assert(segf->start == xStart);
   aspacem_assert(xStart + delta - 1 <= segf->end);

   SizeT seg_old_len = seg->end + 1 - seg->start;

   AM_SANITY_CHECK();
   sres = ML_(am_do_extend_mapping_NO_NOTIFY)( seg->start, 
                                               seg_old_len,
                                               seg_old_len + delta );
   if (sr_isError(sres)) {
      AM_SANITY_CHECK();
      return NULL;
   } else {
      /* the area must not have moved */
      aspacem_assert(sr_Res(sres) == seg->start);
   }

   NSegment seg_copy = *seg;
   seg_copy.end += delta;
   ML_(am_add_segment)( &seg_copy );

   if (0)
      VG_(am_show_nsegments)(0, "VG_(am_extend_map_client) AFTER");

   AM_SANITY_CHECK();
   return ML_(am_find_segment)(addr);
}


/* Remap the old address range to the new address range.  Fails if any
   parameter is not page aligned, if the either size is zero, if any
   wraparound is implied, if the old address range does not fall
   entirely within a single segment, if the new address range overlaps
   with the old one, or if the old address range is not a valid client
   mapping.  If *need_discard is True after a successful return, the
   caller should immediately discard translations from both specified
   address ranges.  */

Bool VG_(am_relocate_nooverlap_client)( /*OUT*/Bool* need_discard,
                                        Addr old_addr, SizeT old_len,
                                        Addr new_addr, SizeT new_len )
{
   *need_discard = False;

   if (old_len == 0 || new_len == 0)
      return False;

   if (!VG_IS_PAGE_ALIGNED(old_addr) || !VG_IS_PAGE_ALIGNED(old_len)
       || !VG_IS_PAGE_ALIGNED(new_addr) || !VG_IS_PAGE_ALIGNED(new_len))
      return False;

   if (old_addr + old_len < old_addr
       || new_addr + new_len < new_addr)
      return False;

   if (old_addr + old_len - 1 < new_addr
       || new_addr + new_len - 1 < old_addr) {
      /* no overlap */
   } else
      return False;

   const NSegment *segLo = ML_(am_find_segment)( old_addr );
   const NSegment *segHi = ML_(am_find_segment)( old_addr + old_len - 1 );
   if (segLo != segHi)
      return False;

   if (segLo->kind != SkFileC && segLo->kind != SkAnonC &&
       segLo->kind != SkShmC)
      return False;

   SysRes sres =
      ML_(am_do_relocate_nooverlap_mapping_NO_NOTIFY)( old_addr, old_len,
                                                       new_addr, new_len );
   if (sr_isError(sres)) {
      AM_SANITY_CHECK();
      return False;
   } else {
      aspacem_assert(sr_Res(sres) == new_addr);
   }

   *need_discard = any_Ts_in_range( old_addr, old_len )
                   || any_Ts_in_range( new_addr, new_len );

   NSegment seg = *segLo;

   /* Mark the new area based on the old seg. */
   if (seg.kind == SkFileC) {
      seg.offset += ((ULong)old_addr) - ((ULong)seg.start);
   }
   seg.start = new_addr;
   seg.end   = new_addr + new_len - 1;
   ML_(am_add_segment)( &seg );

   /* Create a free hole in the old location. */
   am_notify_munmap( old_addr, old_len );

   return True;
}

#endif // HAVE_MREMAP


#if defined(VGO_linux)

/*-----------------------------------------------------------------*/
/*---                                                           ---*/
/*--- A simple parser for /proc/self/maps on Linux 2.4.X/2.6.X. ---*/
/*--- Almost completely independent of the stuff above.  The    ---*/
/*--- only function it 'exports' to the code above this comment ---*/
/*--- is parse_procselfmaps.                                    ---*/
/*---                                                           ---*/
/*-----------------------------------------------------------------*/

/*------BEGIN-procmaps-parser-for-Linux--------------------------*/

/* Size of a smallish table used to read /proc/self/map entries. */
#define M_PROCMAP_BUF 100000

/* static ... to keep it out of the stack frame. */
static HChar procmap_buf[M_PROCMAP_BUF];

/* Records length of /proc/self/maps read into procmap_buf. */
static Int  buf_n_tot;

/* Helper fns. */

static Int hexdigit ( HChar c )
{
   if (c >= '0' && c <= '9') return (Int)(c - '0');
   if (c >= 'a' && c <= 'f') return 10 + (Int)(c - 'a');
   if (c >= 'A' && c <= 'F') return 10 + (Int)(c - 'A');
   return -1;
}

static Int decdigit ( HChar c )
{
   if (c >= '0' && c <= '9') return (Int)(c - '0');
   return -1;
}

static Int readchar ( const HChar* buf, HChar* ch )
{
   if (*buf == 0) return 0;
   *ch = *buf;
   return 1;
}

static Int readhex ( const HChar* buf, UWord* val )
{
   /* Read a word-sized hex number. */
   Int n = 0;
   *val = 0;
   while (hexdigit(*buf) >= 0) {
      *val = (*val << 4) + hexdigit(*buf);
      n++; buf++;
   }
   return n;
}

static Int readhex64 ( const HChar* buf, ULong* val )
{
   /* Read a potentially 64-bit hex number. */
   Int n = 0;
   *val = 0;
   while (hexdigit(*buf) >= 0) {
      *val = (*val << 4) + hexdigit(*buf);
      n++; buf++;
   }
   return n;
}

static Int readdec64 ( const HChar* buf, ULong* val )
{
   Int n = 0;
   *val = 0;
   while (decdigit(*buf) >= 0) {
      *val = (*val * 10) + decdigit(*buf);
      n++; buf++;
   }
   return n;
}


/* Get the contents of /proc/self/maps into a static buffer.  If
   there's a syntax error, it won't fit, or other failure, just
   abort. */

static void read_procselfmaps_into_buf ( void )
{
   Int    n_chunk;
   SysRes fd;
   
   /* Read the initial memory mapping from the /proc filesystem. */
   fd = ML_(am_open)( "/proc/self/maps", VKI_O_RDONLY, 0 );
   if (sr_isError(fd))
      ML_(am_barf)("can't open /proc/self/maps");

   buf_n_tot = 0;
   do {
      n_chunk = ML_(am_read)( sr_Res(fd), &procmap_buf[buf_n_tot],
                              M_PROCMAP_BUF - buf_n_tot );
      if (n_chunk >= 0)
         buf_n_tot += n_chunk;
   } while ( n_chunk > 0 && buf_n_tot < M_PROCMAP_BUF );

   ML_(am_close)(sr_Res(fd));

   if (buf_n_tot >= M_PROCMAP_BUF-5)
      ML_(am_barf_toolow)("M_PROCMAP_BUF");
   if (buf_n_tot == 0)
      ML_(am_barf)("I/O error on /proc/self/maps");

   procmap_buf[buf_n_tot] = 0;
}

/* Parse /proc/self/maps.  For each map entry, call
   record_mapping, passing it, in this order:

      start address in memory
      length
      page protections (using the VKI_PROT_* flags)
      mapped file device and inode
      offset in file, or zero if no file
      filename, zero terminated, or NULL if no file

   So the sig of the called fn might be

      void (*record_mapping)( Addr start, SizeT size, UInt prot,
			      UInt dev, UInt info,
                              ULong foffset, UChar* filename )

   Note that the supplied filename is transiently stored; record_mapping 
   should make a copy if it wants to keep it.

   Nb: it is important that this function does not alter the contents of
       procmap_buf!
*/
static void parse_procselfmaps (
      void (*record_mapping)( Addr addr, SizeT len, UInt prot,
                              ULong dev, ULong ino, Off64T offset, 
                              const HChar* filename ),
      void (*record_gap)( Addr addr, SizeT len )
   )
{
   Int    i, j, i_eol;
   Addr   start, endPlusOne, gapStart;
   HChar* filename;
   HChar  rr, ww, xx, pp, ch, tmp;
   UInt	  prot;
   UWord  maj, min;
   ULong  foffset, dev, ino;

   foffset = ino = 0; /* keep gcc-4.1.0 happy */

   read_procselfmaps_into_buf();

   aspacem_assert('\0' != procmap_buf[0] && 0 != buf_n_tot);

   if (0)
      VG_(debugLog)(0, "procselfmaps", "raw:\n%s\n", procmap_buf);

   /* Ok, it's safely aboard.  Parse the entries. */
   i = 0;
   gapStart = Addr_MIN;
   while (True) {
      if (i >= buf_n_tot) break;

      /* Read (without fscanf :) the pattern %16x-%16x %c%c%c%c %16x %2x:%2x %d */
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
      /* This field is the shared/private flag */
      j = readchar(&procmap_buf[i], &pp);
      if (j == 1 && (pp == 'p' || pp == '-' || pp == 's')) 
                                              i += j; else goto syntaxerror;

      j = readchar(&procmap_buf[i], &ch);
      if (j == 1 && ch == ' ') i += j; else goto syntaxerror;

      j = readhex64(&procmap_buf[i], &foffset);
      if (j > 0) i += j; else goto syntaxerror;

      j = readchar(&procmap_buf[i], &ch);
      if (j == 1 && ch == ' ') i += j; else goto syntaxerror;

      j = readhex(&procmap_buf[i], &maj);
      if (j > 0) i += j; else goto syntaxerror;
      j = readchar(&procmap_buf[i], &ch);
      if (j == 1 && ch == ':') i += j; else goto syntaxerror;
      j = readhex(&procmap_buf[i], &min);
      if (j > 0) i += j; else goto syntaxerror;

      j = readchar(&procmap_buf[i], &ch);
      if (j == 1 && ch == ' ') i += j; else goto syntaxerror;

      j = readdec64(&procmap_buf[i], &ino);
      if (j > 0) i += j; else goto syntaxerror;
 
      goto read_line_ok;

    syntaxerror:
      VG_(debugLog)(0, "Valgrind:", 
                       "FATAL: syntax error reading /proc/self/maps\n");
      { Int k, m;
        HChar buf50[51];
        m = 0;
        buf50[m] = 0;
        k = i - 50;
        if (k < 0) k = 0;
        for (; k <= i; k++) {
           buf50[m] = procmap_buf[k];
           buf50[m+1] = 0;
           if (m < 50-1) m++;
        }
        VG_(debugLog)(0, "procselfmaps", "Last 50 chars: '%s'\n", buf50);
      }
      ML_(am_exit)(1);

    read_line_ok:

      aspacem_assert(i < buf_n_tot);

      /* Try and find the name of the file mapped to this segment, if
         it exists.  Note that file names can contain spaces. */

      // Move i to the next non-space char, which should be either a '/',
      // a '[', or a newline.
      while (procmap_buf[i] == ' ') i++;
      
      // Move i_eol to the end of the line.
      i_eol = i;
      while (procmap_buf[i_eol] != '\n') i_eol++;

      // If there's a filename...
      if (procmap_buf[i] == '/') {
         /* Minor hack: put a '\0' at the filename end for the call to
            'record_mapping', then restore the old char with 'tmp'. */
         filename = &procmap_buf[i];
         tmp = filename[i_eol - i];
         filename[i_eol - i] = '\0';
      } else {
	 tmp = 0;
         filename = NULL;
         foffset = 0;
      }

      prot = 0;
      if (rr == 'r') prot |= VKI_PROT_READ;
      if (ww == 'w') prot |= VKI_PROT_WRITE;
      if (xx == 'x') prot |= VKI_PROT_EXEC;

      /* Linux has two ways to encode a device number when it
         is exposed to user space (via fstat etc). The old way
         is the traditional unix scheme that produces a 16 bit
         device number with the top 8 being the major number and
         the bottom 8 the minor number.
         
         The new scheme allows for a 12 bit major number and
         a 20 bit minor number by using a 32 bit device number
         and putting the top 12 bits of the minor number into
         the top 12 bits of the device number thus leaving an
         extra 4 bits for the major number.
         
         If the minor and major number are both single byte
         values then both schemes give the same result so we
         use the new scheme here in case either number is
         outside the 0-255 range and then use fstat64 when
         available (or fstat on 64 bit systems) so that we
         should always have a new style device number and
         everything should match. */
      dev = (min & 0xff) | (maj << 8) | ((min & ~0xff) << 12);

      if (record_gap && gapStart < start)
         (*record_gap) ( gapStart, start-gapStart );

      if (record_mapping && start < endPlusOne)
         (*record_mapping) ( start, endPlusOne-start,
                             prot, dev, ino,
                             foffset, filename );

      if ('\0' != tmp) {
         filename[i_eol - i] = tmp;
      }

      i = i_eol + 1;
      gapStart = endPlusOne;
   }

#  if defined(VGP_arm_linux)
   /* ARM puts code at the end of memory that contains processor
      specific stuff (cmpxchg, getting the thread local storage, etc.)
      This isn't specified in /proc/self/maps, so do it here.  This
      kludgery causes the view of memory, as presented to
      record_gap/record_mapping, to actually reflect reality.  IMO
      (JRS, 2010-Jan-03) the fact that /proc/.../maps does not list
      the commpage should be regarded as a bug in the kernel. */
   { const Addr commpage_start = ARM_LINUX_FAKE_COMMPAGE_START;
     const Addr commpage_end1  = ARM_LINUX_FAKE_COMMPAGE_END1;
     if (gapStart < commpage_start) {
        if (record_gap)
           (*record_gap)( gapStart, commpage_start - gapStart );
        if (record_mapping)
           (*record_mapping)( commpage_start, commpage_end1 - commpage_start,
                              VKI_PROT_READ|VKI_PROT_EXEC,
                              0/*dev*/, 0/*ino*/, 0/*foffset*/,
                              NULL);
        gapStart = commpage_end1;
     }
   }
#  endif

   if (record_gap && gapStart < Addr_MAX)
      (*record_gap) ( gapStart, Addr_MAX - gapStart + 1 );
}

/*------END-procmaps-parser-for-Linux----------------------------*/

/*------BEGIN-procmaps-parser-for-Darwin-------------------------*/

#elif defined(VGO_darwin)
#include <mach/mach.h>
#include <mach/mach_vm.h>

static unsigned int mach2vki(unsigned int vm_prot)
{
   return
      ((vm_prot & VM_PROT_READ)    ? VKI_PROT_READ    : 0) |
      ((vm_prot & VM_PROT_WRITE)   ? VKI_PROT_WRITE   : 0) |
      ((vm_prot & VM_PROT_EXECUTE) ? VKI_PROT_EXEC    : 0) ;
}

static UInt stats_machcalls = 0;

static void parse_procselfmaps (
      void (*record_mapping)( Addr addr, SizeT len, UInt prot,
                              ULong dev, ULong ino, Off64T offset, 
                              const HChar* filename ),
      void (*record_gap)( Addr addr, SizeT len )
   )
{
   vm_address_t iter;
   unsigned int depth;
   vm_address_t last;

   iter = 0;
   depth = 0;
   last = 0;
   while (1) {
      mach_vm_address_t addr = iter;
      mach_vm_size_t size;
      vm_region_submap_short_info_data_64_t info;
      kern_return_t kr;

      while (1) {
         mach_msg_type_number_t info_count
            = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
         stats_machcalls++;
         kr = mach_vm_region_recurse(mach_task_self(), &addr, &size, &depth,
                                     (vm_region_info_t)&info, &info_count);
         if (kr) 
            return;
         if (info.is_submap) {
            depth++;
            continue;
         }
         break;
      }
      iter = addr + size;

      if (addr > last  &&  record_gap) {
         (*record_gap)(last, addr - last);
      }
      if (record_mapping) {
         (*record_mapping)(addr, size, mach2vki(info.protection),
                           0, 0, info.offset, NULL);
      }
      last = addr + size;
   }

   if ((Addr)-1 > last  &&  record_gap)
      (*record_gap)(last, (Addr)-1 - last);
}

// Urr.  So much for thread safety.
static Bool        css_overflowed;
static ChangedSeg* css_local;
static Int         css_size_local;
static Int         css_used_local;

static Addr Addr__max ( Addr a, Addr b ) { return a > b ? a : b; }
static Addr Addr__min ( Addr a, Addr b ) { return a < b ? a : b; }

static void add_mapping_callback(Addr addr, SizeT len, UInt prot, 
                                 ULong dev, ULong ino, Off64T offset, 
                                 const HChar *filename)
{
   // derived from sync_check_mapping_callback()

   /* JRS 2012-Mar-07: this all seems very dubious to me.  It would be
      safer to see if we can find, in V's segment collection, one
      single segment that completely covers the range [addr, +len)
      (and possibly more), and that has the exact same other
      properties (prot, dev, ino, offset, etc) as the data presented
      here.  If found, we just skip.  Otherwise add the data presented
      here into css_local[]. */

   if (len == 0) return;

   /* The kernel should not give us wraparounds. */
   aspacem_assert(addr <= addr + len - 1); 

   const NSegment *segLo = ML_(am_find_segment)( addr );
   const NSegment *segHi = ML_(am_find_segment)( addr + len - 1 );

   /* NSegments segLo .. segHi inclusive should agree with the presented
      data. */
   for (const NSegment *seg = segLo; True; seg = ML_(am_next_segment)(seg)) {
      switch (seg->kind) {
      case SkAnonV:
      case SkFileV:
         /* Ignore V regions */
         break;

      case SkFree:
      case SkResvn: {
         /* Add mapping */
         ChangedSeg* cs = &css_local[css_used_local];
         if (css_used_local < css_size_local) {
            cs->is_added = True;
            cs->start    = addr;
            cs->end      = addr + len - 1;
            cs->prot     = prot;
            cs->offset   = offset;
            css_used_local++;
         } else {
            css_overflowed = True;
         }
         return;
      }

      case SkAnonC:
      case SkFileC:
      case SkShmC: {
         /* Check permissions on client regions */
         // GrP fixme
         UInt seg_prot = 0;

         if (seg->hasR) seg_prot |= VKI_PROT_READ;
         if (seg->hasW) seg_prot |= VKI_PROT_WRITE;
#        if defined(VGA_x86)
         // GrP fixme sloppyXcheck 
         // darwin: kernel X ignored and spuriously changes? (vm_copy)
         seg_prot |= (prot & VKI_PROT_EXEC);
#        else
         if (seg->hasX) seg_prot |= VKI_PROT_EXEC;
#        endif
         if (seg_prot != prot) {
             if (VG_(clo_trace_syscalls)) 
                 VG_(debugLog)(0,"aspacem","region %p..%p permission "
                                 "mismatch (kernel %x, V %x)\n", 
                                 (void*)seg->start,
                                 (void*)(seg->end+1), prot, seg_prot);
            /* Add mapping for regions with protection changes */
            ChangedSeg* cs = &css_local[css_used_local];
            if (css_used_local < css_size_local) {
               cs->is_added = True;
               cs->start    = addr;
               cs->end      = addr + len - 1;
               cs->prot     = prot;
               cs->offset   = offset;
               css_used_local++;
            } else {
               css_overflowed = True;
            }
	    return;
         }
         break;
      }

      default:
         aspacem_assert(0);
      }
      if (seg == segHi) break;
   }
}

static void remove_mapping_callback(Addr addr, SizeT len)
{
   // derived from sync_check_gap_callback()

   if (len == 0)
      return;

   /* The kernel should not give us wraparounds. */
   aspacem_assert(addr <= addr + len - 1); 

   const NSegment *segLo = ML_(am_find_segment)( addr );
   const NSegment *segHi = ML_(am_find_segment)( addr + len - 1 );

   /* Segments segLo .. segHi inclusive should agree with the
      presented data. */
   for (const NSegment *seg = segLo; True; seg = ML_(am_next_segment)(seg)) {
      if (seg->kind != SkFree && seg->kind != SkResvn) {
         /* V has a mapping, kernel doesn't.  Add to css_local[],
            directives to chop off the part of the V mapping that
            falls within the gap that the kernel tells us is
            present. */
         ChangedSeg* cs = &css_local[css_used_local];
         if (css_used_local < css_size_local) {
            cs->is_added = False;
            cs->start    = Addr__max(seg->start, addr);
            cs->end      = Addr__min(seg->end,   addr + len - 1);
            aspacem_assert(VG_IS_PAGE_ALIGNED(cs->start));
            aspacem_assert(VG_IS_PAGE_ALIGNED(cs->end+1));
            /* I don't think the following should fail.  But if it
               does, just omit the css_used_local++ in the cases where
               it doesn't hold. */
            aspacem_assert(cs->start < cs->end);
            cs->prot     = 0;
            cs->offset   = 0;
            css_used_local++;
         } else {
            css_overflowed = True;
         }
      }
      if (seg == segHi) break;
   }
}


// Returns False if 'css' wasn't big enough.
Bool VG_(get_changed_segments)(
      const HChar* when, const HChar* where, /*OUT*/ChangedSeg* css,
      Int css_size, /*OUT*/Int* css_used)
{
   static UInt stats_synccalls = 1;
   aspacem_assert(when && where);

   if (0)
      VG_(debugLog)(0,"aspacem",
         "[%u,%u] VG_(get_changed_segments)(%s, %s)\n",
         stats_synccalls++, stats_machcalls, when, where
      );

   css_overflowed = False;
   css_local = css;
   css_size_local = css_size;
   css_used_local = 0;

   // Get the list of segs that need to be added/removed.
   parse_procselfmaps(&add_mapping_callback, &remove_mapping_callback);

   *css_used = css_used_local;

   if (css_overflowed) {
      aspacem_assert(css_used_local == css_size_local);
   }

   return !css_overflowed;
}

#endif // defined(VGO_darwin)

/*------END-procmaps-parser-for-Darwin---------------------------*/

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
