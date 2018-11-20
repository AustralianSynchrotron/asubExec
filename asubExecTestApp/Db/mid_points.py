#!/bin/env python
#
# $File: //ASP/tec/epics/asubExec/trunk/asubExecTestApp/Db/mid_points.py $
# $Revision: #4 $
# $DateTime: 2018/11/18 18:15:34 $
# Last checked in by: $Author: starritt $
#

import sys
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
        return 2

    inpa = io.input_data['inpa']
    sys.stderr.write("inpa: %s\n" % str(inpa))

    outa = []
    for j in range (len(inpa) - 1):
        v = (inpa [j] + inpa[j+1])/2.0
        outa.append(v)

    outa = tuple(outa)
    sys.stderr.write("outa: %s\n" % str(outa))
    
    output = {'outa': outa }

    status = io.pack(output, sys.stdout.buffer)
    sys.stderr.write("pack okay %s\n" % status)
    if not status:
        return 2

    sys.stderr.write("wrote %d bytes\n" % io.output_len)
    sys.stderr.write("%s complete\n\n" % iam)

    return 0


if __name__ == "__main__":
    n = main()
    sys.stderr.write ("py exit code %s\n" % n)
    sys.exit (n)

# end
