#!/bin/env python
#
# $File: //ASP/tec/epics/asubExec/trunk/asubExecTestApp/Db/mid_points.py $
# $Revision: #7 $
# $DateTime: 2022/05/25 19:56:35 $
# Last checked in by: $Author: starritt $
#

import datetime
import pprint
import sys

from asubExec import asubExecIO

now = datetime.datetime.now

# ------------------------------------------------------------------------------
#


def main():
    iam = sys.argv[0]
    sys.stderr.write("%s starting at %s\n" % (iam, now()))
    for a in range(1, len(sys.argv)):
        sys.stderr.write("arg%-2s  %s\n" % (a, sys.argv[a]))

    io = asubExecIO()

    status = io.unpack(sys.stdin.buffer)
    sys.stderr.write("unpack okay %s\n" % status)
    if not status:
        return 2

    sys.stderr.write("input:\n%s\n\n" % pprint.pformat(io.input_data, indent=2))
    sys.stderr.write("output spec:\n%s\n\n" % pprint.pformat(io.output_spec, indent=2))

    inpa = io.input_data['inpa']
    outa = []
    for j in range(len(inpa) - 1):
        v = (inpa[j] + inpa[j + 1]) / 2.0
        outa.append(v)

    outa = tuple(outa)
    output = {'outa': outa}
    sys.stderr.write("output:\n%s\n\n" % pprint.pformat(output, indent=2))

    status = io.pack(output, sys.stdout.buffer)
    sys.stderr.write("pack okay %s\n" % status)
    if not status:
        return 2

    sys.stderr.write("wrote %d bytes\n" % io.output_len)
    return 0


if __name__ == "__main__":
    n = main()
    sys.stderr.write("mid_points.py exit code %s at %s\n\n" % (n, now()))
    sys.exit(n)

# end
