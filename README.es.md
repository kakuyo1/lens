<p align="center"><img src="docs/readme/brand/lens-mark.png" width="92" alt="Lens"></p>

<h1 align="center">Lens</h1>

<h3 align="center">Lee inglés en inglés.</h3>

<p align="center">
  Lens es un compañero de la bandeja del sistema de Windows que explica la palabra, el nombre o la frase que seleccionas — en un inglés más claro, junto al texto. Sin cambiar de pestaña, sin abrir un diccionario y sin dar un rodeo por el chino.
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><strong>Descargar para Windows</strong></a> ·
  <a href="#un-vistazo">Verlo en acción</a> ·
  <a href="https://github.com/kakuyo1/lens/releases">Todas las versiones</a>
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><img alt="release" src="https://img.shields.io/github/v/release/kakuyo1/lens?label=release&color=2f9e6e"></a>
  <a href="#descargar"><img alt="Windows 10/11 x64" src="https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-1a1a1f"></a>
  <a href="LICENSE"><img alt="Licencia MIT" src="https://img.shields.io/github/license/kakuyo1/lens?color=2f9e6e"></a>
  <a href="#idiomas"><img alt="Inglés, chino, español y japonés" src="https://img.shields.io/badge/languages-EN%20%7C%20ZH%20%7C%20ES%20%7C%20JA-2f9e6e"></a>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Qt 6 / QML" src="https://img.shields.io/badge/Qt-6.2%2B%20%2F%20QML-41CD52?logo=qt&logoColor=white">
  <img alt="CMake 3.21+" src="https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white">
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ja.md">日本語</a> · Español</sub></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/readme/hero-dark.png">
    <img src="docs/readme/hero-light.png" width="100%" alt="Lens explica la palabra serendipity en inglés en una tarjeta junto a un ensayo en inglés">
  </picture>
</p>

<p align="center"><sub>Las explicaciones son en inglés por defecto. Puedes cambiar a chino, español o japonés desde la burbuja cuando quieras.</sub></p>

## Por qué Lens

La mayoría de las herramientas de consulta te sacan de la página y responden en tu idioma. Terminas el párrafo sin haberlo leído en inglés. Lens apuesta por lo contrario: la forma más rápida de leer mejor inglés es quedarte en inglés.

**Entra inglés, sale un inglés más claro.** Una definición en inglés sencillo, IPA y, si quieres, etimología y un ejemplo. La traducción sigue ahí; simplemente no es la opción predeterminada.

**La respuesta se queda junto al texto.** Lens vive en la bandeja del sistema y no tiene ventana principal. La burbuja aparece junto a la selección, se queda mientras pasas el ratón y desaparece cuando sigues leyendo.

**Una memoria tranquila.** Marca una palabra como Conocida o Nueva. Lens guarda la lista; el compañero de escritorio puede preguntártela más tarde. Sin rachas ni planes de estudio.

## Un vistazo

<p align="center"><img src="docs/readme/english-first.gif" width="600" alt="Cambiar el idioma de las explicaciones de inglés a chino en la burbuja"></p>

El cambio de idioma está en la tarjeta. El inglés es lo predeterminado; el chino está a un clic.

## Primero en inglés. Traducción cuando la pidas.

Las explicaciones son en inglés por defecto. Una selección en chino siempre vuelve en inglés. Cuando quieras chino, abre el globo de la burbuja y cambia el idioma de las explicaciones: la misma tarjeta se responde de nuevo y, si ya hay caché, no se envía ninguna petición.

La barra de acciones ofrece Traducir, Explicar y Copiar texto. Lens gira alrededor de Explicar: te dice qué hace la palabra en ese contexto con palabras que ya conoces, en lugar de sustituirla por una palabra china que puedes saltarte. Traducir también responde en el idioma de las explicaciones: inglés salvo que lo cambies.

## Cómo funciona

<p align="center"><img src="docs/readme/selection-bar.png" width="800" alt="La barra de acciones sobre una palabra seleccionada"></p>

1. Selecciona una palabra, un nombre o una frase en cualquier aplicación.
2. Elige Explicar. La burbuja aparece encima o debajo de la selección.
3. Márcala como Conocida o Nueva y sigue leyendo.

Cuando el texto no se puede seleccionar, pulsa `Ctrl+Alt+S` (se puede cambiar en Ajustes), enmarca una zona de un PDF, una imagen o un fotograma de vídeo y Lens la lee localmente con OCR. El escaneo automático solo vigila las aplicaciones de tu lista blanca.

## Cinco formas de barra de acciones

<p align="center"><img src="docs/readme/selection-styles.gif" width="800" alt="Barra clásica, pestaña lateral, tres tarjetas, etiqueta colgante y abanico"></p>

Elige la forma en Ajustes › Captura y ventanas: barra clásica, pestaña lateral, tres tarjetas, etiqueta colgante o abanico. Las tres acciones y las reglas de canal no cambian.

## Un pequeño bucle de aprendizaje

<p align="center"><img src="docs/readme/learning-loop.png" width="900" alt="El compañero de escritorio con una palabra pendiente y el registro anual de palabras"></p>

Conocida y Nueva viven en una sola lista de palabras. Pasa el ratón por una fila para releer una explicación que Lens ya guardó, sin enviar ninguna petición. Cuando toca repasar una palabra, el compañero de escritorio levanta un cartel; responde Olvidé o Lo recordaba y volverá con un intervalo de entre diez minutos y sesenta días. El registro anual mantiene visible el hábito sin convertirlo en una racha.

## Siete mascotas, si las quieres

<p align="center"><img src="docs/readme/pets.gif" width="900" alt="Las siete mascotas de píxeles de Lens"></p>

Lens incluye siete mascotas de píxeles: Gato de orejas cortas, Búho, Mochi, Fantasma, Medusa, Nube y Perro. Están desactivadas por defecto; activa una en Ajustes › Extensiones. Solo reciben tipos de evento — nunca tus selecciones, explicaciones o texto en pantalla — y los paquetes solo contienen datos.

## También incluido

- **Trae tu propio modelo.** DeepSeek, OpenAI, Gemini, OpenRouter, xAI, Moonshot, Zhipu, Qwen o un endpoint personalizado compatible con OpenAI.
- **Costes visibles.** Coste por petición a partir de los tokens y un presupuesto diario que pausa las consultas, la captura y las peticiones cuando se alcanza.
- **Sincronización cuando la quieras.** Las marcas Conocida/Nueva, el historial, las estadísticas y los ajustes seleccionados se cifran con tu frase de contraseña y se guardan en un gist secreto de tu propia cuenta de GitHub. La clave de API se queda en este dispositivo.
- **Listas de palabras de ida y vuelta.** Importa o exporta `.txt` y `.json`; borra una palabra con una confirmación.
- **Hazlo tuyo.** Cinco formas de barra de acciones; temas Claro, Oscuro, Vidrio mate, Vidrio líquido y Personalizado; y una tipografía inglesa con texto de burbuja de 12–18 px.
- **Cuatro idiomas.** La interfaz y las explicaciones ofrecen inglés, chino, español y japonés. Las traducciones al español y al japonés están generadas por máquina y no han sido revisadas por un hablante nativo.

## Temas

<p align="center"><img src="docs/readme/themes.png" width="900" alt="Claro, Oscuro, Vidrio mate y Vidrio líquido"></p>

Claro y Oscuro son sólidos. Vidrio mate y Vidrio líquido dibujan cada tarjeta sobre una copia desenfocada del escritorio y mantienen a Lens fuera de las capturas de pantalla y del uso compartido mientras están activos. Personalizado te deja elegir los colores y comprueba el contraste con WCAG AA. Las animaciones siguen la configuración de animaciones de Windows.

## Descargar

Descarga `Lens-<version>-setup.exe` desde la [página de versiones](https://github.com/kakuyo1/lens/releases/latest), o `Lens-<version>-portable.zip` de la misma versión. El instalador es por usuario, así que Windows nunca pide permisos de administrador; el zip portátil se ejecuta desde cualquier carpeta.

**Windows 10/11 x64.** Necesitarás una clave de API de un proveedor compatible. Lens no incluye un diccionario de definiciones; incluye una lista de unas 168.000 palabras inglesas solo para comprobar que un candidato es una palabra real.

<details>
<summary>Puede que Windows te avise antes de ejecutarlo</summary>

Lens no tiene firma de código y SmartScreen bloquea los programas que no reconoce. Elige **Más información** y luego **Ejecutar de todos modos**. El aviso se debe a la falta de firma, no a un archivo dañado. Para comprobar que la descarga llegó intacta, compara su SHA-256 con la línea correspondiente del archivo `SHA256SUMS` de la versión; eso demuestra que el archivo no se alteró durante la transferencia, no quién lo publicó.

</details>

## Privacidad

Solo el texto que seleccionas o capturas se envía al proveedor que configures, después de ocultar localmente URL, direcciones de correo y cadenas de seis o más dígitos. El OCR se ejecuta localmente; nunca se sube una captura de pantalla. Tu clave de API y tus datos de aprendizaje se quedan en `%APPDATA%\Lens\settings.json`. Si activas la sincronización, la copia en la nube se cifra con tu frase de contraseña y se guarda en tu propio gist de GitHub; la clave se queda en este dispositivo. Lens no tiene cuenta ni servidor propio. Consulta [SECURITY.md](SECURITY.md) para conocer todos los límites.

## Preguntas frecuentes

**¿Lens es un traductor?** Puede traducir, pero está pensado para explicar. Las explicaciones son en inglés por defecto; cambia el idioma de las explicaciones si quieres una traducción. Las selecciones en chino siempre vuelven en inglés.

**¿Necesito una clave de API?** Sí. Usa la de un proveedor compatible. Lens muestra el coste de cada petición y puede limitar el gasto diario.

**¿Funciona sin conexión?** El OCR y la lista de palabras son locales. Las explicaciones necesitan el proveedor que configures.

**¿Qué sale de mi PC?** Solo la palabra, el nombre o la frase seleccionados, después del enmascaramiento. No se sube la página completa ni una captura completa.

**¿Por qué me avisa Windows?** El instalador no tiene firma de código. El archivo está intacto; el aviso es por la firma que falta. Compruébalo con `SHA256SUMS`.

**¿Hay versión para macOS o Linux?** Por ahora, solo Windows.

## Idiomas

[English](README.md) · [简体中文](README.zh-CN.md) · [日本語](README.ja.md) · [Español](README.es.md)

El idioma de la interfaz y el de las explicaciones son ajustes independientes. Ambos ofrecen inglés, chino, español y japonés.

## Contribuir y licencia

Lens es código abierto bajo la [licencia MIT](LICENSE). Las issues y los pull requests son bienvenidos. Si Lens te ayuda a leer más inglés, una estrella ayuda a que otro lector lo encuentre.

<sub>Las imágenes de la interfaz se han renderizado a partir de los prototipos de diseño actuales de Lens; la compilación publicada sigue el mismo diseño y los mismos tokens.</sub>
