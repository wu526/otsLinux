###################################################################
#		krnllink自动化编译配置文件 Makefile               #
#				彭东  ＠ 2013.06.15.16.30	  #
###################################################################
MAKEFLAGS =

include krnlbuidcmd.mk
include krnlobjs.mk

.PHONY : all everything build_kernel
all: build_kernel
[@echo "link.mk all CMD: depend on 【build_kernel】"

build_kernel: everything
#$(LDER_EXC_BIN)

everything:
[@echo "link.mk CMD: $(LD) $(LDFLAGS) -o $(KERNL_MK_ELFF_FILE) $(BUILD_MK_LINK_OBJS)"
[$(LD) $(LDFLAGS) -o $(KERNL_MK_ELFF_FILE) $(BUILD_MK_LINK_OBJS)

[@echo "link.mk CMD: $(OBJCOPY) $(OJCYFLAGS) $(KERNL_MK_ELFF_FILE) $(KERNL_MK_BINF_FILE)"
[$(OBJCOPY) $(OJCYFLAGS) $(KERNL_MK_ELFF_FILE) $(KERNL_MK_BINF_FILE)
	
#$(BOOT_EXC_ELF) $(BOOT_EXC_BIN)

$(BOOT_EXC_ELF): $(LMOSEM_LINK_OBJS)
[@echo "link.mk $(BOOT_EXC_ELF) CMD: $(LD) $(LDFLAGS) -o $(BOOT_EXC_ELF)  $(LMOSEM_LINK_OBJS)"
[$(LD) $(LDFLAGS) -o $(BOOT_EXC_ELF)  $(LMOSEM_LINK_OBJS)

$(BOOT_EXC_BIN):
[@echo "link.mk $(BOOT_EXC_BIN) CMD: <NULL>"

//$(BUILD_MK_LMKR_EBIN): $(BUILD_MK_LMKR_LINK)
//	$(LD) $(KERNELLDFLAGS) -o $(BUILD_MK_LMKR_EBIN) $(BUILD_MK_LMKR_LINK)