# $File: //ASP/tec/epics/asubExec/trunk/asubExecTestApp/Db/Makefile $
# $Revision: #2 $
# $DateTime: 2019/08/11 16:22:23 $
# Last checked in by: $Author: starritt $
#

TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#----------------------------------------------------
# Create and install (or just install) into <top>/db
# databases, templates, substitutions like this
#
DB += example.db
DB += mid_points.db

#----------------------------------------------------
# Create and install into <top>/bin/<epics_host_arch>
#
SCRIPTS += example.py
SCRIPTS += null.py
SCRIPTS += mid_points.py

#----------------------------------------------------
# If <anyname>.db template is not named <anyname>*.template add
# <anyname>_template = <templatename>

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# end
