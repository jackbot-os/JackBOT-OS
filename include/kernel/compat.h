#ifndef JACKBOT_COMPAT_H
#define JACKBOT_COMPAT_H

/* 注册全部兼容层驱动到 dev 框架 */
void compat_register_drivers(void);
/* 注册全部网卡驱动到 NIC 抽象层 */
void compat_register_nics(void);

#endif
