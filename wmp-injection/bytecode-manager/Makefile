# build helloworld executable when user executes "make"
OBJECTS = bytecode-manager.o libb43.o hex2int.o dataParser.o messageHandler.o auto-bytecode.o bytecode-work.o

# CFLAGS are the flags to use when *compiling*
CFLAGS= -m32
# LFLAGS are the flags to use when *linking*
#LFLAGS=-ldl


#bytecode-manager: bytecode-manager.o
bytecode-manager: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(CFLAGS) -o bytecode-manager

bytecode-manager.o: bytecode-manager.c
	$(CC) $(CFLAGS) -c bytecode-manager.c
libb43.o: libb43.c
	$(CC) $(CFLAGS) -c libb43.c
hex2int.o: hex2int.c
	$(CC) $(CFLAGS) -c hex2int.c
dataParser.o: dataParser.c
	$(CC) $(CFLAGS) -c dataParser.c
messageHandler.o: messageHandler.c
	$(CC) $(CFLAGS) -c messageHandler.c
auto-bytecode.o: auto-bytecode.c
	$(CC) $(CFLAGS) -c auto-bytecode.c
bytecode-work.o: bytecode-work.c
	$(CC) $(CFLAGS) -c bytecode-work.c
	
# remove object files and executable when user executes "make clean"
clean:
	- rm *.o bytecode-manager metamac

MMCFLAGS=-std=gnu99 -Wall -g $(shell pkg-config libxml-2.0 --cflags)
MMLFLAGS=-lm $(shell pkg-config libxml-2.0 --libs) -pthread
MMOBJECTS=metamac.o protocols.o parseconfig.o queue.o metamac-manager.o libb43.o hex2int.o dataParser.o bytecode-work.o

metamac.o: libb43.h metamac.h metamac.c
	$(CC) $(CFLAGS) $(MMCFLAGS) -c metamac.c
protocols.o: metamac.h protocols.h protocols.c
	$(CC) $(CFLAGS) $(MMCFLAGS) -c protocols.c
parseconfig.o: metamac.h parseconfig.h parseconfig.c
	$(CC) $(CFLAGS) $(MMCFLAGS) -c parseconfig.c
queue.o: queue.h queue.c
	$(CC) $(CFLAGS) $(MMCFLAGS) -c queue.c
metamac-manager.o: metamac.h parseconfig.h metamac-manager.c
	$(CC) $(CFLAGS) $(MMCFLAGS) -c metamac-manager.c
metamac: $(MMOBJECTS)
	$(CC)  $(MMOBJECTS) $(MMLFLAGS)  $(CFLAGS) -o metamac

tsfrecorder.o: libb43.h tsfrecorder.c
	$(CC) $(CFLAGS) $(MMCFLAGS) -c tsfrecorder.c
tsfrecorder: tsfrecorder.o libb43.o hex2int.o dataParser.o bytecode-work.o
	$(CC) tsfrecorder.o libb43.o hex2int.o dataParser.o bytecode-work.o $(MMLFLAGS)  $(CFLAGS) -o tsfrecorder