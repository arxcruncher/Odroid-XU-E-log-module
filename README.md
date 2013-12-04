Odroid-XU-E-log-module
==========================

Kernel module logging CPU frequency, temperature and power consumption for Odroid XU+E.

## Installation

- Copy the files "exynos_thermal.h" and "exynos_thermal.c" into "include/linux/platform_data/" and "drivers/thermal/exynos_thermal.c", respectivily. Recompile your kernel (see http://odroid.com/dokuwiki/doku.php?id=en:odroid-xu).
- Compile the module against the kernel like usual
```
make
```
- Insert the module:
```
sudo insmod energy.ko
```

## Reading the log

A directory will be created in the /proc filesystem named "/proc/paristech" including a file "read" which displays the log.
```
cat /proc/paristech/read
```
Before you can read values from the energy and power chips you must enable them via
```
echo 1 > /sys/bus/i2c/drivers/INA231/4-0045/enable
echo 1 > /sys/bus/i2c/drivers/INA231/4-0040/enable
```

Be aware that the module's memory is only 5 seconds, so you will have to read out the log at least once per 5 seconds not to loose any information.

## TODO
- A binary protocol between module and driver would be better?
- This piece of software has a lot in common with ARM's gator software, can we learn something from there?
