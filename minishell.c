#define _POSIX_C_SOURCE 200809L

#include <stdio.h> 
#include <stdlib.h>
#include <sys/types.h>

#include "parser.h"

static void mostrar_comandos(const tline *linea){
    printf("Comandos: %d\n", linea->ncommands);
        for(int i = 0; i < linea->ncommands; i++){
            const tcommand *comando = &linea->commands[i];

            printf("Comando %d (%d argumentos):\n", i + 1, comando->argc);
            for(int j = 0; j < comando->argc; j++){
                printf(" argv[%d] = %s\n", j, comando->argv[j]);
            }
        }
}

int main(void){
    char *entrada = NULL;
    size_t capacidad = 0;

    for(;;){
        fputs("msh>", stdout);
        fflush(stdout);

        ssize_t leidos = getline(&entrada, &capacidad, stdin);

        if(leidos == -1){
            break;
        }

        if(leidos > 0 && entrada[leidos - 1] == '\0'){
            entrada[--leidos] = '\n';
        }

        if(leidos == 0){
            continue;
        }

        tline *linea = tokenize(entrada);
        if(linea == NULL || linea->ncommands == 0){
            continue;
        }
        
        mostrar_comandos(linea);
    }

    int estado = EXIT_SUCCESS;
    if(ferror(stdin)){
        perror("getline");
        estado = EXIT_FAILURE;
    }

    free(entrada);
    return estado;
}