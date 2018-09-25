#######################################################################################################################
# Copyright (C) Virtuosys Limited - All Rights Reserved.
# Unauthorised copying of this file, via any medium is strictly prohibited. Proprietary and confidential. [2015]-[2019]
#######################################################################################################################
#
#   File:           platform_program.mk
#
#   Summary:        Makefile to build platform executables including pb_monitor
#
#   Element:        IES
#
#   Platform:       Linux
#
#   Description:    # Macros
# 					$@ is the name of the file being generated (or target)
# 					$< is the name of the first dependency
# 					The -c flag generates the .o
#   Documentation:
#
#   References:
#
#######################################################################################################################

# Setup tools
include $(MAKE_TOP)/ies-tools.mk

# cross compiler
CC=$(CROSS_COMPILE)gcc

IDIR   = -Iinclude

SOURCES := pb_monitor.c
OBJECTS=$(SOURCES:.c=.o)

EXECUTABLES=pb_monitor

CFLAGS  += $(IDIR)
LIB    =  -lrt
LDFLAGS += -Wall

#all: $(SOURCES) $(EXECUTABLES)

$(EXECUTABLES): $(OBJECTS)
	@echo Linking - $(CC) $<
	$(CC) $(LDFLAGS) $(LIB) $(OBJECTS) -o $@
	chmod a+x $(EXECUTABLES)
	$(CP) -a $(EXECUTABLES) $(PLATFORM_INSTALL_DIR)

%.o: %.c
	@echo Compiling - $(CC) $<
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY: all pb_monitor

clean:	#clean
	rm -rf *.o
	rm $(EXECUTABLES)
