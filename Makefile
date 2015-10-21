
PLATFORM=hvc-50x
PLATFORM=i386
PLATFORM=amd64
DEBUG=0
DEBUG=1
LIBHVC=0

all:
	scons platform=$(PLATFORM) libhvc=$(LIBHVC) debug=$(DEBUG) -j5
	sudo setcap CAP_NET_RAW+eip bin/$(PLATFORM)/netmanage

clean:
	scons platform=$(PLATFORM) -c
