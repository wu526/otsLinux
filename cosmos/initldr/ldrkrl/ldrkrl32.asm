%include "ldrasm.inc"
global _start
global realadr_call_entry
global IDT_PTR
extern ldrkrl_entry ;; 引用外部的符号

[section .text]
[bits 32]
_start:
_entry:
	cli
	lgdt [GDT_PTR]
	lidt [IDT_PTR]
	jmp dword 0x8 :_32bits_mode

_32bits_mode:
	mov ax, 0x10 ; 数据段选择子(目的)
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

	mov esp,0x90000
	call ldrkrl_entry ;;调用ldrkrl_entry函数

	xor ebx,ebx
	;; 跳转到 0x2000000 的内存地址, 根据编译过程发现, 是 hal/下的init_entry.asm
	jmp 0x2000000 
	jmp $

realadr_call_entry:
	pushad ;; 保存通用寄存器
	push    ds
	push    es
	push    fs
	push    gs ;; 保存段寄存器

	call save_eip_jmp

	pop	gs
	pop	fs
	pop	es
	pop	ds
	popad
	ret

save_eip_jmp:
	pop esi ;; 弹出 call save_eip_jmp 时保存的 eip 到 esi 寄存器中
	mov [PM32_EIP_OFF], esi ;; 把 eip 保存到特定的内存空间中, 方便获取返回时的地址
	mov [PM32_ESP_OFF], esp ;; 把 esp 保存到特定的内存空间中

	;; 长跳转这里表示把 cpmty_mode 处的第一个 4字节装入 eip, 其后的2字节装入cs
	;; 这一跳就到了 realintsve.asm 代码中了
	jmp dword far [cpmty_mode]
	
cpmty_mode:
	dd 0x1000
	dw 0x18 ;; 0x18 是第3个描述符, 段基址是0
	jmp $

GDT_START:
knull_dsc: dq 0
kcode_dsc: dq 0x00cf9a000000ffff ;a-e
kdata_dsc: dq 0x00cf92000000ffff
k16cd_dsc: dq 0x00009a000000ffff ;a-e
k16da_dsc: dq 0x000092000000ffff
GDT_END:

GDT_PTR:
GDTLEN	dw GDT_END-GDT_START-1	;GDT界限
GDTBASE	dd GDT_START

IDT_PTR:
IDTLEN	dw 0x3ff
IDTBAS	dd 0 ;; 这是BIOS中断表的地址和长度
