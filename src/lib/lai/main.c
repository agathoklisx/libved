#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <lai.h>
#if (defined(__unix__) || defined(unix)) && !defined(USG)
#include <sys/param.h>
#endif

#ifdef ENABLE_REPL
#define VERSION "Dictu Version: 0.8.0\n"

#include "linenoise.h"

static bool replCountBraces(char *line) {
    int leftBraces = 0;
    int rightBraces = 0;
    bool inString = false;

    for (int i = 0; line[i]; i++) {
        if (line[i] == '\'' || line[i] == '"') {
            inString = !inString;
        }

        if (inString) {
            continue;
        }

        if (line[i] == '{') {
            leftBraces++;
        } else if (line[i] == '}') {
            rightBraces++;
        }
    }

    return leftBraces == rightBraces;
}

static bool replCountQuotes(char *line) {
    int singleQuotes = 0;
    int doubleQuotes = 0;
    char quote = '\0';

    for (int i = 0; line[i]; i++) {
        if (line[i] == '\'' && quote != '"') {
            singleQuotes++;

            if (quote == '\0') {
                quote = '\'';
            }
        } else if (line[i] == '"' && quote != '\'') {
            doubleQuotes++;
            if (quote == '\0') {
                quote = '"';
            }
        } else if (line[i] == '\\') {
            line++;
        }
    }

    return singleQuotes % 2 == 0 && doubleQuotes % 2 == 0;
}

static void repl(DictuVM *vm, int argc, const char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);
    printf(VERSION);
    char *line;

    linenoiseHistoryLoad("history.txt");

    while((line = linenoise(">>> ")) != NULL) {
        char *fullLine = malloc(sizeof(char) * (strlen(line) + 1));
        snprintf(fullLine, strlen(line) + 1, "%s", line);

        linenoiseHistoryAdd(line);
        linenoiseHistorySave("history.txt");

        while (!replCountBraces(fullLine) || !replCountQuotes(fullLine)) {
            free(line);
            line = linenoise("... ");

            if (line == NULL) {
                return;
            }

            char *temp = realloc(fullLine, strlen(fullLine) + strlen(line) + 1);

            if (temp == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }

            fullLine = temp;
            memcpy(fullLine + strlen(fullLine), line, strlen(line) + 1);

            linenoiseHistoryAdd(line);
            linenoiseHistorySave("history.txt");
        }

        dictuInterpret(vm,  "repl", fullLine);

        free(line);
        free(fullLine);
    }
}

#endif /* ENABLE_REPL */

static char *readfile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = malloc(sizeof(char) * (fileSize + 1));
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void runFile(DictuVM *vm, int argc, const char *argv[]) {
    UNUSED(argc);
    char *source = readfile(argv[1]);

    if (source == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", argv[1]);
        exit(74);
    }

    DictuInterpretResult result = dictuInterpret(vm, (char *) argv[1], source);
    free(source); // [owner]

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, const char *argv[]) {
    DictuVM *vm = dictuInitVM(argc == 1, argc, (char **) argv);

    if (argc == 1) {
#ifdef ENABLE_REPL
        repl(vm, argc, argv);
#else
        return 1;
#endif
    } else if (argc >= 2) {
        runFile(vm, argc, argv);
    } else {
        fprintf(stderr, "Usage: dictu [path] [args]\n");
        exit(1);
    }

    dictuFreeVM(vm);
    return 0;
}
