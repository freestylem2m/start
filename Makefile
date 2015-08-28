
PLATFORM=i386
PLATFORM=hvc-50x

all:
	scons platform=$(PLATFORM) debug=1 -j5

clean:
	scons platform=$(PLATFORM) -c
