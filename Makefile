
PLATFORM=i386
PLATFORM=hvc-50x
DEBUG=1
LIBHVC=0

all:
	scons platform=$(PLATFORM) libhvc=$(LIBHVC) debug=$(DEBUG) -j5

clean:
	scons platform=$(PLATFORM) -c
