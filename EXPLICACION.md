# Práctica 1 de Sistemas Operativos: MiniShell

Autor/a: **[escribe aquí tu nombre y apellidos]**  
Curso: 2026/2027  
Archivos de la práctica: `myshell.c`, `parser.h` y la biblioteca `libparser_64.a` (para Linux x86_64).

## Índice

1. [Qué se ha construido](#1-qué-se-ha-construido)
2. [Antes de leer el programa: cinco ideas básicas](#2-antes-de-leer-el-programa-cinco-ideas-básicas)
3. [Cómo se transforma una línea en procesos](#3-cómo-se-transforma-una-línea-en-procesos)
4. [Diseño y estructuras de datos](#4-diseño-y-estructuras-de-datos)
5. [Explicación de las funciones](#5-explicación-de-las-funciones)
6. [Mandatos internos](#6-mandatos-internos)
7. [Señales, segundo plano y terminal](#7-señales-segundo-plano-y-terminal)
8. [Compilación y pruebas](#8-compilación-y-pruebas)
9. [Problemas, decisiones y posibles mejoras](#9-problemas-decisiones-y-posibles-mejoras)

## 1. Qué se ha construido

`myshell.c` es un intérprete de mandatos pequeño. Muestra `msh> `, lee una línea, pide a la biblioteca `parser` que la divida en mandatos y ejecuta lo solicitado. Se pueden encadenar mandatos con `|`, leer desde un fichero con `<`, escribir en un fichero con `>` y redirigir errores con `>&`. Si la línea termina en `&`, se inicia en segundo plano y vuelve el prompt sin esperar.

También incorpora `cd`, `exit`, `umask`, `jobs` y `fg`. Son *mandatos internos*: no se busca un programa externo con esos nombres. El código los reconoce y realiza la operación dentro del propio MiniShell.

La entrada:

```text
cat < datos.txt | grep hola | wc -l > resultado.txt
```

significa: leer `datos.txt`, pasar su contenido a `grep`, pasar las coincidencias a `wc` y guardar el número de líneas en `resultado.txt`. Los tres programas se ponen en marcha sin esperar a que termine el anterior.

## 2. Antes de leer el programa: cinco ideas básicas

**Un proceso** es un programa en ejecución. El MiniShell es un proceso; cuando escribe `ls`, crea otro proceso para ejecutar `ls`.

**Entrada, salida y error estándar** son tres canales que un proceso tiene abiertos al comenzar. Se identifican por los números 0, 1 y 2. Normalmente, el 0 lee del teclado y el 1 y el 2 escriben en la terminal. `>` cambia el destino del canal 1, `<` cambia el origen del 0 y `>&` cambia el destino del 2.

**Una tubería** (`|`) conecta la salida estándar de un proceso con la entrada estándar de otro. Por ejemplo, en `echo hola | wc -c`, `wc` recibe los caracteres que produce `echo`. El sistema proporciona dos extremos para cada tubería: uno de lectura y otro de escritura.

**`fork()`** copia el proceso actual. Su resultado permite distinguir quién está ejecutando el código: en el hijo devuelve 0 y en el padre devuelve el identificador del hijo (PID). El hijo prepara los canales y utiliza `execv()` para sustituirse por el programa solicitado. El padre conserva el control del MiniShell.

**`waitpid()`** permite al padre recoger la terminación de sus hijos. Esperar es necesario tanto para mantener un mandato en primer plano como para evitar que los hijos terminados permanezcan como procesos zombis. Con `WNOHANG` se comprueba si han terminado sin bloquear el prompt.

## 3. Cómo se transforma una línea en procesos

### 3.1. Lectura y análisis

`main()` lee una línea completa con `getline()`. A diferencia de un array de tamaño fijo, esta función amplía la memoria cuando hace falta. Antes de analizar la línea se guarda una copia textual: servirá para mostrar el mandato original en `jobs` o `fg`.

La función `tokenize()` pertenece a la biblioteca proporcionada en la práctica. Devuelve un puntero a `tline`, que contiene:

| Campo | Significado |
| --- | --- |
| `ncommands` | Número de mandatos de la línea. |
| `commands` | Lista de mandatos. Cada `tcommand` tiene `filename` (ruta del ejecutable, o `NULL` si no existe), `argc` (número de argumentos) y `argv` (argumentos). |
| `redirect_input` | Fichero indicado con `<`, o `NULL`. |
| `redirect_output` | Fichero indicado con `>`, o `NULL`. |
| `redirect_error` | Fichero indicado con `>&`, o `NULL`. |
| `background` | Vale 1 si la línea acaba en `&`, y 0 en otro caso. |

Si la línea está vacía o el parser no devuelve mandatos, se vuelve a mostrar el prompt. No se libera el resultado de `tokenize()` desde nuestro código porque la biblioteca facilitada no proporciona una función documentada para hacerlo; no sería seguro suponer cómo administra esa memoria.

### 3.2. Creación de una tubería de varios mandatos

`run_pipeline()` recorre los mandatos de izquierda a derecha. Mantiene en `previous` el extremo de lectura de la tubería anterior. Para cada mandato, salvo el último, crea una nueva tubería. Después llama a `fork()`:

1. El hijo conecta `previous` a su entrada y el extremo de escritura nuevo a su salida con `dup2()`.
2. El hijo cierra los descriptores que ya no necesita y llama a `execv()`.
3. El padre conserva el extremo de lectura de la tubería nueva para el siguiente hijo y cierra los extremos que ya no usa.

Así no hace falta reservar un array de tuberías de tamaño variable. Solo se mantiene la tubería anterior y la siguiente. En una línea de tres mandatos, la conexión queda así:

```text
mandato 1  --tubería A-->  mandato 2  --tubería B-->  mandato 3
```

El primer hijo puede recibir un fichero con `<`. El último puede escribir a un fichero con `>` y a otro con `>&`. El parser identifica esas redirecciones; nuestro código las aplica únicamente en los extremos adecuados de la cadena.

Es importante cerrar los extremos sobrantes. Si un proceso conserva abierto accidentalmente un extremo de escritura, el proceso lector podría quedarse esperando un fin de fichero que nunca llega.

### 3.3. Redirecciones y errores

`redirect_fd()` abre el fichero y sustituye con `dup2()` el canal elegido. Para salida y error utiliza `O_CREAT | O_TRUNC`: crea el fichero si no existe o vacía su contenido anterior. Para entrada utiliza `O_RDONLY`. La máscara de permisos actual participa cuando `open()` crea un fichero.

Si no se puede abrir un fichero se informa con su nombre y la descripción del sistema, por ejemplo `datos.txt: Error. No such file or directory`, y ese hijo termina. Si el parser no encuentra el ejecutable, ese hijo muestra `mandato: No se encuentra el mandato`. Otro hijo de la misma tubería puede haberse iniciado ya, porque la cadena se construye en paralelo.

## 4. Diseño y estructuras de datos

La estructura `job` representa **una línea entera**, no un solo proceso. Esto importa porque `cat | grep | wc &` crea tres hijos, pero `jobs` debe mostrar un único trabajo. Guarda un número de trabajo (`id`), el grupo de procesos (`pgid`), el último PID, un array dinámico con los PID de todos los hijos, el número de hijos pendientes (`remaining`), el texto de la línea y un enlace al siguiente trabajo.

Los trabajos se encadenan en una lista. Se añaden cuando se ejecutan en segundo plano y se eliminan al terminar. Durante la espera de un trabajo en primer plano también se inserta temporalmente en la lista, para que `mark_finished()` pueda llevar la cuenta de sus hijos.

| Variable global | Tipo | Para qué sirve |
| --- | --- | --- |
| `jobs` | `job *` | Cabeza de la lista de trabajos pendientes. |
| `next_job_id` | `int` | Siguiente número que recibiría un trabajo en segundo plano. |
| `interactive` | `int` | Indica si la entrada estándar es una terminal. |
| `shell_pgid` | `pid_t` | Grupo del MiniShell; permite devolverle el control de la terminal. |
| `interrupted` | `volatile sig_atomic_t` | Señala que se ha pulsado Ctrl-C mientras el MiniShell esperaba una línea. |

El array de PID se reserva con `calloc()` porque su tamaño depende del número de mandatos de la línea. `free_job()` libera el array, el texto y la estructura. Se evita así declarar arrays de longitud variable en la pila.

## 5. Explicación de las funciones

| Función | Recibe y devuelve | Papel en el programa |
| --- | --- | --- |
| `main` | No recibe argumentos; devuelve un código de salida. | Configura señales, muestra el prompt, lee líneas, invoca el parser y elige entre mandato interno o tubería. Al salir libera la memoria que controla. |
| `run_builtin` | Una línea `tline *`; devuelve 0, 1 o 2. | Devuelve 0 si debe ejecutarse como programa externo, 1 si ya atendió el mandato interno y 2 si se pidió salir con `exit`. |
| `run_pipeline` | Una línea analizada y su texto original; no devuelve valor. | Reserva un trabajo, crea tuberías e hijos, decide si espera o deja la ejecución en segundo plano. |
| `child_command` | Línea, posición, descriptores y grupo; no regresa al padre. | Conecta entrada y salida, aplica redirecciones y ejecuta el programa con `execv()`. Si falla, termina el hijo. |
| `redirect_fd` | Ruta, canal de destino y opciones de apertura; no devuelve valor. | Abre un fichero y sustituye uno de los tres canales estándar. |
| `wait_for_job` | Un `job *`; no devuelve valor. | Entrega la terminal al grupo si procede, espera a todos los hijos del trabajo y devuelve la terminal al MiniShell. |
| `mark_finished` | Un PID; no devuelve valor. | Busca ese PID dentro de los trabajos y reduce el contador de hijos pendientes una sola vez. |
| `reap_jobs` | Sin argumentos ni retorno. | Recoge hijos terminados sin bloquear y quita los trabajos que ya han acabado. |
| `free_job` y `remove_job` | Un `job *`; no devuelven valor. | Liberan un trabajo; `remove_job` lo desconecta antes de la lista. |
| `set_signal` y `on_sigint` | Número de señal y función manejadora; señal recibida, respectivamente. | Instalan la respuesta a las señales y permiten recuperar el prompt al pulsar Ctrl-C. |

Para seguir la ejecución, conviene leer `main()` primero, después `run_builtin()` y `run_pipeline()`, y por último `child_command()`. Esa secuencia reproduce lo que ocurre al introducir una línea.

## 6. Mandatos internos

### `cd`

`builtin_cd()` llama a `chdir()` **en el propio MiniShell**. Si cambiase el directorio en un hijo, al terminar ese hijo el MiniShell seguiría en el directorio anterior. Sin argumento utiliza la variable de entorno `HOME`; con uno admite rutas absolutas y relativas. Tras cambiar muestra la ruta absoluta obtenida con `getcwd()`. Si recibe más de un argumento o falla el cambio, informa del problema.

### `exit`

Indica a `main()` que salga del bucle de lectura. Solo se acepta sin argumentos. No mata los programas que ya se hayan iniciado en segundo plano.

### `umask`

La máscara indica qué permisos se quitan al crear ficheros. `builtin_umask()` admite exactamente un `0` seguido de tres dígitos entre `0` y `7`, por ejemplo `0027`. Sin argumento consulta y muestra la máscara. Con argumento la cambia y muestra su nuevo valor en octal. Para consultarla, hace `umask(0)` y restaura inmediatamente el valor anterior: la llamada devuelve la máscara previa, pero también cambia la vigente.

### `jobs` y `fg`

`jobs` llama primero a `reap_jobs()` y después muestra los trabajos de segundo plano todavía pendientes, con su número y el texto original. `fg 2` busca el trabajo 2; `fg` sin número escoge el más reciente. Muestra el mandato, lo pasa a primer plano y espera a sus hijos. Si el número no existe, informa de ello.

Los internos se admiten solos, sin tuberías, redirecciones ni `&`. Para `cd`, `exit`, `umask` y `fg` esto evita operaciones sin sentido en un hijo o estados difíciles de interpretar. En esta versión se aplica la misma regla también a `jobs`. Por ejemplo, `jobs > lista.txt` se rechaza expresamente.

## 7. Señales, segundo plano y terminal

Los hijos de una línea pertenecen a un mismo **grupo de procesos** (`pgid`). El padre y cada hijo llaman a `setpgid()` para reducir las carreras de tiempo que pueden ocurrir mientras se crea el grupo. En modo interactivo, `tcsetpgrp()` entrega la terminal al grupo de la línea en primer plano y la devuelve al grupo del MiniShell cuando la línea termina.

Ctrl-C envía `SIGINT` al **grupo que controla la terminal**, no a todos los procesos del ordenador. Por eso el MiniShell mantiene su propio grupo y los trabajos en segundo plano no reciben ese Ctrl-C. Los hijos conservan la acción normal para `SIGINT`: si están en primer plano, Ctrl-C puede interrumpirlos. El MiniShell, en cambio, usa `on_sigint()` para imprimir un salto de línea y continuar con el prompt.

En segundo plano, el padre guarda los PID y muestra el PID del último proceso de la línea entre corchetes. No espera antes de volver al prompt. Cuando puede, `reap_jobs()` recoge los hijos finalizados con `waitpid(..., WNOHANG)`. No se implementa el control de trabajos detenidos con Ctrl-Z: el enunciado no exige gestionar procesos pausados.

Si la entrada es un fichero o una tubería, `isatty()` desactiva la cesión de terminal; así se pueden ejecutar pruebas no interactivas sin intentar controlar una terminal inexistente.

## 8. Compilación y pruebas

### Compilar

En **Linux x86_64**, desde el directorio de la práctica:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 myshell.c libparser_64.a -o myshell
./myshell
```

En otra arquitectura Linux se debe elegir la biblioteca correspondiente que acompaña al enunciado. El ejecutable Linux no se obtiene compilando con MinGW de Windows, ya que `fork()`, `waitpid()` y el control de terminal son funciones POSIX.

### Pruebas realizadas

El código se compiló en Ubuntu 24.04 bajo WSL con el mandato anterior, sin avisos. También se ejecutaron estos casos no interactivos:

| Entrada de prueba | Qué se comprobó |
| --- | --- |
| `echo hola` | Ejecuta un programa con argumento y muestra `hola`. |
| `echo hola \| tr a-z A-Z` | Une dos procesos y muestra `HOLA`. |
| `printf hola \| tr a-z A-Z \| wc -c` | Une tres procesos y muestra `4`. |
| `echo salida > /tmp/msh_prueba_salida_2026.txt` y `cat < /tmp/msh_prueba_salida_2026.txt` | Crea un fichero y recupera su contenido. |
| `pwd`, `cd /tmp`, `pwd` | Cambia el directorio del propio MiniShell y lo mantiene para el siguiente mandato. |
| `umask`, `umask 0027`, `umask` | Consulta, modifica y vuelve a consultar la máscara; el último resultado fue `0027`. |
| `comandoinexistente` | Muestra `comandoinexistente: No se encuentra el mandato`. |
| `sleep 1 &`, `jobs`, `fg 1`, `jobs` | Devuelve el prompt, lista el trabajo, espera al recuperarlo y deja de listarlo. |

La prueba de Ctrl-C debe repetirse en una **terminal interactiva real** durante la defensa: ejecutar `sleep 30`, pulsar Ctrl-C y comprobar que vuelve `msh> `; repetir con `sleep 30 &` y verificar que el trabajo en segundo plano no se interrumpe. Esa interacción no queda validada por las pruebas no interactivas anteriores. También conviene comprobar `>&` y ficheros sin permisos en el sistema donde se entregue.

## 9. Problemas, decisiones y posibles mejoras

Lo más delicado del diseño es que una tubería de varios programas debe ejecutarse como un solo trabajo. Si se esperara al primer hijo antes de crear el segundo, la ejecución podría bloquearse cuando la tubería se llenase. Por ello se crean todos los hijos y solo después se espera, si la línea está en primer plano.

Otra cuestión es distinguir el MiniShell de los procesos que ejecuta: `cd` y `umask` tienen que modificar el estado del padre, mientras que las redirecciones de un mandato externo deben hacerse en el hijo para no alterar permanentemente el prompt. Se eligió una lista de trabajos con todos sus PID para saber cuándo termina realmente una línea completa.

En una implementación futura se podría admitir `jobs` con redirección, informar cuando termina un trabajo en segundo plano, conservar su estado de salida y añadir control de trabajos detenidos. También sería deseable que la biblioteca parser documentase la propiedad y liberación de la memoria que devuelve `tokenize()`.

**Para completar la memoria personal:** añade aquí tus problemas reales al desarrollar y probar la práctica, una crítica constructiva del enunciado y el tiempo que hayas dedicado de verdad. No conviene inventar horas ni pruebas que no hayas hecho. La entrega oficial pide, además del código, una memoria PDF basada en la plantilla suministrada, sin pegar el código fuente, y un ZIP llamado `practica1.zip`.
