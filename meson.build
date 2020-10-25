project('my-jack-utils', 'c')

cc = meson.get_compiler('c')

m_dep = cc.find_library('m', required:false)
j_dep = cc.find_library('jack', required:true)
executable('run', 'main.c', dependencies : [m_dep, j_dep], c_args : ['-O3', '--std=c11'])