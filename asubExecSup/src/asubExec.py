# $File: //ASP/tec/epics/asubExec/trunk/asubExecSup/src/asubExec.py $
# $Revision: #6 $
# $DateTime: 2022/05/25 19:56:35 $
# Last checked in by: $Author: starritt $
#
# Description
# asubExec IO utility class
#
# Copyright (c) 2018-2022 Australian Synchrotron
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# Licence as published by the Free Software Foundation; either
# version 2.1 of the Licence, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public Licence for more details.
#
# You should have received a copy of the GNU Lesser General Public
# Licence along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Original author: Andrew Starritt
# Maintained by:   Andrew Starritt
#
# Contact details:
# as-open-source@ansto.gov.au
# 800 Blackburn Road, Clayton, Victoria 3168, Australia.
#

"""
The asubExec module provides the asubExecIO utility class.
"""


import sys
import struct
from collections import namedtuple


class asubExecIO(object):
    """ asubExec IO utility class.
        This class may be used to unpack data from standard input as provided
        by the asubExec module and pack the result to standard output.

        It must be consistant with the enum asubExecDataType together with the
        strings asubExecStx and  asubExecEtx out of asubExec.h

        TBD: STRING and ENUM parameters.
    """

    # from asubExec.h
    #
    asubExecVersion = (1, 2, 2)

    asubExecTypeSTRING = 0    # STRING
    asubExecTypeCHAR = 1      # CHAR
    asubExecTypeUCHAR = 2     # UCHAR
    asubExecTypeSHORT = 3     # SHORT
    asubExecTypeUSHORT = 4    # USHORT
    asubExecTypeLONG = 5      # LONG
    asubExecTypeULONG = 6     # ULONG
    asubExecTypeFLOAT = 7     # FLOAT
    asubExecTypeDOUBLE = 8    # DOUBLE
    asubExecTypeENUM = 9      # ENUM
    asubExecTypeINT64 = 10    # INT64
    asubExecTypeUINT64 = 11   # UINT64

    asubExecStx = 'asubExec'
    asubExecEtx = 'eod\n'

    """
    This corresponds to INPA..INPU and OUTA..OUTU
    """
    Keys = "abcdefghijklmnopqrstu"

    """
    Defines the DataTypeSpec naamed tuple.
    name is the EPICS data type name
    size is element size in bytes
    format is the struct format string - we use native endieness
    ftype is the python data type - used as a casting callable.
    """
    DataTypeSpec = namedtuple("DataTypeSpec", ('name', 'size', 'format', 'ftype'))

    typeMap = {
        asubExecTypeSTRING: DataTypeSpec("STRING", 40, None, str),
        asubExecTypeCHAR:   DataTypeSpec("CHAR",    1, "=b", int),
        asubExecTypeUCHAR:  DataTypeSpec("UCHAR",   1, "=B", int),
        asubExecTypeSHORT:  DataTypeSpec("SHORT",   2, "=h", int),
        asubExecTypeUSHORT: DataTypeSpec("USHORT",  2, "=H", int),
        asubExecTypeLONG:   DataTypeSpec("LONG",    4, "=i", int),
        asubExecTypeULONG:  DataTypeSpec("ULONG",   4, "=I", int),
        asubExecTypeFLOAT:  DataTypeSpec("FLOAT",   4, "=f", float),
        asubExecTypeDOUBLE: DataTypeSpec("DOUBLE",  8, "=d", float),
        asubExecTypeENUM:   DataTypeSpec("ENUM",    2, None, int),
        asubExecTypeINT64:  DataTypeSpec("INT64",   8, "=q", int),
        asubExecTypeUINT64: DataTypeSpec("UINT64",  8, "=Q", int)
    }

    def __init__(self):
        self._input_data = None
        self._version = None
        self._output_spec = {}
        self._raw_data = None
        self._ptr = None
        self._output_len = None


    @property
    def input_data(self):
        """
        Provides the received input data.
        The input data is a dictionary, keyed by 'inpa', inpb', ... 'inpu',
        with each dictionary value a tuple of float, int or str.
        Example:

        { 'inpa': (1.0, 3.0, 5.0, 7.0, 1.0, 3.0, 5.0, 7.0),
          'inpb': (0.0,),
          'inpc': (0.0,),
          ...
          'inpt': (0.0,),
          'inpu': (0.0,)
        }
        """
        return self._input_data


    @property
    def output_spec(self):
        """
        Specifies the kind and number of elements to be wriiten to standard output.
        Example:
        { 'outa': {'kind': 8, 'number': 7},
          'outb': {'kind': 5, 'number': 5},
          'outc': {'kind': 1, 'number': 5},
          'outd': {'kind': 0, 'number': 5},
          'oute': {'kind': 8, 'number': 1},
          ...
          'outt': {'kind': 8, 'number': 1},
          'outu': {'kind': 8, 'number': 1}
        }

        The kind is as defined in asubExec.h and also replicated in this module.
        Example:
           asubExecTypeDOUBLE = 8

        """
        return self._output_spec


    @property
    def output_len(self):
        """ Output length in bytes """
        return self._output_len

    # -------------------------------------------------------------------------
    #
    def unpack(self, source=sys.stdin.buffer):
        """
        Unpacks the input data and returns True if the unpack successfull.
        The unpacked data is subsequenctly available using the input_data property.
        The exprected return or output specification is available using the
        output_spec property.
        """

        # Decoded input data
        #
        self._input_data = None
        self._output_spec = {}

        # Raw data and associated read pointer.
        #
        self._raw_data = source.read()
        self._ptr = 0

        input_size = len(self._raw_data)
        self.message("input data size : %d" % input_size)

        # Need to calculate the abs min size
        #
        if input_size < 289:
            self.message("input data stream type too short (len=%d)" % input_size)
            return False

        # Unpack meta data - magic text and version number
        #
        stx, version = self._read_prolog()

        if stx != asubExecIO.asubExecStx:
            self.message("input data stream type is not '%s': '%s'" % (asubExecIO.asubExecStx, stx))
            return False

        self.message("input data stream version: %s" % str(version))

        if version != asubExecIO.asubExecVersion:
            self.message("unexpected input data stream, expecting: %s" %
                         str(asubExecIO.asubExecVersion))
            return False

        # Unpack actual user application input data
        #
        arguments = {}
        for key in asubExecIO.Keys:
            field = "inp%s" % key

            # Read the data type
            #
            kind = struct.unpack("=H", self._read(2))[0]

            spec = asubExecIO.typeMap.get(kind, None)
            if spec is None:
                self.message("%s Unhandled type %s\n" % (field, kind))
                return False

#           self.message (key, kind, spec)

            if spec.format is None:
                self.message("%s Unhandled type %s\n" % (field, spec.name))
                return False

            item = self._read_array(spec)
            arguments[field] = item

        self._input_data = arguments

        # Unpack specification application output data
        #
        arguments = {}
        for key in asubExecIO.Keys:
            field = "out%s" % key

            kind = struct.unpack("=H", self._read(2))[0]
            number = struct.unpack("=I", self._read(4))[0]
            spec = {'kind': kind, 'number': number}
            arguments[field] = spec

        self._output_spec = arguments

        # House keeping end of data
        #
        etx = self._read_epilog()
        if etx != asubExecIO.asubExecEtx:
            self.message("input data not terminated: '%s'  %d / %d " % (etx, self._ptr, len(data)))
            return False

        return True

    # -------------------------------------------------------------------------
    #
    def pack(self, output, target=sys.stdout.buffer):
        """
        Packs the given output data into the target buffer.

        The output data is a dictionary, keyed by 'outa', outb', ... 'outu',
        with each dictionary value a tuple or list of floats and/or ints and/or strs.
        The individual element values are first caste to one of float, int or str
        prior to packing.

        If a output data dictionary does not provide the key value, then a default
        replacement value is used  (0.0, ) for float types, (0, ) for interger and
        enum types, and ("",) for string types.

        pack returns True if successfull.
        """

        self._raw_output = bytearray()

        self._write_prolog()

        for key in asubExecIO.Keys:
            field = "out%s" % key

            out_spec = self._output_spec[field]
            kind = out_spec['kind']
            number = out_spec['number']

            spec = asubExecIO.typeMap.get(kind, None)
            if spec is None:
                self.message("%s Unhandled type %s\n" % (field, kind))
                return False

            item = output.get(field, None)
            if item is None:
                ftype = spec.ftype
                if isinstance(ftype, float):
                    item = (0.0, )
                elif isinstance(ftype, int):
                    item = (0, )
                elif isinstance(ftype, str):
                    item = ("", )
                else:
                    item = None

            if not isinstance(item, tuple) and not isinstance(item, list):
                self.message("%s Expected tuple/list, received type %s" % (field, type(item)))
                return False

            t = struct.pack("=H", kind)
            self._write(t)

            self._write_array(item, number, spec, field)

        self._write_epilog()

        target.write(self._raw_output)
        self._output_len = len(self._raw_output)
        return True


    # -------------------------------------------------------------------------
    #
    @staticmethod
    def message(*args, **kwd):
        """
        wrapper around print that writes to stderr.
        """
        print(*args, **kwd, file=sys.stderr)


    # -------------------------------------------------------------------------
    #
    def _read(self, size):
        """
        Read size bytes from the raw input buffer.
        """

        size = int(size)
        after = self._ptr + size
        result = self._raw_data[self._ptr:after]
        self._ptr = after

        return result


    # -------------------------------------------------------------------------
    #
    def _read_prolog(self):
        """
        Unpack meta hheader data - magic text and version number
        """
        stx = self._read(8).decode(encoding="utf8")

        v = struct.unpack("=I", self._read(4))[0]
        version = ((v >> 16) & 255, (v >> 8) & 255, v & 255)

        return stx, version


    # -------------------------------------------------------------------------
    #
    def _read_epilog(self):
        """
        Unpack meta header data - magic text and version number
        """
        etx = self._read(4).decode(encoding="utf8")

        return etx


    # -------------------------------------------------------------------------
    #
    def _read_array(self, spec):
        """ Reads an array of input and returns as a tuple.
            spec    - the DataTypeSpec to used
            source  - the input source buffer
            fmt (format) and (element) size must be consistant
            Returns a tuple of homogeneous values.
        """
        number = struct.unpack("=I", self._read(4))[0]

        result = []
        for j in range(number):
            value = struct.unpack(spec.format, self._read(spec.size))[0]
            result.append(value)

        return tuple(result)


    # -------------------------------------------------------------------------
    #
    def _write(self, item):
        self._raw_output.extend(item)


    # -------------------------------------------------------------------------
    #
    def _write_prolog(self):
        """
        Pack meta hheader data - magic text and version number
        """
        stx = asubExecIO.asubExecStx.encode(encoding="utf8")
        self._write(stx)

        version = asubExecIO.asubExecVersion
        v = (version[0] << 16) + (version[1] << 8) + version[2]
        t = struct.pack("=I", v)
        self._write(t)


    # -------------------------------------------------------------------------
    #
    def _write_epilog(self):
        """
        Pack meta hheader data - magic text and version number
        """
        etx = asubExecIO.asubExecEtx.encode(encoding="utf8")
        self._write(etx)


    # -------------------------------------------------------------------------
    #
    def _write_array(self, item, maximum, spec, field):
        """ Write an array to target
            item    - a tuple/list of values.
            maximum - max number of elements from item
            spec    - the DataTypeSpec to used
            field   - for error merssage only
        """
        number = len(item)
        if number > maximum:
            msg = "%s : number of items written reduced from %d to %d"
            self.message(msg % (field, number, maximum))
            number = maximum

        t = struct.pack("=I", number)
        self._write(t)

        for j in range(number):
            # The ftype is its own convert function
            #
            t = struct.pack(spec.format, spec.ftype(item[j]))
            self._write(t)

 # end
