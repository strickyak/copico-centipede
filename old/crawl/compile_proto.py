# easy_proto_compiler.py
import sys, traceback


class Field(object):
    def __init__(self, name, type_, rep, num, tag):
        self.name = name
        self.type = type_
        self.rep = rep
        self.num = num
        self.tag = tag

    def __repr__(self):
        return repr(vars(self))


class Message(object):
    def __init__(self, name):
        self.name = name
        self.fields = []

    def __repr__(self):
        return repr(vars(self))


class EasyProtoParser(object):
    def __init__(self):
        self.m = None
        self.messages = {}

    def DoLines(self, lines):
        i = 1
        for line in lines:
            line = line.split('#')[0]
            line = line.strip()
            words = line.split()
            if words:
                try:
                    self.DoLine(words)
                except:
                    raise Exception('==== In line %d (%s): ====\n%s' %
                                    (i, line, traceback.format_exc()))
        self.Finish()
        return self

    def Finish(self):
        if self.m:
            self.messages[self.m.name] = self.m

    def DoLine(self, words):
        if words[0] == 'message':
            if self.m:
                self.messages[self.m.name] = self.m
            self.m = Message(words[1])
            return

        rep = False
        if words[0] == 'repeated':
            rep = True
            words = words[1:]

        assert words[2] == '='
        kind = 1 if words[0] == 'int' else 2 if words[0] == 'str' else 3
        num = int(words[3])
        assert 1 <= num <= 29
        tag = 8 * num + kind

        self.m.fields.append(Field(words[1], words[0], rep, num, tag))

    def Numbers(self):
        numbers = {}
        for name, m in sorted(self.messages.items()):
            for f in m.fields:
                fullname = '%s_%s' % (name, f.name)
                numbers[f.tag] = fullname
        return numbers


if __name__ == '__main__':
    lang = 'c'
    if '-p' in sys.argv: lang = 'py'
    if '-k' in sys.argv: lang = 'const'

    lines = [line for line in sys.stdin]
    messages = EasyProtoParser().DoLines(lines).messages
    for name, m in sorted(messages.items()):
        for f in m.fields:
            if lang == 'c':
                print( '#define %30s %3d  // %s %s = %d' % ('%s_%s' % (name,
                                                                      f.name),
                                                           f.tag, 'repeated'
                                                           if f.rep else '',
                                                           f.type, f.num))
            elif lang == 'py':
                print( '%-30s = %3d  # %s %s = %d' % ('%s_%s' % (name, f.name),
                                                     f.tag, 'repeated'
                                                     if f.rep else '', f.type,
                                                     f.num))
            elif lang == 'const':
                print( 'T.%s_%s=%d' % (name, f.name, f.tag))
