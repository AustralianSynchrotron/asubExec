# $File: //ASP/tec/epics/asubExec/trunk/asubExecSup/src/asubExec.py $
# $Revision: #4 $
# $DateTime: 2019/08/10 21:07:07 $
# Last checked in by: $Author: starritt $
#

import sys
import struct
from collections import namedtuple


class asubExecIO(object):
    """ asubExec IO utility class
        This class may be used to unpack data from standard input as provided
        by the asubExec module and pack the result to standard output.
        It must be consistant with the asubExecDataType out of asubExec.h
        Still to address STRING and ENUM parameters.
    """


    # from asubExecCommon.h
    #
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

    # This corresponds to INPA..INPU and OUTA..OUTU
    #
    Keys = "abcdefghijklmnopqrstu"

    # size is element size in bytes
    # format is the struct format string - we use native endianess
    #
    DataTypeSpec = namedtuple("DataTypeSpec", ('name', 'size', 'format', 'type'))

    menuMap = {
        asubExecTypeSTRING:  DataTypeSpec("STRING", 40, None,  str),
        asubExecTypeCHAR:    DataTypeSpec("CHAR",   1,  "=b",  int),
        asubExecTypeUCHAR:   DataTypeSpec("UCHAR",  1,  "=B",  int),
        asubExecTypeSHORT:   DataTypeSpec("SHORT",  2,  "=h",  int),
        asubExecTypeUSHORT:  DataTypeSpec("USHORT", 2,  "=H",  int),
        asubExecTypeLONG:    DataTypeSpec("LONG",   4,  "=i",  int),
        asubExecTypeULONG:   DataTypeSpec("ULONG",  4,  "=I",  int),
        asubExecTypeFLOAT:   DataTypeSpec("FLOAT",  4,  "=f",  float),
        asubExecTypeDOUBLE:  DataTypeSpec("DOUBLE", 8,  "=d",  float),
        asubExecTypeENUM:    DataTypeSpec("ENUM",   2,  None,  int),
        asubExecTypeINT64:   DataTypeSpec("INT64",  8,  "=q",  int),
        asubExecTypeUINT64:  DataTypeSpec("UINT64", 8,  "=Q",  int)
    }

    def __init__(self):
        self._input_data = None
        self._output_spec = {}
        self._ptr = None
        self._output_len = None

    @property
    def input_data(self):
        """ Provides the received input data.
            The input data is a dictionary, keyed by 'inpa', inpb', ... 'inpu',
            with each dictionary value a tuple of float, int or str.
        """
        return self._input_data

    @property
    def output_spec(self):
        """ Specifies that type and number of elements to be send to standard output """
        return self._output_spec

    @property
    def output_len(self):
        """ Output length in bytes """
        return self._output_len

    # -------------------------------------------------------------------------
    #
    def unpack(self, source=sys.stdin.buffer):
        """ Unpacks the input data, which is subsequenctly available using the
            input_data property. 
            unpack returns True if successfull.
        """
        self._input_data = None
        self._output_spec = {}
        self._ptr = 0

        data = source.read()

        arguments = {}
        for key in asubExecIO.Keys:
            field = "inp%s" % key

            # Read the data type
            #
            kind = struct.unpack("=H", data[self._ptr:self._ptr + 2])[0]
            self._ptr += 2

            spec = asubExecIO.menuMap.get(kind, None)
            if spec is None:
                self.message("%s Unhandled type %s\n" % (field, kind))
                return False

#           self.message (key, kind, spec)

            fmt = spec.format
            size = spec.size

            if fmt is None:
                self.message("%s Unhandled type %s\n" % (field, spec.name))
                return False

            item = self._read_array(fmt, size, data)
            arguments[field] = item

        self._input_data = arguments

        arguments = {}
        for key in asubExecIO.Keys:
            field = "out%s" % key

            kind = struct.unpack("=H", data[self._ptr:self._ptr + 2])[0]
            self._ptr += 2
            number = struct.unpack("=I", data[self._ptr:self._ptr + 4])[0]
            self._ptr += 4
            spec = {'kind': kind, 'number': number}
            arguments[field] = spec

        self._output_spec = arguments
        return True

    # -------------------------------------------------------------------------
    #
    def pack(self, output, target=sys.stdout.buffer):
        """ Packs the given output data into the target buffer.
            The output data is a dictionary, keyed by 'outa', outb', ... 'outu',
            with each dictionary value a tuple or list of floats and/or ints and/or strs.
            The individual element values are first caste to one of float, int or str
            prior to packing. 
            If a output data dictionary does not provide the key value, a the
            default replacement value is (0.0, ) - i.e. a single float.
            pack returns True if successfull.
        """

        data = bytearray()

        for key in asubExecIO.Keys:
            field = "out%s" % key

            item = output.get(field, None)
            if item is None:
                item = (0.0, )

            if not isinstance(item, tuple) and not isinstance(item, list):
                self.message("%s Expected tuple/list, received type %s" % (field, type(item)))
                return False

            spec = self._output_spec[field]
            kind = spec['kind']
            spec = asubExecIO.menuMap.get(kind, None)
            if spec is None:
                self.message("%s Unhandled type %s\n" % (field, kind))
                return False

            t = struct.pack("=H", kind)
            data.extend(t)
            fmt = spec.format
            convert = spec.type
            # The type is its own convert function
            self._write_array(item, fmt, convert, data)

        target.write(data)
        self._output_len = len(data)
        return True

    # -------------------------------------------------------------------------
    #
    def message(self, *args):
        """ writes text to stderr - appends a newline 
        """
        n = len(args)
        f = ("%s " * n).strip() + "\n"
        sys.stderr.write(f % args)

    # -------------------------------------------------------------------------
    #
    def _read_array(self, fmt, size, source):
        """ Reads an array of input and returns as a tuple.
            fmt     - the struct.pack format
            size    - the buffered element byte size
            source  - the input source buffer
            fmt (format) and (element) size must be consistant
            Returns a tuple of homogeneous values.
        """
        number = struct.unpack("=I", source[self._ptr:self._ptr + 4])[0]
        self._ptr += 4

        result = []
        for j in range(number):
            value = struct.unpack(fmt, source[self._ptr:self._ptr + size])[0]
            self._ptr += size
            result.append(value)

        return tuple(result)

    # -------------------------------------------------------------------------
    #
    def _write_array(self, item, fmt, convert, target):
        """ Write an array to target 
            item    - a tuple/list of values.
            fmt     - the struct.pack format
            convert - callable that returns a value compatible with fmt
            target  - a bytearray object
        """
        number = len(item)
        t = struct.pack("=I", number)
        target.extend(t)

        for j in range(number):
            t = struct.pack(fmt, convert(item[j]))
            target.extend(t)

 # end
