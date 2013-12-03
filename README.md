linux-log-module-odroid-XU
==========================

Kernel module logging CPU frequency, temperature and power consumption for Odroid XU+E.

### Installation

- Copy the files "exynos_thermal.h" and "exynos_thermal.c" into "include/linux/platform_data/" and "drivers/thermal/exynos_thermal.c", respectivily. Recompile your kernel.
- sudo insmod energy.ko
