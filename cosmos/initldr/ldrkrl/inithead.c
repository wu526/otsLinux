#include "cmctl.h"

/*
#define MDC_ENDGIC 0xaaffaaffaaffaaff
#define MDC_RVGIC 0xffaaffaaffaaffaa
#define REALDRV_PHYADR 0x1000
#define IMGFILE_PHYADR 0x4000000
#define IMGKRNL_PHYADR 0x2000000
#define LDRFILEADR IMGFILE_PHYADR // 0x4000000, 此位置是 GRUB 将我们自己的文件加载到的地方
#define MLOSDSC_OFF (0x1000)
#define MRDDSC_ADR (mlosrddsc_t*)(LDRFILEADR+0x1000) // 0x4001000
*/
void inithead_entry()
{
    init_curs(); // vgastr.c, 设置 cursor_t 对象的值
    close_curs();
    clear_screen(VGADP_DFVL);

    write_realintsvefile(); // 将 initldrsve.bin 复制到 0x1000 地址处
    write_ldrkrlfile();     // 将 initldrkrl.bin 复制到 0x200000 地址处

    // 函数返回到 imginithead.asm 中, 然后会 jmp 到 0x200000 处执行, 即进入 ldrkrl32.asm 中
    return;
}

// 写 initldrsve.bin 文件到特定的内存(0x1000地址)中
void write_realintsvefile()
{
    fhdsc_t *fhdscstart = find_file("initldrsve.bin");
    if (fhdscstart == NULL)
    {
        error("not file initldrsve.bin");
    }

    // src=, dst=REALDRV_PHYADR=0x1000
    m2mcopy((void *)((u32_t)(fhdscstart->fhd_intsfsoff) + LDRFILEADR),
            (void *)REALDRV_PHYADR, (sint_t)fhdscstart->fhd_frealsz);
    return;
}

// 在映像文件中查找对应的文件
fhdsc_t *find_file(char_t *fname)
{
    mlosrddsc_t *mrddadrs = MRDDSC_ADR;
    if (mrddadrs->mdc_endgic != MDC_ENDGIC ||
        mrddadrs->mdc_rv != MDC_RVGIC ||
        mrddadrs->mdc_fhdnr < 2 ||
        mrddadrs->mdc_filnr < 2)
    {
        error("no mrddsc");
    }

    s64_t rethn = -1;
    fhdsc_t *fhdscstart = (fhdsc_t *)((u32_t)(mrddadrs->mdc_fhdbk_s) + LDRFILEADR);

    for (u64_t i = 0; i < mrddadrs->mdc_fhdnr; i++)
    {
        if (strcmpl(fname, fhdscstart[i].fhd_name) == 0)
        {
            rethn = (s64_t)i;
            goto ok_l;
        }
    }
    rethn = -1;

ok_l:
    if (rethn < 0)
    {
        error("not find file");
    }
    return &fhdscstart[rethn];
}

// 写 initldrkrl.bin 文件到特定的内存(0x200000)中
void write_ldrkrlfile()
{
    fhdsc_t *fhdscstart = find_file("initldrkrl.bin");
    if (fhdscstart == NULL)
    {
        error("not file initldrkrl.bin");
    }

    // src=, dst=ILDRKRL_PHYADR=0x200000
    m2mcopy((void *)((u32_t)(fhdscstart->fhd_intsfsoff) + LDRFILEADR),
            (void *)ILDRKRL_PHYADR, (sint_t)fhdscstart->fhd_frealsz);

    return;
}

void error(char_t *estr)
{
    kprint("INITLDR DIE ERROR: %s\n", estr);
    for (;;)
        ;
    return;
}

int strcmpl(const char *a, const char *b)
{
    while (*b && *a && (*b == *a))
    {
        b++;
        a++;
    }

    return *b - *a;
}