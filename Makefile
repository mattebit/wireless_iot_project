# For TMote Sky (emulated in Cooja) use the following target
TARGET ?= sky

# For Zolertia Firefly (testbed) use the following target and board
# Don't forget to make clean when you are changing the board
ifeq ($(TARGET), zoul)
	BOARD	?= firefly
	LDFLAGS += -specs=nosys.specs # For Zolertia Firefly only
	PROJECT_SOURCEFILES += deployment.c
endif


DEFINES=PROJECT_CONF_H=\"project-conf.h\"
CONTIKI_PROJECT = app

PROJECT_SOURCEFILES += nd.c nd-rdc.c netstack.c nd-netstack.c

# Tool to estimate node duty cycle 
PROJECTDIRS += tools
PROJECT_SOURCEFILES += simple-energest.c

all: $(CONTIKI_PROJECT)

CONTIKI_WITH_RIME=0
CONTIKI_WITH_IPV6=0

CONTIKI ?= ../../contiki
include $(CONTIKI)/Makefile.include
