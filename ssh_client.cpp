#include <iostream>
#include <map>
#include <string.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <libgen.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>

#include <libssh2.h>

#define COPYRIGHT "hello world\n\n"
#define EVENT_NUM 5

using namespace std;

struct termios _saved_tio;
int tio_saved = 0;

static int _raw_mode(void)
{
    int rc;
    struct termios tio;

    rc = tcgetattr(fileno(stdin), &tio);
    if (rc != -1)
    {
        _saved_tio = tio;
        tio_saved = 1;
        cfmakeraw(&tio);
        rc = tcsetattr(fileno(stdin), TCSADRAIN, &tio);
    }

    return rc;
}

static int _normal_mode(void)
{
    if (tio_saved)
        return tcsetattr(fileno(stdin), TCSADRAIN, &_saved_tio);

    return 0;
}

int main(int argc, char *argv[])
{
    int sock = 0;
    int stdinFd = 0, epfd = 0;
    unsigned long hostaddr = 0;
    short port = 22;
    char *username = NULL;
    char *password = NULL;
    struct sockaddr_in sin;
    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    char buffer[4096] = {0};
    int n;

    /* Struct winsize for term size */
    struct winsize w_size;
    struct winsize w_size_bck;

    /* For select on stdin */
    fd_set set;
    struct timeval timeval_out;

    printf(COPYRIGHT);

    if (argc > 4)
    {
        hostaddr = inet_addr(argv[1]);
        port = htons(atoi(argv[2]));
        username = argv[3];
        password = argv[4];
    }
    else
    {
        fprintf(stderr, "Usage: %s ip port user password\n", basename(argv[0]));
        return -1;
    }

    if (libssh2_init(0) != 0)
    {
        fprintf(stderr, "libssh2 initialization failed\n");
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    sin.sin_family = AF_INET;
    sin.sin_port = port;
    sin.sin_addr.s_addr = hostaddr;
    if (connect(sock, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) != 0)
    {
        fprintf(stderr, "Failed to established connection!\n");
        return -1;
    }

    stdinFd = fileno(stdin);
    int stdinFlags = fcntl(stdinFd, F_GETFL, 0);
    fcntl(stdinFd, F_SETFL, stdinFlags | O_NONBLOCK);

    int sockFlags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, sockFlags | O_NONBLOCK);

    /* Open a session */
    session = libssh2_session_init();
    if (libssh2_session_startup(session, sock) != 0)
    {
        fprintf(stderr, "Failed Start the SSH session\n");
        return -1;
    }

    /* Authenticate via password */
    if (libssh2_userauth_password(session, username, password) != 0)
    {
        fprintf(stderr, "Failed to authenticate\n");
        goto ERROR;
    }

    /* Open a channel */
    channel = libssh2_channel_open_session(session);
    if (channel == NULL)
    {
        fprintf(stderr, "Failed to open a new channel\n");
        goto ERROR;
    }

    /* Request a PTY */
    if (libssh2_channel_request_pty(channel, "xterm") != 0)
    {
        fprintf(stderr, "Failed to request a pty\n");
        goto ERROR;
    }

    /* Request a shell */
    if (libssh2_channel_shell(channel) != 0)
    {
        fprintf(stderr, "Failed to open a shell\n");
        goto ERROR;
    }

    if (_raw_mode() != 0)
    {
        fprintf(stderr, "Failed to entered in raw mode\n");
        goto ERROR;
    }

    struct epoll_event ev, events[EVENT_NUM];
    epfd = epoll_create(EVENT_NUM);

    ev.data.fd = sock;                         // 要监视的文件描述符，可以是任何打开的在/proc/pid/fd/目录下的fd
    ev.events = EPOLLIN;                       // 监听读状态同时设置LT模式
    epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev); // 注册epoll事件

    ev.data.fd = stdinFd;                         // 要监视的文件描述符，可以是任何打开的在/proc/pid/fd/目录下的fd
    ev.events = EPOLLIN;                          // 监听读状态同时设置LT模式
    epoll_ctl(epfd, EPOLL_CTL_ADD, stdinFd, &ev); // 注册epoll事件

    libssh2_channel_set_blocking(channel, false);
    while (1)
    {
        if (libssh2_channel_eof(channel) == 1)
            break;
        int nfds = epoll_wait(epfd, events, 5, 1000);
        if (libssh2_channel_eof(channel) == 1)
            break;
        for (int i = 0; i < nfds; i++)
        {
            while (1)
            {
                memset(buffer, 0, sizeof buffer);
                if (events[i].data.fd == stdinFd)
                {
                    if ((n = read(stdinFd, buffer, sizeof buffer - 1)) > 0)
                    {
                        libssh2_channel_write(channel, buffer, n);
                    }
                }
                else
                {
                    if ((n = libssh2_channel_read(channel, buffer, sizeof buffer - 1)) > 0)
                    {
                        fprintf(stdout, "%s", buffer);
                        fflush(stdout);
                    }
                }
                if (n <= 0)
                {
                    break;
                }
            }
        }

        ioctl(stdinFd, TIOCGWINSZ, &w_size);
        if ((w_size.ws_row != w_size_bck.ws_row) ||
            (w_size.ws_col != w_size_bck.ws_col))
        {
            w_size_bck = w_size;
            libssh2_channel_request_pty_size(channel, w_size.ws_col, w_size.ws_row);
        }
    }

    libssh2_channel_free(channel);
    channel = NULL;

    _normal_mode();

    libssh2_exit();

    return 0;

ERROR:
    close(sock);
    libssh2_session_disconnect(session, "Session Shutdown, Thank you for playing");
    libssh2_session_free(session);
    return -1;
}


