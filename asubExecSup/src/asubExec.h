/* $File: //ASP/tec/epics/asubExec/trunk/asubExecSup/src/asubExec.h $
 * $Revision: #2 $
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
 */

#ifndef ASUB_EXEC_H
#define ASUB_EXEC_H 1

#ifdef __cplusplus
extern "C" {
#endif

/* Version 1.2.2
 */
#define asubExecVersion  0x00010202

/* Start and end text sent between EPICS IOC and the spanewd purpose.
 */
#define asubExecStx "asubExec"
#define asubExecEtx "eod\n"

/* Add new values to the end of this list.
 * Based on EPICS base 7 menuFtype.h
 * We define own type in order to allow compatibility with earlier
 * versions of EPICS base.
 */
typedef enum asubExecDataType {
   asubExecTypeNone = -1,              /* None */
   asubExecTypeSTRING = 0,             /* STRING */
   asubExecTypeCHAR,                   /* CHAR */
   asubExecTypeUCHAR,                  /* UCHAR */
   asubExecTypeSHORT,                  /* SHORT */
   asubExecTypeUSHORT,                 /* USHORT */
   asubExecTypeLONG,                   /* LONG */
   asubExecTypeULONG,                  /* ULONG */
   asubExecTypeFLOAT,                  /* FLOAT */
   asubExecTypeDOUBLE,                 /* DOUBLE */
   asubExecTypeENUM,                   /* ENUM */
   asubExecTypeINT64,                  /* INT64 */
   asubExecTypeUINT64,                 /* UINT64 */
   NUMBER_OF_FIELD_TYPES               /* Must be last */
} asubExecDataType;

#ifdef __cplusplus
}
#endif

#endif  /* ASUB_EXEC_H */
