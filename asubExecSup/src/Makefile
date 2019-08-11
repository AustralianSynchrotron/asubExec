# $File: //ASP/tec/epics/asubExec/trunk/asubExecSup/src/Makefile $
# $Revision: #5 $
# $DateTime: 2019/08/11 16:22:23 $
# Last checked in by: $Author: starritt $
#
TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

# Make EPICS 7 friendly
#
USR_CFLAGS += -DUSE_TYPED_RSET

#==================================================
# build a support library
#
LIBRARY_IOC += asubExec

DBD += asubExec.dbd

INC += asubExec.h

# specify all source files to be compiled and added to the library
#
asubExec_SRCS += asubExec.c

asubExec_LIBS += $(EPICS_BASE_IOC_LIBS)

# Install in <top>/bin/<EPICS_HOST_ARCH>
# Note: the SCRIPTS set this executable, but it is not a stand alone script
#
SCRIPTS += asubExec.py

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# end
