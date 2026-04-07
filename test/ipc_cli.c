/*
 * ipc_cli.c -- Interactive CLI for manual IPC testing
 *
 * An interactive command-line interface for testing NCD service IPC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ipc_test_common.h"
#include "../src/control_ipc.h"

#if !NCD_PLATFORM_WINDOWS
#include <unistd.h>
#include <termios.h>
#else
#include <windows.h>
#include <conio.h>
#endif

#define MAX_LINE_LEN 1024
#define HISTORY_SIZE 100

static char *command_history[HISTORY_SIZE];
static int history_count = 0;
static int history_pos = 0;

static void print_usage(void) {
    printf("NCD IPC CLI v%s\n", IPC_TEST_VERSION);
    printf("Type 'help' for commands, 'quit' to exit.\n\n");
}

static void print_help(void) {
    printf("Available commands:\n");
    printf("  ping [count]                Send ping request(s)\n");
    printf("  state                       Get state information\n");
    printf("  version                     Get service version\n");
    printf("  heuristic <search> <path>   Submit heuristic update\n");
    printf("  metadata <type> [args...]   Submit metadata update\n");
    printf("  rescan [full|drive|path]    Request rescan\n");
    printf("  flush                       Request persistence\n");
    printf("  shutdown [--force]          Request service shutdown\n");
    printf("  verbose                     Toggle verbose mode\n");
    printf("  help                        Show this help\n");
    printf("  quit, exit                  Exit CLI\n");
    printf("\nMetadata update types:\n");
    printf("  metadata group-add @name <path>\n");
    printf("  metadata exclusion-add <pattern>\n");
    printf("  metadata config <key>=<value>\n");
    printf("  metadata clear-history\n");
}

static void add_to_history(const char *line) {
    if (history_count < HISTORY_SIZE) {
        command_history[history_count] = strdup(line);
        history_count++;
    } else {
        free(command_history[0]);
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            command_history[i] = command_history[i + 1];
        }
        command_history[HISTORY_SIZE - 1] = strdup(line);
    }
    history_pos = history_count;
}

static char *read_line(void) {
    static char line[MAX_LINE_LEN];
    int pos = 0;
    int len = 0;
    int display_pos = 0;
    
    memset(line, 0, sizeof(line));
    
#if NCD_PLATFORM_WINDOWS
    /* Windows: simple line reading */
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hStdin, &mode);
    SetConsoleMode(hStdin, mode & ~ENABLE_ECHO_INPUT & ~ENABLE_LINE_INPUT);
    
    while (1) {
        DWORD read;
        INPUT_RECORD rec;
        if (!ReadConsoleInput(hStdin, &rec, 1, &read)) break;
        
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            char c = rec.Event.KeyEvent.uChar.AsciiChar;
            WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
            
            if (c == '\r' || c == '\n') {
                printf("\n");
                break;
            } else if (vk == VK_ESCAPE) {
                line[0] = '\0';
                printf("\n");
                break;
            } else if (vk == VK_BACK) {
                if (pos > 0) {
                    pos--;
                    printf("\b \b");
                }
            } else if (c >= 32 && c < 127 && pos < MAX_LINE_LEN - 1) {
                line[pos++] = c;
                putchar(c);
            }
        }
    }
    
    SetConsoleMode(hStdin, mode);
    line[pos] = '\0';
    
#else
    /* POSIX: use termios for non-canonical mode */
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    
    while (1) {
        int c = getchar();
        
        if (c == '\n' || c == '\r') {
            printf("\n");
            break;
        } else if (c == 27) {  /* Escape sequence */
            int c2 = getchar();
            if (c2 == '[') {
                int c3 = getchar();
                if (c3 == 'A') {  /* Up arrow */
                    if (history_pos > 0) {
                        history_pos--;
                        /* Clear current line */
                        while (pos > 0) {
                            printf("\b \b");
                            pos--;
                        }
                        /* Print history entry */
                        strcpy(line, command_history[history_pos]);
                        pos = strlen(line);
                        printf("%s", line);
                    }
                } else if (c3 == 'B') {  /* Down arrow */
                    if (history_pos < history_count - 1) {
                        history_pos++;
                        while (pos > 0) {
                            printf("\b \b");
                            pos--;
                        }
                        strcpy(line, command_history[history_pos]);
                        pos = strlen(line);
                        printf("%s", line);
                    } else if (history_pos == history_count - 1) {
                        history_pos = history_count;
                        while (pos > 0) {
                            printf("\b \b");
                            pos--;
                        }
                        line[0] = '\0';
                        pos = 0;
                    }
                }
            } else if (c2 == 27) {  /* Double escape = cancel */
                line[0] = '\0';
                printf("\n");
                pos = 0;
                break;
            }
        } else if (c == 127 || c == 8) {  /* Backspace */
            if (pos > 0) {
                pos--;
                printf("\b \b");
            }
        } else if (c >= 32 && c < 127 && pos < MAX_LINE_LEN - 1) {
            line[pos++] = c;
            putchar(c);
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    line[pos] = '\0';
#endif
    
    return line;
}

static void trim_whitespace(char *str) {
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

static int get_token(char *line, char *token, int max_len) {
    int i = 0;
    int pos = 0;
    
    /* Skip leading whitespace */
    while (isspace((unsigned char)line[i])) i++;
    
    /* Read token */
    while (line[i] && !isspace((unsigned char)line[i]) && pos < max_len - 1) {
        token[pos++] = line[i++];
    }
    token[pos] = '\0';
    
    return i;
}

static void cmd_ping(char *args) {
    int count = 1;
    char count_str[32];
    int n = get_token(args, count_str, sizeof(count_str));
    
    if (count_str[0]) {
        count = atoi(count_str);
        if (count < 1) count = 1;
        if (count > 100) count = 100;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("PING: FAILED (service not running)\n");
        return;
    }
    
    int success = 0;
    for (int i = 0; i < count; i++) {
        ipc_time_t start, end;
        ipc_get_time(&start);
        
        NcdIpcResult result = ipc_client_ping(client);
        
        ipc_get_time(&end);
        double elapsed_ms = ipc_elapsed_ms(&start, &end);
        
        if (result == NCD_IPC_OK) {
            success++;
            if (count == 1) {
                printf("PING: OK (%.2f ms)\n", elapsed_ms);
            } else {
                printf("PING %d: OK (%.2f ms)\n", i + 1, elapsed_ms);
            }
        } else {
            if (count == 1) {
                printf("PING: FAILED (%s)\n", ipc_error_string(result));
            } else {
                printf("PING %d: FAILED (%s)\n", i + 1, ipc_error_string(result));
            }
        }
    }
    
    ipc_client_disconnect(client);
    
    if (count > 1) {
        printf("--- %d pings, %d successful ---\n", count, success);
    }
}

static void cmd_state(void) {
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("Service not running\n");
        return;
    }
    
    NcdIpcStateInfo info;
    NcdIpcResult result = ipc_client_get_state_info(client, &info);
    ipc_client_disconnect(client);
    
    if (result == NCD_IPC_OK) {
        printf("PROTOCOL:   %u\n", info.protocol_version);
        printf("ENCODING:   %s\n", get_encoding_name(info.text_encoding));
        printf("META_GEN:   %llu\n", (unsigned long long)info.meta_generation);
        printf("DB_GEN:     %llu\n", (unsigned long long)info.db_generation);
        printf("META_SIZE:  %u\n", info.meta_size);
        printf("DB_SIZE:    %u\n", info.db_size);
        printf("META_SHM:   %s\n", info.meta_name);
        printf("DB_SHM:     %s\n", info.db_name);
    } else {
        printf("Error: %s\n", ipc_error_string(result));
    }
}

static void cmd_version(void) {
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("Service not running\n");
        return;
    }
    
    NcdIpcVersionInfo info;
    NcdIpcResult result = ipc_client_get_version(client, &info);
    ipc_client_disconnect(client);
    
    if (result == NCD_IPC_OK) {
        printf("APP VERSION:      %s\n", info.app_version);
        printf("BUILD STAMP:      %s\n", info.build_stamp);
        printf("PROTOCOL VERSION: %u\n", info.protocol_version);
    } else {
        printf("Error: %s\n", ipc_error_string(result));
    }
}

static void cmd_heuristic(char *args) {
    char search[256] = {0};
    char path[1024] = {0};
    
    /* Parse search term (first token) */
    int n = get_token(args, search, sizeof(search));
    args += n;
    
    /* Parse path (rest of line) */
    while (isspace((unsigned char)*args)) args++;
    strncpy(path, args, sizeof(path) - 1);
    
    if (!search[0] || !path[0]) {
        printf("Usage: heuristic <search> <path>\n");
        return;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("HEURISTIC: FAILED (service not running)\n");
        return;
    }
    
    NcdIpcResult result = ipc_client_submit_heuristic(client, search, path);
    ipc_client_disconnect(client);
    
    if (result == NCD_IPC_OK) {
        printf("HEURISTIC: ACCEPTED\n");
    } else {
        printf("HEURISTIC: REJECTED (%s)\n", ipc_error_string(result));
    }
}

static void cmd_rescan(char *args) {
    char type[32] = "full";
    get_token(args, type, sizeof(type));
    
    bool drive_mask[26] = {false};
    bool scan_root_only = false;
    
    if (strcmp(type, "full") == 0) {
        for (int i = 0; i < 26; i++) drive_mask[i] = true;
    } else if (strlen(type) == 1 && type[0] >= 'A' && type[0] <= 'Z') {
        drive_mask[type[0] - 'A'] = true;
    } else if (strlen(type) == 1 && type[0] >= 'a' && type[0] <= 'z') {
        drive_mask[type[0] - 'a'] = true;
    } else {
        printf("Usage: rescan [full|<drive-letter>]\n");
        return;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("RESCAN: FAILED (service not running)\n");
        return;
    }
    
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, scan_root_only);
    ipc_client_disconnect(client);
    
    if (result == NCD_IPC_OK) {
        printf("RESCAN: ACCEPTED\n");
    } else {
        printf("RESCAN: REJECTED (%s)\n", ipc_error_string(result));
    }
}

static void cmd_flush(void) {
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("FLUSH: FAILED (service not running)\n");
        return;
    }
    
    NcdIpcResult result = ipc_client_request_flush(client);
    ipc_client_disconnect(client);
    
    if (result == NCD_IPC_OK) {
        printf("FLUSH: SUCCESS\n");
    } else {
        printf("FLUSH: FAILED (%s)\n", ipc_error_string(result));
    }
}

static void cmd_shutdown(char *args) {
    (void)args; /* Unused for now */
    
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("SHUTDOWN: FAILED (service not running)\n");
        return;
    }
    
    printf("Requesting graceful shutdown...\n");
    NcdIpcResult result = ipc_client_request_shutdown(client);
    ipc_client_disconnect(client);
    
    if (result == NCD_IPC_OK) {
        printf("SHUTDOWN: ACCEPTED\n");
    } else {
        printf("SHUTDOWN: REJECTED (%s)\n", ipc_error_string(result));
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* Initialize IPC client */
    if (ipc_client_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize IPC client\n");
        return IPC_EXIT_ERROR;
    }
    
    print_usage();
    
    bool verbose = false;
    
    while (1) {
        /* Print prompt */
        if (ipc_service_exists()) {
            printf("[ncd-ipc] > ");
        } else {
            printf("[ncd-ipc:OFFLINE] > ");
        }
        fflush(stdout);
        
        /* Read command */
        char *line = read_line();
        if (!line[0]) continue;
        
        add_to_history(line);
        trim_whitespace(line);
        if (!line[0]) continue;
        
        /* Parse command */
        char cmd[64];
        int n = get_token(line, cmd, sizeof(cmd));
        char *args = line + n;
        
        /* Execute command */
        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            printf("Goodbye.\n");
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            print_help();
        } else if (strcmp(cmd, "ping") == 0) {
            cmd_ping(args);
        } else if (strcmp(cmd, "state") == 0) {
            cmd_state();
        } else if (strcmp(cmd, "version") == 0) {
            cmd_version();
        } else if (strcmp(cmd, "heuristic") == 0) {
            cmd_heuristic(args);
        } else if (strcmp(cmd, "rescan") == 0) {
            cmd_rescan(args);
        } else if (strcmp(cmd, "flush") == 0) {
            cmd_flush();
        } else if (strcmp(cmd, "shutdown") == 0) {
            cmd_shutdown(args);
        } else if (strcmp(cmd, "verbose") == 0) {
            verbose = !verbose;
            printf("Verbose mode: %s\n", verbose ? "ON" : "OFF");
        } else {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for available commands.\n");
        }
        
        printf("\n");
    }
    
    /* Cleanup */
    for (int i = 0; i < history_count; i++) {
        free(command_history[i]);
    }
    
    ipc_client_cleanup();
    return IPC_EXIT_SUCCESS;
}
