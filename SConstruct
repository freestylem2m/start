# vim:filetype=python

import os
import fnmatch

top = Dir('#').path
home = os.environ["HOME"]

debug = ARGUMENTS.get('debug','0')
platform = ARGUMENTS.get('platform', 'i386')
compiler = ARGUMENTS.get('compiler','gcc')
uselibhvc  = ARGUMENTS.get('libhvc','0')

include_path = [
        top + '/src',
        top + '/src/drivers',
        ]

libs = [ ]

hvc_inc = [
        os.path.join( home, 'git/FreestyleRalinkSDK/RT288x_SDK/source/lib/libnvram' ),
        os.path.join( home, 'git/FMEBuild/src/platform/hvc-50x/libHVC' ),
        ]

hvc_objs = [
        os.path.join( home, 'git/FMEBuild/src/platform/hvc-50x/libHVC/libHVC.o' ),
        ]

hvc_libpath = [
        '-L' + os.path.join( home, 'git/FreestyleRalinkSDK/RT288x_SDK/source/lib/libnvram' ),
        ]
hvc_libs = [
        '-lnvram',
        ]

additional_objects = []

build_dir = os.path.join('build',platform)
exe_dir = os.path.join('bin',platform)
VariantDir( build_dir, '.', duplicate=0 )

build_config = Environment(ENV = os.environ)

build_config.Append(CCFLAGS = '-Wall -Wconversion -Werror -Wunused-parameter')

if platform == 'i386':
    build_config.Append(CCFLAGS = '-Wenum-compare -Wshadow -Warray-bounds -m32' )
    build_config.Append(LINKFLAGS = '-m32' )
    libs.append( '-lrt' )

if platform == 'amd64':
    build_config.Append(CCFLAGS = '-Wenum-compare -Wshadow -Warray-bounds' )
    libs.append( '-lrt' )

if platform == 'hvc-50x':
    print "Building for HVC"
    compiler = '/opt/buildroot-gcc342/bin/mipsel-linux-gcc'
    build_config.Replace(CC=compiler)
    if uselibhvc != '0':
        include_path.append( hvc_inc )
        build_config.Append(CCFLAGS = '-DHVCLIBS' )
        build_config.Append(LINKFLAGS = hvc_libpath )
        additional_objects.append( hvc_objs )
        libs.append( hvc_libs )

if debug == '0':
    build_config.Append(CCFLAGS = '-DNDEBUG -O2' )
    build_config.Append(CCFLAGS = '-fshort-enums -fbounds-check' )
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

netmanage_binary = os.path.join(top,exe_dir,'netmanage')


build_config.Clean( 'src/driver.c' , [ 'src/driver_setup.h', 'src/driver_config.h' ] );
build_config.Append( CPPPATH = include_path )

target = build_config.Program( netmanage_binary, source = [ main_source, additional_objects ], LIBS = libs )


if platform == 'hvc-50x':
    dest_dir = os.path.join( home, 'fme' )
    build_publish = Command( os.path.join(dest_dir, 'netmanage'), netmanage_binary, Copy("$TARGET","$SOURCE"))
    Depends(build_publish, target)
    Default(build_publish, target)

#Return("build_config");
