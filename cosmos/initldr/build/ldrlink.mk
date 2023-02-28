include krnlbuidcmd.mh
include ldrobjs.mh

.PHONY : all everything build_kernel

all: build_kernel
	@echo "./initldr/build ldrlink.mk all CMD: depend on 【build_kernel】"

#INITLDR
build_kernel: everything build_bin
	@echo "./initldr/build ldrlink.mk build_kernel CMD: depend on 【everything build_bin】"

everything : $(INITLDRIMH_ELF) $(INITLDRKRL_ELF) $(INITLDRSVE_ELF)
	@echo "./initldr/build ldrlink.mk everything CMD: depend on 【$(INITLDRIMH_ELF) $(INITLDRKRL_ELF) $(INITLDRSVE_ELF)】"

build_bin: $(INITLDRIMH) $(INITLDRKRL) $(INITLDRSVE)
	@echo "./initldr/build ldrlink.mk build_bin CMD: depend on 【$(INITLDRIMH) $(INITLDRKRL) $(INITLDRSVE)】"

$(INITLDRIMH_ELF): $(INITLDRIMH_LINK)
	@echo "./initldr/build ldrlink.mk $(INITLDRIMH_ELF) CMD: $(LD) $(LDRIMHLDFLAGS) -o $@ $(INITLDRIMH_LINK)"
	$(LD) $(LDRIMHLDFLAGS) -o $@ $(INITLDRIMH_LINK)

$(INITLDRKRL_ELF): $(INITLDRKRL_LINK)
	@echo "./initldr/build ldrlink.mk $(INITLDRKRL_ELF) CMD: $(LD) $(LDRKRLLDFLAGS) -o $@ $(INITLDRKRL_LINK)"
	$(LD) $(LDRKRLLDFLAGS) -o $@ $(INITLDRKRL_LINK)

$(INITLDRSVE_ELF): $(INITLDRSVE_LINK)
	@echo "./initldr/build ldrlink.mk $(INITLDRSVE_ELF) CMD: $(LD) $(LDRSVELDFLAGS) -o $@ $(INITLDRSVE_LINK)"
	$(LD) $(LDRSVELDFLAGS) -o $@ $(INITLDRSVE_LINK)

$(INITLDRIMH): $(INITLDRIMH_ELF)
	@echo "./initldr/build ldrlink.mk $(INITLDRIMH) CMD: $(OBJCOPY) $(OJCYFLAGS) $< $@"
	$(OBJCOPY) $(OJCYFLAGS) $< $@

$(INITLDRKRL): $(INITLDRKRL_ELF)
	@echo "./initldr/build ldrlink.mk $(INITLDRKRL) CMD: $(OBJCOPY) $(OJCYFLAGS) $< $@"
	$(OBJCOPY) $(OJCYFLAGS) $< $@

$(INITLDRSVE): $(INITLDRSVE_ELF)
	@echo "./initldr/build ldrlink.mk $(INITLDRSVE) CMD: $(OBJCOPY) $(OJCYFLAGS) $< $@"
	$(OBJCOPY) $(OJCYFLAGS) $< $@