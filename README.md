# asubExec

The asubExec module is written to be used in conjunction with the aSub record.
It uses the fork() and execvp() paradigm to launch a child process. The child
process should accept input on its standard input and return its result by
writing to its standard output. Inputs are derived from INPA .. INPU, outputs
are written to OUTA .. OUTU.

This module has been developed on and for Linux, specifically CentOS 7 64 bit.

Refer to documentation/asubExec.html for more detail.

