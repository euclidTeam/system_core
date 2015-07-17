/*
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include <cutils/sockets.h>

static int fix_O_NONBLOCK(int s, int type) {
    // If the caller actually wanted a non-blocking socket, fine.
    if ((type & SOCK_NONBLOCK)) return s;

    // Otherwise clear the O_NONBLOCK flag.
    int flags = fcntl(s, F_GETFL);
    if (flags == -1 || fcntl(s, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        close(s);
        return -1;
    }

    return s;
}

// Connect to the given host and port.
// 'timeout' is in seconds (0 for no timeout).
// Returns a file descriptor or -1 on error.
int socket_network_client_timeout(const char* host, int port, int type, int timeout) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;

    struct addrinfo* addrs;
    if (getaddrinfo(host, NULL, &hints, &addrs) != 0) {
        return -1;
    }

    // TODO: try all the addresses if there's more than one?
    int family = addrs[0].ai_family;
    int protocol = addrs[0].ai_protocol;
    struct sockaddr_in addr = *(struct sockaddr_in*)(addrs[0].ai_addr);
    addr.sin_port = htons(port);

    freeaddrinfo(addrs);

    int s = socket(family, type | SOCK_NONBLOCK, protocol);
    if (s == -1) return -1;

    int rc = connect(s, (struct sockaddr*) &addr, sizeof(addr));
    if (rc == 0) {
        return fix_O_NONBLOCK(s, type);
    } else if (rc == -1 && errno != EINPROGRESS) {
        close(s);
        return -1;
    }

    fd_set r_set;
    FD_ZERO(&r_set);
    FD_SET(s, &r_set);
    fd_set w_set = r_set;

    struct timeval ts;
    ts.tv_sec = timeout;
    ts.tv_usec = 0;
    if ((rc = select(s + 1, &r_set, &w_set, NULL, (timeout != 0) ? &ts : NULL)) == -1) {
        close(s);
        return -1;
    }
    if (rc == 0) {   // we had a timeout
        errno = ETIMEDOUT;
        close(s);
        return -1;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    if (FD_ISSET(s, &r_set) || FD_ISSET(s, &w_set)) {
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            close(s);
            return -1;
        }
    } else {
        close(s);
        return -1;
    }

    if (error) {  // check if we had a socket error
        errno = error;
        close(s);
        return -1;
    }

    return fix_O_NONBLOCK(s, type);
}

int socket_network_client(const char* host, int port, int type) {
    return socket_network_client_timeout(host, port, type, 0);
}
