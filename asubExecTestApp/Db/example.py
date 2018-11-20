#!/bin/env python
#
# $File: //ASP/tec/epics/asubExec/trunk/asubExecTestApp/Db/example.py $
# $Revision: #4 $
# $DateTime: 2018/11/18 18:15:34 $
# Last checked in by: $Author: starritt $
#

import os
import sys
import time
from asubExec import asubExecIO

# ------------------------------------------------------------------------------
#
def main():
    iam = sys.argv[0]
    sys.stderr.write("%s starting\n" % iam)
    for a in range (1, len(sys.argv)):
        sys.stderr.write("arg%-2s  %s\n" % (a, sys.argv[a]))

    io = asubExecIO()

    status = io.unpack(sys.stdin.buffer)
    sys.stderr.write("unpack okay %s\n" % status)
    if not status:
        return 4

    arguments = io.input_data
    sys.stderr.write("input:    %s\n\n" % arguments)
    sys.stderr.write("out spec: %s\n\n" % io.output_spec)

    time.sleep(4.0)

    # No to the quazi actual processing
    #
    output = {'outa': (1.0, 2.0, 3, "4.0"),
              'outb': (1, 2, 3, 4, 5, 6, 7.0, "8"),
              'outc': [5, 55, 555, 333, 33, 3 ] }
    sys.stderr.write("%s\n\n" % output)

    status = io.pack(output, sys.stdout.buffer)
    sys.stderr.write("pack okay %s\n" % status)
    if not status:
        return 4

    sys.stderr.write("wrote %d bytes\n" % io.output_len)
    sys.stderr.write("%s complete\n\n" % iam)

    return 5


if __name__ == "__main__":
    n = main()
    sys.stderr.write ("py exit code %s\n" % n)
    sys.exit (n)
    # do not use os._exit here

# end
