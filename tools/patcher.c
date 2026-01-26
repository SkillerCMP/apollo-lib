#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "apollo.h"

#ifdef __PS3_PC__
#define CLI_VERSION     APOLLO_LIB_VERSION " PS3/big-endian"
#else
#define CLI_VERSION     APOLLO_LIB_VERSION
#endif

static int log = 0;

static void ensure_trailing_sep(char* path, size_t cap)
{
    if (!path || cap == 0) return;
    size_t n = strlen(path);
    if (n == 0) return;

    char c = path[n-1];
    if (c == '/' || c == '\\') return;

#ifdef _WIN32
    char sep = '\\';
#else
    // Prefer the style already used in the path, otherwise '/'
    char sep = (strchr(path, '\\') && !strchr(path, '/')) ? '\\' : '/';
#endif

    if (n + 1 < cap)
    {
        path[n] = sep;
        path[n+1] = 0;
    }
}

static void* cli_host_callback(int id, int* size)
{
    static char data_path[1024];

    switch (id)
    {
    case APOLLO_HOST_DATA_PATH:
    case APOLLO_HOST_TEMP_PATH:
        if (!data_path[0])
        {
            const char* env = getenv("APOLLO_DATA_PATH");
            if (env && *env)
            {
                strncpy(data_path, env, sizeof(data_path)-2);
                data_path[sizeof(data_path)-2] = 0;
            }
            else
            {
#ifdef _WIN32
                char mod[1024];
                DWORD l = GetModuleFileNameA(NULL, mod, (DWORD)sizeof(mod));
                if (l > 0 && l < sizeof(mod))
                {
                    mod[l] = 0;
                    // strip filename, keep directory
                    for (int i = (int)l - 1; i >= 0; --i)
                    {
                        if (mod[i] == '\\' || mod[i] == '/')
                        {
                            mod[i+1] = 0;
                            break;
                        }
                    }
                    strncpy(data_path, mod, sizeof(data_path)-2);
                    data_path[sizeof(data_path)-2] = 0;
                }
                else
                {
                    strncpy(data_path, ".\\", sizeof(data_path)-2);
                    data_path[sizeof(data_path)-2] = 0;
                }
#else
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)))
                {
                    strncpy(data_path, cwd, sizeof(data_path)-2);
                    data_path[sizeof(data_path)-2] = 0;
                }
                else
                {
                    strncpy(data_path, "./", sizeof(data_path)-2);
                    data_path[sizeof(data_path)-2] = 0;
                }
#endif
            }

            ensure_trailing_sep(data_path, sizeof(data_path));
        }

        if (size) *size = (int)strlen(data_path);
        return data_path;

    default:
        if (size) *size = 1;
        return "";
    }
}


void print_usage(const char* argv0)
{
    printf("USAGE: %s file.savepatch 1,2,7-10,18 [data-file.bin]\n\n", argv0);
    printf("  file.savepatch: The cheat patch file to apply\n");
    printf("  1,2,7-10,18:    The list of codes to apply\n");
    printf("  data-file.bin:  The target file to patch\n\n");
    return;
}

void dbglogger_log(const char* fmt, ...)
{
    if (!log)
        return;

    char buffer[0x800];

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);

    printf("- %s\n", buffer);
}

int is_active_code(const char* a, int id)
{
    int val, end;
    char* arg = strdup(a);

    for (char* tmp = strtok(arg, ","); tmp; tmp = strtok(NULL, ","))
    {
        if (((sscanf(tmp, "%d-%d", &val, &end) == 2) && (id >= val && id <= end)) ||
            ((sscanf(tmp, "%d", &val) == 1) && (val == id)))
        {
            free(arg);
            return 1;
        }
    }

    free(arg);
    return 0;
}

static void get_user_options(code_entry_t* entry)
{
    option_value_t* val;

    printf("\n[%s] Options:\n", entry->name);
    for (int j, i=0; i<entry->options_count; i++)
    {
        j = 0;
        printf("  Tag: %s\n", entry->options[i].line);
        for (list_node_t* node = list_head(entry->options[i].opts); (val = list_get(node)); node = list_next(node))
        {
            printf("%4d.  %s\n", j++, val->name);
        }
        printf("\n Select option: ");
        scanf("%d", &entry->options[i].sel);

        val = list_get_item(entry->options[i].opts, entry->options[i].sel);
        if (val)
        {
            printf("  Selected: %s\n", val->name);
            printf("  Value   : %s\n\n", val->value);
        }
    }
}

int main(int argc, char **argv)
{
    size_t len;
    char *data;
    list_t* list_codes;

    printf("\nApollo cheat patcher v%s - (c) 2022-2026 by Bucanero\n\n", CLI_VERSION);

    if (--argc < 2)
    {
        print_usage(argv[0]);
        return -1;
    }

    if (strchr(argv[1], '\n') && strchr(argv[1], '[') && strchr(argv[1], ']'))
    {
        // Direct string codes as input
        len = strlen(argv[1]);
        data = malloc(len);
        memcpy(data, argv[1], len);
    }
    else if (read_buffer(argv[1], (uint8_t **) &data, &len) != 0)
    {
        printf("[*] Could Not Access The File (%s)\n", argv[1]);
        return -1;
    }
    data = realloc(data, len+1);
    data[len] = 0;

    code_entry_t* code = calloc(1, sizeof(code_entry_t));
    code->name = argv[1];
    code->file = argv[1];

    list_codes = list_alloc();
    list_append(list_codes, code);
    load_patch_code_list(data, list_codes, NULL, NULL);
    free(data);

    list_node_t *node = list_head(list_codes);

    printf("[i] Applying codes [%s] to %s...\n", argv[2], (argc == 2) ? "script target file" : argv[3]);

    for (len=1, node = list_next(node); (code = list_get(node)); node = list_next(node), len++)
    {
        if (code->activated || is_active_code(argv[2], len))
        {
            log++;
            if (code->options_count)
                get_user_options(code);

            printf("\n===============[ Applying code #%ld ]===============\n", len);
            if (apply_cheat_patch_code((argc == 2) ? code->file : argv[3], code, cli_host_callback))
                printf("- OK\n");
            else
                printf("- ERROR!\n");
        }
    }

    free_patch_var_list();
    printf("\nPatching completed: %d codes applied\n\n", log);
}
