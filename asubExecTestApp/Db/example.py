#!/bin/env python
#
# $File: //ASP/tec/epics/asubExec/trunk/asubExecTestApp/Db/example.py $
# $Revision: #5 $
# $DateTime: 2019/08/10 21:07:07 $
# Last checked in by: $Author: starritt $
#

import os
import sys
import time
import pprint

from asubExec import asubExecIO


# ------------------------------------------------------------------------------
#
def main():
    iam = sys.argv[0]
    sys.stderr.write("%s starting\n" % iam)
    for a in range(1, len(sys.argv)):
        sys.stderr.write("arg%-2s  %s\n" % (a, sys.argv[a]))

    io = asubExecIO()

    status = io.unpack(sys.stdin.buffer)
    sys.stderr.write("unpack okay %s\n" % status)
    if not status:
        return 4
    
    arguments = io.input_data
    sys.stderr.write("input:\n%s\n\n" % pprint.pformat (arguments, indent=2))
#   sys.stderr.write("out spec:\n%s\n\n" % pprint.pformat (io.output_spec, indent=2))

    # Sumulate a long process time
    #
    time.sleep(1.0)

    # No to the quazi actual processing
    #
    output = {}
    output ['outa'] = list (arguments['inpb'])
    output ['outb'] = list (arguments['inpc'])
    output ['outc'] = list (arguments['inpa'])
    
    sys.stderr.write("%s\n\n" % pprint.pformat (output, indent=2))

    status = io.pack(output, sys.stdout.buffer)
    sys.stderr.write("pack okay %s\n" % status)
    if not status:
        return 4

    sys.stderr.write("wrote %d bytes\n" % io.output_len)
    sys.stderr.write("%s complete\n" % iam)

    return 5


if __name__ == "__main__":
    n = main()
    sys.stderr.write("example.py exit code %s\n\n" % n)
    sys.exit(n)
    # do not use os._exit here

# end
