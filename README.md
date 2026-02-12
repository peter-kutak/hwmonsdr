# hwmonsdr
Propagate SDR sensors read from IPMI via hwmon linux api

# motivation
display IPMI fans rpm in s-tui

# structure
## kernel module
creates virtual hwmon devices in /sys/..../hwmonX
by writing to /update it creates item per sensor and updates its value

## user space  service
writing code to comunictae with IPMI is dificult and tricky, so generic ipmitool is used.
script reads sensor values by ipmitool and writes to /sys/.../hwmonX/update
ipmitool read is slow and takes about 20seconds.
timer/service runs script periodically.

# AI
programmed with help ChatGPT-5
