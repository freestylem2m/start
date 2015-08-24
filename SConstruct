# vim:filetype=python

import os
import fnmatch

top = Dir('#').path

debug = ARGUMENTS.get('debug',0)
platform = ARGUMENTS.get('platform', 'i386')
compiler = ARGUMENTS.get('compiler','gcc')

include_path = [
        top + '/src',
        top + '/src/drivers'
        ]

build_dir = os.path.join('build',platform)
VariantDir( build_dir, '.', duplicate=0 )

build_config = Environment(ENV = os.environ)

build_config.Append(CCFLAGS = '-Wall -Wconversion -Werror -Wshadow -Wunused-parameter')
if platform == 'i386':
    build_config.Append(CCFLAGS = '-Wenum-compare -Warray-bounds -m32' )
    build_config.Append(LINKFLAGS = '-m32' )

if platform == 'hvc-50x':
    print "Building for HVC"

if debug == 0:
    build_config.Append(CCFLAGS = '-fshort-enums -fbounds-check' )

if debug == 0:
    build_config.Append(CCFLAGS = '-DNDEBUG -O2' )
else:
    build_config.Append(CCFLAGS = '-g -O0' )

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

objs = [];

main_source = []
for root, dirnames, filenames in os.walk( 'src' ):
    for filename in fnmatch.filter( filenames, '*.c' ):
        main_source.append( os.path.join( top,build_dir,root,filename ))

print main_source

netmanage_binary = os.path.join(top,build_dir,'netmanage')

build_config.Clean( 'src/driver.c' , [ 'src/driver_setup.h', 'src/driver_config.h' ] );
build_config.Append( CPPPATH = include_path )
build_config.Program( netmanage_binary, main_source )

#Return("build_config");
