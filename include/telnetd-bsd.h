void telnetd_init(int port);
void telnetd_set_process_line_cb(void (*process)(const char *));
void telnetd_set_interrupt_cb(void (*process)(void));
void telnetd_set_new_connection_cb(void (*connection)(void));
void telnetd_send(const char *data);
void telnetd_disconnect(void);
#define telnetd_printf(fmt, ...) do { \
    char data[1024]; \
    sprintf(data, fmt, ##__VA_ARGS__); \
    telnetd_send(data); \
    } while(0)
    
