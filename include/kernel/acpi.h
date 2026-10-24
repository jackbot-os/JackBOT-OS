#ifndef JACKBOT_ACPI_H
#define JACKBOT_ACPI_H
/*
 * JackBOT OS ACPI 最小解析层
 * 扫描 BIOS/EBDA 找 RSDP，枚举 RSDT/XSDT 中的表，定位 FADT，
 * 取得 PM1 控制块与 \_S5 睡眠类型，从而实现真实的 ACPI 软关机；
 * 并在 DSDT 中检测控制法电池(PNP0C0A)、嵌入式控制器(PNP0C09)、
 * 交流电源(ACPI0003) 是否存在。
 *
 * 说明：电量百分比/充电状态需要运行 AML 解释器去执行 _BST/_STA，
 * 属于后续阶段；本模块只做“设备存在性 + 电源寄存器”这一层真实能力。
 */
#include <stdint.h>

struct acpi_info {
    int found;            /* 找到 RSDP */
    int revision;         /* 0=ACPI 1.0(RSDT)，2=ACPI 2.0+(XSDT) */
    int table_count;
    int has_fadt;
    int has_dsdt;
    int has_battery;      /* DSDT 中存在 PNP0C0A 控制法电池 */
    int has_ec;           /* DSDT 中存在 PNP0C09 嵌入式控制器 */
    int has_ac_adapter;   /* DSDT 中存在 ACPI0003 交流电源 */
    uint32_t pm1a_evt;
    uint32_t pm1b_evt;
    uint32_t pm1a_cnt;
    uint32_t pm1b_cnt;
    uint16_t sci_int;
    uint8_t slp_typa;     /* 从 \_S5 提取；0xFF 表示未解析出 */
    uint8_t slp_typb;
};

void acpi_init(void);
const struct acpi_info *acpi_get(void);

/* 真实 ACPI 软关机：成功触发不返回（机器断电）；失败返回 -1。 */
int acpi_poweroff(void);
/* 真实 ACPI 重启（通过 PSCI 不适用 PC，用 8042；这里返回 -1 时由调用方回退）。 */
int acpi_reset(void);

#endif
