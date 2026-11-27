/* Fail the native build if any complete fixed protocol message cannot fit. */
#include "tmux.h"

typedef char tmux_identify_capacity[
    sizeof(struct msg_identify_data) + IMSG_HEADER_SIZE <= MAX_IMSGSIZE ? 1 : -1];
typedef char tmux_command_capacity[
    sizeof(struct msg_command_data) + IMSG_HEADER_SIZE <= MAX_IMSGSIZE ? 1 : -1];
typedef char tmux_lock_capacity[
    sizeof(struct msg_lock_data) + IMSG_HEADER_SIZE <= MAX_IMSGSIZE ? 1 : -1];
typedef char tmux_environ_capacity[
    sizeof(struct msg_environ_data) + IMSG_HEADER_SIZE <= MAX_IMSGSIZE ? 1 : -1];
typedef char tmux_print_capacity[
    sizeof(struct msg_print_data) + IMSG_HEADER_SIZE <= MAX_IMSGSIZE ? 1 : -1];
typedef char tmux_shell_capacity[
    sizeof(struct msg_shell_data) + IMSG_HEADER_SIZE <= MAX_IMSGSIZE ? 1 : -1];
