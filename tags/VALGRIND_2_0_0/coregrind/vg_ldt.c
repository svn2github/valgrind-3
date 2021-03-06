
/*--------------------------------------------------------------------*/
/*--- Simulation of Local Descriptor Tables               vg_ldt.c ---*/
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

/* Details of the LDT simulation
   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  
   When a program runs natively, the linux kernel allows each *thread*
   in it to have its own LDT.  Almost all programs never do this --
   it's wildly unportable, after all -- and so the kernel never
   allocates the structure, which is just as well as an LDT occupies
   64k of memory (8192 entries of size 8 bytes).

   A thread may choose to modify its LDT entries, by doing the
   __NR_modify_ldt syscall.  In such a situation the kernel will then
   allocate an LDT structure for it.  Each LDT entry is basically a
   (base, limit) pair.  A virtual address in a specific segment is
   translated to a linear address by adding the segment's base value.
   In addition, the virtual address must not exceed the limit value.

   To use an LDT entry, a thread loads one of the segment registers
   (%cs, %ss, %ds, %es, %fs, %gs) with the index of the LDT entry (0
   .. 8191) it wants to use.  In fact, the required value is (index <<
   3) + 7, but that's not important right now.  Any normal instruction
   which includes an addressing mode can then be made relative to that
   LDT entry by prefixing the insn with a so-called segment-override
   prefix, a byte which indicates which of the 6 segment registers
   holds the LDT index.

   Now, a key constraint is that valgrind's address checks operate in
   terms of linear addresses.  So we have to explicitly translate
   virtual addrs into linear addrs, and that means doing a complete
   LDT simulation.

   Calls to modify_ldt are intercepted.  For each thread, we maintain
   an LDT (with the same normally-never-allocated optimisation that
   the kernel does).  This is updated as expected via calls to
   modify_ldt.

   When a thread does an amode calculation involving a segment
   override prefix, the relevant LDT entry for the thread is
   consulted.  It all works.

   There is a conceptual problem, which appears when switching back to
   native execution, either temporarily to pass syscalls to the
   kernel, or permanently, when debugging V.  Problem at such points
   is that it's pretty pointless to copy the simulated machine's
   segment registers to the real machine, because we'd also need to
   copy the simulated LDT into the real one, and that's prohibitively
   expensive.

   Fortunately it looks like no syscalls rely on the segment regs or
   LDT being correct, so we can get away with it.  Apart from that the
   simulation is pretty straightforward.  All 6 segment registers are
   tracked, although only %ds, %es, %fs and %gs are allowed as
   prefixes.  Perhaps it could be restricted even more than that -- I
   am not sure what is and isn't allowed in user-mode.
*/

#include "vg_include.h"
/* Allocate and deallocate LDTs for threads. */

/* Create an LDT.  If the parent_ldt is NULL, zero out the
   new one.  If non-NULL, copy the parent. */
VgLdtEntry* VG_(allocate_LDT_for_thread) ( VgLdtEntry* parent_ldt )
{
   UInt        nbytes, i;
   VgLdtEntry* ldt;

   if (0)
      VG_(printf)("allocate_LDT_for_thread: parent = %p\n", parent_ldt );
   vg_assert(VG_LDT_ENTRY_SIZE == sizeof(VgLdtEntry));
   nbytes = VG_M_LDT_ENTRIES * VG_LDT_ENTRY_SIZE;
 
   if (parent_ldt == NULL) {
      /* Allocate a new zeroed-out one. */
      ldt = (VgLdtEntry*)VG_(arena_calloc)(VG_AR_CORE, /*align*/4, nbytes, 1);
   } else {
     ldt = (VgLdtEntry*)VG_(arena_malloc)(VG_AR_CORE, nbytes);
     for (i = 0; i < VG_M_LDT_ENTRIES; i++)
        ldt[i] = parent_ldt[i];
   }

   return ldt;
}

/* Free an LDT created by the above function. */
void VG_(deallocate_LDT_for_thread) ( VgLdtEntry* ldt )
{
   if (0)
      VG_(printf)("deallocate_LDT_for_thread: ldt = %p\n", ldt );
   if (ldt != NULL)
      VG_(arena_free)(VG_AR_CORE, ldt);
}



/* Fish the base field out of an VgLdtEntry.  This is the only part we
   are particularly interested in. */

static 
void *wine_ldt_get_base( const VgLdtEntry *ent )
{
    return (void *)(ent->LdtEnt.Bits.BaseLow |
                    ((unsigned long)ent->LdtEnt.Bits.BaseMid) << 16 |
                    ((unsigned long)ent->LdtEnt.Bits.BaseHi) << 24);
}

static 
unsigned int wine_ldt_get_limit( const VgLdtEntry *ent )
{
    unsigned int limit = ent->LdtEnt.Bits.LimitLow 
                         | (ent->LdtEnt.Bits.LimitHi << 16);
    if (ent->LdtEnt.Bits.Granularity) limit = (limit << 12) | 0xfff;
    return limit;
}


/* Actually _DO_ the segment translation.  This is the whole entire
   point of this accursed, overcomplicated, baroque, pointless
   segment-override-and-LDT/GDT garbage foisted upon us all by Intel,
   in its infinite wisdom. 

   THIS IS CALLED FROM GENERATED CODE (AND SO RUNS ON REAL CPU). 
*/
Addr VG_(do_useseg) ( UInt seg_selector, Addr virtual_addr )
{
   Addr        base;
   UInt        limit;
   VgLdtEntry* the_ldt;

   if (0) 
      VG_(printf)("do_useseg: seg_selector = %p, vaddr = %p\n", 
                  seg_selector, virtual_addr);

   seg_selector &= 0x0000FFFF;
  
   /* Sanity check the segment selector.  Ensure that TI=1 (LDT) and
      that RPL=11b (least privilege).  These form the bottom 3 bits
      of the selector. */
   vg_assert((seg_selector & 7) == 7);

   /* convert it onto a table index */
   seg_selector >>= 3;
   vg_assert(seg_selector >= 0 && seg_selector < 8192);

   /* Come up with a suitable LDT entry.  We look at the thread's LDT,
      which is pointed to by a VG_(baseBlock) entry.  If null, we will
      use an implied zero-entry -- although this usually implies the
      program is in deep trouble, since it is using LDT entries which
      it probably hasn't set up. */
   the_ldt = (VgLdtEntry*)VG_(baseBlock)[VGOFF_(ldt)];
   if (the_ldt == NULL) {
      base = 0;
      limit = 0;
      VG_(message)(
         Vg_UserMsg,
         "Warning: segment-override prefix encountered, but thread has no LDT"
      );
   } else {
      base = (Addr)wine_ldt_get_base ( &the_ldt[seg_selector] );
      limit = (UInt)wine_ldt_get_limit ( &the_ldt[seg_selector] );
   }

   /* Note, this check is just slightly too slack.  Really it should
    be "if (virtual_addr + size - 1 >= limit)," but we don't have the
    size info to hand.  Getting it could be significantly complex.  */
   if (virtual_addr >= limit) {
      VG_(message)(
         Vg_UserMsg,
         "Warning: segment access: virtual addr %d exceeds segment limit of %d",
         virtual_addr, limit
      );
   }

   return base + virtual_addr;
}


/* Translate a struct modify_ldt_ldt_s to an VgLdtEntry, using the
   Linux kernel's logic (cut-n-paste of code in linux/kernel/ldt.c).  */

static
void translate_to_hw_format ( /* IN  */ struct vki_modify_ldt_ldt_s* inn,
			      /* OUT */ VgLdtEntry* out,
                                        Int oldmode )
{
   UInt entry_1, entry_2;

   if (0)
      VG_(printf)("translate_to_hw_format: base %p, limit %d\n", 
                  inn->base_addr, inn->limit );

   /* Allow LDTs to be cleared by the user. */
   if (inn->base_addr == 0 && inn->limit == 0) {
      if (oldmode ||
          (inn->contents == 0      &&
           inn->read_exec_only == 1   &&
           inn->seg_32bit == 0      &&
           inn->limit_in_pages == 0   &&
           inn->seg_not_present == 1   &&
           inn->useable == 0 )) {
         entry_1 = 0;
         entry_2 = 0;
         goto install;
      }
   }

   entry_1 = ((inn->base_addr & 0x0000ffff) << 16) |
             (inn->limit & 0x0ffff);
   entry_2 = (inn->base_addr & 0xff000000) |
             ((inn->base_addr & 0x00ff0000) >> 16) |
             (inn->limit & 0xf0000) |
             ((inn->read_exec_only ^ 1) << 9) |
             (inn->contents << 10) |
             ((inn->seg_not_present ^ 1) << 15) |
             (inn->seg_32bit << 22) |
             (inn->limit_in_pages << 23) |
             0x7000;
   if (!oldmode)
      entry_2 |= (inn->useable << 20);

   /* Install the new entry ...  */
  install:
   out->LdtEnt.Words.word1 = entry_1;
   out->LdtEnt.Words.word2 = entry_2;
}


/*
 * linux/kernel/ldt.c
 *
 * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

/*
 * read_ldt() is not really atomic - this is not a problem since
 * synchronization of reads and writes done to the LDT has to be
 * assured by user-space anyway. Writes are atomic, to protect
 * the security checks done on new descriptors.
 */
static
Int read_ldt ( ThreadId tid, UChar* ptr, UInt bytecount )
{
   Int err;
   UInt i, size;
   Char* ldt;

   if (0)
      VG_(printf)("read_ldt: tid = %d, ptr = %p, bytecount = %d\n",
                  tid, ptr, bytecount );

   ldt = (Char*)(VG_(threads)[tid].ldt);
   err = 0;
   if (ldt == NULL)
      /* LDT not allocated, meaning all entries are null */
      goto out;

   size = VG_M_LDT_ENTRIES * VG_LDT_ENTRY_SIZE;
   if (size > bytecount)
      size = bytecount;

   err = size;
   for (i = 0; i < size; i++)
      ptr[i] = ldt[i];

  out:
   return err;
}


static
Int write_ldt ( ThreadId tid, void* ptr, UInt bytecount, Int oldmode )
{
   Int error;
   VgLdtEntry* ldt;
   struct vki_modify_ldt_ldt_s* ldt_info; 

   if (0)
      VG_(printf)("write_ldt: tid = %d, ptr = %p, "
                  "bytecount = %d, oldmode = %d\n",
                  tid, ptr, bytecount, oldmode );

   ldt      = VG_(threads)[tid].ldt;
   ldt_info = (struct vki_modify_ldt_ldt_s*)ptr;

   error = -VKI_EINVAL;
   if (bytecount != sizeof(struct vki_modify_ldt_ldt_s))
      goto out;

   error = -VKI_EINVAL;
   if (ldt_info->entry_number >= VG_M_LDT_ENTRIES)
      goto out;
   if (ldt_info->contents == 3) {
      if (oldmode)
         goto out;
      if (ldt_info->seg_not_present == 0)
         goto out;
   }

   /* If this thread doesn't have an LDT, we'd better allocate it
      now. */
   if (ldt == NULL) {
      ldt = VG_(allocate_LDT_for_thread)( NULL );
      VG_(threads)[tid].ldt = ldt;
   }

   /* Install the new entry ...  */
   translate_to_hw_format ( ldt_info, &ldt[ldt_info->entry_number], oldmode );
   error = 0;

  out:
   return error;
}


Int VG_(sys_modify_ldt) ( ThreadId tid,
                          Int func, void* ptr, UInt bytecount )
{
   Int ret = -VKI_ENOSYS;

   switch (func) {
   case 0:
      ret = read_ldt(tid, ptr, bytecount);
      break;
   case 1:
      ret = write_ldt(tid, ptr, bytecount, 1);
      break;
   case 2:
      VG_(unimplemented)("sys_modify_ldt: func == 2");
      /* god knows what this is about */
      /* ret = read_default_ldt(ptr, bytecount); */
      /*UNREACHED*/
      break;
   case 0x11:
      ret = write_ldt(tid, ptr, bytecount, 0);
      break;
   }
   return ret;
}


/*--------------------------------------------------------------------*/
/*--- end                                                 vg_ldt.c ---*/
/*--------------------------------------------------------------------*/
