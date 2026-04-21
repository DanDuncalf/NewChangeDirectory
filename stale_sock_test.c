#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

int main() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/ncd_1000_control.sock", sizeof(addr.sun_path)-1);
    int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    printf("connect returned %d\n", r);
    perror("connect");
    close(fd);
    return 0;
}
