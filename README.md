# asubExec - aSub record support module

## Introduction

The asubExec module is intended to be used in conjunction with the aSub record.
It uses the fork() and execvp() paradigm to launch a child process.
The child process should accept input on its standard input and return its
result by writing to its standard output.
Inputs to the childe process are derived from INPA .. INPU, outputs from the
child process are written to OUTA .. OUTU.

__Note:__ This module as been developed on and for Linux, specifically
CentOS&nbsp;7 64&nbsp; bit.

The user should be familiarise his/herself with the aSub record prior to
using this module.

## aSub record definiton

The program/script to be executed is defined in the aSub record's EXEC info field.
This must be either an absolute pathname, a path name relative to the IOC's
current directory (typically the location of the IOC's st.cmd file) or the
program/script must be accessable via the PATH environment variable.

An optional timeout may be specified.
This value defines the maximum allowed time, in seconds, that the child process
is allowed to run.
The default timeout value is 60 seconds, i.e. one minute.

Addtional process arguments (upto 9) may also be specified using the
ARG1, ARG2,  ... ARG9 info fields.

__Note:__ the first process argument is set to the record name if not
otherwise specified.


Example:
```
record (aSub, "RECORD_NAME") {    
   info (EXEC, "exectuable_file")
   info (TIMEOUT, "10.0")
   # if ARG1 not specified, ARG1 is set to record name
   info (ARG2, "additional parameter")
   info (ARG3, "additional parameter")
   ...
   field (DESC, "A meanful description")
   field (SCAN, "<Scan mode for the record>")
   field (INAM, "asubExecInit")
   field (SNAM, "asubExecProcess")
   ...
   field (INPA, ...) # etc
}
```
## EXECutable

The specified EXEC file may be any executable, e.g. a complied program,
or a script such as a python script, a bash script etc., that can accept and
decode the input data from standard input and generate and encode output data
to standard output in the expected format.

### asubExecIO

The module contains a python helper class, asubExecIO, located in asubExec.py,
to support any python scripts specified as the EXEC file.
This is "built" into the asubExec's module's <top>bin/<epics_host_arch> directory.
The example/test programs included with the module show how this is used.
From python/ipython, running  

    import asubExec
    help (asubExec)

also provides useful documentation.

## Interface to child process

The input is encoded from the current values extracted from the A .. U fields,
and the result is decoded and written to the output fields VALA .. VALU.

The input data is encoded as a direct binary copy of:

    stx, version, FTA, NOA, A, FTB, NOB, B ..., FTU, NOU, U, FTVA, NOVA, ... FTVU, NOVU, stx

The stx field is 8 bytes and is 'asubExec' (utf8 encoding)<br>
The version field is 4 bytes and encodes the current version<br>
The FTx fields occupy 2 bytes and are encoded as per asubExec.h<br>
The NOx fields occupy 4 bytes and are encoded as an epicsUInt32<br>
The A, B, ... U fields are a direct binary copy of the input data.<br>
The FTVx fields occupy 2 bytes and are encoded as per asubExec.h<br>
The NOVx fields occupy 4 bytes and are encoded as an epicsUInt32<br>
The etx field is 4 bytes and indicates end of data, 'eod\n' (utf8 encoding)

The input data also includes the FTVx/NOVx information to allow the child
process to generate its response in the correct format.

The child process output should be encoded a binary format similar to
the input data and should contain:

    stx, version, FTVA, NOVA, A, FTVB, NOVB, B ..., FTVU, NOVU, U, etc

The FTVx fields occupy 2 bytes and are encoded as per menuFtype.h<br>
The NOVx fields occupy 4 bytes and are encoded as an epicsUInt32<br>
The A, B, ... U  fields are a copied directed to the output VALA, VALB,
... VALU fields.<br>

The asubExec module verifies that the output received from the child process
is as expected.
Currently type mis matches (FTVx) are not handled and are essentially ignored.
Later releases of this module may include type casting.

Number of elements mis-matches (NOVx), are handled by discarding additonal
elements or leaving exisiting elements undefined if not enough were provided.

## IOC Shell

The IOC shell variable asubExecDebug controls the verbosity of any output.
Use
 - <0 for no output
 - 0 for errors only (the default)
 - 1 for errors and warnings,
 - 2 for for errors, warnings and information,
 - \>=3 for all messages including very detailed messages.

Error and warning messages are output using errlogPrintf, information and
details messages use printf.
Messgaes are preceeded by time of day (to the milli-second), e.g.:

    14:21:13.091 (MIDPT:ASUB:EXEC) asubExec.asubExecInit: Starting
    14:21:13.091 (MIDPT:ASUB:EXEC) asubExec.asubExecInit: EXEC=mid_points.py
    14:21:13.093 (MIDPT:ASUB:EXEC) asubExec.executeThread: executeThread starting...
    14:21:13.093 (MIDPT:ASUB:EXEC) asubExec.executeThread: executeThread sleeping  ...

__Note:__ Any output sent to stderr from the child process appear on the IOC's
shell output.

## Incuding asubExec into an IOC

The usual. In the IOC's configure/RELEASE file (directly or via an include):

    ASUBEXEC=<The location of the asubExec module>

And in the IOC's main src Makefile, include:

    My_IOC_DBD  += asubExec.dbd
    My_IOC_LIBS += asubExec

See asubExecTestApp/src/Makefile as an example.

## Notes

The minimum time this appears to take is approx 50 mSec from start to
finish using a basic test function, i.e min_points.db and mid_point.py.


<font size="2">
Last updated: Mon May 24 14:22:19 AEST 2022
</font>
