# vim:set filetype=python:

prog = Environment(CCFLAGS = '-Wall -g ')

prog.CacheDir('cache')

slist = ['mope.c']

mope = prog.Program(target = 'mope', source = slist)

Default(mope)
