#define _FILE_OFFSET_BITS	64

#include <stdio.h>
#include <elf.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "vg_include.h"

#include "ume.h"
#include "ume_arch.h"
#include "ume_archdefs.h"

static int stack[SIGSTKSZ*4];
static int our_argc;

/* Where we expect to find all our aux files (namely, stage2) */
static const char *valgrind_lib = VG_LIBDIR;

/* stage2's name */
static const char stage2[] = "stage2";

/* Modify the auxv the kernel gave us to make it look like we were
   execed as the shared object.

   This also inserts a new entry into the auxv table so we can
   communicate some extra information to stage2 (namely, the fd of the
   padding file, so it can identiry and remove the padding later).
*/
static void *fix_auxv(void *v_init_esp, const struct exeinfo *info)
{
   struct ume_auxv *auxv;
   int *newesp;
   int seen;
   int delta;
   int i;
   static const int new_entries = 2;

   /* make sure we're running on the private stack */
   assert(&delta >= stack && &delta < &stack[sizeof(stack)/sizeof(*stack)]);
   
   /* find the beginning of the AUXV table */
   auxv = find_auxv(v_init_esp);

   /* Work out how we should move things to make space for the new
      auxv entry. It seems that ld.so wants a 16-byte aligned stack on
      entry, so make sure that's the case. */
   newesp = (int *)(((unsigned long)v_init_esp - new_entries * sizeof(*auxv)) & ~0xf);
   delta = (char *)v_init_esp - (char *)newesp;

   memmove(newesp, v_init_esp, (char *)auxv - (char *)v_init_esp);
   
   v_init_esp = (void *)newesp;
   auxv -= delta/sizeof(*auxv);

   /* stage2 needs this so it can clean up the padding we leave in
      place when we start it */
   auxv[0].a_type = AT_UME_PADFD;
   auxv[0].a_val = as_getpadfd();

   /* This will be needed by valgrind itself so that it can
      subsequently execve() children.  This needs to be done here
      because /proc/self/exe will go away once we unmap stage1. */
   auxv[1].a_type = AT_UME_EXECFD;
   auxv[1].a_val = open("/proc/self/exe", O_RDONLY);

   /* make sure the rest are sane */
   for(i = new_entries; i < delta/sizeof(*auxv); i++) {
      auxv[i].a_type = AT_IGNORE;
      auxv[i].a_val = 0;
   }

   /* OK, go through and patch up the auxv entries to match the new
      executable */
   seen = 0;
   for(; auxv->a_type != AT_NULL; auxv++) {
      if (0)
	 printf("doing auxv %p %4x: %d %p\n", auxv, auxv->a_type, auxv->a_val, auxv->a_ptr);

      switch(auxv->a_type) {
      case AT_PHDR:
	 seen |= 1;
	 auxv->a_val = info->phdr;
	 break;

      case AT_PHNUM:
	 seen |= 2;
	 auxv->a_val = info->phnum;
	 break;

      case AT_BASE:
	 seen |= 4;
	 auxv->a_val = info->interp_base;
	 break;

      case AT_ENTRY:
	 seen |= 8;
	 auxv->a_val = info->entry;
	 break;
      }
   }

   /* If we didn't see all the entries we need to fix up, then we
      can't make the new executable viable. */
   if (seen != 0xf) {
      fprintf(stderr, "fix_auxv: we didn't see enough auxv entries (seen=%x)\n", seen);
      exit(1);
   }

   return v_init_esp;
}

static void hoops(void)
{
   int err;
   struct exeinfo info;
   extern char _end;
   int *esp;
   char buf[strlen(valgrind_lib) + sizeof(stage2) + 16];

   info.exe_base = PGROUNDUP(&_end);
   info.exe_end  = PGROUNDDN(ume_exec_esp);

   /* XXX FIXME: how can stage1 know where stage2 wants things placed?
      Options:
      - we could look for a symbol
      - it could have a special PHDR (v. ELF specific)
      - something else?
    */
   info.map_base = 0xb0000000;
   info.setbrk = 1;		/* ask do_exec to move the brk-base */
   info.argv = NULL;

   snprintf(buf, sizeof(buf), "%s/%s", valgrind_lib, stage2);

   err = do_exec(buf, &info);

   if (err != 0) {
      fprintf(stderr, "failed to load %s: %s\n",
	      buf, strerror(err));
      exit(1);
   }

   /* Make sure stage2's dynamic linker can't tromp on the lower part
      of the address space. */
   as_pad(0, (void *)info.map_base);
   
   esp = fix_auxv(ume_exec_esp, &info);

   if (0) {
      int prmap(void *start, void *end, const char *perm, off_t off, int maj, int min, int ino) {
	 printf("mapping %10p-%10p %s %02x:%02x %d\n",
		start, end, perm, maj, min, ino);
	 return 1;
      }
      printf("---------- launch stage 2 ----------\n");
      printf("eip=%p esp=%p\n", (void *)info.init_eip, esp);
      foreach_map(prmap);
   }

   ume_go(info.init_eip, (addr_t)esp);   
}

int main(int argc, char **argv)
{
   const char *cp = getenv(VALGRINDLIB);

   if (cp != NULL)
      valgrind_lib = cp;

   assert(ume_exec_esp != NULL);

   our_argc = argc;

   /* move onto another stack so we can play with the main one */
   ume_go((addr_t)hoops, (addr_t)stack + sizeof(stack));
}
