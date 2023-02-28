###################################################################
#		主控自动化编译配置文件 Makefile			                      #
#				彭东                                               #
###################################################################

# MKDIR = mkdir
# RMDIR = rmdir
CP = cp
CD = cd

# createimg rule
DD = dd  
RM = rm

# KIMG rule
LKIMG = ./lmoskrlimg -m k

VM = qemu-system-x86_64
DBUGVM = bochs -q
IMGTOVDI = qemu-img convert -f raw -O vdi
IMGTOVMDK = qemu-img convert -f raw -O vmdk

VVMRLMOSFLGS = -C $(BUILD_PATH) -f vbox.mkf
VBOXVMFLGS = -C $(VM_PATH) -f vbox.mkf
VMFLAGES = -smp 4 -hda $(VMDKFNAME) -m 256 -enable-kvm

IMGSECTNR = 204800
PHYDSK = /dev/sdb
VDIFNAME = hd.vdi
VMDKFNAME = hd.img

KRNLEXCIMG = Cosmos.bin
MAKE = make
MAKEFLAGS = -sR
X86BARD = -f ./Makefile.x86

LOGOFILE = logo.bmp background.bmp
FONTFILE = font.fnt
BUILD_PATH = ./build
EXKNL_PATH = ./exckrnl
DSTPATH = ../exckrnl
RELEDSTPATH = ../release
INITLDR_BUILD_PATH =./initldr/build/
INITLDR_PATH =./initldr/
CPLILDR_PATH =./release/
INSTALL_PATH =/boot/
INSTALLSRCFILE_PATH =./release/Cosmos.eki

# SRCFILE $(KRNLEXCIMG)=Cosmos.bin
SRCFILE = $(BOOTEXCIMG) $(KRNLEXCIMG) $(LDEREXCIMG) $(SHELEXCIMG)
RSRCFILE = $(BOOTEXCIMG) $(KRNLEXCIMG) $(LDEREXCIMG) $(SHELEXCIMG) #$(VDIFNAME) $(VMDKFNAME)

INITLDRIMH = initldrimh.bin
INITLDRKRL = initldrkrl.bin
INITLDRSVE = initldrsve.bin

CPLILDRSRC= $(INITLDR_BUILD_PATH)$(INITLDRSVE) $(INITLDR_BUILD_PATH)$(INITLDRKRL) $(INITLDR_BUILD_PATH)$(INITLDRIMH)

LKIMG_INFILE = $(INITLDRSVE) $(INITLDRKRL) $(KRNLEXCIMG) $(FONTFILE) $(LOGOFILE)
.PHONY : install x32 build print clean all krnlexc cpkrnl wrimg phymod exc vdi vdiexc cprelease release createimg

build: clean print all

all:
	@echo "Main Makefile CMD: $(MAKE) $(X86BARD)"
	$(MAKE) $(X86BARD)

#cpkrnl cprelease
release: clean all cplmildr cpkrnl cprelease KIMG
	@echo "release CMD: depend on 【clean all cplmildr cpkrnl cprelease KIMG】"

clean:
	@echo "Clean CMD: $(CD) $(INITLDR_PATH); $(MAKE) clean"
	$(CD) $(INITLDR_PATH); $(MAKE) clean

	@echo "clean CMD: $(CD) $(BUILD_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.map *.lib *.btoj *.vdi *.elf *vmdk *.lds *.mk *.mki krnlobjs.mh"
	$(CD) $(BUILD_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.map *.lib *.btoj *.vdi *.elf *vmdk *.lds *.mk *.mki krnlobjs.mh

	@echo "clean CMD: $(CD) $(EXKNL_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.map *.lib *.btoj *.vdi *.elf *vmdk *.lds *.mk *.mki krnlobjs.mh"
	$(CD) $(EXKNL_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.map *.lib *.btoj *.vdi *.elf *vmdk *.lds *.mk *.mki krnlobjs.mh

	@echo "clean CMD: $(CD) $(CPLILDR_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.eki *.map *.lib *.btoj *.elf *.vdi *vmdk *.lds *.mk *.mki krnlobjs.mh"
	$(CD) $(CPLILDR_PATH); $(RM) -f *.o *.bin *.i *.krnl *.s *.eki *.map *.lib *.btoj *.elf *.vdi *vmdk *.lds *.mk *.mki krnlobjs.mh

print:
	#@echo '*********正在开始编译构建系统*************'

krnlexc: cpkrnl wrimg exc
	@echo "krnlexc CMD: depend on 【cpkrnl wrimg exc】"

dbugkrnl: cpkrnl wrimg dbugexc
	@echo "dbugkrnl CMD: depend on 【cpkrnl wrimg dbugexc】"

vdiexc:  vdi
	@echo "vdiexc CMD depend on 【vdi】: $(MAKE) $(VBOXVMFLGS)"
	$(MAKE) $(VBOXVMFLGS)

vdi:cpkrnl wrimg
	@echo "cpkrnl wrimg CMD: depend on 【cpkrnl wrimg】 $(CD) $(EXKNL_PATH) && $(IMGTOVDI) $(DSKIMG) $(VDIFNAME)"
	$(CD) $(EXKNL_PATH) && $(IMGTOVDI) $(DSKIMG) $(VDIFNAME)

	@echo "cpkrnl wrimg CMD: depend on 【cpkrnl wrimg】 $(CD) $(EXKNL_PATH) && $(IMGTOVMDK) $(DSKIMG) $(VMDKFNAME)"
	$(CD) $(EXKNL_PATH) && $(IMGTOVMDK) $(DSKIMG) $(VMDKFNAME)

cplmildr:
	@echo "cplmildr CMD: $(CP) $(CPFLAGES) $(CPLILDRSRC) $(CPLILDR_PATH)"
	$(CP) $(CPFLAGES) $(CPLILDRSRC) $(CPLILDR_PATH)

cpkrnl:
	@echo "cpkrnl CMD: $(CD) $(BUILD_PATH) && $(CP) $(CPFLAGES) $(SRCFILE) $(DSTPATH)"
	$(CD) $(BUILD_PATH) && $(CP) $(CPFLAGES) $(SRCFILE) $(DSTPATH)

cprelease:
	@echo "cprelease CMD: $(CD) $(EXKNL_PATH) && $(CP) $(CPFLAGES) $(RSRCFILE) $(RELEDSTPATH)"
	$(CD) $(EXKNL_PATH) && $(CP) $(CPFLAGES) $(RSRCFILE) $(RELEDSTPATH)

exc:
	@echo "exc CMD: $(CD) $(EXKNL_PATH) && $(VM) $(VMFLAGES)"
	$(CD) $(EXKNL_PATH) && $(VM) $(VMFLAGES)

dbugexc:
	@echo "dbugexc CMD: $(CD) $(EXKNL_PATH) && $(DBUGVM)"
	$(CD) $(EXKNL_PATH) && $(DBUGVM)

KIMG:
	@echo '生成Cosmos内核映像文件 KIMG CMD: $(CD) $(CPLILDR_PATH) && $(LKIMG) -lhf $(INITLDRIMH) -o Cosmos.eki -f $(LKIMG_INFILE)'
	$(CD) $(CPLILDR_PATH) && $(LKIMG) -lhf $(INITLDRIMH) -o Cosmos.eki -f $(LKIMG_INFILE)

KVMRUN:
	@echo "KVMRUN CMD: $(VM) $(VMFLAGES)"
	$(VM) $(VMFLAGES)
#$(MAKE) $(KVMRLMOSFLGS)

VBOXRUN:
	@echo "VBOXRUN CMD: $(MAKE) $(VVMRLMOSFLGS)"
	$(MAKE) $(VVMRLMOSFLGS)

kvmtest: release KVMRUN
	@echo "kvmtest CMD: depend on 【release KVMRUN】"

vboxtest: release VBOXRUN
	@echo "vboxtest CMD: depend on 【release VBOXRUN】"

createimg:
	@echo "createimg CMD: $(DD) bs=512 if=/dev/zero of=$(DSKIMG) count=$(IMGSECTNR)"
	#$(DD) bs=512 if=/dev/zero of=$(DSKIMG) count=$(IMGSECTNR)

install:
	@echo 'install CMD: sudo $(CP) $(CPFLAGES) $(INSTALLSRCFILE_PATH) $(INSTALL_PATH)'
	#sudo $(CP) $(CPFLAGES) $(INSTALLSRCFILE_PATH) $(INSTALL_PATH)
