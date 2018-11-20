# $File: //ASP/tec/epics/asubExec/trunk/asubExecTestApp/src/Makefile $
# $Revision: #1 $
# $DateTime: 2018/11/03 17:51:37 $
# Last checked in by: $Author: starritt $
#

TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = asubExecTest
# asubExecTest.dbd will be created and installed
DBD += asubExecTest.dbd

# asubExecTest.dbd will be made up from these files:
asubExecTest_DBD += base.dbd

# Include dbd files from all support applications:
#
asubExecTest_DBD += asubExec.dbd

# Add all the support libraries needed by this IOC
#
asubExecTest_LIBS += asubExec

# asubExecTest_registerRecordDeviceDriver.cpp derives from asubExecTest.dbd
asubExecTest_SRCS += asubExecTest_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
asubExecTest_SRCS_DEFAULT += asubExecTestMain.cpp
asubExecTest_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#asubExecTest_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
asubExecTest_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# end
