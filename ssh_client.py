import sys
import time
import paramiko
import tty
import termios
import os

def getch():
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        char = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return char

def interactive_shell(chan):
    time.sleep(1)
    while chan.recv_ready():
        recv_data = chan.recv(1024)
        sys.stdout.buffer.write(recv_data)
        sys.stdout.flush()
    try:
        command_buffer = ""
        while True:
            char = getch()
            chan.send(char)

            time.sleep(0.1)
            if chan.recv_ready():
                recv_data = chan.recv(1024)
                sys.stdout.buffer.write(recv_data)
                sys.stdout.flush()

    except KeyboardInterrupt:
        chan.send('\x03')  # 发送 Ctrl+C 到服务器
        print("^C")
        chan.close()

def main():
    hostname = '10.21.4.128'
    port = 22
    username = 'root'
    password = 'root'

    # 创建 SSH 客户端
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        # 连接到远程主机
        client.connect(hostname, port, username, password)

        # 打开交互式 shell
        chan = client.invoke_shell()

        # 进入交互式 shell
        interactive_shell(chan)

    except Exception as e:
        print(f"Error: {e}")
    finally:
        # 关闭连接
        client.close()

if __name__ == '__main__':
    main()

