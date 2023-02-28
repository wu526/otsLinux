
MAKEFLAGS = -s
HEADFILE_PATH = ../include/
KRNLBOOT_PATH = ../ldrkrl/
CCBUILDPATH	= $(KRNLBOOT_PATH)

include krnlbuidcmd.mh
include ldrobjs.mh

.PHONY : all everything  build_kernel

all: build_kernel 
	@echo "./initldr/build dir all CMD: depend on 【build_kernel】"

build_kernel:everything
	@echo "./initldr/build dir build_kernel CMD: depend on 【everything】"
	
everything : $(INITLDRIMH_OBJS) $(INITLDRKRL_OBJS) $(INITLDRSVE_OBJS)
	@echo "./initldr/build dir everything CMD: depend on 【$(INITLDRIMH_OBJS) $(INITLDRKRL_OBJS) $(INITLDRSVE_OBJS)】"

include krnlbuidrule.mh
