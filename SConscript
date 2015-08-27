# vim:filetype=python

import os
import fnmatch
Import ('env')
Import ('platform')

top = '.'

debug = ARGUMENTS.get('debug',0)
#platform = ARGUMENTS.get('platform', 'i386')
compiler = ARGUMENTS.get('compiler','gcc')

include_path = [
        top + '/src',
        top + '/src/drivers'
        ]

libs = [ '-lrt' ]

build_dir = os.path.join('build',platform)
exe_dir = os.path.join( env.Dir('#').abspath, 'src', 'platform',platform ,'build','bin')
VariantDir( build_dir, '.', duplicate=0 )

build_config = env.Clone()

build_config.Replace( CCFLAGS = ' -Wall -Wconversion -Werror -Wshadow -Wunused-parameter' )
build_config.Replace( LIBS = '' )
if platform == 'i386':
    build_config.Append( CCFLAGS = ' -Wenum-compare -Warray-bounds -m32' )
    build_config.Append( LINKFLAGS = '-m32' )

if platform == 'hvc-50x':
    print "Building for HVC"
    compiler = '/opt/buildroot-gcc342/bin/mipsel-linux-gcc'
    build_config.Replace(CC=compiler)
else:
    build_config.Append(CCFLAGS = ' -fshort-enums -fbounds-check' )

if debug == 0:
    build_config.Append(CCFLAGS = ' -DNDEBUG -O2' )
else:
    build_config.Append(CCFLAGS = ' -g -O0' )

driver_source = []
for root, dirnames, filenames in os.walk('src/drivers' ):
    for filename in fnmatch.filter(filenames, '*.h' ):
        driver_source.append( os.path.join(root,filename).split('/',1)[1] )

driver_source.sort()
driver_config = open( 'src/driver_config.h', 'w' )
for filename in driver_source:
    driver_config.write('#include "' + filename + '"\n' )
driver_config.close()

driver_setup = []
for root, dirnames, filenames in os.walk( 'src/drivers' ):
    for filename in fnmatch.filter( filenames, '*.w' ):
        driver_setup.append( os.path.join( root,filename ).split('/',1)[1] )

driver_setup.sort()
driver_config = open( 'src/driver_setup.h', 'w' )
for filename in driver_setup:
    driver_config.write( '#include "' + filename + '"\n' )
driver_config.close()

main_source = []
for root, dirnames, filenames in os.walk( 'src' ):
    for filename in fnmatch.filter( filenames, '*.c' ):
        main_source.append( os.path.join( top,build_dir,root,filename ))

netmanage_binary = os.path.join(top,exe_dir,'netmanage')

build_config.Clean( 'src/driver.c' , [ 'src/driver_setup.h', 'src/driver_config.h' ] )
build_config.Clean( 'src/netmanage.c', netmanage_binary )

build_config.Append( CPPPATH = include_path )
build_config.Program( netmanage_binary, source=main_source, LIBS = libs )
Default( netmanage_binary )

