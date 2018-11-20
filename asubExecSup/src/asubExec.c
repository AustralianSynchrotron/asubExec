/* $File: //ASP/tec/epics/asubExec/trunk/asubExecSup/src/asubExec.c $
 * $Revision: #5 $
 * $DateTime: 2018/11/18 18:15:34 $
 * Last checked in by: $Author: starritt $
 *
 * Description
 * The asubExec module is written to be used in conjunction with the aSub record.
 * It uses the fork() and execvp() paradigm to launch a child process.
 * The child process should accept input on its standard input and return its
 * result by writing to its standard output.
 *
 * Note:
 * This module as been developed on and for Linux, specifically CentOS 7 64 bit.
 *
 * The process file to be executed is defined the record's EXEC info field.
 * The file must be either an absolute pathname or the file must be accessable
 * via the PATH environment variable.
 *
 * An optional timeout may be specified. This value defines the maximum allowed
 * time, in seconds, that the child process is allowed to run. The default time
 * allowed is 3.2E+9 seconds (~100 years), i.e. essentially for ever.
 *
 * Addtional process arguments (upto 9) may also be specified using the ARG1 ... ARG9
 * info fields. Note the first process argument is automatically set to the record
 * name if not otherwise specified.
 *
 * Example:
 *
 * record (aSub, "RECORD_NAME") {
 *
 *   info (EXEC, "exectuable_file")
 *   info (TIMEOUT, "10.0")
 *   # if ARG1 not specified, ARG1 set to record name
 *   info (ARG2, "additional parameter")
 *   info (ARG3, "additional parameter")
 *   ...
 *
 *   field (INAM, "asubExecInit")
 *   field (SNAM, "asubExecProcess")
 *   ...
 * }
 *
 * The specified EXEC file may be any executable, e.g. a complied program, 
 * a bash script etc., that can decode the input data and generate output data
 * in the expected format.
 * Note:
 * This module contains a python helper class, asubExecIO in asubExec.py, to
 * support python scripts specified as the executed file.
 *
 * The input is encoded from the current values extracted from A .. U fields,
 * and the result is decoded and applied to the output fields VALA .. VALU.
 *
 * The input data is encoded as a direct binary copy of:
 *
 *   FTA, NOA, *A, FTB, NOB, *B ..., FTU, NOU, *U, FTVA, NOVA, ... FTVU, NOVU
 *
 * The FTx fields occupy 2 bytes and are encoded as per menuFtype.h
 * The NOx fields occupy 4 bytes and are encoded as an epicsUInt32
 * The *x fields are a direct binary copy of the input data.
 * The FTVx fields occupy 2 bytes and are encoded as per menuFtype.h
 * The NOVx fields occupy 4 bytes and are encoded as an epicsUInt32
 *
 * The input data also includes the FTVx/NOVx information to allow the child
 * process to generate its response in the correct format.
 *
 * The child process output should be encoded a binary format similar to the
 * input data and should contain:
 *
 *  FTVA, NOVA, *A, FTVB, NOVB, *B ..., FTVU, NOVU, *U
 *
 * The FTVx fields occupy 2 bytes and are encoded as per menuFtype.h
 * The NOVx fields occupy 4 bytes and are encoded as an epicsUInt32
 * The *x fields are a direct binary copy of the input.
 *
 * The asubExec module verifies that the output received from the child process
 * is as expected. Currently type mis matches (FTVx) are not handled and are
 * essentially ignored - later releases of this module may include type casting.
 *
 * Number of elements mis-matches (NOVx), are handled by discarding additonal
 * elements or leaving elements undefined if not enough were provided.
 *
 * Source code formatting:  indent -kr -pcs -i3 -cli3 -nut -l96
 *
 * Copyright (c) 2018  Australian Synchrotron
 *
 * The asubExec library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * You can also redistribute the asubExec library and/or modify it under the
 * terms of the Lesser GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version when this library is disributed with and as part of the
 * EPICS QT Framework (https://github.com/qtepics).
 *
 * The asubExec library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License and
 * the Lesser GNU General Public License along with the asubExec library.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact details:
 * andrew.starritt@synchrotron.org.au
 * 800 Blackburn Road, Clayton, Victoria 3168, Australia.
 *
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <aSubRecord.h>
#include <alarm.h>
#include <cantProceed.h>
#include <dbAccess.h>
#include <dbBase.h>
#include <dbDefs.h>
#include <dbStaticLib.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsExit.h>
#include <epicsExport.h>
#include <epicsString.h>
#include <epicsStdlib.h>
#include <epicsTypes.h>
#include <errlog.h>
#include <menuFtype.h>
#include <recGbl.h>
#include <recSup.h>
#include <registryFunction.h>


/* A to U - both input and output
 */
#define NUMBER_IO_FIELDS    21

/* Args are 1 to 9, allow 2 extra for execed file and sentinal
*/
#define NUMBER_OF_ARGS      9
#define ARG_LENGTH          (NUMBER_OF_ARGS + 2)

/* Normal exit codes are in range 0 to 127
 * These are special case error pseudo exit codes
 */
#define BASE_EXIT_CODE      128
#define SETUP_EXIT_CODE     (BASE_EXIT_CODE + 0)
#define NO_EXEC_EXIT_CODE   (BASE_EXIT_CODE + 1)
#define TIMEOUT_EXIT_CODE   (BASE_EXIT_CODE + 2)
#define WAITPID_EXIT_CODE   (BASE_EXIT_CODE + 3)

enum PipeIndex {
   PIPE_READ = 0,
   PIPE_WRITE,
   PIPE_SIZE                    /* must be last */
};

typedef int HalfDuplexPipe[PIPE_SIZE];

/* Private info alocated to each aSub record instance using this module.
 */
typedef struct ExecInfo {
   aSubRecord *prec;              /* record reference */
   epicsThreadId thread_id;       /* monitor thread id */
   epicsEventId event;            /* monitor thread signal event */
   const char *argv[ARG_LENGTH];  /* arguments 0, 1 .. 9, 10 is NULL */
   double timeOut;                /* max time in seconds that child process allowed to run */
   pid_t pid;                     /* child process' pid */
   int fdin;                      /* file desciptor for child process stdin */
   int fdout;                     /* file desciptor for child process stdout */
   int exitCode;                  /* child process' exit code */
} ExecInfo;


static int asubExecDebug = 0;
static bool iocRunnig =true;


/*------------------------------------------------------------------------------
 * Initiate smooth thread shutdown
 */
void shutdown (void* userPvt)
{
   iocRunnig = false;
   ExecInfo * pExecInfo = (ExecInfo *)userPvt;
   if (pExecInfo) {
      epicsEventSignal (pExecInfo->event);      /* wake up thread */
   }
}


/*------------------------------------------------------------------------------
 * Perform and immediate process exit.
 */
static void child_exit (const int status)
{
   /* Don't run our parent's atexit() handlers
    */
   _exit (status);
}

/*------------------------------------------------------------------------------
 */
static void monitorProcess (ExecInfo * pExecInfo)
{
   if (!pExecInfo) {   /* sainity check */
      errlogPrintf ("monitorThread: pExecInfo is null\n");
      return;
   }

   const double dt = pExecInfo->timeOut >= 60.0 ? 1.0 : 0.1;
   aSubRecord* prec = pExecInfo->prec;

   /* Calculated end time
    */
   epicsTimeStamp endTime;
   epicsTimeGetCurrent (&endTime);
   epicsTimeAddSeconds (&endTime, pExecInfo->timeOut);

   /* Monitor the child process
    */
   while (iocRunnig) {
      epicsThreadSleep (dt);
      if (!iocRunnig) break;

      /* Wait for process to change state.
       */
      int status;
      pid_t pid = waitpid (pExecInfo->pid, &status, WNOHANG);

      if (pid == pExecInfo->pid) {
         /* Child process is complete - simple
          */
         if (asubExecDebug > 2)
            printf ("%s: child process complete\n", prec->name);
         pExecInfo->exitCode = WEXITSTATUS (status);
         break;
      }

      if (pid != 0) {
         /* an unexpected return value occured, either an error or another pid.
          */
         perror ("waitpid");
         errlogPrintf ("%s monitorThread:  waitpid (%d) => %d, status = %d\n",
                       prec->name, pExecInfo->pid, pid, status);

         pExecInfo->exitCode = WAITPID_EXIT_CODE;
         break;
      }

      /* pid == 0 - still waiting for child process to complete.
       */
      if (asubExecDebug > 4)
         printf ("%s: child process still running\n", prec->name);

      /* Has the allowed time expired ?
       */
      epicsTimeStamp timeNow;
      epicsTimeGetCurrent (&timeNow);
      if (epicsTimeLessThan (&timeNow, &endTime)) continue;

      /* Timeout - shutdown child process.
       */
      if (asubExecDebug > 2)
         printf ("%s: child process timeout\n", prec->name);

      /* First ask nicely, then allow 2 seconds fopr orderly shutdown.
       */
      if (asubExecDebug > 2)
         printf ("%s: sending SIGTERM to pid %d\n", prec->name, pExecInfo->pid);
      status = kill (pExecInfo->pid, SIGTERM);

      bool terminated = false;
      double allow = 2.0;
      while (iocRunnig && (allow >= 0.0)) {
         epicsThreadSleep (0.1);
         if (!iocRunnig) break;
         allow -= 0.1;

         pid_t pid = waitpid (pExecInfo->pid, &status, WNOHANG);
         if (pid == pExecInfo->pid) {
            /* Child process is now complete.
             */
            if (asubExecDebug > 2)
               printf ("%s: process (pid=%d) terminated\n", prec->name, pExecInfo->pid);
            terminated = true;
            break;
         }
      }

      if (!terminated) {
         /* No more Mr. Nice Guy ...
          */
         if (asubExecDebug > 2)
            printf ("%s: sending SIGKILL to pid %d\n", prec->name, pExecInfo->pid);

         kill (pExecInfo->pid, SIGKILL);
         waitpid (pExecInfo->pid, &status, 0);

         if (asubExecDebug > 2)
            printf ("%s: process (pid=%d) killed\n", prec->name, pExecInfo->pid);
      }

      pExecInfo->exitCode = TIMEOUT_EXIT_CODE;
      break;
   }
}


/*------------------------------------------------------------------------------
 * This thread the function essentially waits for the child process to terminate
 * and then calls the records process function to deal with the response.
 */
static void monitorThread (ExecInfo * pExecInfo)
{
   if (!pExecInfo) {   /* sainity check */
      errlogPrintf ("monitorThread: pExecInfo is null\n");
      return;
   }

   aSubRecord* prec = pExecInfo->prec;
#ifdef USE_TYPED_RSET
   struct typed_rset *rset = prec->rset;
#else
   struct rset *rset = prec->rset;
#endif

   if (asubExecDebug > 2)
      printf ("%s: monitorThread starting...\n", prec->name);

   while (iocRunnig) {

      if (asubExecDebug > 2)
         printf ("%s: monitorThread sleeping  ...\n", prec->name);

      epicsEventWait (pExecInfo->event);
      if (!iocRunnig) break;

      if (asubExecDebug > 2)
         printf ("%s: monitorThread awake ...\n", prec->name);

      monitorProcess (pExecInfo);

      /* One way or another, the child process is (deemed) complete
       * Initiate processing part 2
       */
      rset->process ((dbCommon *) prec);
   }

   if (asubExecDebug > 2)
      printf ("%s: monitorThread terminated\n", prec->name);
}


/*------------------------------------------------------------------------------
 * Record functions
 *------------------------------------------------------------------------------
 */
static long asubExecInit (aSubRecord * prec)
{
   int j;
   long status;
   ExecInfo *pExecInfo;

   /* Allocate memory for this record's private data.
    */
   pExecInfo = (ExecInfo *) callocMustSucceed (1, sizeof (ExecInfo), "asubExecInit");
   prec->dpvt = pExecInfo;

   pExecInfo->prec = prec;
   pExecInfo->event = epicsEventCreate (epicsEventEmpty);

   for (j = 0; j < ARG_LENGTH; j++) {
      pExecInfo->argv[j] = NULL;
   }

   pExecInfo->timeOut = 3.2E+9;   /* ~100 years - essentially for ever */
   pExecInfo->pid = -1;
   pExecInfo->fdin = -1;
   pExecInfo->fdout = -1;

   DBENTRY entry;
   dbInitEntry (pdbbase, &entry);
   status = dbFindRecord (&entry, prec->name);
   if (status != 0) {
      errlogPrintf ("asubExecInit: can't find own record %s\n", prec->name);
      return -1;
   }

   status = dbFindInfo (&entry, "EXEC");
   if ((status != 0) || !entry.pinfonode) {
      errlogPrintf ("asubExecInit: can't find info %s entry\n", "EXEC");
      return -1;
   }

   dbInfoNode *n = entry.pinfonode;
   pExecInfo->argv[0] = epicsStrDup (n->string);

   status = dbFindInfo (&entry, "ARG1");
   if ((status == 0) && entry.pinfonode) {
      pExecInfo->argv[1] = epicsStrDup (entry.pinfonode->string);
   } else {
      /* Default value for arg1 */
      pExecInfo->argv[1] = epicsStrDup (prec->name);
   }

   /* Now do args 2 through 9
    */
   for (j = 2; j <= NUMBER_OF_ARGS; j++) {
      char infoName [8];
      snprintf (infoName, sizeof (infoName), "ARG%d", j);

      status = dbFindInfo (&entry, infoName);
      if ((status == 0) && entry.pinfonode) {
         pExecInfo->argv[j] = epicsStrDup (entry.pinfonode->string);
      }
   }

   /* Extract timeout if it has been specified
    */
   status = dbFindInfo (&entry, "TIMEOUT");
   if ((status == 0) && entry.pinfonode) {
      char *endptr;
      pExecInfo->timeOut = epicsStrtod (entry.pinfonode->string, &endptr);
   }

   /* Use record name as task name
    */
   pExecInfo->thread_id = epicsThreadCreate     /*  */
       (prec->name, epicsThreadPriorityMin,
        epicsThreadGetStackSize (epicsThreadStackMedium),
        (EPICSTHREADFUNC) monitorThread, pExecInfo);

   epicsAtExit (shutdown, pExecInfo);

   if (asubExecDebug > 2)
      printf ("+++ asubExecInit %s: %s=%s\n", prec->name, n->name, n->string);

   return 0;
}


/*------------------------------------------------------------------------------
 */
static long asubExecProcessPart1 (aSubRecord * prec)
{
   ExecInfo *pExecInfo;
   pid_t pid;
   int status;
   HalfDuplexPipe input_data;
   HalfDuplexPipe output_data;
   int return_pipe_size;

   pExecInfo = (ExecInfo *) prec->dpvt;

   /* Ensure not erroneous
    */
   pExecInfo->pid = -1;
   pExecInfo->fdin = -1;
   pExecInfo->fdout = -1;
   pExecInfo->exitCode = -1;

   /* Create pippes to comunicate with child process
    */
   status = pipe (input_data);
   if (status != 0) {
      perror ("pipe (input_data)");
      return -1;
   }

   status = pipe (output_data);
   if (status != 0) {
      perror ("pipe (output_data)");
      return -1;
   }

   /* Create child process
    */
   pid = fork ();
   if (pid < 0) {
      perror ("pipe (error_data)");
      return (-1);
   }

   /* Are we the child or the parent process?
    */
   if (pid == 0) {
      /* We are the child.
       */
      sigset_t emptyMask;
      int status;
      int fd;
      int maxfd;
      char message[80];

      /* Reset which signals are blocked. The child process inherites these
       * from the EPICS IOC. We need a "clean" slate, in particular, so that
       * the child process will respond to a SIGTERM signal.
       */
      sigemptyset (&emptyMask);
      status = sigprocmask (SIG_SETMASK, &emptyMask, NULL);
      if (status != 0) {
         perror ("sigprocmask ()");
         child_exit (SETUP_EXIT_CODE);
      }

      /* Connect standard IO to the pipes.
       * Dupilcate file descriptors to standard in/out
       */
      fd = dup2 (input_data[PIPE_READ], STDIN_FILENO);
      if (fd != STDIN_FILENO) {
         perror ("dup2 ()");
         child_exit (SETUP_EXIT_CODE);
      }

      fd = dup2 (output_data[PIPE_WRITE], STDOUT_FILENO);
      if (fd != STDOUT_FILENO) {
         perror ("dup2 ()");
         child_exit (SETUP_EXIT_CODE);
      }

      /* from posix/osdProcess.c
       * close all open files except for STDIO so they will not be inherited
       * by the spawned process. This include unused pipe descriptors.
       *
       * We "know" standard file descriptors are 0, 1 and 2
       */
      maxfd = sysconf (_SC_OPEN_MAX);
      for (fd = 3; fd <= maxfd; fd++) {
         close (fd);
      }

      /* Now exec to new process.
       */
      pExecInfo->argv [ARG_LENGTH - 1] = NULL;  /* belts 'n' braces */

      /* caste to get rid of that pesky warning.
       */
      status = execvp (pExecInfo->argv[0], (char *const *) pExecInfo->argv);

      /* The exec call failed - it returned.
       */
      snprintf (message, sizeof (message), "execvp (%s, ...) -> %d", pExecInfo->argv[0], status);
      perror (message);
      child_exit (NO_EXEC_EXIT_CODE);    /** does not return - most important **/
   }

   /* We are the parent. Save the child process id.
    */
   pExecInfo->pid = pid;

   if (asubExecDebug > 0) {
      printf ("%s: %s (pid=%d) starting\n", prec->name, pExecInfo->argv[0], pExecInfo->pid);
   }

   /* Save file pipe file descriptors and close unused pipe ends.
    */
   pExecInfo->fdin = input_data[PIPE_WRITE];
   status = close (input_data[PIPE_READ]);
   if (status != 0) {
      perror ("close  (input_data [in])");
   }

   pExecInfo->fdout = output_data[PIPE_READ];
   status = close (output_data[PIPE_WRITE]);
   if (status != 0) {
      perror ("close  (output_data [out])");
   }

   /* Set pipe to maximum allowed size
    */
   return_pipe_size = 1048576;
   status = fcntl (pExecInfo->fdout, F_SETPIPE_SZ, return_pipe_size);
   if ((status < 0) || (status != return_pipe_size)) {
      errlogPrintf ("fcntl (fdout): status: %d\n", status);
      perror ("fcntl (fdout)");
   }

   /* Now encode/buffer up all the input and send to the child process
    */
   int j;
   ssize_t total = 0;
   ssize_t out;
   for (j = 0; j < NUMBER_IO_FIELDS; j++) {
      const epicsEnum16 inputType = (&prec->fta)[j];
      const epicsUInt32 number = (&prec->noa)[j];
      const void *data = (&prec->a)[j];
      const long elementSize = dbValueSize (inputType);

      out = write (pExecInfo->fdin, &inputType, sizeof (inputType));
      total += out;
      out = write (pExecInfo->fdin, &number, sizeof (number));
      total += out;
      out = write (pExecInfo->fdin, data, number * elementSize);
      total += out;
   }

   /* And encode the expected output format
    * Like above, but no data - just type and number of elements.
    */
   for (j = 0; j < NUMBER_IO_FIELDS; j++) {
      const epicsEnum16 outputType = (&prec->ftva)[j];
      const epicsUInt32 number = (&prec->nova)[j];

      out = write (pExecInfo->fdin, &outputType, sizeof (outputType));
      total += out;
      out = write (pExecInfo->fdin, &number, sizeof (number));
      total += out;
   }

   status = close (pExecInfo->fdin);
   if (status != 0) {
      perror ("close  (input_data [out])");
   }

   if (asubExecDebug > 2) {
      printf ("%s: wrote %d bytes\n", prec->name, (int) total);
   }
   return 0;
}


/*------------------------------------------------------------------------------
 */
static long asubExecProcessPart2 (aSubRecord * prec)
{
   static epicsUInt8 discard[512 * 1024];    /* TODO: use lseek instead ??? */

   ExecInfo *pExecInfo;
   pExecInfo = (ExecInfo *) prec->dpvt;

   int status;
   int j;
   long result = pExecInfo->exitCode;

   if (asubExecDebug > 2)
      printf ("exit code: %d\n", pExecInfo->exitCode);

   /* Unpack the response
    */
   ssize_t total = 0;
   ssize_t numBytes = 0;
   for (j = 0; j < NUMBER_IO_FIELDS; j++) {
      const char key = (char) ((int) 'A' + j);

      const epicsEnum16 outputType = (&prec->ftva)[j];
      const epicsUInt32 outputNumber = (&prec->nova)[j];
      void *data = (&prec->vala)[j];

      epicsEnum16 readType;
      epicsUInt32 readNumber;

      numBytes = read (pExecInfo->fdout, &readType, sizeof (readType));
      if (numBytes <= 0)
         break;
      total += numBytes;

      if (readType >= menuFtype_NUM_CHOICES) {
         errlogPrintf ("FTV%c: out of range (%d)\n", key, readType);
         result = -1;
         break;
      }

      numBytes = read (pExecInfo->fdout, &readNumber, sizeof (readNumber));
      if (numBytes <= 0)
         break;
      total += numBytes;

      const long elementSize = dbValueSize (readType);

      if (readType == outputType) {
         /* We have a winner - types match, so element sizes match
          */
         epicsUInt32 less = readNumber <= outputNumber ? readNumber : outputNumber;
         epicsUInt32 skip = readNumber - less;

         numBytes = read (pExecInfo->fdout, data, less * elementSize);
         if (numBytes <= 0)
            break;
         total += numBytes;

         if (skip > 0) {
            numBytes = read (pExecInfo->fdout, discard, skip * elementSize);
            if (numBytes < 0)
               break;
            total += numBytes;
         }

         if (readNumber != outputNumber) {
            errlogPrintf ("NOV%c mis-match exp: %d, actual: %d\n", key, outputNumber,
                          readNumber);
         }

      } else {
         /* type mis match - eventually we will caste, but for now just discard
          */
         errlogPrintf ("FTV%c mis-match exp: %d, actual %d\n", key, outputType, readType);
         epicsUInt32 skip = readNumber;
         numBytes = read (pExecInfo->fdout, discard, skip * elementSize);
         if (numBytes < 0)
            break;
         total += numBytes;
      }
   }

   if (numBytes <= 0) {
      /* Unexpected end of input
       */
      result = -1;
   }

   status = close (pExecInfo->fdout);
   if (status != 0) {
      perror ("close  (output_data [in])");
   }

   if (asubExecDebug > 2) {
      printf ("%s: read %d bytes\n", prec->name, (int) total);
   }

   if (asubExecDebug > 0) {
      printf ("%s: %s (pid=%d) complete\n", prec->name, pExecInfo->argv[0], pExecInfo->pid);
   }

   return result;
}


/*------------------------------------------------------------------------------
 */
static long asubExecProcess (aSubRecord * prec)
{
   ExecInfo *pExecInfo;
   pExecInfo = (ExecInfo *) prec->dpvt;
   long status;

   if (!pExecInfo) {
      errlogPrintf ("%s - no ExecInfo in dpvt\n", prec->name);
      return -1;
   }

   if (prec->pact == FALSE) {
      prec->pact = TRUE;
      status = asubExecProcessPart1 (prec);
      epicsEventSignal (pExecInfo->event);      /* wake up thread */
   } else {
      status = asubExecProcessPart2 (prec);
      prec->pact = FALSE;
   }

   return status;
}


/* -----------------------------------------------------------------------------
 */
epicsRegisterFunction (asubExecInit);
epicsRegisterFunction (asubExecProcess);
epicsExportAddress (int, asubExecDebug);

/* end */
