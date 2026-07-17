# HINT: python2 print_pb.py _generated_prim.h bc.proto < premade/bc > premade/bc.listing

import re, sys

import compile_proto

# "// :ByteCode: BC_LitStr 4 ['nth']"
# "// :ByteCode: BC_Plus 5 []"
BC_DESC_MATCHER = re.compile(
    '^// :ByteCode: BC_([A-Za-z0-9]+) ([0-9]+) [[](.*)[]]')
bcp = None


class MessagePrinter:
    def __init__(self, v, numbers):
        self.v = v
        self.i = 0
        self.pre = '  '
        self.numbers = numbers

    def VarInt(self):
        shift = 0
        z = 0
        while True:
            a = self.v[self.i]
            self.i += 1
            z += ((a & 127) << shift)
            shift += 7
            if not (a & 128):
                return z

    def Mess(self):
        prev_ttag = -1
        count = 0
        while True:
            if self.i >= len(self.v):
                raise Exception('Expected another tag in Message')
            ttag = self.v[self.i]
            self.i += 1
            if not ttag:
                print( '%s ;' % self.pre)
                return self.i

            if ttag == prev_ttag:
                count += 1
            else:
                count = 0
                prev_ttag = ttag

            tag, kind = ttag >> 3, ttag & 7
            assert 1 <= tag <= 29
            assert 1 <= kind <= 3
            fullname = self.numbers.get(ttag, '??')

            if kind == 1:
                x = self.VarInt()
                print( '%s %d. (%02x) %d %s: int %d' % (self.pre, count, ttag, tag,
                                                   fullname, x))
            elif kind == 2:
                n = self.VarInt()
                x = self.v[self.i:self.i + n]
                self.i += n
                s = ''.join([chr(e) for e in x])
                print( '%s %d. (%02x) %d %s: str %s' % (self.pre, count, ttag, tag,
                                                   fullname, repr(s)))
                if bcp and fullname.endswith('bytecode'):
                    bcp.Render([ord(e) for e in s], self.pre)
            elif kind == 3:
                print( '%s %d. (%02x) %d %s: message:' % (self.pre, count, ttag, tag,
                                                     fullname))
                old = self.pre
                self.pre += '  '
                self.Mess()
                self.pre = old
            else:
                raise 0


class BytecodePrinter(object):
    def __init__(self, lines):
        self.bc = {}
        for line in lines:
            m = BC_DESC_MATCHER.match(line)
            if m:
                op, opnum, arg = m.groups()
                self.bc[int(opnum)] = (op, arg)

    def Render(self, codes, pre):
        try:
            i = 6
            codes = codes[6:]
            while codes:
                offset = i
                c, i = codes.pop(0), i + 1
                op, arg = self.bc[c]
                if arg:
                    args = []
                    for label in arg.split(','):
                        a, i = codes.pop(0), i + 1
                        args.append(a)
                    print( '%s  [%2d] %2d  %s %s' % (pre, offset, c, op, ' '.join(
                        str(e) for e in args)))
                else:
                    print( '%s  [%2d] %2d  %s' % (pre, offset, c, op))
        except IndexError as ex:
            print( '%s  EXCEPTION while rendering bytecodes: %s' % (pre, ex))


if __name__ == '__main__':
    numbers = {}
    for arg in sys.argv[1:]:
        if arg.endswith('.proto'):
            lines = [line for line in open(arg)]
            numbers.update(
                compile_proto.EasyProtoParser().DoLines(lines).Numbers())
        elif arg.endswith('.h'):
            lines = [line for line in open(arg)]
            bcp = BytecodePrinter(lines)
        else:
            raise Exception('bad command line arg: %s' % repr(arg))

    vec = [ch for ch in sys.stdin.buffer.read()]
    i = MessagePrinter(vec, numbers).Mess()
    if i < len(vec):
        print( 'Unused:', vec[i:])
