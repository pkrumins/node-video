import Options
from os import unlink, symlink, popen
from os.path import exists 

srcdir = "."
blddir = "build"
VERSION = "0.0.1"

def set_options(opt):
  opt.tool_options("compiler_cxx")

def configure(conf):
  conf.check_tool("compiler_cxx")
  conf.check_tool("node_addon")
  conf.check(lib='ogg', libpath=['/lib', '/usr/lib', '/usr/local/lib', '/usr/local/libogg/lib', '/usr/local/pkg/libogg/lib', '/usr/local/pkg/libogg-1.2.0/lib'])
  conf.check(lib='theoradec', libpath=['/lib', '/usr/lib', '/usr/local/lib', '/usr/local/libtheora/lib', '/usr/local/pkg/libtheora/lib', '/usr/local/pkg/libtheora-1.1.1/lib'])
  conf.check(lib='theoraenc', uselib='THEORADEC', libpath=['/lib', '/usr/lib', '/usr/local/lib', '/usr/local/libtheora/lib', '/usr/local/pkg/libtheora/lib', '/usr/local/pkg/libtheora-1.1.1/lib'])

def build(bld):
  obj = bld.new_task_gen("cxx", "shlib", "node_addon")
  obj.target = "video"
  obj.source = "video.cc"
  obj.uselib = "OGG THEORAENC THEORADEC"

def shutdown():
  if Options.commands['clean']:
    if exists('video.node'): unlink('video.node')
  else:
    if exists('build/default/video.node') and not exists('video.node'):
      symlink('build/default/video.node', 'video.node')

