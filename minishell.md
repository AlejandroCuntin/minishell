# Explicación codigo

## Función auxiliar: `mostrar_comandos`

### Cabecera

Encapsulamos esta funcion unicamente para este archivo usando `static` en la cabecera de la función.

La función recibe un puntero constante a la estructura que representa toda la línea analizada. El uso de `const` garantiza que la función solo leerá los datos sin modificarlos.

#### Primer for:
En el primer for leemos el numero total de comandos separados por tuberias `|` 
Creamos un array de tipos `tcommand`, mientras recorremos los comandos, vamos metiendolos en el array `comando`

#### Segundo for:

El segundo for recorre la cantidad de argumentos de un comando concreto
Y mostramos por pantalla el array con cada uno de los argumentos


## Main

### Variables globales:

Inicialización de la memoria dinámica:
Al inicializar el puntero `entrada` a `NULL` y `capacidad` a `0`, le delegamos a `getline()` la responsabilidad de reservar memoria dinámica (`malloc`) según lo larga que sea la línea que escriba el usuario, y de redimensionarla (`realloc`) automáticamente en futuras iteraciones si el usuario escribe una línea más larga.

### Lectura

`for(;;)`: Bucle para esperar comandos continuamente.
Con `fflush` fuerza para que aparezca inmediatamente en la terminal el resultado.
`gtline` lee una linea completa de la entrada estandar `stdin`, recibe las dos variables.
