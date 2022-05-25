/* $File: //ASP/tec/epics/asubExec/trunk/asubExecSup/src/asubExec.c $
 * $Revision: #11 $
 * $DateTime: 2022/05/23 21:26:35 $
 * Last checked in by: $Author: starritt $
 *
 * The asubExec module is written to be used in conjunction with the aSub record.
 * It uses the fork() and execvp() paradigm to launch a child process.
 *
 * Copyright (c) 2018-2022  Australian Synchrotron
 *
 * The asubExec module is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * You can also redistribute the asubExec module and/or modify it under the
 * terms of the Lesser GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version when this library is disributed with and as part of the
 * EPICS QT Framework (https://github.com/qtepics).
 *
 * The asubExec module is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the asubExec library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact details:
 * andrew.starritt@synchrotron.org.au
 * 800 Blackburn Road, Clayton, Victoria 3168, Australia.
 *
 *
 * Description
 * The child process should accept input on its standard input and return its
 * result by writing to its standard output.
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
 * Note:
 * This module as been developed on and for Linux, specifically CentOS 7 64 bit.
 */

#include "asubExec.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
#include <epicsVersion.h>
#include <errlog.h>
#include <menuFtype.h>
#include <recGbl.h>
#include <recSup.h>
#include <registryFunction.h>


/* A to U - both input and output
 */
#define NUMBER_IO_FIELDS    21

/* Args are 1 to 9, allow 2 extra for the exec-ed file and sentinal
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
   PIPE_SIZE                      /* must be last */
};

typedef int HalfDuplexPipe[PIPE_SIZE];

/* Private info allocated to each aSub record instance using this module.
 */
typedef struct ExecInfo {
   aSubRecord* prec;              /* record reference */
   epicsThreadId thread_id;       /* monitor thread id */
   epicsEventId event;            /* monitor thread signal event */
   const char* argv[ARG_LENGTH];  /* arguments 0, 1 .. 9, 10 is NULL */
   double timeOut;                /* max time in seconds that a child process allowed to run */
   epicsTimeStamp endTime;        /* the end time */
   pid_t pid;                     /* child process' pid */
   int fdput;                     /* file desciptor for child process stdin  - we write to it */
   int fdget;                     /* file desciptor for child process stdout - we read from it */
   int exitCode;                  /* child process' exit code */
   long status;                   /* return status to record processing */
} ExecInfo;


static int asubExecDebug = 0;     /* exported to IOC shell */
static bool iocIsRunning = true;


/*------------------------------------------------------------------------------
 */
static char* now() {
   struct timeval theTime;           // essentially secs and usecs.
   struct tm bt;                     // broken-down time

   gettimeofday(&theTime, NULL);
   localtime_r(&theTime.tv_sec, &bt);

   static char buffer [24];

   int mSec = theTime.tv_usec / 1000;
   snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
            bt.tm_hour, bt.tm_min, bt.tm_sec, mSec);
   return buffer;
}

/*------------------------------------------------------------------------------
 * Extend perror to be like printf
 */
static void perrorf (const char* function,
                     const int line_no,
                     const char* format, ...)
{
   static const char* red   = "\033[31;1m";
   static const char* reset = "\033[00m";

   char message1 [200];
   char message2 [240];
   va_list arguments;
   va_start (arguments, format);
   vsnprintf (message1, sizeof (message1), format, arguments);
   va_end (arguments);
   snprintf (message2, sizeof (message2), "%s asubExec::%s:%d %s%s%s",
             now(), function, line_no, red, message1, reset);
   perror (message2);
}

#define PERRORF(...) perrorf (   __FUNCTION__, __LINE__, __VA_ARGS__);


/*------------------------------------------------------------------------------
 * Wrapper function around printf/errlogPrintf.
 */
static void devprintf (const int requiredDebug,
                       aSubRecord* prec,
                       const char* function,
                       const char* format, ...)
{
   if (asubExecDebug >= requiredDebug) {

      char message [200];
      va_list arguments;
      va_start (arguments, format);
      vsnprintf (message, sizeof (message), format, arguments);
      va_end (arguments);

      if (requiredDebug >= 2) {
         // Console only.
         //
         printf ("%s (%s) asubExec.%s: %s", now(), prec->name, function, message);
      } else {
         // Errors and warnings: console and the IOC logger.
         //
         errlogPrintf ("%s (%s) %s: %s", now(), prec->name, function, message);
      }
   }
}

/*------------------------------------------------------------------------------
 * Wrapper macros to devprintf.
 * aSubRecord* prec  must be in scope.
 */
#define ERROR(...)    devprintf (0, prec,__FUNCTION__, __VA_ARGS__);
#define WARN(...)     devprintf (1, prec,__FUNCTION__, __VA_ARGS__);
#define INFO(...)     devprintf (2, prec,__FUNCTION__, __VA_ARGS__);
#define DETAIL(...)   devprintf (3, prec,__FUNCTION__, __VA_ARGS__);


/*------------------------------------------------------------------------------
 * We do not want to just send the raw menuFtype values - these have been
 * extended and the under lying values changed from 3.15 to 7.0
 * Convert from EPICS menuFtype to asubExecDataType which we will ensure we
 * alway have fixed values.
 */
static asubExecDataType menuFtype2asubExecDataType (const menuFtype type)
{
   switch (type) {
      case menuFtypeSTRING: return asubExecTypeSTRING;
      case menuFtypeCHAR:   return asubExecTypeCHAR;
      case menuFtypeUCHAR:  return asubExecTypeUCHAR;
      case menuFtypeSHORT:  return asubExecTypeSHORT;
      case menuFtypeUSHORT: return asubExecTypeUSHORT;
      case menuFtypeLONG:   return asubExecTypeLONG;
      case menuFtypeULONG:  return asubExecTypeULONG;
      case menuFtypeFLOAT:  return asubExecTypeFLOAT;
      case menuFtypeDOUBLE: return asubExecTypeDOUBLE;
      case menuFtypeENUM:   return asubExecTypeENUM;
#if EPICS_VERSION >= 7
      case menuFtypeINT64:  return asubExecTypeINT64;
      case menuFtypeUINT64: return asubExecTypeUINT64;
#endif
      default:              return asubExecTypeNone;
   }
   return asubExecTypeNone;
}

/*------------------------------------------------------------------------------
 * Convert from asubExecDataType to EPICS menuFtype.
 */
static menuFtype asubExecDataType2menuFtype (const asubExecDataType type)
{
   switch (type) {
      case asubExecTypeSTRING: return menuFtypeSTRING;
      case asubExecTypeCHAR:   return menuFtypeCHAR;
      case asubExecTypeUCHAR:  return menuFtypeUCHAR;
      case asubExecTypeSHORT:  return menuFtypeSHORT;
      case asubExecTypeUSHORT: return menuFtypeUSHORT;
      case asubExecTypeLONG:   return menuFtypeLONG;
      case asubExecTypeULONG:  return menuFtypeULONG;
      case asubExecTypeFLOAT:  return menuFtypeFLOAT;
      case asubExecTypeDOUBLE: return menuFtypeDOUBLE;
      case asubExecTypeENUM:   return menuFtypeENUM;
#if EPICS_VERSION >= 7
      case asubExecTypeINT64:  return menuFtypeINT64;
      case asubExecTypeUINT64: return menuFtypeUINT64;
#endif
      default:                 return menuFtype_NUM_CHOICES + 1;
   }
   return menuFtype_NUM_CHOICES + 1;
}


/*------------------------------------------------------------------------------
 * Perform and immediate process exit.
 */
static void childExit (const int status)
{
   /* Don't run our parent's atexit() handlers.
    */
   _exit (status);
}


/*------------------------------------------------------------------------------
 * Macro function - perform standard sanity checks.
 * Assumes function has an aSubRecord* prec parameter or similar.
 * NOTE: Auto declares ExecInfo* pExecInfo
 */
#define STANDARD_CHECK(errorReturnValue)                                       \
if (!prec) {                                                                   \
   errlogPrintf ("asubExec %s - null prec\n", __FUNCTION__);                   \
   return errorReturnValue;                                                    \
}                                                                              \
ExecInfo* pExecInfo = (ExecInfo *) prec->dpvt;                                 \
if (!pExecInfo) {                                                              \
   errlogPrintf ("asubExec %s - no ExecInfo in %s dpvt\n",                     \
                 __FUNCTION__, prec->name);                                    \
   return errorReturnValue;                                                    \
}


/*------------------------------------------------------------------------------
 * Initiate a controlled thread shutdown.
 */
static void shutdown (void* item)
{
   aSubRecord* prec = (aSubRecord*) item;
   STANDARD_CHECK ();
   iocIsRunning = false;
   epicsEventSignal (pExecInfo->event);      /* wake up thread */
}


/*------------------------------------------------------------------------------
 * Creates and starts the child process.
 * Returns true if and only if successfull.
 * NOTE: any child process std err output gets direted to the IOC shell console.
 */
static bool startChildProcess (aSubRecord* prec)
{
   STANDARD_CHECK (false);

   pid_t pid;
   int status;
   HalfDuplexPipe input_data;
   HalfDuplexPipe output_data;

   /* Ensure not erroneous
    */
   pExecInfo->pid = -1;
   pExecInfo->fdput = -1;
   pExecInfo->fdget = -1;
   pExecInfo->exitCode = -1;

   /* Create pipes to comunicate with child process.
    */
   status = pipe (input_data);
   if (status != 0) {
      PERRORF ("pipe (input_data)");
      return false;
   }

   status = pipe (output_data);
   if (status != 0) {
      PERRORF ("pipe (output_data)");
      return false;
   }

   /* Create child process
    */
   pid = fork ();
   if (pid < 0) {
      /* We have had a forking error. */
      PERRORF ("fork ()");
      return false;
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

      /* Reset which signals are blocked. The child process inherites these
       * from the EPICS IOC. We need a "clean" slate, in particular, so that
       * the child process will respond to a SIGTERM signal.
       * The child process is free to catch SIGTERM if needs be.
       */
      sigemptyset (&emptyMask);
      status = sigprocmask (SIG_SETMASK, &emptyMask, NULL);
      if (status != 0) {
         PERRORF ("sigprocmask ()");
         childExit (SETUP_EXIT_CODE);
      }

      /* Connect standard IO to the pipes.
       * Duplicate file descriptors to standard in/out
       */
      fd = dup2 (input_data[PIPE_READ], STDIN_FILENO);
      if (fd != STDIN_FILENO) {
         PERRORF ("dup2 (stdin)");
         childExit (SETUP_EXIT_CODE);
      }

      fd = dup2 (output_data[PIPE_WRITE], STDOUT_FILENO);
      if (fd != STDOUT_FILENO) {
         PERRORF ("dup2 (stdin)");
         childExit (SETUP_EXIT_CODE);
      }

      /* from posix/osdProcess.c
       * close all open files except for STDIO so they will not be inherited
       * by the spawned process. This includes the unused pipe descriptors.
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

      /* Caste to get rid of that pesky warning.
       */
      status = execvp (pExecInfo->argv[0], (char *const *) pExecInfo->argv);

      /* The exec call failed - it returned - this is unexpected.
       */
      PERRORF ("execvp (\"%s\", ...) -> %d", pExecInfo->argv[0], status);
      childExit (NO_EXEC_EXIT_CODE);    /** does not return - most important **/
   }

   /* We are the parent. Save the child process id.
    */
   pExecInfo->pid = pid;

   INFO ("%s (pid=%d) starting\n", pExecInfo->argv[0], pExecInfo->pid);

   /* Save file pipe file descriptors and close unused pipe ends.
    */
   pExecInfo->fdput = input_data[PIPE_WRITE];
   status = close (input_data[PIPE_READ]);
   if (status != 0) {
      PERRORF ("close  (input_data [in])");
   }

   pExecInfo->fdget = output_data[PIPE_READ];
   status = close (output_data[PIPE_WRITE]);
   if (status != 0) {
      PERRORF ("close  (output_data [out])");
   }

   /* Set put/get files non blocking - we need to be able to kill the
    * child process if it exceeds allowed time and/or monitor IOC shutdown
    * requests.
    */
   int flags;

   flags = fcntl (pExecInfo->fdput, F_GETFL, 0);
   flags |= O_NONBLOCK;
   flags = fcntl (pExecInfo->fdput, F_SETFL, flags);

   flags = fcntl (pExecInfo->fdget, F_GETFL, 0);
   flags |= O_NONBLOCK;
   flags = fcntl (pExecInfo->fdget, F_SETFL, flags);

   /* Lastly calculate timeout/end time beyond which the child process
    * will be terminated.
    */
   epicsTimeGetCurrent (&pExecInfo->endTime);
   epicsTimeAddSeconds (&pExecInfo->endTime, pExecInfo->timeOut);

   return true;
}


/*------------------------------------------------------------------------------
 * Wait for and/or kill the child process.
 */
static void waitChildProcess (aSubRecord* prec, const bool immediate)
{
   STANDARD_CHECK ();

   static const double dt = 0.005;

   /* Calculated term/kill times
    * We allow 0.1s wiggle roon before issing SIGTERM, after which we allow a
    * further 2 seconds for the process to terminate before issing a SIGKILL.
    */
   epicsTimeStamp termTime;
   epicsTimeStamp killTime;
   bool sigTermIssued;

   epicsTimeGetCurrent (&termTime);
   epicsTimeAddSeconds (&termTime, 0.1);

   epicsTimeGetCurrent (&killTime);
   epicsTimeAddSeconds (&killTime, 2.1);

   sigTermIssued = false;

   /* Monitor the child process
    */
   while (iocIsRunning) {
      epicsThreadSleep (dt);
      if (!iocIsRunning) break;

      /* Wait for process to change state.
       */
      int status;
      pid_t pid = waitpid (pExecInfo->pid, &status, WNOHANG);

      if (pid == pExecInfo->pid) {
         /* Child process is complete - simple.
          */
         INFO ("child process complete\n");
         pExecInfo->exitCode = WEXITSTATUS (status);
         break;
      }

      if (pid != 0) {
         /* an unexpected return value occured, either an error or another pid.
          */
         PERRORF ("waitpid");
         ERROR ("waitpid (%d) => %d, status = %d\n", pExecInfo->pid, pid, status);

         pExecInfo->exitCode = WAITPID_EXIT_CODE;
         break;
      }

      /* pid == 0 - still waiting for child process to complete.
       */
      DETAIL ("child process still running\n");

      /* Has the allowed time expired ?
       */
      epicsTimeStamp timeNow;
      epicsTimeGetCurrent (&timeNow);
      if (epicsTimeLessThan (&timeNow, &termTime)) continue;

      if (!sigTermIssued) {
         /* Timeout - shutdown child process.
          */
         INFO ("child process timeout\n");

         /* First ask nicely, then allow 2 seconds fopr orderly shutdown.
          */
         INFO ("sending SIGTERM to pid %d\n", pExecInfo->pid);

         status = kill (pExecInfo->pid, SIGTERM);
         pExecInfo->exitCode = TIMEOUT_EXIT_CODE;

         sigTermIssued = true;
         continue;
      }

      /* Allow a bit extra time/wiggle room before we issue a kill order
       */
      if (epicsTimeLessThan (&timeNow, &killTime)) continue;

      /* No more Mr. Nice Guy ...
       */
      INFO ("sending SIGKILL to pid %d\n", pExecInfo->pid);

      kill (pExecInfo->pid, SIGKILL);
      pExecInfo->exitCode = TIMEOUT_EXIT_CODE;

      waitpid (pExecInfo->pid, &status, 0);

      INFO ("process (pid=%d) killed\n", pExecInfo->pid);

      pExecInfo->exitCode = TIMEOUT_EXIT_CODE;
      break;
   }
}

/*------------------------------------------------------------------------------
 * A wrapper around the write function to check for IOC termination, timeout
 * and would be blocking "errors".
 */
ssize_t writeWrapper (aSubRecord* prec, const void* buffer, const size_t count)
{
   static const double dt = 0.005;

   STANDARD_CHECK (-1);

   ssize_t numBytes;   /* result */

   while (true) {
      /* Has the IOC has stopped running ?
       */
      if (!iocIsRunning) {
         INFO ("IOC terminated\n");
         numBytes = -1;
         break;
      }

      /* Has the allowed time expired ?
       */
      epicsTimeStamp timeNow;
      epicsTimeGetCurrent (&timeNow);
      if (epicsTimeGreaterThan (&timeNow, &pExecInfo->endTime)) {
         INFO ("child process timeout\n");
         numBytes = -2;
         break;
      }

      /* The IOC is still running and no timeout - try an actual write.
       */
      numBytes = write (pExecInfo->fdput, buffer, count);
      if (numBytes >= 0) {
         /* the write went okay */
         break;
      }

      const int theError = errno;
      if ((theError != EAGAIN) && (theError != EWOULDBLOCK)) {
         /* This is an actual error
          */
         PERRORF ("write (,, %d)", (int) count);
         break;
      }

      /* Sleep a while and try again.
       */
      DETAIL ("writeWrapper: epicsThreadSleep\n");
      epicsThreadSleep (dt);
   }

   return numBytes;
}


/*------------------------------------------------------------------------------
 * A wrapper around the read function to check for IOC termination, timeout
 * and would be blocking "errors".
 */
ssize_t readWrapper (aSubRecord* prec, void* buffer, const size_t count)
{
   static const double dt = 0.005;

   STANDARD_CHECK (-1);

   ssize_t numBytes;   /* result */

   while (true) {
      /* Has the IOC has stopped running ?
       */
      if (!iocIsRunning) {
         INFO ("IOC terminated\n");
         numBytes = -1;
         break;
      }

      /* Has the allowed time expired ?
       */
      epicsTimeStamp timeNow;
      epicsTimeGetCurrent (&timeNow);
      if (epicsTimeGreaterThan (&timeNow, &pExecInfo->endTime)) {
         INFO ("child process timeout\n");
         numBytes = -2;
         break;
      }

      /* The IOC is still running and no timeout - try an actual read.
       */
      numBytes = read (pExecInfo->fdget, buffer, count);
      if (numBytes >= 0) {
         /* the read went okay - albeit 0 read for end of input */
         /* TODO: check for numBytes < count  - go round the loop */
         break;
      }

      const int theError = errno;
      if ((theError != EAGAIN) && (theError != EWOULDBLOCK)) {
         /* This is an actual error
          */
         PERRORF ("read (,, %d)", (int) count);
         break;
      }

      /* Sleep a while and try again.
       */
      DETAIL("epicsThreadSleep\n");
      epicsThreadSleep (dt);
   }

   return numBytes;
}


/*------------------------------------------------------------------------------
 * Encodes input fields A, B, ... U and writes data to child process.
 * Also encodes info about the output fields (type and max elements).
 */
static ssize_t encodeAndWriteInputs (aSubRecord* prec)
{
   ssize_t total;
   ssize_t numBytes;
   int j;

   total = 0;

   /* First house keeping - magic word and version.
    */
   numBytes = writeWrapper (prec, asubExecStx, strnlen(asubExecStx, 80));
   total += numBytes;

   const epicsUInt32 version = asubExecVersion;
   numBytes = writeWrapper (prec, &version, sizeof(version));
   total += numBytes;

   for (j = 0; j < NUMBER_IO_FIELDS; j++) {
      const menuFtype inputType = (&prec->fta)[j];
      const epicsInt16 extFieldType = menuFtype2asubExecDataType (inputType);
      const epicsUInt32 number = (&prec->noa)[j];
      const void *data = (&prec->a)[j];
      const long elementSize = dbValueSize (inputType);

      numBytes = writeWrapper (prec, &extFieldType, sizeof (extFieldType));
      if (numBytes < 0)
         break;
      total += numBytes;

      numBytes = writeWrapper (prec, &number, sizeof (number));
      if (numBytes < 0)
         break;
      total += numBytes;

      numBytes = writeWrapper (prec, data, number * elementSize);
      if (numBytes < 0)
         break;
      total += numBytes;
   }

   /* And encode the expected output format.
    * Like above, but no data - just type and number of elements.
    */
   for (j = 0; j < NUMBER_IO_FIELDS; j++) {
      const menuFtype outputType = (&prec->ftva)[j];
      const epicsInt16 extFieldType = menuFtype2asubExecDataType (outputType);
      const epicsUInt32 number = (&prec->nova)[j];

      numBytes = writeWrapper (prec, &extFieldType, sizeof (extFieldType));
      if (numBytes < 0)
         break;
      total += numBytes;

      numBytes = writeWrapper (prec, &number, sizeof (number));
      if (numBytes < 0)
         break;
      total += numBytes;
   }

   /* And lastly terminate data stream.
    */
   numBytes = writeWrapper (prec, asubExecEtx, strnlen(asubExecEtx, 80));
   total += numBytes;

   return total;
}

/*------------------------------------------------------------------------------
 * Reads data from the child process and decodes into fields VALA, VALB, ... VALU
 */
static ssize_t readAndDecodeOutputs(aSubRecord* prec)
{
   const size_t stxLen = strnlen(asubExecStx, 80);
   const size_t etxLen = strnlen(asubExecEtx, 80);

   ssize_t total;
   ssize_t numBytes;
   int j;

   /* Unpack the response.
    */
   total = 0;
   numBytes = 0;

   epicsUInt8 meta [80];
   epicsUInt32 version;

   numBytes = readWrapper (prec, &meta, stxLen);
   total += numBytes;

   DETAIL ("read meta data\n");

   if (memcmp(&meta, asubExecStx, stxLen) != 0) {
      ERROR ("read stx invalid\n");
      return -1;
   }

   numBytes = readWrapper (prec, &version, sizeof(version));
   total += numBytes;

   /* Check version - skip minor version number \
    */
   if ((version & 0x00FFFF00) != (asubExecVersion & 0x00FFFF00)) {
      ERROR ("version mis match, read %06X, expecting %06X\n",
              version, asubExecVersion);
      return -1;
   }


   for (j = 0; j < NUMBER_IO_FIELDS; j++) {
      static epicsUInt8 discard[512 * 1024];    /* TODO: use lseek instead ? */

      const char key = (char) ((int) 'A' + j);  /* for diagnostic outputs */

      const epicsEnum16 outputType = (&prec->ftva)[j];
      const epicsUInt32 outputNumber = (&prec->nova)[j];
      void *data = (&prec->vala)[j];

      epicsInt16 readExtType;
      epicsEnum16 readIntType;
      epicsUInt32 readNumber;

      numBytes = readWrapper (prec, &readExtType, sizeof (readExtType));
      if (numBytes <= 0)
         break;
      total += numBytes;

      /* Map standard to EPICS version specific type
        */
       readIntType = asubExecDataType2menuFtype (readExtType);
       if (readIntType < 0 || readIntType >= menuFtype_NUM_CHOICES) {
         ERROR ("read FTV%c type is invalid\n", key);
         numBytes = -1;
         break;
      }

      numBytes = readWrapper (prec, &readNumber, sizeof (readNumber));
      if (numBytes <= 0)
         break;
      total += numBytes;

      const long elementSize = dbValueSize (readIntType);

      if (readIntType == outputType) {
         /* We have a winner - types match, so element sizes match
          */
         epicsUInt32 less = readNumber <= outputNumber ? readNumber : outputNumber;
         epicsUInt32 skip = readNumber - less;

         numBytes = readWrapper (prec, data, less * elementSize);
         if (numBytes <= 0)
            break;
         total += numBytes;

         if (skip > 0) {
            numBytes = readWrapper (prec, discard, skip * elementSize);
            if (numBytes < 0)
               break;
            total += numBytes;
         }

         if (readNumber != outputNumber) {
            WARN ("NOV%c size mis-match expected: %d, actual: %d\n",
                  key, outputNumber, readNumber);
         }

      } else {
         /* type mis match - eventually we may caste, but for now just discard
          */
         ERROR ("FTV%c mis-match expected: %d, actual %d\n",
                key, outputType, readIntType);
         epicsUInt32 skip = readNumber;
         numBytes = readWrapper (prec, discard, skip * elementSize);
         if (numBytes < 0)
            break;
         total += numBytes;
      }
   }

   if (numBytes >= 0) {
      numBytes = readWrapper (prec, &meta, etxLen);
      total += numBytes;

      if (memcmp(meta, asubExecEtx, etxLen) != 0) {
         ERROR ("read etx invalid\n");
         return -1;
      }
   }

   return total;
}


/*------------------------------------------------------------------------------
 * executeProcess does all the hard work - it runs asynchronously in the
 * aSub record's associated thread.
 */
static bool executeProcess (aSubRecord* prec)
{
   STANDARD_CHECK (false);

   bool result = startChildProcess (prec);
   if (!result) return result;

   ssize_t total;
   int status;

   /* First encode/buffer up all the input and send to the child process.
    * We write all the output data before reading any input data.
    * The rules of the game are that the nonminated program/script should
    * consume all its input before processing and generating any significant
    * amount of output.  The pipes provide some leeway here.
    */
   total = encodeAndWriteInputs (prec);

   status = close (pExecInfo->fdput);
   if (status != 0) {
      PERRORF ("close  (input_data [out])");
   }

   INFO ("wrote %d bytes\n", (int) total);

   /* Unpack the response
    */
   total = readAndDecodeOutputs (prec);

   /* We expect all reads to be good and to have read the minimum number of
    * expected bytes.
    */
   if (total <= NUMBER_IO_FIELDS * (sizeof (epicsInt16) + sizeof (epicsUInt32))) {
      /* Unexpected end of input, timeout or IOC terminate or insuffient data
       */
      PERRORF ("failure");
      result = false;
   }

   status = close (pExecInfo->fdget);
   if (status != 0) {
      PERRORF ("close  (output_data [in])");
   }

   INFO ("read %d bytes\n", (int) total);
   INFO ("%s (pid=%d) complete\n", pExecInfo->argv[0], pExecInfo->pid);

   waitChildProcess (prec, false);

   INFO ("process exit code: %d\n", pExecInfo->exitCode);

   return result;
}

/*------------------------------------------------------------------------------
 * Thread function
 * This thread the function essentially waits for the child process to terminate
 * and then calls the records process function to deal with the response.
 */
static void executeThread (aSubRecord* prec)
{
   STANDARD_CHECK ();

#if USE_TYPED_RSET && EPICS_VERSION >= 7
   struct typed_rset *rset = prec->rset;
#else
   struct rset *rset = prec->rset;
#endif

   INFO ("executeThread starting...\n");

   /* thread runs indefinitly - use epicsAtExit test
    */
   while (iocIsRunning) {

      INFO ("executeThread sleeping  ...\n");

      epicsEventWait (pExecInfo->event);
      if (!iocIsRunning) break;

      INFO ("executeThread awake ...\n", now(), prec->name);

      bool status = executeProcess (prec);
      pExecInfo->status = status ? 0 : -1;

      /* One way or another, the child process is (deemed) complete.
       * Initiate processing part 2
       */
      rset->process ((dbCommon *) prec);
   }

   INFO ("executeThread terminated\n");
}


/*------------------------------------------------------------------------------
 * Record functions
 *------------------------------------------------------------------------------
 */
static long asubExecInit (aSubRecord* prec)
{
   int j;
   long status;
   ExecInfo* pExecInfo;

   /* Always do the intro regardless of the asubExecDebug level.
    */
   {
      int t = asubExecDebug;
      asubExecDebug = 4;
      INFO ("Starting\n");
      asubExecDebug = t;
   }

   /* Allocate and save memory for this record's private data.
    */
   pExecInfo = (ExecInfo *) callocMustSucceed (1, sizeof (ExecInfo), "asubExecInit");
   prec->dpvt = pExecInfo;

   pExecInfo->prec = prec;
   pExecInfo->event = epicsEventCreate (epicsEventEmpty);

   for (j = 0; j < ARG_LENGTH; j++) {
      pExecInfo->argv[j] = NULL;
   }

   pExecInfo->timeOut = 60.0;   /* default: one minute */
   pExecInfo->pid = -1;
   pExecInfo->fdput = -1;
   pExecInfo->fdget = -1;

   /* Search for this record's INFO fields
    */
   DBENTRY entry;
   dbInitEntry (pdbbase, &entry);
   status = dbFindRecord (&entry, prec->name);
   if (status != 0) {
      ERROR ("dbFindRecord can't find own record\n");
      prec->pact = 1;
      return -1;
   }

   status = dbFindInfo (&entry, "EXEC");
   if ((status != 0) || !entry.pinfonode) {
      ERROR ("dbFindInfo can't find:  info (EXEC, ...)\n");
      prec->pact = 1;
      return -1;
   }

   dbInfoNode *infoNode = entry.pinfonode;
   pExecInfo->argv[0] = epicsStrDup (infoNode->string);

   status = dbFindInfo (&entry, "ARG1");
   if ((status == 0) && entry.pinfonode) {
      pExecInfo->argv[1] = epicsStrDup (entry.pinfonode->string);
   } else {
      /* Default value for arg1 is own record name */
      pExecInfo->argv[1] = epicsStrDup (prec->name);
   }

   /* Now do args 2 through 9.
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
      double t;
      t = epicsStrtod (entry.pinfonode->string, &endptr);
      if (endptr == entry.pinfonode->string) {
         WARN ("Invalid time specified, using default\n");
      } else if (t < 0.1) {
         WARN ("Negative/very small timeout specified, using 0.1 second\n");
         pExecInfo->timeOut = 0.1;
      } else {
         pExecInfo->timeOut = t;  /* All okay  */
      }
      INFO ("timeout %.2fs\n", pExecInfo->timeOut);
   }

   /* Use record name as the task name.
    */
   pExecInfo->thread_id = epicsThreadCreate     /*  */
       (prec->name, epicsThreadPriorityMedium,
        epicsThreadGetStackSize (epicsThreadStackMedium),
        (EPICSTHREADFUNC) executeThread, prec);

   /* Register IOC shut down in order to perform a clean exit.
    */
   epicsAtExit (shutdown, prec);

   INFO ("%s=%s\n", infoNode->name, infoNode->string);

   return 0;
}

/*------------------------------------------------------------------------------
 */
static long asubExecProcess (aSubRecord* prec)
{
   STANDARD_CHECK (-1);

   long status;

   DETAIL ("pact=%d\n", prec->pact);

   if (prec->pact == FALSE) {
      /* wake up thread */
      prec->pact = TRUE;
      epicsEventSignal (pExecInfo->event);
      status = 0;
   } else {
      /* thread is complete */
      status = pExecInfo->status;
      prec->pact = FALSE;
   }

   DETAIL ("pact=%d, status=%ld\n",prec->pact, status);

   return status;
}


/* -----------------------------------------------------------------------------
 */
epicsRegisterFunction (asubExecInit);
epicsRegisterFunction (asubExecProcess);
epicsExportAddress (int, asubExecDebug);

/* end */
