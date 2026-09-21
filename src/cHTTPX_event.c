#include "cHTTPX_event.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef CHTTPX_PLATFORM_WINDOWS

struct chttpx_event_loop
{
    WSAPOLLFD* pollfds;
    void** data;
    size_t count;
    size_t capacity;
    volatile LONG wake_pending;
};

static short native_events(uint32_t events)
{
    short value = 0;
    if (events & CHTTPX_EVENT_READ)
        value |= POLLRDNORM;
    if (events & CHTTPX_EVENT_WRITE)
        value |= POLLWRNORM;
    return value;
}

static int ensure_capacity(chttpx_event_loop_t* loop, size_t capacity)
{
    if (capacity <= loop->capacity)
        return 1;
    size_t next = loop->capacity ? loop->capacity * 2 : 64;
    while (next < capacity)
        next *= 2;
    WSAPOLLFD* pollfds = realloc(loop->pollfds, next * sizeof(*pollfds));
    if (!pollfds)
        return 0;
    void** data = realloc(loop->data, next * sizeof(*data));
    if (!data)
    {
        loop->pollfds = pollfds;
        return 0;
    }
    loop->pollfds = pollfds;
    loop->data = data;
    loop->capacity = next;
    return 1;
}

static ptrdiff_t find_fd(chttpx_event_loop_t* loop, chttpx_socket_t fd)
{
    for (size_t i = 0; i < loop->count; i++)
        if (loop->pollfds[i].fd == fd)
            return (ptrdiff_t)i;
    return -1;
}

chttpx_event_loop_t* _chttpx_event_create(void)
{
    return calloc(1, sizeof(chttpx_event_loop_t));
}

void _chttpx_event_destroy(chttpx_event_loop_t* loop)
{
    if (!loop)
        return;
    free(loop->pollfds);
    free(loop->data);
    free(loop);
}

int _chttpx_event_add(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop || find_fd(loop, fd) >= 0 || !ensure_capacity(loop, loop->count + 1))
        return -1;
    loop->pollfds[loop->count].fd = fd;
    loop->pollfds[loop->count].events = native_events(events);
    loop->pollfds[loop->count].revents = 0;
    loop->data[loop->count] = data;
    loop->count++;
    return 0;
}

int _chttpx_event_mod(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop)
        return -1;
    ptrdiff_t index = find_fd(loop, fd);
    if (index < 0)
        return -1;
    loop->pollfds[index].events = native_events(events);
    loop->data[index] = data;
    return 0;
}

void _chttpx_event_del(chttpx_event_loop_t* loop, chttpx_socket_t fd)
{
    if (!loop)
        return;
    ptrdiff_t index = find_fd(loop, fd);
    if (index < 0)
        return;
    size_t i = (size_t)index;
    size_t last = loop->count - 1;
    if (i != last)
    {
        loop->pollfds[i] = loop->pollfds[last];
        loop->data[i] = loop->data[last];
    }
    loop->count--;
}

int _chttpx_event_wait(chttpx_event_loop_t* loop, chttpx_event_t* events, size_t max_events, int timeout_ms)
{
    if (!loop || !events || max_events == 0)
        return -1;

    size_t output = 0;
    if (InterlockedExchange(&loop->wake_pending, 0) != 0)
        events[output++] = (chttpx_event_t){.fd = INVALID_SOCKET, .events = CHTTPX_EVENT_WAKE, .data = NULL};

    if (output == max_events)
        return (int)output;

    int wait_ms = timeout_ms;
    if (wait_ms < 0 || wait_ms > 10)
        wait_ms = 10;

    int ready = WSAPoll(loop->pollfds, (ULONG)loop->count, wait_ms);
    if (ready == SOCKET_ERROR)
        return -1;

    for (size_t i = 0; i < loop->count && output < max_events; i++)
    {
        short revents = loop->pollfds[i].revents;
        if (!revents)
            continue;
        uint32_t value = 0;
        if (revents & (POLLRDNORM | POLLRDBAND | POLLIN))
            value |= CHTTPX_EVENT_READ;
        if (revents & (POLLWRNORM | POLLOUT))
            value |= CHTTPX_EVENT_WRITE;
        if (revents & (POLLERR | POLLHUP | POLLNVAL))
            value |= CHTTPX_EVENT_ERROR;
        events[output++] = (chttpx_event_t){.fd = loop->pollfds[i].fd, .events = value, .data = loop->data[i]};
    }

    if (output < max_events && InterlockedExchange(&loop->wake_pending, 0) != 0)
        events[output++] = (chttpx_event_t){.fd = INVALID_SOCKET, .events = CHTTPX_EVENT_WAKE, .data = NULL};

    return (int)output;
}

void _chttpx_event_wake(chttpx_event_loop_t* loop)
{
    if (loop)
        InterlockedExchange(&loop->wake_pending, 1);
}

int _chttpx_socket_set_nonblocking(chttpx_socket_t fd)
{
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
}

#elif defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

struct chttpx_event_loop
{
    int fd;
    int wake_fd;
};

static uint32_t native_events(uint32_t events)
{
    uint32_t value = EPOLLRDHUP;
    if (events & CHTTPX_EVENT_READ)
        value |= EPOLLIN;
    if (events & CHTTPX_EVENT_WRITE)
        value |= EPOLLOUT;
    return value;
}

chttpx_event_loop_t* _chttpx_event_create(void)
{
    chttpx_event_loop_t* loop = calloc(1, sizeof(*loop));
    if (!loop)
        return NULL;
    loop->fd = epoll_create1(EPOLL_CLOEXEC);
    loop->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (loop->fd < 0 || loop->wake_fd < 0)
        goto error;
    struct epoll_event event = {.events = EPOLLIN, .data.ptr = NULL};
    if (epoll_ctl(loop->fd, EPOLL_CTL_ADD, loop->wake_fd, &event) != 0)
        goto error;
    return loop;
error:
    if (loop->wake_fd >= 0)
        close(loop->wake_fd);
    if (loop->fd >= 0)
        close(loop->fd);
    free(loop);
    return NULL;
}

void _chttpx_event_destroy(chttpx_event_loop_t* loop)
{
    if (!loop)
        return;
    close(loop->wake_fd);
    close(loop->fd);
    free(loop);
}

int _chttpx_event_add(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop)
        return -1;
    struct epoll_event event = {.events = native_events(events), .data.ptr = data};
    return epoll_ctl(loop->fd, EPOLL_CTL_ADD, fd, &event);
}

int _chttpx_event_mod(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop)
        return -1;
    struct epoll_event event = {.events = native_events(events), .data.ptr = data};
    return epoll_ctl(loop->fd, EPOLL_CTL_MOD, fd, &event);
}

void _chttpx_event_del(chttpx_event_loop_t* loop, chttpx_socket_t fd)
{
    if (loop)
        epoll_ctl(loop->fd, EPOLL_CTL_DEL, fd, NULL);
}

int _chttpx_event_wait(chttpx_event_loop_t* loop, chttpx_event_t* events, size_t max_events, int timeout_ms)
{
    if (!loop || !events || max_events == 0)
        return -1;
    if (max_events > 256)
        max_events = 256;
    struct epoll_event native[256];
    int count;
    do
    {
        count = epoll_wait(loop->fd, native, (int)max_events, timeout_ms);
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        return -1;

    int output = 0;
    for (int i = 0; i < count; i++)
    {
        if (!native[i].data.ptr)
        {
            uint64_t value;
            while (read(loop->wake_fd, &value, sizeof(value)) > 0)
            {
            }
            events[output++] = (chttpx_event_t){.fd = loop->wake_fd, .events = CHTTPX_EVENT_WAKE, .data = NULL};
            continue;
        }
        uint32_t value = 0;
        if (native[i].events & EPOLLIN)
            value |= CHTTPX_EVENT_READ;
        if (native[i].events & EPOLLOUT)
            value |= CHTTPX_EVENT_WRITE;
        if (native[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            value |= CHTTPX_EVENT_ERROR;
        events[output++] = (chttpx_event_t){.fd = -1, .events = value, .data = native[i].data.ptr};
    }
    return output;
}

void _chttpx_event_wake(chttpx_event_loop_t* loop)
{
    if (!loop)
        return;
    uint64_t value = 1;
    ssize_t ignored = write(loop->wake_fd, &value, sizeof(value));
    (void)ignored;
}

int _chttpx_socket_set_nonblocking(chttpx_socket_t fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include <errno.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

struct chttpx_event_loop
{
    int fd;
};

static int change_filter(chttpx_event_loop_t* loop, chttpx_socket_t fd, int16_t filter, bool enabled, void* data)
{
    struct kevent change;
    EV_SET(&change, (uintptr_t)fd, filter, EV_ADD | (enabled ? EV_ENABLE : EV_DISABLE), 0, 0, data);
    return kevent(loop->fd, &change, 1, NULL, 0, NULL);
}

chttpx_event_loop_t* _chttpx_event_create(void)
{
    chttpx_event_loop_t* loop = calloc(1, sizeof(*loop));
    if (!loop)
        return NULL;
    loop->fd = kqueue();
    if (loop->fd < 0)
    {
        free(loop);
        return NULL;
    }
    struct kevent wake;
    EV_SET(&wake, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(loop->fd, &wake, 1, NULL, 0, NULL) != 0)
    {
        close(loop->fd);
        free(loop);
        return NULL;
    }
    return loop;
}

void _chttpx_event_destroy(chttpx_event_loop_t* loop)
{
    if (!loop)
        return;
    close(loop->fd);
    free(loop);
}

int _chttpx_event_add(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop)
        return -1;
    if (change_filter(loop, fd, EVFILT_READ, (events & CHTTPX_EVENT_READ) != 0, data) != 0)
        return -1;
    if (change_filter(loop, fd, EVFILT_WRITE, (events & CHTTPX_EVENT_WRITE) != 0, data) != 0)
    {
        _chttpx_event_del(loop, fd);
        return -1;
    }
    return 0;
}

int _chttpx_event_mod(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop)
        return -1;
    if (change_filter(loop, fd, EVFILT_READ, (events & CHTTPX_EVENT_READ) != 0, data) != 0)
        return -1;
    return change_filter(loop, fd, EVFILT_WRITE, (events & CHTTPX_EVENT_WRITE) != 0, data);
}

void _chttpx_event_del(chttpx_event_loop_t* loop, chttpx_socket_t fd)
{
    if (!loop)
        return;
    struct kevent changes[2];
    EV_SET(&changes[0], (uintptr_t)fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[1], (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(loop->fd, changes, 2, NULL, 0, NULL);
}

int _chttpx_event_wait(chttpx_event_loop_t* loop, chttpx_event_t* events, size_t max_events, int timeout_ms)
{
    if (!loop || !events || max_events == 0)
        return -1;
    if (max_events > 256)
        max_events = 256;
    struct kevent native[256];
    struct timespec timeout;
    struct timespec* timeout_ptr = NULL;
    if (timeout_ms >= 0)
    {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout;
    }
    int count;
    do
    {
        count = kevent(loop->fd, NULL, 0, native, (int)max_events, timeout_ptr);
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        return -1;

    size_t output = 0;
    for (int i = 0; i < count && output < max_events; i++)
    {
        if (native[i].filter == EVFILT_USER)
        {
            events[output++] = (chttpx_event_t){.fd = -1, .events = CHTTPX_EVENT_WAKE, .data = NULL};
            continue;
        }
        void* data = native[i].udata;
        uint32_t value = native[i].filter == EVFILT_READ ? CHTTPX_EVENT_READ : CHTTPX_EVENT_WRITE;
        if (native[i].flags & (EV_ERROR | EV_EOF))
            value |= CHTTPX_EVENT_ERROR;

        size_t existing = output;
        for (size_t j = 0; j < output; j++)
        {
            if (events[j].data == data)
            {
                existing = j;
                break;
            }
        }
        if (existing < output)
            events[existing].events |= value;
        else
            events[output++] = (chttpx_event_t){.fd = (chttpx_socket_t)native[i].ident, .events = value, .data = data};
    }
    return (int)output;
}

void _chttpx_event_wake(chttpx_event_loop_t* loop)
{
    if (!loop)
        return;
    struct kevent wake;
    EV_SET(&wake, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    kevent(loop->fd, &wake, 1, NULL, 0, NULL);
}

int _chttpx_socket_set_nonblocking(chttpx_socket_t fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#else

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

struct chttpx_event_loop
{
    struct pollfd* pollfds;
    void** data;
    size_t count;
    size_t capacity;
    int wake_pipe[2];
};

static short native_events(uint32_t events)
{
    short value = 0;
    if (events & CHTTPX_EVENT_READ)
        value |= POLLIN;
    if (events & CHTTPX_EVENT_WRITE)
        value |= POLLOUT;
    return value;
}

static int ensure_capacity(chttpx_event_loop_t* loop, size_t capacity)
{
    if (capacity <= loop->capacity)
        return 1;
    size_t next = loop->capacity ? loop->capacity * 2 : 64;
    while (next < capacity)
        next *= 2;
    struct pollfd* pollfds = realloc(loop->pollfds, next * sizeof(*pollfds));
    if (!pollfds)
        return 0;
    void** data = realloc(loop->data, next * sizeof(*data));
    if (!data)
    {
        loop->pollfds = pollfds;
        return 0;
    }
    loop->pollfds = pollfds;
    loop->data = data;
    loop->capacity = next;
    return 1;
}

static ptrdiff_t find_fd(chttpx_event_loop_t* loop, chttpx_socket_t fd)
{
    for (size_t i = 0; i < loop->count; i++)
        if (loop->pollfds[i].fd == fd)
            return (ptrdiff_t)i;
    return -1;
}

chttpx_event_loop_t* _chttpx_event_create(void)
{
    chttpx_event_loop_t* loop = calloc(1, sizeof(*loop));
    if (!loop)
        return NULL;
    if (pipe(loop->wake_pipe) != 0 || _chttpx_socket_set_nonblocking(loop->wake_pipe[0]) != 0 || _chttpx_socket_set_nonblocking(loop->wake_pipe[1]) != 0)
    {
        free(loop);
        return NULL;
    }
    if (!ensure_capacity(loop, 1))
    {
        close(loop->wake_pipe[0]);
        close(loop->wake_pipe[1]);
        free(loop);
        return NULL;
    }
    loop->pollfds[0] = (struct pollfd){.fd = loop->wake_pipe[0], .events = POLLIN};
    loop->data[0] = NULL;
    loop->count = 1;
    return loop;
}

void _chttpx_event_destroy(chttpx_event_loop_t* loop)
{
    if (!loop)
        return;
    close(loop->wake_pipe[0]);
    close(loop->wake_pipe[1]);
    free(loop->pollfds);
    free(loop->data);
    free(loop);
}

int _chttpx_event_add(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop || find_fd(loop, fd) >= 0 || !ensure_capacity(loop, loop->count + 1))
        return -1;
    loop->pollfds[loop->count] = (struct pollfd){.fd = fd, .events = native_events(events)};
    loop->data[loop->count] = data;
    loop->count++;
    return 0;
}

int _chttpx_event_mod(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data)
{
    if (!loop)
        return -1;
    ptrdiff_t index = find_fd(loop, fd);
    if (index < 0)
        return -1;
    loop->pollfds[index].events = native_events(events);
    loop->data[index] = data;
    return 0;
}

void _chttpx_event_del(chttpx_event_loop_t* loop, chttpx_socket_t fd)
{
    if (!loop)
        return;
    ptrdiff_t index = find_fd(loop, fd);
    if (index <= 0)
        return;
    size_t i = (size_t)index;
    size_t last = loop->count - 1;
    if (i != last)
    {
        loop->pollfds[i] = loop->pollfds[last];
        loop->data[i] = loop->data[last];
    }
    loop->count--;
}

int _chttpx_event_wait(chttpx_event_loop_t* loop, chttpx_event_t* events, size_t max_events, int timeout_ms)
{
    if (!loop || !events || max_events == 0)
        return -1;
    int ready;
    do
    {
        ready = poll(loop->pollfds, loop->count, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0)
        return -1;

    size_t output = 0;
    for (size_t i = 0; i < loop->count && output < max_events; i++)
    {
        short revents = loop->pollfds[i].revents;
        if (!revents)
            continue;
        if (i == 0)
        {
            char drain[64];
            while (read(loop->wake_pipe[0], drain, sizeof(drain)) > 0)
            {
            }
            events[output++] = (chttpx_event_t){.fd = loop->wake_pipe[0], .events = CHTTPX_EVENT_WAKE, .data = NULL};
            continue;
        }
        uint32_t value = 0;
        if (revents & POLLIN)
            value |= CHTTPX_EVENT_READ;
        if (revents & POLLOUT)
            value |= CHTTPX_EVENT_WRITE;
        if (revents & (POLLERR | POLLHUP | POLLNVAL))
            value |= CHTTPX_EVENT_ERROR;
        events[output++] = (chttpx_event_t){.fd = loop->pollfds[i].fd, .events = value, .data = loop->data[i]};
    }
    return (int)output;
}

void _chttpx_event_wake(chttpx_event_loop_t* loop)
{
    if (!loop)
        return;
    const char value = 1;
    ssize_t ignored = write(loop->wake_pipe[1], &value, 1);
    (void)ignored;
}

int _chttpx_socket_set_nonblocking(chttpx_socket_t fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#endif
