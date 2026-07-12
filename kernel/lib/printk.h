#ifndef JANI_KERNEL_LIB_PRINTK_H
#define JANI_KERNEL_LIB_PRINTK_H

void kputs(const char *str);
void printk(const char *format, ...);

#endif
