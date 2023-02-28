###################################################################
#		krnlbuidrule自动化编译配置文件 Makefile		  #
#				彭东 
###################################################################

# script 中的 .S 文件最终是用来生成 .mk 或 .lds 文件的, 生成 Makefile文件时, 会将 [ 替换为 tab 键
CCSTR		= 	'CC -[M] 正在构建... '$<
PRINTCSTR 	=	@echo $(CCSTR) 

%.krnl : $(CCBUILDPATH)%.c
[@echo "krnlbuildrule.mk krnl gen by .c CMD: [$(CC) $(CFLAGS) -o $@ $<"
[$(CC) $(CFLAGS) -o $@ $<

%.krnl : $(CCBUILDPATH)%.S
[@echo "krnlbuildrule.mk krnl gen by .S CMD: [$(CC) $(CFLAGS) -o $@ $<"
[$(CC) $(CFLAGS) -o $@ $<

%.o : $(CCBUILDPATH)%.asm
[@echo "krnlbuildrule.mk obj gen by .asm CMD: [$(ASM) $(ASMFLGS) -o $@ $<"
[$(ASM) $(ASMFLGS) -o $@ $<

%.o : $(CCBUILDPATH)%.c
[@echo "krnlbuilderule.mk obj gen by c CMD: [$(CC) $(CFLAGS) -o $@ $<"
[$(CC) $(CFLAGS) -o $@ $<

%.o : $(CCBUILDPATH)%.S
[@echo "krnlbuilderule.mk obj gen by .S CMD: [$(CC) $(CFLAGS) -o $@ $<"
[$(CC) $(CFLAGS) -o $@ $<


%.lds : $(CCBUILDPATH)%.S
[@echo "krnlbuilderule.mk lds gen by .S CMD: [$(CC) $(CPPFLGSLDS) -o $@ $<"
[$(CC) $(CPPFLGSLDS) -o $@ $<

%.mkh : $(CCBUILDPATH)%.S
[@echo "krnlbuilderule.mk mkh gen by .S CMD: [$(CC) $(CPPFLGSLDS) -o $@ $<"
[$(CC) $(CPPFLGSLDS) -o $@ $<

%.mk : $(CCBUILDPATH)%.S
[@echo "krnlbuilderule.mk mk gen by .S CMD: [$(CC) $(CPPFLGSLDS) -o $@ $<"
[$(CC) $(CPPFLGSLDS) -o $@ $<
