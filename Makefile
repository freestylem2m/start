
PLATFORM=i386
PLATFORM=hvc-50x
DEBUG=1

all:
	scons platform=$(PLATFORM) debug=$(DEBUG) -j5

clean:
	scons platform=$(PLATFORM) -c
