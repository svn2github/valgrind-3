
/*--------------------------------------------------------------------*/
/*--- Sets of words, with unique set identifiers.                  ---*/
/*---                                                 hg_wordset.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Helgrind, a Valgrind tool for detecting errors
   in threaded programs.

   Copyright (C) 2007-2008 OpenWorks LLP
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

   Neither the names of the U.S. Department of Energy nor the
   University of California nor the names of its contributors may be
   used to endorse or promote products derived from this software
   without prior written permission.
*/

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"

#define HG_(str) VGAPPEND(vgHelgrind_,str)
#include "hg_wordfm.h"
#include "hg_wordset.h"

//------------------------------------------------------------------//
//--- Word Cache                                                 ---//
//------------------------------------------------------------------//

typedef
   struct { UWord arg1; UWord arg2; UWord res; }
   WCacheEnt;

/* Each cache is a fixed sized array of N_WCACHE_STAT_MAX entries.
   However only the first .dynMax are used.  This is because at some
   point, expanding the cache further overall gives a slowdown because
   searching more entries more than negates any performance advantage
   from caching those entries in the first place.  Hence use .dynMax
   to allow the size of the cache(s) to be set differently for each
   different WordSetU. */
#define N_WCACHE_STAT_MAX 64
typedef
   struct {
      WCacheEnt ent[N_WCACHE_STAT_MAX];
      UWord     dynMax; /* 1 .. N_WCACHE_STAT_MAX inclusive */
      UWord     inUse;  /* 0 .. dynMax inclusive */
   }
   WCache;

#define WCache_INIT(_zzcache,_zzdynmax)                              \
   do {                                                              \
      tl_assert((_zzdynmax) >= 1);                                   \
      tl_assert((_zzdynmax) <= N_WCACHE_STAT_MAX);                   \
      (_zzcache).dynMax = (_zzdynmax);                               \
      (_zzcache).inUse = 0;                                          \
   } while (0)

#define WCache_INVAL(_zzcache)                                       \
   do {                                                              \
      (_zzcache).inUse = 0;                                          \
   } while (0)

#define WCache_LOOKUP_AND_RETURN(_retty,_zzcache,_zzarg1,_zzarg2)    \
   do {                                                              \
      UWord   _i;                                                    \
      UWord   _arg1  = (UWord)(_zzarg1);                             \
      UWord   _arg2  = (UWord)(_zzarg2);                             \
      WCache* _cache = &(_zzcache);                                  \
      tl_assert(_cache->dynMax >= 1);                                \
      tl_assert(_cache->dynMax <= N_WCACHE_STAT_MAX);                \
      tl_assert(_cache->inUse >= 0);                                 \
      tl_assert(_cache->inUse <= _cache->dynMax);                    \
      if (_cache->inUse > 0) {                                       \
         if (_cache->ent[0].arg1 == _arg1                            \
             && _cache->ent[0].arg2 == _arg2)                        \
            return (_retty)_cache->ent[0].res;                       \
         for (_i = 1; _i < _cache->inUse; _i++) {                    \
            if (_cache->ent[_i].arg1 == _arg1                        \
                && _cache->ent[_i].arg2 == _arg2) {                  \
               WCacheEnt tmp     = _cache->ent[_i-1];                \
               _cache->ent[_i-1] = _cache->ent[_i];                  \
               _cache->ent[_i]   = tmp;                              \
               return (_retty)_cache->ent[_i-1].res;                 \
            }                                                        \
         }                                                           \
      }                                                              \
   } while (0)

#define WCache_UPDATE(_zzcache,_zzarg1,_zzarg2,_zzresult)            \
   do {                                                              \
      Word    _i;                                                    \
      UWord   _arg1  = (UWord)(_zzarg1);                             \
      UWord   _arg2  = (UWord)(_zzarg2);                             \
      UWord   _res   = (UWord)(_zzresult);                           \
      WCache* _cache = &(_zzcache);                                  \
      tl_assert(_cache->dynMax >= 1);                                \
      tl_assert(_cache->dynMax <= N_WCACHE_STAT_MAX);                \
      tl_assert(_cache->inUse >= 0);                                 \
      tl_assert(_cache->inUse <= _cache->dynMax);                    \
      if (_cache->inUse < _cache->dynMax)                            \
         _cache->inUse++;                                            \
      for (_i = _cache->inUse-1; _i >= 1; _i--)                      \
         _cache->ent[_i] = _cache->ent[_i-1];                        \
      _cache->ent[0].arg1 = _arg1;                                   \
      _cache->ent[0].arg2 = _arg2;                                   \
      _cache->ent[0].res  = _res;                                    \
   } while (0)


//------------------------------------------------------------------//
//---                          WordSet                           ---//
//---                       Implementation                       ---//
//------------------------------------------------------------------//

typedef
   struct {
      WordSetU* owner; /* for sanity checking */
      UWord*    words;
      UWord     size; /* Really this should be SizeT */
      UWord     refcount; 
   }
   WordVec;

#define N_RECYCLE_CACHE_MAX 32

/* ix2vec[0 .. ix2vec_used-1] are pointers to the lock sets (WordVecs)
   really.  vec2ix is the inverse mapping, mapping WordVec* to the
   corresponding ix2vec entry number.  The two mappings are mutually
   redundant. */
struct _WordSetU {
      void*     (*alloc)(SizeT);
      void      (*dealloc)(void*);
      WordFM*   vec2ix; /* WordVec-to-WordSet mapping tree */
      WordVec** ix2vec; /* WordSet-to-WordVec mapping array */
      UWord     ix2vec_size;
      UWord     ix2vec_used;
      WordSet   empty; /* cached, for speed */
      /* Caches for some operations */
      WCache    cache_addTo;
      WCache    cache_delFrom;
      WCache    cache_intersect;
      WCache    cache_minus;
      /* recycle cache */
      UWord     recycle_cache[N_RECYCLE_CACHE_MAX];
      UInt      recycle_cache_n;
      /* Stats */
      UWord     n_add;
      UWord     n_add_uncached;
      UWord     n_del;
      UWord     n_del_uncached;
      UWord     n_union;
      UWord     n_intersect;
      UWord     n_intersect_uncached;
      UWord     n_minus;
      UWord     n_minus_uncached;
      UWord     n_elem;
      UWord     n_doubleton;
      UWord     n_isEmpty;
      UWord     n_isSingleton;
      UWord     n_isSorE;
      UWord     n_anyElementOf;
      UWord     n_elementOf;
      UWord     n_isSubsetOf;
      UWord     n_recycle;
   };

/* Create a new WordVec of the given size.  The WordVec and the array
   of UWord its .words field points at, are allocated in the same
   piece of memory, so as to reduce allocator overheads.  We have to
   be careful to ensure that the pointed-to array is suitably aligned,
   but that should be OK since WordVec has a size of 3 machine words.
   To be on the safe side this is asserted for. */
static WordVec* new_WV_of_size ( WordSetU* wsu, UWord sz )
{
   HChar*   allocated_mem;
   WordVec* wv;
   tl_assert(sz >= 0);
   tl_assert(0 == (sizeof(WordVec) % sizeof(UWord)));
   allocated_mem = wsu->alloc( sizeof(WordVec) + (SizeT)sz * sizeof(UWord));
   wv = (WordVec*) allocated_mem;
   wv->owner = wsu;
   wv->words = NULL;
   wv->size = sz;
   wv->refcount = 0;
   if (sz > 0) {
      wv->words = (UWord*)(allocated_mem + sizeof(WordVec));
      tl_assert(0 == (UWord)(wv->words) % sizeof(UWord));
   }
   return wv;
}

static void delete_WV ( WordVec* wv )
{
   void (*dealloc)(void*) = wv->owner->dealloc;
   dealloc(wv);
}
static void delete_WV_for_FM ( UWord wv ) {
   delete_WV( (WordVec*)wv );
}

static Word cmp_WordVecs_for_FM ( UWord wv1W, UWord wv2W )
{
   UWord    i;
   WordVec* wv1    = (WordVec*)wv1W;
   WordVec* wv2    = (WordVec*)wv2W;
   UWord    common = wv1->size < wv2->size ? wv1->size : wv2->size;
   for (i = 0; i < common; i++) {
      if (wv1->words[i] == wv2->words[i])
         continue;
      if (wv1->words[i] < wv2->words[i])
         return -1;
      if (wv1->words[i] > wv2->words[i])
         return 1;
      tl_assert(0);
   }
   /* Ok, the common sections are identical.  So now consider the
      tails.  Both sets are considered to finish in an implied
      sequence of -infinity. */
   if (wv1->size < wv2->size) {
      tl_assert(common == wv1->size);
      return -1; /* impliedly, wv1 contains some -infinitys in places
                    where wv2 doesn't. */
   }
   if (wv1->size > wv2->size) {
      tl_assert(common == wv2->size);
      return 1;
   }
   tl_assert(common == wv1->size);
   return 0; /* identical */
}

static void ensure_ix2vec_space ( WordSetU* wsu )
{
   UInt      i, new_sz;
   WordVec** new_vec;
   tl_assert(wsu->ix2vec_used <= wsu->ix2vec_size);
   if (wsu->ix2vec_used < wsu->ix2vec_size)
      return;
   new_sz = 2 * wsu->ix2vec_size;
   if (new_sz == 0) new_sz = 2;
   new_vec = wsu->alloc( new_sz * sizeof(WordVec*) );
   tl_assert(new_vec);
   for (i = 0; i < wsu->ix2vec_size; i++)
      new_vec[i] = wsu->ix2vec[i];
   if (wsu->ix2vec)
      wsu->dealloc(wsu->ix2vec);
   wsu->ix2vec = new_vec;
   wsu->ix2vec_size = new_sz;
}

/* Index into a WordSetU, doing the obvious range check.  Failure of
   the assertions marked XXX and YYY is an indication of passing the
   wrong WordSetU* in the public API of this module. */
static inline WordVec* do_ix2vec ( WordSetU* wsu, WordSet ws )
{
   WordVec* wv;
   tl_assert(wsu->ix2vec_used <= wsu->ix2vec_size);
   if (wsu->ix2vec_used > 0)
      tl_assert(wsu->ix2vec);
   /* If this assertion fails, it may mean you supplied a 'ws'
      that does not come from the 'wsu' universe. */
   tl_assert(ws < wsu->ix2vec_used); /* XXX */
   wv = wsu->ix2vec[ws];
   /* Make absolutely sure that 'ws' is a member of 'wsu'. */
   tl_assert(wv);
   tl_assert(wv->owner == wsu); /* YYY */
   return wv;
}

/* See if wv is contained within wsu.  If so, deallocate wv and return
   the index of the already-present copy.  If not, add wv to both the
   vec2ix and ix2vec mappings and return its index. 
*/
static WordSet add_or_dealloc_WordVec( WordSetU* wsu, WordVec* wv_new )
{
   Bool     have;
   WordVec* wv_old;
   UWord/*Set*/ ix_old = -1;
   /* Really WordSet, but need something that can safely be casted to
      a Word* in the lookupFM.  Making it WordSet (which is 32 bits)
      causes failures on a 64-bit platform. */
   tl_assert(wv_new->owner == wsu);
   have = HG_(lookupFM)( wsu->vec2ix, 
                         (Word*)&wv_old, (Word*)&ix_old,
                         (Word)wv_new );
   if (have) {
      tl_assert(wv_old != wv_new);
      tl_assert(wv_old);
      tl_assert(wv_old->owner == wsu);
      tl_assert(ix_old < wsu->ix2vec_used);
      tl_assert(wsu->ix2vec[ix_old] == wv_old);
      delete_WV( wv_new );
      return (WordSet)ix_old;
   } else {
      ensure_ix2vec_space( wsu );
      tl_assert(wsu->ix2vec);
      tl_assert(wsu->ix2vec_used < wsu->ix2vec_size);
      wsu->ix2vec[wsu->ix2vec_used] = wv_new;
      HG_(addToFM)( wsu->vec2ix, (Word)wv_new, (Word)wsu->ix2vec_used );
      if (0) VG_(printf)("aodW %d\n", (Int)wsu->ix2vec_used );
      wsu->ix2vec_used++;
      tl_assert(wsu->ix2vec_used <= wsu->ix2vec_size);
      return (WordSet)(wsu->ix2vec_used - 1);
   }
}

// Similar to add_or_dealloc_WordVec, but different allocation policy: 
// If wv_new is already in wsu, just return it's index and do not deallocate. 
// Else allocate new WordVec, and copy wv_new there. 
static WordSet find_or_alloc_and_add_WordVec( WordSetU* wsu, WordVec* wv_new )
{
   Bool     have;
   WordVec* wv_old;
   UWord/*Set*/ ix_old = -1;
   tl_assert(wv_new->owner == wsu);
   have = HG_(lookupFM)( wsu->vec2ix, 
                         (Word*)&wv_old, (Word*)&ix_old,
                         (Word)wv_new );
   if (have) {
      tl_assert(wv_old != wv_new);
      tl_assert(wv_old);
      tl_assert(wv_old->owner == wsu);
      tl_assert(ix_old < wsu->ix2vec_used);
      tl_assert(wsu->ix2vec[ix_old] == wv_old);
      return (WordSet)ix_old;
   } else {
      // allocate new WordVec and copy contents from wv_new.
      WordVec *wv_tmp = new_WV_of_size( wsu, wv_new->size );
      UInt i;
      for (i = 0; i < wv_new->size; i++) {
         wv_tmp->words[i] = wv_new->words[i];
      }
      wv_new = wv_tmp;
      ensure_ix2vec_space( wsu );
      tl_assert(wsu->ix2vec);
      tl_assert(wsu->ix2vec_used < wsu->ix2vec_size);
      wsu->ix2vec[wsu->ix2vec_used] = wv_new;
      HG_(addToFM)( wsu->vec2ix, (Word)wv_new, (Word)wsu->ix2vec_used );
      if (0) VG_(printf)("aodW %d\n", (Int)wsu->ix2vec_used );
      wsu->ix2vec_used++;
      tl_assert(wsu->ix2vec_used <= wsu->ix2vec_size);
      return (WordSet)(wsu->ix2vec_used - 1);
   }
}



WordSetU* HG_(newWordSetU) ( void* (*alloc_nofail)( SizeT ),
                             void  (*dealloc)(void*),
                             Word  cacheSize )
{
   WordSetU* wsu;
   WordVec*  empty;

   wsu          = alloc_nofail( sizeof(WordSetU) );
   VG_(memset)( wsu, 0, sizeof(WordSetU) );
   wsu->alloc   = alloc_nofail;
   wsu->dealloc = dealloc;
   wsu->vec2ix  = HG_(newFM)( alloc_nofail, dealloc, cmp_WordVecs_for_FM );
   wsu->ix2vec_used = 0;
   wsu->ix2vec_size = 0;
   wsu->ix2vec      = NULL;
   WCache_INIT(wsu->cache_addTo,     cacheSize);
   WCache_INIT(wsu->cache_delFrom,   cacheSize);
   WCache_INIT(wsu->cache_intersect, cacheSize);
   WCache_INIT(wsu->cache_minus,     cacheSize);
   empty = new_WV_of_size( wsu, 0 );
   wsu->empty = add_or_dealloc_WordVec( wsu, empty );

   return wsu;
}

void HG_(deleteWordSetU) ( WordSetU* wsu )
{
   void (*dealloc)(void*) = wsu->dealloc;
   tl_assert(wsu->vec2ix);
   HG_(deleteFM)( wsu->vec2ix, delete_WV_for_FM, NULL/*val-finalizer*/ );
   if (wsu->ix2vec)
      dealloc(wsu->ix2vec);
   dealloc(wsu);
}

WordSet HG_(emptyWS) ( WordSetU* wsu )
{
   return wsu->empty;
}

Bool HG_(isEmptyWS) ( WordSetU* wsu, WordSet ws )
{
   WordVec* wv = do_ix2vec( wsu, ws );
   wsu->n_isEmpty++;
   if (wv->size == 0) {
      tl_assert(ws == wsu->empty);
      return True;
   } else {
      tl_assert(ws != wsu->empty);
      return False;
   }
}

Bool HG_(isSingletonWS) ( WordSetU* wsu, WordSet ws, UWord w )
{
   WordVec* wv;
   tl_assert(wsu);
   wsu->n_isSingleton++;
   wv = do_ix2vec( wsu, ws );
   return (Bool)(wv->size == 1 && wv->words[0] == w);
}

Bool HG_(isSingletonOrEmptyWS) ( WordSetU* wsu, WordSet ws, UWord w )
{
   WordVec* wv;
   tl_assert(wsu);
   wsu->n_isSorE++;
   wv = do_ix2vec( wsu, ws );
   if (wv->size == 0) return True;
   return (Bool)(wv->size == 1 && wv->words[0] == w);
}

UWord HG_(cardinalityWS) ( WordSetU* wsu, WordSet ws )
{
   WordVec* wv;
   tl_assert(wsu);
   wv = do_ix2vec( wsu, ws );
   tl_assert(wv->size >= 0);
   return wv->size;
}

UWord HG_(anyElementOfWS) ( WordSetU* wsu, WordSet ws )
{
   WordVec* wv;
   tl_assert(wsu);
   wsu->n_anyElementOf++;
   wv = do_ix2vec( wsu, ws );
   tl_assert(wv->size >= 1);
   return wv->words[0];
}

UWord HG_(cardinalityWSU) ( WordSetU* wsu )
{
   tl_assert(wsu);
   return wsu->ix2vec_used;
}

void HG_(getPayloadWS) ( /*OUT*/UWord** words, /*OUT*/UWord* nWords, 
                         WordSetU* wsu, WordSet ws )
{
   WordVec* wv;
   tl_assert(wsu);
   wv = do_ix2vec( wsu, ws );
   tl_assert(wv->size >= 0);
   *nWords = wv->size;
   *words  = wv->words;
}


// Increment the refcount of ws by sz. 
void    HG_(refWS)          ( WordSetU *wsu, WordSet ws, UWord sz)
{
   WordVec* wv;
   tl_assert(wsu);
   wv = do_ix2vec( wsu, ws );
   tl_assert(wv->size >= 0);
   wv->refcount += sz;
}

// Decrement the refcount of ws by sz and return the new refcount value.
UWord    HG_(unrefWS)        ( WordSetU *wsu, WordSet ws, UWord sz)
{
   WordVec* wv;
   tl_assert(wsu);
   wv = do_ix2vec( wsu, ws );
   tl_assert(wv->size >= 0);
   tl_assert(wv->refcount >= sz);
   wv->refcount -= sz;
   return wv->refcount;
}

// Get the current refcount of ws.
UWord    HG_(getRefWS)        ( WordSetU *wsu, WordSet ws)
{
   WordVec* wv;
   tl_assert(wsu);
   wv = do_ix2vec( wsu, ws );
   tl_assert(wv->size >= 0);
   return wv->refcount;
}


/*
   WordSet Recycling. 
   Once HG_(recycleWS) is called on a ws, 
   the memory allocated for ws is freed and ws is removed from vec2ix. 
   The slot wsu->ix2vec[ws] is assigned NULL and remains NULL forever. 
   We also have to flush cashes (otherwise if the user creates a new set 
   equal to the recycled one, cache may return the index of the recycled ws). 

   We maintain a cache os WordSets to recycle and do recycling 
   every N_RECYCLE_CACHE_MAX call to HG_(recycleWS). 
   This is done to minimize the number of addTo/delFrom/etc cache invals.

   Possible improvement to this scheme: 
   - Do not call deleteWV, instead maintain our own free list (?). 
   - Recycle the slot wsu->ix2vec[ws] (will complicate sanity checking). 
*/
void    HG_(recycleWS)      ( WordSetU *wsu, WordSet ws) 
{
   tl_assert(wsu);

   if (wsu->recycle_cache_n == N_RECYCLE_CACHE_MAX) {
      // cache is full, do the recycling
      UInt i;
      for (i = 0; i < wsu->recycle_cache_n; i++) {
         WordSet ws_to_recycle = wsu->recycle_cache[i];
         WordVec *wv = do_ix2vec( wsu, ws_to_recycle );
         tl_assert(wv->size >= 0);
         tl_assert(wv->refcount == 0);
         HG_(delFromFM)(wsu->vec2ix, NULL, NULL, (Word)wv);
         delete_WV(wv);
         wsu->ix2vec[ws_to_recycle] = NULL; 
         wsu->n_recycle++;
      }
      WCache_INVAL(wsu->cache_addTo);
      WCache_INVAL(wsu->cache_delFrom);
      WCache_INVAL(wsu->cache_intersect);
      WCache_INVAL(wsu->cache_minus);
      wsu->recycle_cache_n = 0;
   }
   tl_assert(wsu->recycle_cache_n < N_RECYCLE_CACHE_MAX);
   wsu->recycle_cache[wsu->recycle_cache_n++] = ws;
}


Bool HG_(plausibleWS) ( WordSetU* wsu, WordSet ws )
{
   if (wsu == NULL) return False;
   if (ws < 0 || ws >= wsu->ix2vec_used)
      return False;
   return True;
}

Bool HG_(saneWS_SLOW) ( WordSetU* wsu, WordSet ws )
{
   WordVec* wv;
   UWord    i;
   if (wsu == NULL) return False;
   if (ws < 0 || ws >= wsu->ix2vec_used)
      return False;
   if (wsu->ix2vec_used > wsu->ix2vec_size) 
      return False;
   if (wsu->ix2vec_used > 0)
      if (!wsu->ix2vec)
         return False;
   if ((ws >= wsu->ix2vec_used))
      return False;
   wv = wsu->ix2vec[ws];
   if (!wv) 
      return False;
   if (wv->owner != wsu) 
      return False;
   if (wv->size < 0) return False;
   if (wv->size > 0) {
      for (i = 0; i < wv->size-1; i++) {
         if (wv->words[i] >= wv->words[i+1])
            return False;
      }
   }
   return True;
}

Bool HG_(elemWS) ( WordSetU* wsu, WordSet ws, UWord w )
{
   UWord    i;
   WordVec* wv = do_ix2vec( wsu, ws );
   wsu->n_elem++;
   for (i = 0; i < wv->size; i++) {
      if (wv->words[i] == w)
         return True;
   }
   return False;
}

WordSet HG_(doubletonWS) ( WordSetU* wsu, UWord w1, UWord w2 )
{
   UWord    wv_arr[2];
   WordVec  wv_;
   WordVec  *wv = &wv_;

   wv_.owner = wsu;
   wv_.words = wv_arr;
   wv_.size  = 2;

   wsu->n_doubleton++;
   if (w1 == w2) {
      wv->size = 1;
      wv->words[0] = w1;
   }
   else if (w1 < w2) {
      wv->words[0] = w1;
      wv->words[1] = w2;
   }
   else {
      tl_assert(w1 > w2);
      wv->words[0] = w2;
      wv->words[1] = w1;
   }
   return find_or_alloc_and_add_WordVec( wsu, wv );
}

WordSet HG_(singletonWS) ( WordSetU* wsu, UWord w )
{
   return HG_(doubletonWS)( wsu, w, w );
}

WordSet HG_(isSubsetOf) ( WordSetU* wsu, WordSet small, WordSet big )
{
   wsu->n_isSubsetOf++;
   return small == HG_(intersectWS)( wsu, small, big );
}

void HG_(ppWS) ( WordSetU* wsu, WordSet ws )
{
   UWord    i;
   WordVec* wv;
   tl_assert(wsu);
   wv = do_ix2vec( wsu, ws );
   VG_(printf)("{");
   for (i = 0; i < wv->size; i++) {
      VG_(printf)("%p", (void*)wv->words[i]);
      if (i < wv->size-1)
         VG_(printf)(",");
   }
   VG_(printf)("}");
}

void HG_(ppWSUstats) ( WordSetU* wsu, HChar* name )
{
   Int i;
   Int d_size = 10;
   Int size_distribution[10] = {0,0,0,0,0,0,0,0,0,0};

   VG_(printf)("   WordSet \"%s\":\n", name);
   VG_(printf)("      addTo        %,10u (%,u uncached)\n",
               wsu->n_add, wsu->n_add_uncached);
   VG_(printf)("      delFrom      %,10u (%,u uncached)\n", 
               wsu->n_del, wsu->n_del_uncached);
   VG_(printf)("      union        %,10u\n", wsu->n_union);
   VG_(printf)("      intersect    %,10u (%u uncached) [nb. incl isSubsetOf]\n", 
               wsu->n_intersect, wsu->n_intersect_uncached);
   VG_(printf)("      minus        %,10u (%u uncached)\n",
               wsu->n_minus, wsu->n_minus_uncached);
   VG_(printf)("      elem         %,10u\n",   wsu->n_elem);
   VG_(printf)("      doubleton    %,10u\n",   wsu->n_doubleton);
   VG_(printf)("      isEmpty      %,10u\n",   wsu->n_isEmpty);
   VG_(printf)("      isSingleton  %,10u\n",   wsu->n_isSingleton);
   VG_(printf)("      isSorEmpty   %,10u\n",   wsu->n_isSorE);
   VG_(printf)("      anyElementOf %,10u\n",   wsu->n_anyElementOf);
   VG_(printf)("      elementOf    %,10u\n",   wsu->n_elementOf);
   VG_(printf)("      isSubsetOf   %,10u\n",   wsu->n_isSubsetOf);
   VG_(printf)("      cardinality  %,10u\n",   (int)HG_(cardinalityWSU)(wsu));
   VG_(printf)("      recycle      %,10u\n",   wsu->n_recycle);

   // compute and print size distributions 
   for (i = 0; i < (Int)HG_(cardinalityWSU)(wsu); i++) {
      if (!HG_(saneWS_SLOW(wsu, i))) continue;
      WordVec *wv = do_ix2vec( wsu, i );
      Int size = wv->size;
      if (size >= d_size) size = d_size-1;
      size_distribution[size]++;
   }
   tl_assert(size_distribution[0] == 1);
   for (i = 1; i < d_size; i++) {
      if (size_distribution[i] == 0) continue;
      if(i == d_size-1)
         VG_(printf)("      size[>=%d] %10d\n",  i,  size_distribution[i]);
      else
         VG_(printf)("      size[%d]   %10d\n",  i,  size_distribution[i]);
   }
}

WordSet HG_(addToWS) ( WordSetU* wsu, WordSet ws, UWord w )
{
   UWord    k, j;
   WordVec* wv = do_ix2vec( wsu, ws ); ;
   UWord    wv_new_arr[wv->size+1]; // C99 array. 
   WordVec  wv_new_ = {wsu, wv_new_arr, wv->size+1};  
   WordVec  *wv_new = &wv_new_;
   WordSet  result = (WordSet)(-1); /* bogus */

   wsu->n_add++;
   WCache_LOOKUP_AND_RETURN(WordSet, wsu->cache_addTo, ws, w);
   wsu->n_add_uncached++;

   /* If already present, this is a no-op. */
   for (k = 0; k < wv->size; k++) {
      if (wv->words[k] == w) {
         result = ws;
         goto out;
      }
   }
   /* Ok, not present.  Build a new one ... */
   k = j = 0;
   for (; k < wv->size && wv->words[k] < w; k++) {
      wv_new->words[j++] = wv->words[k];
   }
   wv_new->words[j++] = w;
   for (; k < wv->size; k++) {
      tl_assert(wv->words[k] > w);
      wv_new->words[j++] = wv->words[k];
   }
   tl_assert(j == wv_new->size);

   /* Find any existing copy, or add the new one. */
   result = find_or_alloc_and_add_WordVec(wsu, wv_new);
   tl_assert(result != (WordSet)(-1));

  out:
   WCache_UPDATE(wsu->cache_addTo, ws, w, result);
   return result;
}


WordSet HG_(delFromWS) ( WordSetU* wsu, WordSet ws, UWord w )
{
   UWord    i, j, k;
   WordSet  result = (WordSet)(-1); /* bogus */
   WordVec* wv = do_ix2vec( wsu, ws );
   UWord    wv_new_arr[wv->size-1]; // C99 array. 
   WordVec  wv_new_ = {wsu, wv_new_arr, wv->size-1};  
   WordVec  *wv_new = &wv_new_;

   wsu->n_del++;

   /* special case empty set */
   if (wv->size == 0) {
      tl_assert(ws == wsu->empty);
      return ws;
   }

   WCache_LOOKUP_AND_RETURN(WordSet, wsu->cache_delFrom, ws, w);
   wsu->n_del_uncached++;

   /* If not already present, this is a no-op. */
   for (i = 0; i < wv->size; i++) {
      if (wv->words[i] == w)
         break;
   }
   if (i == wv->size) {
      result = ws;
      goto out;
   }
   /* So w is present in ws, and the new set will be one element
      smaller. */
   tl_assert(i >= 0 && i < wv->size);
   tl_assert(wv->size > 0);

   j = k = 0;
   for (; j < wv->size; j++) {
      if (j == i)
         continue;
      wv_new->words[k++] = wv->words[j];
   }
   tl_assert(k == wv_new->size);

   result = find_or_alloc_and_add_WordVec(wsu, wv_new);
   if (wv->size == 1) {
      tl_assert(result == wsu->empty);
   }

  out:
   WCache_UPDATE(wsu->cache_delFrom, ws, w, result);
   return result;
}

WordSet HG_(unionWS) ( WordSetU* wsu, WordSet ws1, WordSet ws2 )
{
   UWord    i1, i2, k, sz;
   WordVec* wv_new;
   WordVec* wv1 = do_ix2vec( wsu, ws1 );
   WordVec* wv2 = do_ix2vec( wsu, ws2 );
   wsu->n_union++;
   sz = 0;
   i1 = i2 = 0;
   while (1) {
      if (i1 >= wv1->size || i2 >= wv2->size)
         break;
      sz++;
      if (wv1->words[i1] < wv2->words[i2]) {
         i1++;
      } else 
      if (wv1->words[i1] > wv2->words[i2]) {
         i2++;
      } else {
         i1++;
         i2++;
      }
   }
   tl_assert(i1 <= wv1->size);
   tl_assert(i2 <= wv2->size);
   tl_assert(i1 == wv1->size || i2 == wv2->size);
   if (i1 == wv1->size && i2 < wv2->size) {
      sz += (wv2->size - i2);
   }
   if (i2 == wv2->size && i1 < wv1->size) {
      sz += (wv1->size - i1);
   }

   wv_new = new_WV_of_size( wsu, sz );
   k = 0;

   i1 = i2 = 0;
   while (1) {
      if (i1 >= wv1->size || i2 >= wv2->size)
         break;
      if (wv1->words[i1] < wv2->words[i2]) {
         wv_new->words[k++] = wv1->words[i1];
         i1++;
      } else 
      if (wv1->words[i1] > wv2->words[i2]) {
         wv_new->words[k++] = wv2->words[i2];
         i2++;
      } else {
         wv_new->words[k++] = wv1->words[i1];
         i1++;
         i2++;
      }
   }
   tl_assert(i1 <= wv1->size);
   tl_assert(i2 <= wv2->size);
   tl_assert(i1 == wv1->size || i2 == wv2->size);
   if (i1 == wv1->size && i2 < wv2->size) {
      while (i2 < wv2->size)
         wv_new->words[k++] = wv2->words[i2++];
   }
   if (i2 == wv2->size && i1 < wv1->size) {
      while (i1 < wv1->size)
         wv_new->words[k++] = wv1->words[i1++];
   }

   tl_assert(k == sz);

   return add_or_dealloc_WordVec( wsu, wv_new );
}

WordSet HG_(intersectWS) ( WordSetU* wsu, WordSet ws1, WordSet ws2 )
{
   UWord    i1, i2, k, sz;
   WordSet  ws_new = (WordSet)(-1); /* bogus */
   WordVec* wv_new;
   WordVec* wv1; 
   WordVec* wv2; 

   wsu->n_intersect++;

   /* Deal with an obvious case fast. */
   if (ws1 == ws2)
      return ws1;

   /* Since intersect(x,y) == intersect(y,x), convert both variants to
      the same query.  This reduces the number of variants the cache
      has to deal with. */
   if (ws1 > ws2) {
      WordSet wst = ws1; ws1 = ws2; ws2 = wst;
   }

   WCache_LOOKUP_AND_RETURN(WordSet, wsu->cache_intersect, ws1, ws2);
   wsu->n_intersect_uncached++;

   wv1 = do_ix2vec( wsu, ws1 );
   wv2 = do_ix2vec( wsu, ws2 );
   sz = 0;
   i1 = i2 = 0;
   while (1) {
      if (i1 >= wv1->size || i2 >= wv2->size)
         break;
      if (wv1->words[i1] < wv2->words[i2]) {
         i1++;
      } else 
      if (wv1->words[i1] > wv2->words[i2]) {
         i2++;
      } else {
         sz++;
         i1++;
         i2++;
      }
   }
   tl_assert(i1 <= wv1->size);
   tl_assert(i2 <= wv2->size);
   tl_assert(i1 == wv1->size || i2 == wv2->size);

   wv_new = new_WV_of_size( wsu, sz );
   k = 0;

   i1 = i2 = 0;
   while (1) {
      if (i1 >= wv1->size || i2 >= wv2->size)
         break;
      if (wv1->words[i1] < wv2->words[i2]) {
         i1++;
      } else 
      if (wv1->words[i1] > wv2->words[i2]) {
         i2++;
      } else {
         wv_new->words[k++] = wv1->words[i1];
         i1++;
         i2++;
      }
   }
   tl_assert(i1 <= wv1->size);
   tl_assert(i2 <= wv2->size);
   tl_assert(i1 == wv1->size || i2 == wv2->size);

   tl_assert(k == sz);

   ws_new = add_or_dealloc_WordVec( wsu, wv_new );
   if (sz == 0) {
      tl_assert(ws_new == wsu->empty);
   }

   tl_assert(ws_new != (WordSet)(-1));
   WCache_UPDATE(wsu->cache_intersect, ws1, ws2, ws_new);

   return ws_new;
}

WordSet HG_(minusWS) ( WordSetU* wsu, WordSet ws1, WordSet ws2 )
{
   UWord    i1, i2, k, sz;
   WordSet  ws_new = (WordSet)(-1); /* bogus */
   WordVec* wv_new;
   WordVec* wv1;
   WordVec* wv2;
   
   wsu->n_minus++;
   WCache_LOOKUP_AND_RETURN(WordSet, wsu->cache_minus, ws1, ws2);
   wsu->n_minus_uncached++;

   wv1 = do_ix2vec( wsu, ws1 );
   wv2 = do_ix2vec( wsu, ws2 );
   sz = 0;
   i1 = i2 = 0;
   while (1) {
      if (i1 >= wv1->size || i2 >= wv2->size)
         break;
      if (wv1->words[i1] < wv2->words[i2]) {
         sz++;
         i1++;
      } else 
      if (wv1->words[i1] > wv2->words[i2]) {
         i2++;
      } else {
         i1++;
         i2++;
      }
   }
   tl_assert(i1 <= wv1->size);
   tl_assert(i2 <= wv2->size);
   tl_assert(i1 == wv1->size || i2 == wv2->size);
   if (i2 == wv2->size && i1 < wv1->size) {
      sz += (wv1->size - i1);
   }

   wv_new = new_WV_of_size( wsu, sz );
   k = 0;

   i1 = i2 = 0;
   while (1) {
      if (i1 >= wv1->size || i2 >= wv2->size)
         break;
      if (wv1->words[i1] < wv2->words[i2]) {
         wv_new->words[k++] = wv1->words[i1];
         i1++;
      } else 
      if (wv1->words[i1] > wv2->words[i2]) {
         i2++;
      } else {
         i1++;
         i2++;
      }
   }
   tl_assert(i1 <= wv1->size);
   tl_assert(i2 <= wv2->size);
   tl_assert(i1 == wv1->size || i2 == wv2->size);
   if (i2 == wv2->size && i1 < wv1->size) {
      while (i1 < wv1->size)
         wv_new->words[k++] = wv1->words[i1++];
   }

   tl_assert(k == sz);

   ws_new = add_or_dealloc_WordVec( wsu, wv_new );
   if (sz == 0) {
      tl_assert(ws_new == wsu->empty);
   }

   tl_assert(ws_new != (WordSet)(-1));
   WCache_UPDATE(wsu->cache_minus, ws1, ws2, ws_new);

   return ws_new;
}

static __attribute__((unused))
void show_WS ( WordSetU* wsu, WordSet ws )
{
   UWord i;
   WordVec* wv = do_ix2vec( wsu, ws );
   VG_(printf)("#%u{", ws);
   for (i = 0; i < wv->size; i++) {
      VG_(printf)("%lu", wv->words[i]);
      if (i < wv->size-1)
         VG_(printf)(",");
   }
   VG_(printf)("}\n");
}

//------------------------------------------------------------------//
//---                        end WordSet                         ---//
//---                       Implementation                       ---//
//------------------------------------------------------------------//

/*--------------------------------------------------------------------*/
/*--- end                                             hg_wordset.c ---*/
/*--------------------------------------------------------------------*/
// vim:shiftwidth=3:softtabstop=3:expandtab
