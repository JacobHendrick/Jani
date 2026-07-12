#ifndef JANI_KERNEL_DRIVERS_SERIAL_H
#define JANI_KERNEL_DRIVERS_SERIAL_H

void serial_init(void);
void serial_write_char(char c);
void serial_write_string(const char *str);

#endif
