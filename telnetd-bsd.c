#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "mem.h"
//#include "os.h"

#include "lwip/sockets.h"
#include "telnetd-bsd.h"

static void (*process_line)(const char *) = 0;
static void (*interrupt)(void) = 0;
static void (*connection)(void) = 0;

static int telnet_sock = 0;
static int sock = 0;

#define TN_BASE 240
#define TN_SE 240
#define TN_NOP 241
#define TN_DM 242
#define TN_BRK 243
#define TN_IP 244
#define TN_AO 245
#define TN_AYT 246
#define TN_EC 247
#define TN_EL 248
#define TN_GA 249
#define TN_SB 250
#define TN_WILL 251
#define TN_WONT 252
#define TN_DO 253
#define TN_DONT 254
#define TN_IAC 255

char *commands[] =
{
    "SE",
    "NOP",
    "DM",
    "BRK",
    "IP",
    "AO",
    "AYT",
    "EC",
    "EL",
    "GA",
    "SB",
    "WILL",
    "WONT",
    "DO",
    "DONT",
    "IAC"
};

static uint8_t cmd_buffer[16];
static int cmd_len = 0;
static uint8_t in_cmd = 0;
static char recv_buf[256];

static void process_telnet_command()
{
    if (cmd_len == 2)
    {
    }
    else
    {
        unsigned char cmd[4] = {TN_IAC, 0, (uint8_t)cmd_buffer[2], 0};

        if ((uint8_t)cmd_buffer[1] == TN_WILL)
        {
            switch ((uint8_t)cmd_buffer[1])
            {
            case 37: // auth
            case 24: // terminal type
            case 31: // window size
            case 32: // speed
            case 33: // flow
            case 34: // linemode
            case 39: // new env
            case 35: // x display loc
                cmd[1] = TN_DONT;
                break;
            }
            if (cmd[1] != 0)
                telnetd_send(cmd);
            return;
        }

        if ((uint8_t)cmd_buffer[1] == TN_DO)
        {
            switch ((uint8_t)cmd_buffer[1])
            {
            case 3: // suppress go ahead
                cmd[1] = TN_WILL;
                break;
            case 5: // terminal type
                cmd[1] = TN_WONT;
                break;
            }
            if (cmd[1] != 0)
                telnetd_send(cmd);
            return;
        }
    }
}

static void telnet_recv_cb(char *data, unsigned short len)
{
    static char real_data[256];
    static int l = 0;
    static uint8_t crlf[] = {0x0d, 0x0a, 0};
    static uint8_t bs[] = {0x08, 0x20, 0x08, 0};

    for (int i = 0 ; i < len ; i++)
    {
        if (data[i] != (char)TN_IAC && !in_cmd)
        {
            if (data[i] == 0)
                continue;

            if (data[i] == 0x0d) // cr
            {
                telnetd_send(crlf);

                real_data[l] = 0;
                if (process_line)
                    process_line(real_data);

                l = 0;
            }
            if (data[i] == 0x08 || data[i] == 0x7f) // bs + del
            {
                if (l > 0)
                {
                    telnetd_send(bs);
                    l--;
                }
                continue;
            }
            if (data[i] == 0x03) // CTRL-C
            {
                telnetd_send(crlf);

                real_data[l] = 0;
                if (interrupt)
                    interrupt();

                l = 0;
            }
            if ((uint8_t)data[i] < 32 || (uint8_t)data[i] > 126)
                continue;

            if (l < 255)
            {
                real_data[l++] = data[i];
                char echo[] = {0, 0};
                echo[0] = data[i];
                telnetd_send(echo);
                //espconn_send(arg, (uint8_t *)&data[i], 1);
            }
            continue;
        }
        else
        {
            if (data[i] == (char)TN_IAC)
            {
                if (in_cmd)
                {
                    process_telnet_command();
                    cmd_len = 0;
                }
                in_cmd = 1;
                cmd_buffer[cmd_len++] = data[i];
            }
            else
            {
                cmd_buffer[cmd_len++] = data[i];
                if (cmd_len == 2 && (uint8_t)data[i] != TN_WILL && (uint8_t)data[i] != TN_DO && (uint8_t)data[i] != TN_WONT && (uint8_t)data[i] != TN_DONT)
                {
                    process_telnet_command();
                    cmd_len = 0;
                    in_cmd = 0;
                }
                if (cmd_len == 3)
                {
                    process_telnet_command();
                    cmd_len = 0;
                    in_cmd = 0;
                }
            }
        }
    }
}

static void telnet_connect_cb()
{
    unsigned char cmd[] = { TN_IAC, TN_WILL, 1, 0 };
    telnetd_send(cmd);

    if (connection)
        connection();
}

void telnetd_set_process_line_cb(void (*process)(const char *))
{
    process_line = process;
}

void telnetd_set_interrupt_cb(void (*intr)(void))
{
    interrupt = intr;
}

void telnetd_set_new_connection_cb(void (*conn)(void))
{
    connection = conn;
}

void telnetd_send(const char *data)
{
    if (!sock)
        return;

    send(sock, data, strlen(data), 0);
}

void telnetd_disconnect()
{
    if (!sock)
        return;

    close(sock);
    sock = 0;
}

static void telnetd_loop(void *pv)
{
    for(;;)
    {
        int m = telnet_sock;
        if (sock > telnet_sock)
            m = sock;

        fd_set in;

        FD_ZERO(&in);

        FD_SET(telnet_sock, &in);
        if (sock != 0)
            FD_SET(sock, &in);

        int cc = select(m + 1, &in, NULL, NULL, NULL);
        if (FD_ISSET(telnet_sock, &in))
        {
            if (sock != 0)
            {
                close(sock);
            }
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            sock = accept(telnet_sock, (struct sockaddr *)&addr, &len);
            if (sock < 0)
            {
                sock = 0;
                continue;
            }
            telnet_connect_cb();
        }
        else if (sock > 0 && FD_ISSET(sock, &in))
        {
            int len = recv(sock, recv_buf, sizeof(recv_buf), 0);
            if (len == 0)
            {
                sock = 0;
                continue;
            }
            telnet_recv_cb(recv_buf, len);
        }
    }
}

void telnetd_init(int port)
{
    telnet_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(23);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(telnet_sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(telnet_sock, 5);

    xTaskCreate(telnetd_loop, "TELNETD", 8192, NULL, 2, NULL);
}
