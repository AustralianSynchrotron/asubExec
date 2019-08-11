#!/bin/env python
#
# $File: //ASP/tec/epics/asubExec/trunk/asubExecTestApp/Db/null.py $
# $Revision: #1 $
# $DateTime: 2019/08/11 16:22:23 $
# Last checked in by: $Author: starritt $
#

import sys
import time

# ------------------------------------------------------------------------------
#
def main():
    time.sleep(2.0)
    return 0


if __name__ == "__main__":
    n = main()
    sys.exit(n)
    # do not use os._exit here

# end
