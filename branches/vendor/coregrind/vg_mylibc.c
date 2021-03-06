
/*--------------------------------------------------------------------*/
/*--- Reimplementation of some C library stuff, to avoid depending ---*/
/*--- on libc.so.                                                  ---*/
/*---                                                  vg_mylibc.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an x86 protected-mode emulator 
   designed for debugging and profiling binaries on x86-Unixes.

   Copyright (C) 2000-2002 Julian Seward 
      jseward@acm.org
      Julian_Seward@muraroa.demon.co.uk

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

   The GNU General Public License is contained in the file LICENSE.
*/

#include "vg_include.h"



/* ---------------------------------------------------------------------
   Really Actually DO system calls.
   ------------------------------------------------------------------ */

/* Ripped off from /usr/include/asm/unistd.h. */

static
UInt vg_do_syscall0 ( UInt syscallno )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno) );
   return __res;
}


static
UInt vg_do_syscall1 ( UInt syscallno, UInt arg1 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1) );
   return __res;
}


static
UInt vg_do_syscall2 ( UInt syscallno, 
                      UInt arg1, UInt arg2 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2) );
   return __res;
}


static
UInt vg_do_syscall3 ( UInt syscallno, 
                      UInt arg1, UInt arg2, UInt arg3 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2),
                       "d" (arg3) );
   return __res;
}


static
UInt vg_do_syscall4 ( UInt syscallno, 
                      UInt arg1, UInt arg2, UInt arg3, UInt arg4 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2),
                       "d" (arg3),
                       "S" (arg4) );
   return __res;
}


#if 0
static
UInt vg_do_syscall5 ( UInt syscallno, 
                      UInt arg1, UInt arg2, UInt arg3, UInt arg4, 
                      UInt arg5 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2),
                       "d" (arg3),
                       "S" (arg4),
                       "D" (arg5) );
   return __res;
}
#endif

/* ---------------------------------------------------------------------
   Wrappers around system calls, and other stuff, to do with signals.
   ------------------------------------------------------------------ */

/* sigemptyset, sigfullset, sigaddset and sigdelset return 0 on
   success and -1 on error.  
*/
Int VG_(ksigfillset)( vki_ksigset_t* set )
{
   Int i;
   if (set == NULL)
      return -1;
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      set->ws[i] = 0xFFFFFFFF;
   return 0;
}

Int VG_(ksigemptyset)( vki_ksigset_t* set )
{
   Int i;
   if (set == NULL)
      return -1;
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      set->ws[i] = 0x0;
   return 0;
}

Int VG_(ksigaddset)( vki_ksigset_t* set, Int signum )
{
   if (set == NULL)
      return -1;
   if (signum < 1 && signum > VKI_KNSIG)
      return -1;
   signum--;
   set->ws[signum / VKI_KNSIG_BPW] |= (1 << (signum % VKI_KNSIG_BPW));
   return 0;
}

Int VG_(ksigismember) ( vki_ksigset_t* set, Int signum )
{
   if (set == NULL)
      return -1;
   if (signum < 1 && signum > VKI_KNSIG)
      return -1;
   signum--;
   if (1 & ((set->ws[signum / VKI_KNSIG_BPW]) >> (signum % VKI_KNSIG_BPW)))
      return 1;
   else
      return 0;
}


/* The functions sigaction, sigprocmask, sigpending and sigsuspend
   return 0 on success and -1 on error.  
*/
Int VG_(ksigprocmask)( Int how, 
                       const vki_ksigset_t* set, 
                       vki_ksigset_t* oldset)
{
   Int res 
      = vg_do_syscall4(__NR_rt_sigprocmask, 
                       how, (UInt)set, (UInt)oldset, 
                       VKI_KNSIG_WORDS * VKI_BYTES_PER_WORD);
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(ksigaction) ( Int signum,  
                      const vki_ksigaction* act,  
                      vki_ksigaction* oldact)
{
   Int res
     = vg_do_syscall4(__NR_rt_sigaction,
                      signum, (UInt)act, (UInt)oldact, 
                      VKI_KNSIG_WORDS * VKI_BYTES_PER_WORD);
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(ksigaltstack)( const vki_kstack_t* ss, vki_kstack_t* oss )
{
   Int res
     = vg_do_syscall2(__NR_sigaltstack, (UInt)ss, (UInt)oss);
   return VG_(is_kerror)(res) ? -1 : 0;
}

 
Int VG_(ksignal)(Int signum, void (*sighandler)(Int))
{
   Int res;
   vki_ksigaction sa;
   sa.ksa_handler = sighandler;
   sa.ksa_flags = VKI_SA_ONSTACK | VKI_SA_RESTART;
   sa.ksa_restorer = NULL;
   res = VG_(ksigemptyset)( &sa.ksa_mask );
   vg_assert(res == 0);
   res = vg_do_syscall4(__NR_rt_sigaction,
                        signum, (UInt)(&sa), (UInt)NULL,
                        VKI_KNSIG_WORDS * VKI_BYTES_PER_WORD);
   return VG_(is_kerror)(res) ? -1 : 0;
}


/* ---------------------------------------------------------------------
   mmap/munmap, exit
   ------------------------------------------------------------------ */

/* Returns -1 on failure. */
void* VG_(mmap)( void* start, UInt length, 
                 UInt prot, UInt flags, UInt fd, UInt offset)
{
   Int  res;
   UInt args[6];
   args[0] = (UInt)start;
   args[1] = length;
   args[2] = prot;
   args[3] = flags;
   args[4] = fd;
   args[5] = offset;
   res = vg_do_syscall1(__NR_mmap, (UInt)(&(args[0])) );
   return VG_(is_kerror)(res) ? ((void*)(-1)) : (void*)res;
}

/* Returns -1 on failure. */
Int VG_(munmap)( void* start, Int length )
{
   Int res = vg_do_syscall2(__NR_munmap, (UInt)start, (UInt)length );
   return VG_(is_kerror)(res) ? -1 : 0;
}

void VG_(exit)( Int status )
{
   (void)vg_do_syscall1(__NR_exit, (UInt)status );
   /* Why are we still alive here? */
   /*NOTREACHED*/
   vg_assert(2+2 == 5);
}

/* ---------------------------------------------------------------------
   printf implementation.  The key function, vg_vprintf(), emits chars 
   into a caller-supplied function.  Distantly derived from:

      vprintf replacement for Checker.
      Copyright 1993, 1994, 1995 Tristan Gingold
      Written September 1993 Tristan Gingold
      Tristan Gingold, 8 rue Parmentier, F-91120 PALAISEAU, FRANCE

   (Checker itself was GPL'd.)
   ------------------------------------------------------------------ */


/* Some flags.  */
#define VG_MSG_SIGNED    1 /* The value is signed. */
#define VG_MSG_ZJUSTIFY  2 /* Must justify with '0'. */
#define VG_MSG_LJUSTIFY  4 /* Must justify on the left. */


/* Copy a string into the buffer. */
static void
myvprintf_str ( void(*send)(Char), Int flags, Int width, Char* str, 
                Bool capitalise )
{
#  define MAYBE_TOUPPER(ch) (capitalise ? VG_(toupper)(ch) : (ch))

   Int i, extra;
   Int len = VG_(strlen)(str);

   if (width == 0) {
      for (i = 0; i < len; i++)
         send(MAYBE_TOUPPER(str[i]));
      return;
   }

   if (len > width) {
      for (i = 0; i < width; i++)
         send(MAYBE_TOUPPER(str[i]));
      return;
   }

   extra = width - len;
   if (flags & VG_MSG_LJUSTIFY) {
      for (i = 0; i < extra; i++)
         send(' ');
   }
   for (i = 0; i < len; i++)
      send(MAYBE_TOUPPER(str[i]));
   if (!(flags & VG_MSG_LJUSTIFY)) {
      for (i = 0; i < extra; i++)
         send(' ');
   }

#  undef MAYBE_TOUPPER
}

/* Write P into the buffer according to these args:
 *  If SIGN is true, p is a signed.
 *  BASE is the base.
 *  If WITH_ZERO is true, '0' must be added.
 *  WIDTH is the width of the field.
 */
static void
myvprintf_int64 ( void(*send)(Char), Int flags, Int base, Int width, ULong p)
{
   Char buf[40];
   Int  ind = 0;
   Int  i;
   Bool neg = False;
   Char *digits = "0123456789ABCDEF";
 
   if (base < 2 || base > 16)
      return;
 
   if ((flags & VG_MSG_SIGNED) && (Long)p < 0) {
      p   = - (Long)p;
      neg = True;
   }

   if (p == 0)
      buf[ind++] = '0';
   else {
      while (p > 0) {
         buf[ind++] = digits[p % base];
         p /= base;
       }
   }

   if (neg)
      buf[ind++] = '-';

   if (width > 0 && !(flags & VG_MSG_LJUSTIFY)) {
      for(; ind < width; ind++) {
         vg_assert(ind < 39);
         buf[ind] = (flags & VG_MSG_ZJUSTIFY) ? '0': ' ';
      }
   }

   /* Reverse copy to buffer.  */
   for (i = ind -1; i >= 0; i--)
      send(buf[i]);

   if (width > 0 && (flags & VG_MSG_LJUSTIFY)) {
      for(; ind < width; ind++)
         send((flags & VG_MSG_ZJUSTIFY) ? '0': ' ');
   }
}


/* A simple vprintf().  */
void
VG_(vprintf) ( void(*send)(Char), const Char *format, va_list vargs )
{
   int i;
   int flags;
   int width;
   Bool is_long;

   /* We assume that vargs has already been initialised by the 
      caller, using va_start, and that the caller will similarly
      clean up with va_end.
   */

   for (i = 0; format[i] != 0; i++) {
      if (format[i] != '%') {
         send(format[i]);
         continue;
      }
      i++;
      /* A '%' has been found.  Ignore a trailing %. */
      if (format[i] == 0)
         break;
      if (format[i] == '%') {
         /* `%%' is replaced by `%'. */
         send('%');
         continue;
      }
      flags = 0;
      is_long = False;
      width = 0; /* length of the field. */
      /* If '-' follows '%', justify on the left. */
      if (format[i] == '-') {
         flags |= VG_MSG_LJUSTIFY;
         i++;
      }
      /* If '0' follows '%', pads will be inserted. */
      if (format[i] == '0') {
         flags |= VG_MSG_ZJUSTIFY;
         i++;
      }
      /* Compute the field length. */
      while (format[i] >= '0' && format[i] <= '9') {
         width *= 10;
         width += format[i++] - '0';
      }
      while (format[i] == 'l') {
         i++;
         is_long = True;
      }

      switch (format[i]) {
         case 'd': /* %d */
            flags |= VG_MSG_SIGNED;
            if (is_long)
               myvprintf_int64(send, flags, 10, width, 
                               (ULong)(va_arg (vargs, Long)));
            else
               myvprintf_int64(send, flags, 10, width, 
                               (ULong)(va_arg (vargs, Int)));
            break;
         case 'u': /* %u */
            if (is_long)
               myvprintf_int64(send, flags, 10, width, 
                               (ULong)(va_arg (vargs, ULong)));
            else
               myvprintf_int64(send, flags, 10, width, 
                               (ULong)(va_arg (vargs, UInt)));
            break;
         case 'p': /* %p */
            send('0');
            send('x');
            myvprintf_int64(send, flags, 16, width, 
                            (ULong)((UInt)va_arg (vargs, void *)));
            break;
         case 'x': /* %x */
            if (is_long)
               myvprintf_int64(send, flags, 16, width, 
                               (ULong)(va_arg (vargs, ULong)));
            else
               myvprintf_int64(send, flags, 16, width, 
                               (ULong)(va_arg (vargs, UInt)));
            break;
         case 'c': /* %c */
            send(va_arg (vargs, int));
            break;
         case 's': case 'S': { /* %s */
            char *str = va_arg (vargs, char *);
            if (str == (char*) 0) str = "(null)";
            myvprintf_str(send, flags, width, str, format[i]=='S');
            break;
         }
         default:
            break;
      }
   }
}


/* A general replacement for printf().  Note that only low-level 
   debugging info should be sent via here.  The official route is to
   to use vg_message().  This interface is deprecated.
*/
static char myprintf_buf[100];
static int  n_myprintf_buf;

static void add_to_myprintf_buf ( Char c )
{
   if (n_myprintf_buf >= 100-10 /*paranoia*/ ) {
      if (VG_(clo_logfile_fd) >= 0)
         VG_(write)
           (VG_(clo_logfile_fd), myprintf_buf, VG_(strlen)(myprintf_buf));
      n_myprintf_buf = 0;
      myprintf_buf[n_myprintf_buf] = 0;      
   }
   myprintf_buf[n_myprintf_buf++] = c;
   myprintf_buf[n_myprintf_buf] = 0;
}

void VG_(printf) ( const char *format, ... )
{
   va_list vargs;
   va_start(vargs,format);
   
   n_myprintf_buf = 0;
   myprintf_buf[n_myprintf_buf] = 0;      
   VG_(vprintf) ( add_to_myprintf_buf, format, vargs );

   if (n_myprintf_buf > 0 && VG_(clo_logfile_fd) >= 0)
      VG_(write)
         ( VG_(clo_logfile_fd), myprintf_buf, VG_(strlen)(myprintf_buf));

   va_end(vargs);
}


/* A general replacement for sprintf(). */
static Char* vg_sprintf_ptr;

static void add_to_vg_sprintf_buf ( Char c )
{
   *vg_sprintf_ptr++ = c;
}

void VG_(sprintf) ( Char* buf, Char *format, ... )
{
   va_list vargs;
   va_start(vargs,format);

   vg_sprintf_ptr = buf;
   VG_(vprintf) ( add_to_vg_sprintf_buf, format, vargs );
   add_to_vg_sprintf_buf(0);

   va_end(vargs);
}


/* ---------------------------------------------------------------------
   Misc str* functions.
   ------------------------------------------------------------------ */

Bool VG_(isspace) ( Char c )
{
   return (c == ' ' || c == '\n' || c == '\t' || c == 0);
}


Int VG_(strlen) ( const Char* str )
{
   Int i = 0;
   while (str[i] != 0) i++;
   return i;
}


Long VG_(atoll) ( Char* str )
{
   Bool neg = False;
   Long n = 0;
   if (*str == '-') { str++; neg = True; };
   while (*str >= '0' && *str <= '9') {
      n = 10*n + (Long)(*str - '0');
      str++;
   }
   if (neg) n = -n;
   return n;
}


Char* VG_(strcat) ( Char* dest, const Char* src )
{
   Char* dest_orig = dest;
   while (*dest) dest++;
   while (*src) *dest++ = *src++;
   *dest = 0;
   return dest_orig;
}


Char* VG_(strncat) ( Char* dest, const Char* src, Int n )
{
   Char* dest_orig = dest;
   while (*dest) dest++;
   while (*src && n > 0) { *dest++ = *src++; n--; }
   *dest = 0;
   return dest_orig;
}


Char* VG_(strpbrk) ( const Char* s, const Char* accept )
{
   const Char* a;
   while (*s) {
      a = accept;
      while (*a)
         if (*a++ == *s)
            return (Char *) s;
      s++;
   }
   return NULL;
}


Char* VG_(strcpy) ( Char* dest, const Char* src )
{
   Char* dest_orig = dest;
   while (*src) *dest++ = *src++;
   *dest = 0;
   return dest_orig;
}


/* Copy bytes, not overrunning the end of dest and always ensuring
   zero termination. */
void VG_(strncpy_safely) ( Char* dest, const Char* src, Int ndest )
{
   Int i;
   vg_assert(ndest > 0);
   i = 0;
   dest[i] = 0;
   while (True) {
      if (src[i] == 0) return;
      if (i >= ndest-1) return;
      dest[i] = src[i];
      i++;
      dest[i] = 0;
   }
}


void VG_(strncpy) ( Char* dest, const Char* src, Int ndest )
{
   VG_(strncpy_safely)( dest, src, ndest+1 ); 
}


Int VG_(strcmp) ( const Char* s1, const Char* s2 )
{
   while (True) {
      if (*s1 == 0 && *s2 == 0) return 0;
      if (*s1 == 0) return -1;
      if (*s2 == 0) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++;
   }
}


Int VG_(strcmp_ws) ( const Char* s1, const Char* s2 )
{
   while (True) {
      if (VG_(isspace)(*s1) && VG_(isspace)(*s2)) return 0;
      if (VG_(isspace)(*s1)) return -1;
      if (VG_(isspace)(*s2)) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++;
   }
}


Int VG_(strncmp) ( const Char* s1, const Char* s2, Int nmax )
{
   Int n = 0;
   while (True) {
      if (n >= nmax) return 0;
      if (*s1 == 0 && *s2 == 0) return 0;
      if (*s1 == 0) return -1;
      if (*s2 == 0) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++; n++;
   }
}


Int VG_(strncmp_ws) ( const Char* s1, const Char* s2, Int nmax )
{
   Int n = 0;
   while (True) {
      if (n >= nmax) return 0;
      if (VG_(isspace)(*s1) && VG_(isspace)(*s2)) return 0;
      if (VG_(isspace)(*s1)) return -1;
      if (VG_(isspace)(*s2)) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++; n++;
   }
}


Char* VG_(strstr) ( const Char* haystack, Char* needle )
{
   Int n = VG_(strlen)(needle);
   while (True) {
      if (haystack[0] == 0) 
         return NULL;
      if (VG_(strncmp)(haystack, needle, n) == 0) 
         return (Char*)haystack;
      haystack++;
   }
}


Char* VG_(strchr) ( const Char* s, Char c )
{
   while (True) {
      if (*s == c) return (Char*)s;
      if (*s == 0) return NULL;
      s++;
   }
}


Char VG_(toupper) ( Char c )
{
   if (c >= 'a' && c <= 'z')
      return c + ('A' - 'a'); 
   else
      return c;
}


Char* VG_(strdup) ( ArenaId aid, const Char* s )
{
    Int   i;
    Int   len = VG_(strlen)(s) + 1;
    Char* res = VG_(malloc) (aid, len);
    for (i = 0; i < len; i++)
       res[i] = s[i];
    return res;
}


/* ---------------------------------------------------------------------
   A simple string matching routine, purloined from Hugs98.
      `*'    matches any sequence of zero or more characters
      `?'    matches any single character exactly 
      `\c'   matches the character c only (ignoring special chars)
      c      matches the character c only
   ------------------------------------------------------------------ */

/* Keep track of recursion depth. */
static Int recDepth;

static Bool stringMatch_wrk ( Char* pat, Char* str )
{
   vg_assert(recDepth >= 0 && recDepth < 500);
   recDepth++;
   for (;;) {
      switch (*pat) {
         case '\0' : return (*str=='\0');
         case '*'  : do {
                        if (stringMatch_wrk(pat+1,str)) {
                           recDepth--;
                           return True;
                        }
                     } while (*str++);
                     recDepth--;
                     return False;
         case '?'  : if (*str++=='\0') {
                        recDepth--;
                        return False;
                     }
                     pat++;
                     break;
         case '\\' : if (*++pat == '\0') {
                        recDepth--;
                        return False; /* spurious trailing \ in pattern */
                     }
                     /* falls through to ... */
         default   : if (*pat++ != *str++) {
                        recDepth--;
                        return False;
                     }
                     break;
      }
   }
}

Bool VG_(stringMatch) ( Char* pat, Char* str )
{
   Bool b;
   recDepth = 0;
   b = stringMatch_wrk ( pat, str );
   /*
   VG_(printf)("%s   %s   %s\n",
	       b?"TRUE ":"FALSE", pat, str);
   */
   return b;
}


/* ---------------------------------------------------------------------
   Assertery.
   ------------------------------------------------------------------ */

#define EMAIL_ADDR "jseward@acm.org"

void VG_(assert_fail) ( Char* expr, Char* file, Int line, Char* fn )
{
   VG_(printf)("\n%s: %s:%d (%s): Assertion `%s' failed.\n",
               "valgrind", file, line, fn, expr );
   VG_(printf)("Please report this bug to me at: %s\n\n", EMAIL_ADDR);
   VG_(shutdown_logging)();
   /* vg_restore_SIGABRT(); */
   VG_(exit)(1);
}

void VG_(panic) ( Char* str )
{
   VG_(printf)("\nvalgrind: the `impossible' happened:\n   %s\n", str);
   VG_(printf)("Basic block ctr is approximately %llu\n", VG_(bbs_done) );
   VG_(printf)("Please report this bug to me at: %s\n\n", EMAIL_ADDR);
   VG_(shutdown_logging)();
   /* vg_restore_SIGABRT(); */
   VG_(exit)(1);
}

#undef EMAIL_ADDR


/* ---------------------------------------------------------------------
   Primitive support for reading files.
   ------------------------------------------------------------------ */

/* Returns -1 on failure. */
Int VG_(open_read) ( Char* pathname )
{
   Int fd;
   /* VG_(printf)("vg_open_read %s\n", pathname ); */

   /* This gets a segmentation fault if pathname isn't a valid file.
      I don't know why.  It seems like the call to open is getting
      intercepted and messed with by glibc ... */
   /* fd = open( pathname, O_RDONLY ); */
   /* ... so we go direct to the horse's mouth, which seems to work
      ok: */
   const int O_RDONLY = 0; /* See /usr/include/bits/fcntl.h */
   fd = vg_do_syscall3(__NR_open, (UInt)pathname, O_RDONLY, 0);
   /* VG_(printf)("result = %d\n", fd); */
   if (VG_(is_kerror)(fd)) fd = -1;
   return fd;
}
 

void VG_(close) ( Int fd )
{
   vg_do_syscall1(__NR_close, fd);
}


Int VG_(read) ( Int fd, void* buf, Int count)
{
   Int res;
   /* res = read( fd, buf, count ); */
   res = vg_do_syscall3(__NR_read, fd, (UInt)buf, count);
   if (VG_(is_kerror)(res)) res = -1;
   return res;
}

Int VG_(write) ( Int fd, void* buf, Int count)
{
   Int res;
   /* res = write( fd, buf, count ); */
   res = vg_do_syscall3(__NR_write, fd, (UInt)buf, count);
   if (VG_(is_kerror)(res)) res = -1;
   return res;
}

/* Misc functions looking for a proper home. */

/* We do getenv without libc's help by snooping around in
   VG_(client_env) as determined at startup time. */
Char* VG_(getenv) ( Char* varname )
{
   Int i, n;
   n = VG_(strlen)(varname);
   for (i = 0; VG_(client_envp)[i] != NULL; i++) {
      Char* s = VG_(client_envp)[i];
      if (VG_(strncmp)(varname, s, n) == 0 && s[n] == '=') {
         return & s[n+1];
      }
   }
   return NULL;
}

/* You'd be amazed how many places need to know the current pid. */
Int VG_(getpid) ( void )
{
   Int res;
   /* res = getpid(); */
   res = vg_do_syscall0(__NR_getpid);
   return res;
}


/* ---------------------------------------------------------------------
   Primitive support for bagging memory via mmap.
   ------------------------------------------------------------------ */

void* VG_(get_memory_from_mmap) ( Int nBytes )
{
   static UInt tot_alloc = 0;
   void* p = VG_(mmap)( 0, nBytes,
                        VKI_PROT_READ|VKI_PROT_WRITE|VKI_PROT_EXEC, 
                        VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, -1, 0 );
   if (p != ((void*)(-1))) {
      tot_alloc += (UInt)nBytes;
      if (0)
         VG_(printf)("get_memory_from_mmap: %d tot, %d req\n",
                     tot_alloc, nBytes);
      return p;
   }
   VG_(printf)("vg_get_memory_from_mmap failed on request of %d\n", 
               nBytes);
   VG_(panic)("vg_get_memory_from_mmap: out of memory!  Fatal!  Bye!\n");
}


/*--------------------------------------------------------------------*/
/*--- end                                              vg_mylibc.c ---*/
/*--------------------------------------------------------------------*/
