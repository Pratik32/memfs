/* Things to remember:
 * ##################
 * Always add newline char at the end of your debug message.syslog is a bitch,
 * it won't show the next message after that immediatly.
 * Also, to enable all types of messages fire following cmd:
 * echo "7" > /proc/kernel/printk << this contains all printk
 * levels "7" will enable all of them.
 */
#ifdef DBG
#define DEBUG(str, args...) printk(str, ##args)
#else
#define DEBUG(str, args...)
#endif
