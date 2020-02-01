/* Copyright © 2018-2019 N. Van Bossuyt.                                      */
/* This code is licensed under the MIT License.                               */
/* See: LICENSE.md                                                            */

/* syscalls.c syscalls handeling code                                         */

/*
 * TODO:
 * - Check pointers from user space.
 * - File system syscalls.
 * - Shared memory syscalls.
 */

#include <libsystem/atomic.h>
#include <libsystem/cstring.h>
#include <libsystem/error.h>
#include <libsystem/logger.h>
#include <libsystem/system.h>

#include <abi/Syscalls.h>

#include "clock.h"
#include "filesystem/Filesystem.h"
#include "memory.h"
#include "serial.h"
#include "syscalls.h"
#include "tasking.h"

typedef int (*syscall_handler_t)(int, int, int, int, int);

bool syscall_validate_ptr(uintptr_t ptr, size_t size)
{
    return ptr >= 0x100000 &&
           ptr + size >= 0x100000 &&
           ptr + size >= ptr;
}

/* --- Process -------------------------------------------------------------- */

int sys_process_this(void)
{
    return sheduler_running_id();
}

int sys_process_launch(Launchpad *launchpad)
{
    if (!syscall_validate_ptr((uintptr_t)launchpad, sizeof(Launchpad)))
    {
        return -ERR_BAD_ADDRESS;
    }

    return task_launch(sheduler_running(), launchpad);
}

int sys_process_exit(int code)
{
    task_exit(code);
    return 0;
}

int sys_process_cancel(int pid)
{
    int result;

    ATOMIC({
        result = task_cancel(task_getbyid(pid), -1);
    });

    return result;
}

int sys_process_map(uint addr, uint count)
{
    return task_memory_map(sheduler_running(), addr, count);
}

int sys_process_unmap(uint addr, uint count)
{
    return task_memory_unmap(sheduler_running(), addr, count);
}

int sys_process_alloc(uint count)
{
    return task_memory_alloc(sheduler_running(), count);
}

int sys_process_free(uint addr, uint count)
{
    task_memory_free(sheduler_running(), addr, count);
    return 0;
}

int sys_process_get_cwd(char *buffer, uint size)
{
    task_get_cwd(sheduler_running(), buffer, size);
    return 0;
}

int sys_process_set_cwd(const char *path)
{
    return task_set_cwd(sheduler_running(), path);
}

int sys_process_sleep(int time)
{
    return task_sleep(sheduler_running(), time);
}

int sys_process_wakeup(int tid)
{
    int result;

    ATOMIC({
        result = task_wakeup(task_getbyid(tid));
    });

    return result;
}

int sys_process_wait(int tid, int *exitvalue)
{
    return task_wait(tid, exitvalue);
}

/* --- Shared memory -------------------------------------------------------- */

int sys_shared_memory_alloc(int pagecount)
{
    return task_shared_memory_alloc(sheduler_running(), pagecount);
}

int sys_shared_memory_acquire(int shm, uint *addr)
{
    return task_shared_memory_acquire(sheduler_running(), shm, addr);
}

int sys_shared_memory_release(int shm)
{
    return task_shared_memory_release(sheduler_running(), shm);
}

/* --- Messaging ------------------------------------------------------------ */

int sys_messaging_send(message_t *event)
{
    return task_messaging_send(sheduler_running(), event);
}

int sys_messaging_broadcast(const char *channel, message_t *event)
{
    return task_messaging_broadcast(sheduler_running(), channel, event);
}

int sys_messaging_request(message_t *request, message_t *result, int timeout)
{
    return task_messaging_request(sheduler_running(), request, result, timeout);
}

int sys_messaging_receive(message_t *message, int wait)
{
    return task_messaging_receive(sheduler_running(), message, wait);
}

int sys_messaging_respond(message_t *request, message_t *result)
{
    return task_messaging_respond(sheduler_running(), request, result);
}

int sys_messaging_subscribe(const char *channel)
{
    return task_messaging_subscribe(sheduler_running(), channel);
}

int sys_messaging_unsubscribe(const char *channel)
{
    return task_messaging_unsubscribe(sheduler_running(), channel);
}

/* --- Filesystem ----------------------------------------------------------- */

int sys_filesystem_mkdir(const char *dir_path)
{
    Path *path = task_cwd_resolve(sheduler_running(), dir_path);

    int result = filesystem_mkdir(path);

    path_delete(path);

    return result;
}

int sys_filesystem_mkpipe(const char *fifo_path)
{
    Path *path = task_cwd_resolve(sheduler_running(), fifo_path);

    int result = filesystem_mkpipe(path);

    path_delete(path);

    return result;
}

int sys_filesystem_link(const char *old_path, const char *new_path)
{
    Path *oldp = task_cwd_resolve(sheduler_running(), old_path);
    Path *newp = task_cwd_resolve(sheduler_running(), new_path);

    int result = filesystem_mklink(oldp, newp);

    path_delete(oldp);
    path_delete(newp);

    return result;
}

int sys_filesystem_unlink(const char *link_path)
{
    Path *path = task_cwd_resolve(sheduler_running(), link_path);

    int result = filesystem_unlink(path);

    path_delete(path);

    return result;
}

int sys_filesystem_rename(const char *old_path, const char *new_path)
{
    Path *oldp = task_cwd_resolve(sheduler_running(), old_path);
    Path *newp = task_cwd_resolve(sheduler_running(), new_path);

    int result = filesystem_rename(oldp, newp);

    path_delete(oldp);
    path_delete(newp);

    return result;
}

/* --- Sytem info getter ---------------------------------------------------- */

int sys_system_get_info(SystemInfo *info)
{
    strlcpy(info->kernel_name, __kernel_name, SYSTEM_INFO_FIELD_SIZE);

    snprintf(info->kernel_release, SYSTEM_INFO_FIELD_SIZE,
             __kernel_version_format,

             __kernel_version_major,
             __kernel_version_minor,
             __kernel_version_patch,
             __kernel_version_codename);

    strlcpy(info->system_name, "skift", SYSTEM_INFO_FIELD_SIZE);

    // FIXME: this should not be hard coded.
    strlcpy(info->machine, "machine", SYSTEM_INFO_FIELD_SIZE);

    return ERR_SUCCESS;
}

int sys_system_get_status(SystemStatus *status)
{
    // FIXME: get a real uptime value;
    status->uptime = 0;

    status->total_ram = memory_get_total();
    status->used_ram = memory_get_used();

    status->running_tasks = task_count();

    return -ERR_SUCCESS;
}

int sys_system_get_time(TimeStamp *timestamp)
{
    *timestamp = clock_now();

    return -ERR_SUCCESS;
}

int sys_system_get_ticks()
{
    return sheduler_get_ticks();
}

/* --- Handles -------------------------------------------------------------- */

int sys_handle_open(int *handle, const char *path, OpenFlag flags)
{
    if (!syscall_validate_ptr((uintptr_t)handle, sizeof(int)))
    {
        return ERR_BAD_ADDRESS;
    }

    return task_fshandle_open(sheduler_running(), handle, path, flags);
}

int sys_handle_close(int handle)
{
    return task_fshandle_close(sheduler_running(), handle);
}

int sys_handle_select(int *handles, SelectEvent *events, size_t count, int *selected)
{
    if (!syscall_validate_ptr((uintptr_t)handles, sizeof(int) * count) ||
        !syscall_validate_ptr((uintptr_t)events, sizeof(SelectEvent) * count) ||
        !syscall_validate_ptr((uintptr_t)selected, sizeof(int)))
    {
        return ERR_BAD_ADDRESS;
    }

    return task_fshandle_select(sheduler_running(), handles, events, count, selected);
}

int sys_handle_read(int handle, char *buffer, size_t size, size_t *readed)
{
    if (!syscall_validate_ptr((uintptr_t)buffer, size) ||
        !syscall_validate_ptr((uintptr_t)readed, sizeof(size_t)))
    {
        return ERR_BAD_ADDRESS;
    }

    return task_fshandle_read(sheduler_running(), handle, buffer, size, readed);
}

int sys_handle_write(int handle, const char *buffer, size_t size, size_t *written)
{
    if (!syscall_validate_ptr((uintptr_t)buffer, size) ||
        !syscall_validate_ptr((uintptr_t)written, sizeof(size_t)))
    {
        return ERR_BAD_ADDRESS;
    }

    return task_fshandle_write(sheduler_running(), handle, buffer, size, written);
}

int sys_handle_call(int handle, int request, void *args)
{
    return task_fshandle_call(sheduler_running(), handle, request, args);
}

int sys_handle_seek(int handle, int offset, Whence whence)
{
    return task_fshandle_seek(sheduler_running(), handle, offset, whence);
}

int sys_handle_tell(int handle, Whence whence, int *offset)
{
    if (!syscall_validate_ptr((uintptr_t)offset, sizeof(int)))
    {
        return ERR_BAD_ADDRESS;
    }

    return task_fshandle_tell(sheduler_running(), handle, whence, offset);
}

int sys_handle_stat(int handle, FileState *state)
{
    if (!syscall_validate_ptr((uintptr_t)state, sizeof(FileState)))
    {
        return ERR_BAD_ADDRESS;
    }

    return task_fshandle_stat(sheduler_running(), handle, state);
}

int sys_handle_connect(int *handle, const char *path)
{
    return task_fshandle_connect(sheduler_running(), handle, path);
}

int sys_handle_accept(int handle, int *connection_handle)
{
    return task_fshandle_accept(sheduler_running(), handle, connection_handle);
}

int sys_handle_send(int handle, Message *message)
{
    return task_fshandle_send(sheduler_running(), handle, message);
}

int sys_handle_receive(int handle, Message *message)
{
    return task_fshandle_receive(sheduler_running(), handle, message);
}

int sys_handle_payload(int handle, Message *message)
{
    return task_fshandle_payload(sheduler_running(), handle, message);
}

int sys_handle_discard(int handle)
{
    return task_fshandle_discard(sheduler_running(), handle);
}

static int (*syscalls[__SYSCALL_COUNT])() = {
    [SYS_PROCESS_THIS] = sys_process_this,
    [SYS_PROCESS_LAUNCH] = sys_process_launch,
    [SYS_PROCESS_EXIT] = sys_process_exit,
    [SYS_PROCESS_CANCEL] = sys_process_cancel,
    [SYS_PROCESS_SLEEP] = sys_process_sleep,
    [SYS_PROCESS_WAKEUP] = sys_process_wakeup,
    [SYS_PROCESS_WAIT] = sys_process_wait,
    [SYS_PROCESS_GET_CWD] = sys_process_get_cwd,
    [SYS_PROCESS_SET_CWD] = sys_process_set_cwd,
    [SYS_PROCESS_MAP] = sys_process_map,
    [SYS_PROCESS_UNMAP] = sys_process_unmap,
    [SYS_PROCESS_ALLOC] = sys_process_alloc,
    [SYS_PROCESS_FREE] = sys_process_free,

    [SYS_SHARED_MEMORY_ALLOC] = sys_shared_memory_alloc,
    [SYS_SHARED_MEMORY_ACQUIRE] = sys_shared_memory_acquire,
    [SYS_SHARED_MEMORY_RELEASE] = sys_shared_memory_release,

    [SYS_MESSAGING_SEND] = sys_messaging_send,
    [SYS_MESSAGING_BROADCAST] = sys_messaging_broadcast,
    [SYS_MESSAGING_REQUEST] = sys_messaging_request,
    [SYS_MESSAGING_RECEIVE] = sys_messaging_receive,
    [SYS_MESSAGING_RESPOND] = sys_messaging_respond,
    [SYS_MESSAGING_SUBSCRIBE] = sys_messaging_subscribe,
    [SYS_MESSAGING_UNSUBSCRIBE] = sys_messaging_unsubscribe,

    [SYS_FILESYSTEM_MKDIR] = sys_filesystem_mkdir,
    [SYS_FILESYSTEM_MKPIPE] = sys_filesystem_mkpipe,
    [SYS_FILESYSTEM_LINK] = sys_filesystem_link,
    [SYS_FILESYSTEM_UNLINK] = sys_filesystem_unlink,
    [SYS_FILESYSTEM_RENAME] = sys_filesystem_rename,

    [SYS_SYSTEM_GET_INFO] = sys_system_get_info,
    [SYS_SYSTEM_GET_STATUS] = sys_system_get_status,
    [SYS_SYSTEM_GET_TIME] = sys_system_get_time,
    [SYS_SYSTEM_GET_TICKS] = sys_system_get_ticks,

    [SYS_HANDLE_OPEN] = sys_handle_open,
    [SYS_HANDLE_CLOSE] = sys_handle_close,
    [SYS_HANDLE_SELECT] = sys_handle_select,
    [SYS_HANDLE_READ] = sys_handle_read,
    [SYS_HANDLE_WRITE] = sys_handle_write,
    [SYS_HANDLE_CALL] = sys_handle_call,
    [SYS_HANDLE_SEEK] = sys_handle_seek,
    [SYS_HANDLE_TELL] = sys_handle_tell,
    [SYS_HANDLE_STAT] = sys_handle_stat,
    [SYS_HANDLE_CONNECT] = sys_handle_connect,
    [SYS_HANDLE_ACCEPT] = sys_handle_accept,
    [SYS_HANDLE_SEND] = sys_handle_send,
    [SYS_HANDLE_RECEIVE] = sys_handle_receive,
    [SYS_HANDLE_PAYLOAD] = sys_handle_payload,
    [SYS_HANDLE_DISCARD] = sys_handle_discard,
};

syscall_handler_t syscall_get_handler(Syscall syscall)
{
    if (syscall >= 0 && syscall < __SYSCALL_COUNT)
    {
        if ((syscall_handler_t)syscalls[syscall] == NULL)
        {
            logger_error("Syscall not implemented ID=%d call by PROCESS=%d.", syscall, sheduler_running_id());
        }

        return (syscall_handler_t)syscalls[syscall];
    }
    else
    {
        logger_error("Unknow syscall ID=%d call by PROCESS=%d.", syscall, sheduler_running_id());
        return NULL;
    }
}

#define SYSCALL_NAMES_ENTRY(__entry) #__entry,

static const char *syscall_names[] = {SYSCALL_LIST(SYSCALL_NAMES_ENTRY)};

void syscall_dispatcher(processor_context_t *context)
{
    Syscall syscall = (Syscall)context->eax;
    syscall_handler_t handler = syscall_get_handler(syscall);

    if (handler != NULL)
    {
        context->eax = handler(context->ebx, context->ecx, context->edx, context->esi, context->edi);
    }
    else
    {
        logger_info("context: EBX=%08x, ECX=%08x, EDX=%08x, ESI=%08x, EDI=%08x", context->ebx, context->ecx, context->edx, context->esi, context->edi);
        context->eax = -ERR_FUNCTION_NOT_IMPLEMENTED;
    }

    if (syscall >= SYS_HANDLE_OPEN && context->eax != ERR_SUCCESS)
    {
        logger_info("Syscall %s(0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x) returned %s", syscall_names[syscall], context->ebx, context->ecx, context->edx, context->esi, context->edi, error_to_string(context->eax));
    }

    if (syscall < SYS_HANDLE_OPEN && (int)context->eax < 0)
    {
        logger_info("Syscall %s(0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x) returned %s", syscall_names[syscall], context->ebx, context->ecx, context->edx, context->esi, context->edi, error_to_string(-context->eax));
    }
}