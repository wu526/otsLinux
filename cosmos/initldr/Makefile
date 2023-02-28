###################################################################
#		主控自动化编译配置文件 Makefile
#				彭东  
###################################################################

MAKEFLAGS = -sR

MKDIR = mkdir
RMDIR = rmdir
CP = cp
CD = cd
DD = dd
RM = rm

BUILD_PATH = ./build
MAKE = make
INITLDRMFLGS = -C $(BUILD_PATH) -f initldr.mk
INITLDRLINKMFLGS = -C $(BUILD_PATH) -f ldrlink.mk
SRCFILE = $(BOOTEXCIMG) $(KRNLEXCIMG) $(LDEREXCIMG) $(SHELEXCIMG)
RSRCFILE = $(BOOTEXCIMG) $(KRNLEXCIMG) $(LDEREXCIMG) $(SHELEXCIMG) $(VDIFNAME) $(VMDKFNAME)

.PHONY : build print clean all

build: clean print all
	@echo "initldr dir build CMD: depend on 【clean print all】"

all:
	@echo "initldr dir all CMD: $(MAKE) $(INITLDRMFLGS)"
	$(MAKE) $(INITLDRMFLGS)

	@echo "initldr dir all CMD: $(MAKE) $(INITLDRLINKMFLGS)"
	$(MAKE) $(INITLDRLINKMFLGS)

clean:
	@echo "initldr dir clean CMD: $(CD) $(BUILD_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.map *.lib *.btoj *.vdi *vmdk *.eki *.elf"
	$(CD) $(BUILD_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.map *.lib *.btoj *.vdi *vmdk *.eki *.elf
	
print:
	#@echo '*********正在开始编译构建系统*************'




