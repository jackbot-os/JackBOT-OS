#ifndef _FEATURES_H
#define _FEATURES_H

/* 移植层 features.h —— 裁剪自 musl-1.2.5 src/include/features.h（MIT）。
 * 原文件会再 include 公共版 ../../include/features.h（特性测试宏开关），
 * 移植层不需要；只保留 musl 源码依赖的三个内部宏：weak / hidden / weak_alias。
 */

#define weak __attribute__((__weak__))
#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) \
	extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))

#endif