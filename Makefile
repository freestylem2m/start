
PLATFORM=i386
PLATFORM=hvc-50x
DEBUG=0

all:
	scons platform=$(PLATFORM) debug=$(DEBUG) -j5

clean:
	scons platform=$(PLATFORM) -c
