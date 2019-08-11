# asubExec

The asubExec module is written to be used in conjunction with the aSub record.
It uses the fork() and execvp() paradigm to launch a child process. A sutable
child process will accept input on its standard input and return its result by
writing to its standard output. Inputs are derived from INPA .. INPU, outputs
are written to OUTA .. OUTU.

This module has been developed on and for Linux, specifically CentOS 7 64 bit.

The process file to be executed is defined in the aSub record's EXEC info field.
The file must be either an absolute pathname, a relative path with respect
to the IOC's current directory or the file must be accessible via the PATH
environment variable.

An optional timeout may be specified. This value defines the maximum allowed
time, in seconds, that the child process is allowed to run. The default time
allowed is 3.2E+9 seconds (~100 years), i.e. essentially for ever.

Additional process arguments, up-to 9, may also be specified using the ARG1 ...
ARG9 info fields. Note the first process argument is automatically set to the
record name if not otherwise specified.

Example:

```
record (aSub, "RECORD_NAME") {

    info (EXEC, "exectuable_file")
    info (TIMEOUT, "10.0")
    # if ARG1 not specified, ARG1 is set to record name
    info (ARG2, "additional parameter")
    info (ARG3, "additional parameter")
    ...

    field (INAM, "asubExecInit")
    field (SNAM, "asubExecProcess")
    ...
}
```


The specified EXEC file may be any executable, e.g. a complied program,
a bash script etc., that can decode the input data and generate output data
in the expected format.

Note: the module contains a python helper class, asubExecIO in asubExec.py,
to support python scripts specified as the executed file.

The input is encoded from the current values extracted from A .. U fields,
and the result is decoded and applied to the output fields VALA .. VALU.

The input data is encoded as a direct binary copy of:

    FTA, NOA, *A, FTB, NOB, *B ..., FTU, NOU, *U, FTVA, NOVA, ... FTVU, NOVU

* The FTx fields occupy 2 bytes and are encoded as per asubExecDataType (out of asubExec.h)
* The NOx fields occupy 4 bytes and are encoded as an epicsUInt32
* The *x fields are a direct binary copy of the input data.
* The FTVx fields occupy 2 bytes and are encoded as per asubExecDataType
* The NOVx fields occupy 4 bytes and are encoded as an epicsUInt32

The input data also includes the FTVx/NOVx information to allow the child process
to generate its response in the correct format.

The child process output should be encoded a binary format similar to
the input data and should contain:

    FTVA, NOVA, *A, FTVB, NOVB, *B ..., FTVU, NOVU, *U

* The FTVx fields occupy 2 bytes and are encoded as per asubExecDataType<br>
* The NOVx fields occupy 4 bytes and are encoded as an epicsUInt32<br>
* The *x fields are a direct binary copy of the input.<br>

The asubExec module verifies that the output received from the child process
is as expected. Currently type mis-matches (FTVx) are not handled and are
essentially ignored - later releases of this module may include type casting.

Number of elements mis-matches (NOVx), are handled by discarding additional
elements or leaving elements undefined if not enough were provided.

Last updated: Sun Aug 11 11:30:27 AEST 2019
