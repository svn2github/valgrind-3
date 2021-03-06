/*
  This file is part of drd, a data race detector.

  Copyright (C) 2006-2008 Bart Van Assche
  bart.vanassche@gmail.com

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


// Reader-writer lock state information.


#ifndef __DRD_RWLOCK_H
#define __DRD_RWLOCK_H


#include "drd_clientobj.h"        // struct rwlock_info
#include "drd_thread.h"           // DrdThreadId
#include "drd_vc.h"
#include "pub_tool_basics.h"      // Addr


struct rwlock_info;


void rwlock_set_trace(const Bool trace_rwlock);
struct rwlock_info* rwlock_pre_init(const Addr rwlock);
void rwlock_post_destroy(const Addr rwlock);
void rwlock_pre_rdlock(const Addr rwlock);
void rwlock_post_rdlock(const Addr rwlock, const Bool took_lock);
void rwlock_pre_wrlock(const Addr rwlock);
void rwlock_post_wrlock(const Addr rwlock, const Bool took_lock);
void rwlock_pre_unlock(const Addr rwlock);
void rwlock_thread_delete(const DrdThreadId tid);


#endif /* __DRD_RWLOCK_H */
