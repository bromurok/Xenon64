# Xenon64

Este es un proyecto basado en el emulador Mupen64, adaptado para utilizar formato **XEX** en Xbox 360 en lugar de **ELF32**.

El proyecto fue creado con fines de prueba, ya que se sabe que el intérprete es bastante lento. Aun así, se sube para que otras personas puedan revisarlo y, si es posible, encontrar formas de mejorar su funcionamiento y rendimiento.

La verdad desconozco si el intérprete actual puede llegar a mejorarse completamente o si se podría intentar implementar un **dynarec estático** para conseguir una mejora de rendimiento mucho mayor. El código queda disponible por si alguien quiere intentar mejorar el proyecto, corregir errores o encontrar nuevas formas de hacerlo funcionar mejor.

También es importante mencionar que gran parte del proyecto fue realizado con ayuda de IA, por lo que todavía cuenta con muchos errores y problemas pendientes.

Actualmente son pocos los juegos que llegan a verse "bien". Entre ellos están:

- Super Mario 64
- The Legend of Zelda: Ocarina of Time
- Mario Kart 64

El emulador funcionando en Xenia tiene un rendimiento mucho mejor que en hardware real de Xbox 360.

Todavía falta arreglar varias cosas, como:

- Problemas de sonido.
- Juegos que no muestran correctamente la imagen.
- Texturas que no cargan.
- Problemas de compatibilidad con muchos juegos.
- Mejoras generales de rendimiento.

Este proyecto no tendrá necesariamente un desarrollo constante. De vez en cuando intentaré corregir errores, intentar mejorar el rendmiento, aunque también existe la posibilidad de que termine en el olvido por mi persona.

## Problemas con Visual Studio

Puede que tengan problemas con los **includes** al abrir el proyecto en Visual Studio 2010, ya que las rutas de los directorios de inclusión no están configuradas de forma global (si mal no recuerdo).

Es posible que tengan que agregar manualmente los directorios necesarios en la configuración del proyecto para que Visual Studio encuentre correctamente los archivos de cabecera.

## Créditos

Todos los créditos al creador o creadores de Mupen360 por el trabajo original realizado.

Este proyecto utiliza como base ese trabajo y busca continuar experimentando con el emulador en Xbox 360.
