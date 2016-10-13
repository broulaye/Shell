/*
 * This plugin implement a function
 * that adds up the digit of a number given to it
 * and return the result
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "../esh.h"
#include "../esh-sys-utils.h"

static bool
init_plugin(struct esh_shell *shell)
{
    printf("Plugin 'addDigits' initialized...\n");
    return true;
}

/* Implement addDigit plugin.
 * Returns true if handled, false otherwise. */
static bool
addDigits_plugin(struct esh_command *cmd)
{
    if (strcmp(cmd->argv[0], "addDigits"))
        return false;

    char *n = cmd->argv[1];
    // if no argument is given, 0 is printed
    if (n == NULL) {
        printf("0");
    }
    else {
        long number = strtol(n, (char **)NULL, 10);
        long digit = 0;
        long sum = 0;
        while (number > 0)
        {
            digit = number % 10;
            sum  = sum + digit;
            number /= 10;
        }
        printf("Sum of the digits is: %ld\n", sum);

    }


    return true;
}

struct esh_plugin esh_module = {
  .rank = 2,
  .init = init_plugin,
  .process_builtin = addDigits_plugin
};
