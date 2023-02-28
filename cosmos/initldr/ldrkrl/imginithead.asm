MBT_HDR_FLAGS	EQU 0x00010003
MBT_HDR_MAGIC	EQU 0x1BADB002
MBT2_MAGIC	EQU 0xe85250d6
global _start

;;global _entry  ;; 为了验证我的计算结果
;;global _32bits_mode

extern inithead_entry
[section .text]
[bits 32]
;; 通过链接脚本后 _start 的值是链接脚本中的值, 可以在 ./initldr/build/initldrimh.map 中查看 _start 符号的值
_start:  
	jmp _entry

align 4
mbt_hdr: ;; grub1 头
	dd MBT_HDR_MAGIC  ;; 魔数
	dd MBT_HDR_FLAGS ;; flags
	dd -(MBT_HDR_MAGIC+MBT_HDR_FLAGS) ;; checksum
	dd mbt_hdr  ;; header_addr if flags[16] is set
	dd _start  ;; load_addr, 该grub需要加载的位置, if flags[16] is set
	dd 0 ;; load_end_addr if flags[16] is set
	dd 0 ;; bss_end_addr if flags[16] is set
	dd _entry ;; entry_addr if flags[16] is set
	;
	; multiboot header
	;
ALIGN 8
mbhdr: ;; grub2 头
	DD	0xE85250D6 ;; 魔数
	DD	0 ;; 架构, 0 是 x86
	DD	mhdrend - mbhdr ;; grub2头的长度
	DD	-(0xE85250D6 + 0 + (mhdrend - mbhdr)) ;; checksum

	;; multiboot_header_tag_address
	DW	2, 0 ;; type=2, 表示 multiboot_header_tag_address, flags=0
	DD	24 ;; size
	DD	mbhdr ;; header_addr
	DD	_start ;; load_addr
	DD	0 ;; load_end_addr, 0表示数据段和代码段一样占用整个数据空间
	DD	0 ;; bss_end_addr

	;; multiboot_header_tag_entry_address
	DW	3, 0 ;; type=3 表示 multiboot_header_tag_entry_address, flags=0
	DD	12 ;; size
	DD	_entry  ;; entry_addr

	DD      0  
	DW	0, 0
	DD	8
mhdrend:

_entry:
	cli ;; 关中断

	;; 关硬件中断(不可屏蔽中断)
	in al, 0x70
	or al, 0x80	
	out 0x70,al

	lgdt [GDT_PTR]
	jmp dword 0x8 :_32bits_mode ;; 长跳转刷新CS影子寄存器

_32bits_mode:
	mov ax, 0x10
	mov ds, ax
	mov ss, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	xor eax,eax
	xor ebx,ebx
	xor ecx,ecx
	xor edx,edx
	xor edi,edi
	xor esi,esi
	xor ebp,ebp
	xor esp,esp
	mov esp,0x7c00
	call inithead_entry ; 调用inithead_entry 函数, 在 inithead.c 中实现
	jmp 0x200000 ;; 跳转到 0x200000, ldrkrl32.asm 会放在此地址处

GDT_START:
knull_dsc: dq 0
kcode_dsc: dq 0x00cf9e000000ffff ;代码段描述符, c 9e 
kdata_dsc: dq 0x00cf92000000ffff ;数据段描述符, c 92
k16cd_dsc: dq 0x00009e000000ffff ;代码段描述符, 0 9e
k16da_dsc: dq 0x000092000000ffff
GDT_END:
GDT_PTR:
GDTLEN	dw GDT_END-GDT_START-1	;GDT界限
GDTBASE	dd GDT_START
