/*
   This file is part of Callgrind, a Valgrind tool for call graph
   profiling programs.

   Copyright (C) 2002-2007, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)

   This tool is derived from and contains lot of code from Cachegrind
   Copyright (C) 2002 Nicholas Nethercote (njn@valgrind.org)

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

/*
 * Functions related to interactive commands via "callgrind.cmd"
 */

#include "config.h"
#include "global.h"

#include <pub_tool_threadstate.h> // VG_N_THREADS

// Version for the syntax in command/result files for interactive control
#define COMMAND_VERSION "1.0"

static Char outbuf[FILENAME_LEN + FN_NAME_LEN + OBJ_NAME_LEN];

static Char* command_file = 0;
static Char* command_file2 = 0;
static Char* current_command_file = 0;
static Char* result_file = 0;
static Char* result_file2 = 0;
static Char* current_result_file = 0;
static Char* info_file = 0;
static Char* dump_base = 0;

static Int thisPID = 0;

/**
 * Setup for interactive control of a callgrind run
 */
static void setup_control(void)
{
  Int fd, size;
  SysRes res;
  Char* dir, *dump_filename;

  CLG_ASSERT(thisPID != 0);

  fd = -1;
  dir = CLG_(get_base_directory)();
  dump_base = CLG_(get_dump_file_base)();

  /* base name of dump files with PID ending */
  size = VG_(strlen)(dump_base) + 10;
  dump_filename = (char*) CLG_MALLOC(size);
  CLG_ASSERT(dump_filename != 0);
  VG_(sprintf)(dump_filename, "%s.%d", dump_base, thisPID);

  /* name of command file */
  size = VG_(strlen)(dir) + VG_(strlen)(DEFAULT_COMMANDNAME) +10;
  command_file = (char*) CLG_MALLOC(size);
  CLG_ASSERT(command_file != 0);
  VG_(sprintf)(command_file, "%s/%s.%d",
	       dir, DEFAULT_COMMANDNAME, thisPID);

  /* This is for compatibility with the "Force Now" Button of current
   * KCachegrind releases, as it doesn't use ".pid" to distinguish
   * different callgrind instances from same base directory.
   */
  command_file2 = (char*) CLG_MALLOC(size);
  CLG_ASSERT(command_file2 != 0);
  VG_(sprintf)(command_file2, "%s/%s",
	       dir, DEFAULT_COMMANDNAME);

  size = VG_(strlen)(dir) + VG_(strlen)(DEFAULT_RESULTNAME) +10;
  result_file = (char*) CLG_MALLOC(size);
  CLG_ASSERT(result_file != 0);
  VG_(sprintf)(result_file, "%s/%s.%d",
	       dir, DEFAULT_RESULTNAME, thisPID);

  /* If we get a command from a command file without .pid, use
   * a result file without .pid suffix
   */
  result_file2 = (char*) CLG_MALLOC(size);
  CLG_ASSERT(result_file2 != 0);
  VG_(sprintf)(result_file2, "%s/%s",
               dir, DEFAULT_RESULTNAME);

  info_file = (char*) CLG_MALLOC(VG_(strlen)(DEFAULT_INFONAME) + 10);
  CLG_ASSERT(info_file != 0);
  VG_(sprintf)(info_file, "%s.%d", DEFAULT_INFONAME, thisPID);

  CLG_DEBUG(1, "Setup for interactive control (PID: %d):\n", thisPID);
  CLG_DEBUG(1, "  dump file base: '%s'\n", dump_filename);
  CLG_DEBUG(1, "  command file:   '%s'\n", command_file);
  CLG_DEBUG(1, "  result file:    '%s'\n", result_file);
  CLG_DEBUG(1, "  info file:      '%s'\n", info_file);

  /* create info file to indicate that we are running */ 
  res = VG_(open)(info_file, VKI_O_WRONLY|VKI_O_TRUNC, 0);
  if (res.isError) { 
    res = VG_(open)(info_file, VKI_O_CREAT|VKI_O_WRONLY,
		   VKI_S_IRUSR|VKI_S_IWUSR);
    if (res.isError) {
      VG_(message)(Vg_DebugMsg, 
		   "warning: can't write info file '%s'", info_file);
      info_file = 0;
      fd = -1;
    }
  }
  if (!res.isError)
      fd = (Int) res.res;
  if (fd>=0) {
    Char buf[512];
    Int i;

    WRITE_STR3(fd,
	       "# This file is generated by Callgrind-" VERSION ".\n"
	       "# It is used to enable controlling the supervision of\n"
	       "#  '", VG_(args_the_exename), "'\n"
	       "# by external tools.\n\n");
    
    VG_(sprintf)(buf, "version: " COMMAND_VERSION "\n");
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
    
    WRITE_STR3(fd, "base: ", dir, "\n");
    WRITE_STR3(fd, "dumps: ", dump_filename, "\n");
    WRITE_STR3(fd, "control: ", command_file, "\n");
    WRITE_STR3(fd, "result: ", result_file, "\n");

    WRITE_STR2(fd, "cmd: ", VG_(args_the_exename));    
    for (i = 0; i < VG_(sizeXA)( VG_(args_for_client) ); i++) {
        HChar* arg = * (HChar**)VG_(indexXA)( VG_(args_for_client), i );
	if (!arg) continue;
	WRITE_STR2(fd, " ", arg);
    }
    VG_(write)(fd, "\n", 1);
    VG_(close)(fd);
  }
}

void CLG_(init_command)()
{
  thisPID = VG_(getpid)();
  setup_control();
}

void CLG_(finish_command)()
{
  /* unlink info file */
  if (info_file) VG_(unlink)(info_file);
}


static Int createRes(Int fd)
{
    SysRes res;

    if (fd > -2) return fd;

    /* fd == -2: No error, but we need to create the file */
    CLG_ASSERT(current_result_file != 0);
    res = VG_(open)(current_result_file,
		   VKI_O_CREAT|VKI_O_WRONLY|VKI_O_TRUNC,
		   VKI_S_IRUSR|VKI_S_IWUSR);

    /* VG_(open) can return any negative number on error. Remap errors to -1,
     * to not confuse it with our special value -2
     */
    if (res.isError) fd = -1;
    else fd = (Int) res.res;

    return fd;
}

/* Run Info: Persistant information of the callgrind run */
static Int dump_info(Int fd)
{
    Char* buf = outbuf;
    int i;
    
    if ( (fd = createRes(fd)) <0) return fd;

    /* creator */
    VG_(sprintf)(buf, "creator: callgrind-" VERSION "\n");
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    /* version */
    VG_(sprintf)(buf, "version: " COMMAND_VERSION "\n");
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
    
    /* "pid:" line */
    VG_(sprintf)(buf, "pid: %d\n", VG_(getpid)());
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
    
    /* "base:" line */
    WRITE_STR3(fd, "base: ", dump_base, "\n");
    
    /* "cmd:" line */
    WRITE_STR2(fd, "cmd: ", VG_(args_the_exename));
    for (i = 0; i < VG_(sizeXA)( VG_(args_for_client) ); i++) {
        HChar* arg = * (HChar**)VG_(indexXA)( VG_(args_for_client), i );
	if (!arg) continue;
	WRITE_STR2(fd, " ", arg);
    }
    VG_(write)(fd, "\n", 1);

    return fd;
}


/* Helper for dump_state */

Int dump_fd;

void static dump_state_of_thread(thread_info* ti)
{
    Char* buf = outbuf;
    int t = CLG_(current_tid);
    Int p, i;
    static FullCost sum = 0, tmp = 0;
    BBCC *from, *to;
    call_entry* ce;

    p = VG_(sprintf)(buf, "events-%d: ", t);
    CLG_(init_cost_lz)( CLG_(sets).full, &sum );
    CLG_(copy_cost_lz)( CLG_(sets).full, &tmp, ti->lastdump_cost );
    CLG_(add_diff_cost)( CLG_(sets).full, sum,
			ti->lastdump_cost,
			ti->states.entry[0]->cost);
    CLG_(copy_cost)( CLG_(sets).full, ti->lastdump_cost, tmp );
    p += CLG_(sprint_mappingcost)(buf + p, CLG_(dumpmap), sum);
    p += VG_(sprintf)(buf+p, "\n");
    VG_(write)(dump_fd, (void*)buf, p);

    p = VG_(sprintf)(buf, "frames-%d: %d\n", t,
		     CLG_(current_call_stack).sp);
    VG_(write)(dump_fd, (void*)buf, p);
    ce = 0;
    for(i = 0; i < CLG_(current_call_stack).sp; i++) {
      ce = CLG_(get_call_entry)(i);
      /* if this frame is skipped, we don't have counters */
      if (!ce->jcc) continue;
      
      from = ce->jcc->from;
      p = VG_(sprintf)(buf, "function-%d-%d: %s\n",t, i, 
		       from->cxt->fn[0]->name);	    
      VG_(write)(dump_fd, (void*)buf, p);
      
      p = VG_(sprintf)(buf, "calls-%d-%d: ",t, i);
      p+= VG_(sprintf)(buf+p, "%llu\n", ce->jcc->call_counter);
      VG_(write)(dump_fd, (void*)buf, p);
      
      /* FIXME: EventSets! */
      CLG_(copy_cost)( CLG_(sets).full, sum, ce->jcc->cost );
      CLG_(copy_cost)( CLG_(sets).full, tmp, ce->enter_cost );
      CLG_(add_diff_cost)( CLG_(sets).full, sum,
			  ce->enter_cost, CLG_(current_state).cost );
      CLG_(copy_cost)( CLG_(sets).full, ce->enter_cost, tmp );
      
      p = VG_(sprintf)(buf, "events-%d-%d: ",t, i);
      p += CLG_(sprint_mappingcost)(buf + p, CLG_(dumpmap), sum );
      p += VG_(sprintf)(buf+p, "\n");
      VG_(write)(dump_fd, (void*)buf, p);
    }
    if (ce && ce->jcc) {
      to = ce->jcc->to;
      p = VG_(sprintf)(buf, "function-%d-%d: %s\n",t, i, 
		       to->cxt->fn[0]->name );	    
      VG_(write)(dump_fd, (void*)buf, p);
    }
}

/* Dump info on current callgrind state */
static Int dump_state(Int fd)
{
    Char* buf = outbuf;
    thread_info** th;
    int t, p;
    Int orig_tid = CLG_(current_tid);

    if ( (fd = createRes(fd)) <0) return fd;

    VG_(sprintf)(buf, "instrumentation: %s\n",
		 CLG_(instrument_state) ? "on":"off");
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    if (!CLG_(instrument_state)) return fd;

    VG_(sprintf)(buf, "executed-bbs: %llu\n", CLG_(stat).bb_executions);
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    VG_(sprintf)(buf, "executed-calls: %llu\n", CLG_(stat).call_counter);
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    VG_(sprintf)(buf, "distinct-bbs: %d\n", CLG_(stat).distinct_bbs);
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    VG_(sprintf)(buf, "distinct-calls: %d\n", CLG_(stat).distinct_jccs);
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    VG_(sprintf)(buf, "distinct-functions: %d\n", CLG_(stat).distinct_fns);
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    VG_(sprintf)(buf, "distinct-contexts: %d\n", CLG_(stat).distinct_contexts);
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    /* "events:" line. Given here because it will be dynamic in the future */
    p = VG_(sprintf)(buf, "events: ");
    CLG_(sprint_eventmapping)(buf+p, CLG_(dumpmap));
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
    VG_(write)(fd, "\n", 1);
		
    /* "part:" line (number of last part. Is 0 at start */
    VG_(sprintf)(buf, "\npart: %d\n", CLG_(get_dump_counter)());
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
		
    /* threads */
    th = CLG_(get_threads)();
    p = VG_(sprintf)(buf, "threads:");
    for(t=1;t<VG_N_THREADS;t++) {
	if (!th[t]) continue;
	p += VG_(sprintf)(buf+p, " %d", t);
    }
    p += VG_(sprintf)(buf+p, "\n");
    VG_(write)(fd, (void*)buf, p);

    VG_(sprintf)(buf, "current-tid: %d\n", orig_tid);
    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

    /* current event counters */
    dump_fd = fd;
    CLG_(forall_threads)(dump_state_of_thread);

    return fd;
}

void CLG_(check_command)()
{
    /* check for dumps needed */
    static Char buf[512];
    static Char cmdBuffer[512];
    Char *cmdPos = 0, *cmdNextLine = 0;
    Int fd, bytesRead = 0, do_kill = 0;
    SysRes res;
    Int currentPID;
    static Int check_counter = 0;

    /* Check for PID change, i.e. whether we run as child after a fork.
     * If yes, we setup interactive control for the new process
     */
    currentPID = VG_(getpid)();
    if (thisPID != currentPID) {
	thisPID = currentPID;
	setup_control();
    }

    /* Toggle between 2 command files, with/without ".pid" postfix
     * (needed for compatibility with KCachegrind, which wants to trigger
     *  a dump by writing into a command file without the ".pid" postfix)
     */
    check_counter++;
    if (check_counter % 2) {
	current_command_file = command_file;
	current_result_file  = result_file;
    }
    else {
	current_command_file = command_file2;
	current_result_file  = result_file2;
    }
    
    res = VG_(open)(current_command_file, VKI_O_RDONLY,0);
    if (!res.isError) {
	fd = (Int) res.res;
	bytesRead = VG_(read)(fd,cmdBuffer,500);
	cmdBuffer[500] = 0; /* no command overrun please */
	VG_(close)(fd);
	/* don't delete command file on read error (e.g. EAGAIN) */
	if (bytesRead>0) {
	    cmdPos = cmdBuffer;
	}
    }

    /* force creation of result file if needed */
    fd = -2;

    while((bytesRead>0) && *cmdPos) {
      
	/* Calculate pointer for next line */
	cmdNextLine = cmdPos+1;
	while((bytesRead>0) && *cmdNextLine && (*cmdNextLine != '\n')) {
	  cmdNextLine++;
	  bytesRead--;
	}
	if ((bytesRead>0) && (*cmdNextLine == '\n')) {
	  *cmdNextLine = 0;
	  cmdNextLine++;
	  bytesRead--;
	} 

	/* Command with integer option */
	if ((*cmdPos >= '0') && (*cmdPos <='9')) {
	  int value = *cmdPos-'0';
	  cmdPos++;
	  while((*cmdPos >= '0') && (*cmdPos <='9')) {
	    value = 10*value + (*cmdPos-'0');
	    cmdPos++;
	  }
	  while((*cmdPos == ' ') || (*cmdPos == '\t')) cmdPos++;
	  
	  switch(*cmdPos) {
#if CLG_ENABLE_DEBUG
	    /* verbosity */
	  case 'V':
	  case 'v':
	    CLG_(clo).verbose = value;
	    break;
#endif
	  default:
	    break;	      
	  }

	  cmdPos = cmdNextLine;
	  continue;
	}  

	/* Command with boolean/switch option */
	if ((*cmdPos=='+') || 
	    (*cmdPos=='-')) {
	  int value = (cmdPos[0] == '+');
	  cmdPos++;
	  while((*cmdPos == ' ') || (*cmdPos == '\t')) cmdPos++;
	  
	  switch(*cmdPos) {
	  case 'I':
	  case 'i':
	    CLG_(set_instrument_state)("Command", value);
	    break;

	  default:
	    break;
	  }

	  cmdPos = cmdNextLine;
	  continue;
	}

	/* regular command */
	switch(*cmdPos) {
	case 'D':
	case 'd':
	  /* DUMP */

	  /* skip command */
	  while(*cmdPos && (*cmdPos != ' ')) cmdPos++;
	  if (*cmdPos)
	    VG_(sprintf)(buf, "Dump Command:%s", cmdPos);
	  else
	    VG_(sprintf)(buf, "Dump Command");
	  CLG_(dump_profile)(buf, False);
	  break;
	    
	case 'Z':
	case 'z':
	    CLG_(zero_all_cost)(False);
	    break;

	case 'K':
	case 'k':
	    /* Kill: Delay to be able to remove command file before. */
	    do_kill = 1;
	    break;

	case 'I':
	case 'i':
	    fd = dump_info(fd);
	    break;

	case 's':
	case 'S':
	    fd = dump_state(fd);
	    break;

	case 'O':
	case 'o':
	    /* Options Info */
	    if ( (fd = createRes(fd)) <0) break;

	    VG_(sprintf)(buf, "\ndesc: Option: --skip-plt=%s\n",
			 CLG_(clo).skip_plt ? "yes" : "no");
	    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));	    
	    VG_(sprintf)(buf, "desc: Option: --collect-jumps=%s\n",
			 CLG_(clo).collect_jumps ? "yes" : "no");
	    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
	    VG_(sprintf)(buf, "desc: Option: --separate-recs=%d\n",
			 CLG_(clo).separate_recursions);
	    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
	    VG_(sprintf)(buf, "desc: Option: --separate-callers=%d\n",
			 CLG_(clo).separate_callers);
	    VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

	    break;

	default:
	  break;
	}

	cmdPos = cmdNextLine;
    }

    /* If command executed, delete command file */
    if (cmdPos) VG_(unlink)(current_command_file);
    if (fd>=0) VG_(close)(fd);	    

    if (do_kill) {
      VG_(message)(Vg_UserMsg,
		   "Killed because of command from %s", current_command_file);
      CLG_(fini)(0);
      VG_(exit)(1);
    }
}
