#!../../bin/linux-x86_64/asubExecTest

# $File: //ASP/tec/epics/asubExec/trunk/iocBoot/iocasubExecTest/st.cmd $
# $Revision: #5 $
# $DateTime: 2019/08/11 16:22:23 $
# Last checked in by: $Author: starritt $
#

#- You may have to change asubExecTest to something else
#- everywhere it appears in this file

< envPaths
epicsEnvSet("PATH","${PATH}:${TOP}/bin/linux-x86_64")

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/asubExecTest.dbd"
asubExecTest_registerRecordDeviceDriver pdbbase

# Set amount of reporting 
# Normally 0
#
var asubExecDebug 4

## Load record instances
#
dbLoadRecords("db/example.db", "")
dbLoadRecords("db/mid_points.db", "")

cd "${TOP}/iocBoot/${IOC}"
iocInit

# end
