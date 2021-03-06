// M.I.S.H.A. - Misha's Interactive Shell Advance
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/wait.h>

#define BUF_SIZE 1024

char *read_arbitrary_length(unsigned long *result_len) {
    char buf[BUF_SIZE];
    char *result = malloc(0);
    int count = read(0, buf, BUF_SIZE);

    while (count == BUF_SIZE) {
        *result_len += BUF_SIZE;
        result = realloc(result, *result_len);
        memcpy(result + *result_len - BUF_SIZE, buf, BUF_SIZE);
        count = read(0, buf, BUF_SIZE);
    }

    if (count != 0) {
        *result_len += count;
        result = realloc(result, *result_len);
        memcpy(result + *result_len - count, buf, count);
    }

    return result;
}

char *read_no_enter(unsigned long *result_len) {
    char *x = read_arbitrary_length(result_len);
    if (*result_len != 0) {
        *result_len -= 1;
    }
    return x;
}

enum Tree_type {
    TREE_PARENTHESES, TREE_ARGUMENT, TREE_PIPE, TREE_TO_FILE
};

struct Tree_node {
    enum Tree_type type;
    void *parent;
    void *data;
};

struct Tree_parentheses_data {
    struct Tree_node **arr;  // always has space for one more
    size_t arr_len;
};

struct Tree_argument_data {
    char *s;
    size_t len;
};

struct Tree_pipe_data {
    struct Tree_node *a;
    struct Tree_node *b;
};

struct Tree_to_file_data {
    struct Tree_node *a;
    struct Tree_argument_data *out;
};

void Tree_print(struct Tree_node *tree) {
    if (tree->type == TREE_PARENTHESES) {
        write(1, "Parentheses(", 12);
        struct Tree_parentheses_data *data = (struct Tree_parentheses_data *)tree->data;
        if (data->arr_len > 0) {
            Tree_print(data->arr[0]);
        }
        for (int i = 1; i < data->arr_len; i++) {
            write(1, ", ", 2);
            Tree_print(data->arr[i]);
        }
        write(1, ")", 1);
    } else if (tree->type == TREE_ARGUMENT) {
        struct Tree_argument_data *data = (struct Tree_argument_data *)tree->data;
        write(1, "Argument(", 9);
        write(1, data->s, data->len);
        write(1, ")", 1);
    } else if (tree->type == TREE_TO_FILE) {
        struct Tree_to_file_data *data = (struct Tree_to_file_data *)tree->data;
        write(1, "ToFile(", 7);
        Tree_print(data->a);
        write(1, ", ", 2);
        write(1, data->out->s, data->out->len);
        write(1, ")", 1);
    } else if (tree->type == TREE_PIPE) {
        struct Tree_pipe_data *data = (struct Tree_pipe_data *)tree->data;
        write(1, "Pipe(", 5);
        Tree_print(data->a);
        write(1, ", ", 2);
        Tree_print(data->b);
        write(1, ")", 1);
    }
}

void Tree_free(struct Tree_node *tree) {
    if (tree->type == TREE_PARENTHESES) {
        struct Tree_parentheses_data *data = (struct Tree_parentheses_data *)tree->data;
        for (int i = 0; i < data->arr_len; i++) {
            Tree_free(data->arr[i]);
        }
        free(data->arr);
        free(data);
    } else if (tree->type == TREE_ARGUMENT) {
        struct Tree_argument_data *data = (struct Tree_argument_data *)tree->data;
        free(data->s);
        free(data);
    } else if (tree->type == TREE_TO_FILE) {
        struct Tree_to_file_data *data = (struct Tree_to_file_data *)tree->data;
        Tree_free(data->a);
        free(data->out->s);
        free(data->out);
        free(data);
    } else if (tree->type == TREE_PIPE) {
        struct Tree_pipe_data *data = (struct Tree_pipe_data *)tree->data;
        Tree_free(data->a);
        Tree_free(data->b);
        free(data);
    }
    free(tree);
}

struct Tree_node *init_parentheses() {
    struct Tree_node *p = calloc(1, sizeof(struct Tree_node));
    p->type = TREE_PARENTHESES;

    struct Tree_parentheses_data *data = malloc(sizeof(struct Tree_parentheses_data));
    data->arr = malloc(sizeof(struct Tree_node));
    data->arr_len = 0;
    p->data = data;

    return p;
}

void append_to_parentheses(struct Tree_node *paren, struct Tree_node *append) {
    append->parent = paren;
    struct Tree_parentheses_data *data = (struct Tree_parentheses_data *)paren->data;
    data->arr[data->arr_len++] = append;
    data->arr = realloc(data->arr, (data->arr_len + 1) * sizeof(struct Tree_node));
}

void finalize_parentheses(struct Tree_node *paren) {
    // finalize - make pipes and stuff happen
    struct Tree_parentheses_data *data = (struct Tree_parentheses_data *)paren->data;
    if (data->arr_len == 3 && (data->arr[1]->type == TREE_TO_FILE || data->arr[1]->type == TREE_PIPE)) {
        if (data->arr[1]->type == TREE_TO_FILE && data->arr[2]->type == TREE_ARGUMENT) {
            struct Tree_to_file_data *to_file_data = malloc(sizeof(struct Tree_to_file_data));
            to_file_data->a = data->arr[0];
            to_file_data->out = data->arr[2]->data;
            free(data->arr[2]);
            data->arr[1]->data = to_file_data;
        } else if (data->arr[1]->type == TREE_PIPE) {
            struct Tree_pipe_data *pipe_data = malloc(sizeof(struct Tree_pipe_data));
            pipe_data->a = data->arr[0];
            pipe_data->b = data->arr[2];
            data->arr[1]->data = pipe_data;
        }
        data->arr[0] = data->arr[1];
        data->arr_len = 1;
    }
}

struct Tree_node *read_and_parse_line() {
    write(1, "> ", 2);
    unsigned long x_len = 0;
    char *cmd = read_no_enter(&x_len);
    // parse it
    // Let's define a simple format: 
    // - double quotes only, and \ to escape anything
    // - write to file with (whatever) > filename
    // - pipe with (whatever) | (grep)
    // - (maybe?) use command as argument with cat $(find -name thing)
    
    // so to parse we need to do a simple tree
    struct Tree_node *tree = init_parentheses();

    for (int i = 0; i < x_len; i++) {
        switch (cmd[i]) {
            case ' ': break;
            case '>': {
                struct Tree_node *p = calloc(1, sizeof(struct Tree_node));
                p->type = TREE_TO_FILE;
                append_to_parentheses(tree, p);
            } break;
            case '|': {
                struct Tree_node *p = calloc(1, sizeof(struct Tree_node));
                p->type = TREE_PIPE;
                append_to_parentheses(tree, p);
            } break;
            case '(': {
                // enter parentheses
                struct Tree_node *new_tree = init_parentheses();
                append_to_parentheses(tree, new_tree);
                tree = new_tree;
            } break;
            case ')': {
                // exit parentheses
                if (tree->parent != NULL) {
                    finalize_parentheses(tree);
                    tree = tree->parent;
                } else {
                    // Panic!
                    write(1, "WTF!!!!!\n", 9);
                }
            } break;
            case '"': {
                // enter quotes
                i++;
                size_t in_quotes_len = 0;
                for (int j = i; j < x_len; j++) {
                    if (cmd[j] == '\\') {
                        j++;
                    } else if (cmd[j] == '"') {
                        break;
                    }
                    in_quotes_len += 1;
                }
                char *in_quotes = malloc(in_quotes_len + 1);
                in_quotes[in_quotes_len] = '\0';
                for (int j = 0; j < in_quotes_len; j++) {
                    if (cmd[i] == '\\') {
                        i++;
                    } else if (cmd[i] == '"') {
                        break;
                    }
                    in_quotes[j] = cmd[i];
                    i++;
                }
                struct Tree_node *p = calloc(1, sizeof(struct Tree_node));
                p->type = TREE_ARGUMENT;

                struct Tree_argument_data *data = malloc(sizeof(struct Tree_argument_data));
                data->len = in_quotes_len;
                data->s = in_quotes;
                p->data = data;

                append_to_parentheses(tree, p);
            } break;
            default: {
                size_t arg_len = 0;
                for (int j = i; j < x_len; j++) {
                    if (cmd[j] == ')' || cmd[j] == ' ') {
                        break;
                    }
                    arg_len++;
                }
                char *in_arg = malloc(arg_len + 1);
                in_arg[arg_len] = 0;
                int first_i = i;
                while (i < x_len && cmd[i] != ')' && cmd[i] != ' ') {
                    in_arg[i - first_i] = cmd[i];
                    i++;
                }
                if (cmd[i] == ')' || cmd[i] == ' ') {
                    i--;
                }

                struct Tree_node *p = calloc(1, sizeof(struct Tree_node));
                p->type = TREE_ARGUMENT;

                struct Tree_argument_data *data = malloc(sizeof(struct Tree_argument_data));
                data->len = arg_len;
                data->s = in_arg;
                p->data = data;

                append_to_parentheses(tree, p);
            } break;
        }
    }

    finalize_parentheses(tree);
    free(cmd);
    return tree;
}

char **Tree_extract_argv(struct Tree_node *tree) {
    if (tree->type == TREE_PARENTHESES) {
        struct Tree_parentheses_data *data = (struct Tree_parentheses_data *)tree->data;
        char **argv = malloc(sizeof(char *) * (data->arr_len + 1));
        int argv_len = 0;
        for (int j = 0; j < data->arr_len; j++) {
            if (data->arr[j]->type == TREE_ARGUMENT) {
                argv[argv_len++] = ((struct Tree_argument_data *)data->arr[j]->data)->s;
            }
        }
        argv[data->arr_len] = NULL;
        return argv;
    } else if (tree->type == TREE_ARGUMENT) {
        char **argv = calloc(1, sizeof(char *) * 2);
        struct Tree_argument_data *data = (struct Tree_argument_data *)tree->data;
        argv[0] = data->s;
        return argv;
    }
    return NULL;
}

bool try_cd_or_exit_or_whatever(char **argv) {
    if (strcmp(argv[0], "exit") == 0) {
        exit(0);
    } else if (strcmp(argv[0], "cd") == 0) {
        // change PWD env variable...
        write(1, "TODO: implement cd\n", 19);
        return true;
    }
    return false;
}

void look_in_path(char **name) {
    if (access(*name, F_OK) == 0) {
        return;  // file exists
    }
    char *path = strdup(getenv("PATH"));
    char *saveptr = NULL;
    char *token = strtok_r(path, ":", &saveptr);
    while (token != NULL) {
        unsigned long token_len = strlen(token);
        unsigned long argv0_len = strlen(*name);
        char *token_plus_me = malloc(token_len + 2 + argv0_len);
        strcpy(token_plus_me, token);
        token_plus_me[token_len] = '/';
        strcpy(token_plus_me + token_len + 1, *name);

        if (access(token_plus_me, F_OK) == 0) {
            *name = token_plus_me;
            free(path);
            return;
        }
        free(token_plus_me);

        token = strtok_r(NULL, ":", &saveptr);
    }
    *name = NULL;
    free(path);
}

void Parentheses_execute(struct Tree_node *tree) {
    struct Tree_parentheses_data *data = (struct Tree_parentheses_data *)tree->data;
    if (data->arr_len == 1) {
        if (data->arr[0]->type == TREE_PARENTHESES) return Parentheses_execute(data->arr[0]);  // useless parentheses - run whatever's inside
        else if (data->arr[0]->type == TREE_TO_FILE) {
            // To file!
            struct Tree_to_file_data *ttf_data = (struct Tree_to_file_data *)data->arr[0]->data;
            char **argv = Tree_extract_argv(ttf_data->a);
            if (!try_cd_or_exit_or_whatever(argv)) {
                char *cmd = argv[0];
                look_in_path(&cmd);
                if (cmd == NULL) {
                    write(2, "Not found.\n", 11);
                    return;
                }
                if (fork() == 0) {
                    close(1);
                    open(ttf_data->out->s, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
                    execve(cmd, argv, __environ);
                } else {
                    wait(NULL);
                }
            }
            free(argv);
            return;
        } else if (data->arr[0]->type == TREE_PIPE) {
            // Pipe!
            puts("Pipe!!\n");
            return;
        }
    }
    // run arguments as a command, let's use execve
    char **argv = Tree_extract_argv(tree);
    if (!try_cd_or_exit_or_whatever(argv)) {
        char *cmd = argv[0];
        look_in_path(&cmd);
        if (cmd == NULL) {
            write(2, "Not found.\n", 11);
            return;
        }
        if (fork() == 0) {
            execve(cmd, argv, __environ);
        } else {
            wait(NULL);
        }
    }
    free(argv);
}

int main() {
    while (true) {
        struct Tree_node *tree = read_and_parse_line();
        Tree_print(tree);
        puts("\n");
        Parentheses_execute(tree);
        Tree_free(tree);
    }
    return 0;
}
