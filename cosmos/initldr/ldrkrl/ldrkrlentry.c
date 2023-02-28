#include "cmctl.h"

extern idtr_t IDT_PTR;

void ldrkrl_entry()
{
    init_curs();
    close_curs();
    clear_screen(VGADP_DFVL);

    // 初始化系统, 如设置页表等, 为后续进入长模式做准备
    init_bstartparm(); // bstartparam.c

    // 返回到 ldrkrl32.asm 中, 然后 jmp 到 0x2000000 处执行, 此时就会进入长模式
    return;
}

void kerror(char_t *kestr)
{
    kprint("INITKLDR DIE ERROR:%s\n", kestr);
    for (;;)
        ;
    return;
}

#pragma GCC push_options
#pragma GCC optimize("O0")

void die(u32_t dt)
{
    u32_t dttt = dt, dtt = dt;
    if (dt == 0)
    {
        for (;;)
            ;
    }

    for (u32_t i = 0; i < dt; i++)
    {
        for (u32_t j = 0; j < dtt; j++)
        {
            for (u32_t k = 0; k < dttt; k++)
            {
                ;
            }
        }
    }

    return;
}

#pragma GCC pop_options
