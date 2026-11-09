<p align="center"><img src="docs/readme/brand/lens-mark.png" width="92" alt="Lens"></p>

<h1 align="center">Lens</h1>

<h3 align="center">Lee inglés en inglés.</h3>

<p align="center">
  Lens es un compañero de la bandeja del sistema de Windows que explica la palabra, el nombre o la frase que seleccionas — en un inglés más claro, junto al texto. Sin cambiar de pestaña, sin abrir un diccionario y sin dar un rodeo por el chino. Entre lecturas, una mascota de escritorio te devuelve las palabras que tocan y te las pregunta.
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><strong>Descargar para Windows</strong></a> ·
  <a href="#un-vistazo">Verlo en acción</a> ·
  <a href="https://kakuyo1.github.io/lens/">Página del producto</a> ·
  <a href="https://github.com/kakuyo1/lens/releases">Todas las versiones</a>
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><img alt="release" src="https://img.shields.io/github/v/release/kakuyo1/lens?label=release&color=2f9e6e"></a>
  <a href="#descargar"><img alt="Windows 10/11 x64" src="https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-1a1a1f"></a>
  <a href="LICENSE"><img alt="Licencia MIT" src="https://img.shields.io/github/license/kakuyo1/lens?color=2f9e6e"></a>
  <a href="#también-incluido"><img alt="Inglés, chino, chino tradicional, español y japonés" src="https://img.shields.io/badge/languages-EN%20%7C%20ZH%20%7C%20ZH--HANT%20%7C%20ES%20%7C%20JA-2f9e6e"></a>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Qt 6 / QML" src="https://img.shields.io/badge/Qt-6.2%2B%20%2F%20QML-41CD52?logo=qt&logoColor=white">
  <img alt="CMake 3.21+" src="https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white">
</p>

<p align="center"><sub><a href="README.en.md">English</a> · <a href="README.md">简体中文</a> · <a href="README.zh-TW.md">繁體中文</a> · <a href="README.ja.md">日本語</a> · Español</sub></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/readme/hero-dark.png">
    <img src="docs/readme/hero-light.png" width="100%" alt="Lens explica la palabra serendipity en inglés en una tarjeta junto a un ensayo en inglés">
  </picture>
</p>

<p align="center"><sub>Las explicaciones son en inglés por defecto. Mantén pulsado el globo de la tarjeta para ver tu lengua materna; el cambio permanente está en Ajustes.</sub></p>

## Por qué Lens

La mayoría de las herramientas de consulta te sacan de la página y responden en tu idioma. Terminas el párrafo sin haberlo leído en inglés. Lens apuesta por lo contrario: la forma más rápida de leer mejor inglés es quedarte en inglés.

- **Entra inglés, sale un inglés más claro.** Las explicaciones son en inglés por defecto: una definición en inglés sencillo, IPA y, si quieres, etimología y un ejemplo. Una selección en chino, japonés o español se traduce al inglés; una selección en inglés se responde en el idioma de las explicaciones, inglés por defecto. La traducción sigue ahí; simplemente no es la opción predeterminada.
- **El idioma de las explicaciones es un ajuste, no un interruptor en la tarjeta.** Inglés, chino, chino tradicional, español o japonés: cámbialo una vez y todas las tarjetas lo siguen, respondidas desde la caché cuando esa palabra ya la tiene.
- **La barra de acciones ofrece Traducir, Explicar y Copiar texto.** Lens gira alrededor de Explicar: te dice qué hace la palabra en ese contexto con palabras que ya conoces, en lugar de sustituirla por una palabra china que puedes saltarte.
- **Una palabra que se puede oír.** Un altavoz junto a la pronunciación: púlsalo a él o a la propia pronunciación y la palabra se lee una vez. Las frases, los nombres y las traducciones no suenan.
- **La respuesta se queda junto al texto.** Lens vive en la bandeja del sistema y no tiene ventana principal. La burbuja aparece junto a la selección, se queda mientras pasas el ratón y desaparece cuando sigues leyendo.
- **Tres veredictos, una memoria tranquila.** Marca una palabra como Conocida, Dominado o Nueva. Conocida sigue en práctica y su línea de escaneo se dibuja más firme; Dominado la retira del escaneo y de las preguntas de la mascota; Nueva la mantiene en práctica. Sin rachas ni planes de estudio.
- **Una mascota que pregunta.** Cuando toca repasar una palabra, aparece un cartel a los pies de la mascota. Si aciertas, la siguiente pausa es más larga; si fallas, vuelve antes. Una palabra dominada no vuelve a preguntarse. La mascota solo recibe tipos de evento y no puede leer tu pantalla, la selección ni las definiciones. Desactivada por defecto; actívala en Ajustes › Extensiones.
- **Una definición por la que se puede entrar.** Las palabras de una definición que quedan por encima de tu nivel conservan el subrayado de puntos. Ábrela y la misma tarjeta baja un nivel, con una ruta para volver; la caché, el presupuesto y las reglas de veredicto acompañan cada paso.
- **El globo es el único control de idioma que queda en la tarjeta.** Con el idioma de las explicaciones en inglés, mantenlo pulsado medio segundo y el significado en tu lengua materna aparece bajo la definición en inglés; al soltar, desaparece. La lengua materna es un ajuste propio, chino por defecto. Una palabra cuya explicación ya incluye ese idioma no cuesta nada; cualquier otra se pide una vez por sesión.

## Un vistazo

<p align="center"><img src="docs/readme/english-first.gif" width="600" alt="Mantener pulsado el globo de la tarjeta para ver el significado en chino bajo la definición en inglés"></p>

La respuesta sigue en inglés. Mantén el globo para ver tu lengua materna; al soltar, la tarjeta vuelve al inglés.

<p align="center"><img src="docs/readme/feature-tour.gif" width="600" alt="Pulsar Imágenes en la palabra crane carga sus fotos; «¿No es este sentido?» cambia la tarjeta al sentido de grúa, con sus fotos"></p>

Pulsa Imágenes en una palabra para cargar sus fotos. «¿No es este sentido?» cambia la tarjeta en su sitio al otro sentido de la palabra, con sus fotos.

## Dilo en voz alta

Cada tarjeta de palabra lleva un altavoz junto a la pronunciación; pulsa uno u otra y la palabra se lee una vez. Nada más de la tarjeta suena: ni la definición, ni una frase, ni un nombre.

- **Por defecto habla primero la voz de Windows**, con el acento que elijas: estadounidense por defecto, británico si lo pides.
- **Cuando la máquina no tiene voz para ese acento**, Lens usa lo que hayas elegido en Ajustes › Lectura y aprendizaje: un audio en línea (Youdao por defecto, o Google Translate), un modelo local que descargas una vez (Kokoro, unos 180 MB, o LJSpeech, unos 41 MB) o silencio. La elección es explícita: Lens no cambia en silencio entre en línea y local.
- **La reproducción automática está desactivada por defecto.** Cuando la activas, la tarjeta que abre una consulta se lee una vez y la misma palabra no se repite.
- **La grabación se guarda en `%APPDATA%\Lens\audio`**, con un límite de 50 MB, y nunca cuenta para el presupuesto diario.

## Texto que no se puede seleccionar

Pulsa `Ctrl+Alt+S` (se puede cambiar en Ajustes › OCR), enmarca una zona de un PDF, una imagen o un fotograma de vídeo y Lens la lee localmente con OCR.

- **Tesseract, incluido con Lens, es el motor automático.** También puedes descargar RapidOCR-json desde Ajustes, o apuntar a tu propia copia y, tras detectarla y confiar en ella, elegirla; la carpeta de modelos debe ser una ruta ASCII.
- **El OCR en línea es opcional.** Pregunta antes de cada subida y puede apuntar a Baidu OCR, Tencent Cloud OCR, Alibaba Cloud OCR, Google Cloud Vision u OCR.space.
- **El escaneo automático es otro canal y sigue siendo experimental.** No usa OCR; ahora solo funciona con Chrome en primer plano: dibuja una línea de puntos bajo las palabras que pueden estar por encima de tu nivel; mantén Alt para ver la señal del sentido, o deja el puntero sobre una palabra para que aparezca un botón **Dominado** que la retira. Sus resultados aún no son fiables.

## Consulta al pasar el cursor

Los enlaces y los titulares suelen no poder seleccionarse. Cambia a la consulta al pasar el cursor (por defecto `Ctrl+Alt+H`; puedes cambiarla en Ajustes › Captura y ventanas), deja el puntero sobre una palabra durante un segundo y Lens la traduce. Pulsa la tecla otra vez para volver a la selección. La consulta solo lee el texto bajo el puntero: no hace clic, no toca el portapapeles, no lee campos de contraseña y solo funciona en las aplicaciones de tu lista blanca de escaneo.

<p align="center"><img src="docs/readme/hover-lookup.gif" width="600" alt="Tras cambiar a la consulta al pasar el cursor, el puntero se queda sobre serendipity en un enlace durante un segundo y una tarjeta da su significado"></p>

## El cajón de copiar y el análisis

<p align="center"><img src="docs/readme/selection-drawer.png" width="800" alt="El cajón de Copiar texto con Corregir gramática y Análisis para una frase seleccionada"></p>

Copiar texto es un cajón, no una sola acción: al pasar el ratón solo salen las acciones que admite esa selección.

- Corregir ortografía en una palabra, Corregir gramática en una frase y Análisis en una frase inglesa de hasta 80 palabras.
- Ninguna se guarda en caché y cada una cuenta para el presupuesto diario como cualquier otra petición.

<p align="center"><img src="docs/readme/breakdown.png" width="800" alt="La respuesta de Análisis: la frase teñida por sus cuatro roles, con una leyenda debajo"></p>

Análisis tiñe cada parte de la frase con uno de cuatro colores — sujeto, verbo, complemento, adverbial — con una leyenda debajo; una corrección subraya solo las palabras que cambió.

## Un pequeño bucle de aprendizaje

<p align="center"><img src="docs/readme/learning-loop.gif" width="900" alt="El cartel de repaso de Fantasma junto a ella y el registro anual de palabras"></p>

Conocida, Dominado y Nueva viven en una sola lista de palabras, con un segmento para cada una arriba.

- Pasa el ratón por una fila para releer una explicación que Lens ya guardó, sin enviar ninguna petición.
- Cuando toca repasar una palabra, aparece junto al compañero de escritorio un pequeño cartel, el suyo; responde Olvidé o Lo recordaba y volverá con un intervalo de entre diez minutos y sesenta días. Una palabra dominada no vuelve a preguntarse.
- El registro anual mantiene visible el hábito sin convertirlo en una racha: un cuadro por consulta, en uno de doce colores a tu elección (verde por defecto).

## Mascotas que te devuelven las palabras

<p align="center"><img src="docs/readme/pets.gif" width="900" alt="Las siete mascotas de píxeles de Lens"></p>

Lens incluye siete mascotas de píxeles: Gato de orejas cortas, Búho, Mochi, Fantasma, Medusa, Nube y Perro. Están desactivadas por defecto; activa una en Ajustes › Extensiones.

- Cada mascota tiene su propio vestuario de nueve piezas y seis escenas — cuatro propias y dos compartidas; una bufanda o unas gafas inclinan qué escena elige.
- Su tamaño y su ropa se recuerdan por mascota, mientras la ventana se queda donde la dejas.
- Deambulan, se esconden en el borde de la pantalla, notan el puntero y se pueden coger y lanzar; cuando la barra de tareas está fija, a veces saltan a ella, rebotan, caminan despacio hasta un extremo y vuelven.
- Solo reciben tipos de evento — nunca tus selecciones, explicaciones o texto en pantalla — y los paquetes solo contienen datos.

Las seis de Fantasma: el reloj de medianoche, la calabaza de Halloween, el espejo y el té de fantasmas son suyos; el escritorio y el baño los comparte con las demás mascotas — cada escena es una animación propia. La calabaza, en movimiento:

<p align="center"><img src="docs/readme/pet-lantern.gif" width="420" alt="La escena de la calabaza de Fantasma, animada"></p>

Lens también admite mascotas personalizadas: `skills/lens-pet-maker` del repositorio es una skill de Claude Code que ayuda a una IA a generar un paquete de mascota. Clona el repositorio para usarla, copia la carpeta resultante en `%APPDATA%\Lens\pets\` y actívala en Ajustes › Extensiones.

## También incluido

- **Trae tu propio modelo.** DeepSeek, OpenAI, Gemini, OpenRouter, xAI, Moonshot, Zhipu, Qwen o un endpoint personalizado compatible con OpenAI.
- **Costes visibles.** Coste por petición a partir de los tokens y un presupuesto diario que pausa las consultas, la captura y las peticiones cuando se alcanza.
- **Informe de lectura semanal.** El perfil semanal de Estadísticas muestra las consultas de la semana por hora, categoría gramatical y palabras más consultadas; pulsa «Interpretar la semana» y el modelo escribe cinco observaciones en una tarjeta de informe semanal, y su botón «Relato» escribe un relato corto con las palabras de la semana. Solo se envía cuando pulsas, y solo lleva recuentos y palabras.
- **Sincronización cuando la quieras.** Los veredictos, el historial, las estadísticas y los ajustes seleccionados se cifran con tu frase de contraseña y se guardan en un gist secreto de tu propia cuenta de GitHub. La clave de API se queda en este dispositivo.
- **Listas de palabras de ida y vuelta.** La ventana de palabras importa y exporta `.txt` y `.json`, y exporta CSV y Anki TSV con las explicaciones ya guardadas; la fila Palabras dominadas de Ajustes › Lectura y aprendizaje importa una vez una lista con una palabra por línea y marca sus palabras como dominadas. Borra una palabra con una confirmación.
- **Fotos cuando ayudan.** Si el modelo cree que una foto explica mejor un sentido, la tarjeta puede abrir tres imágenes de Wikimedia Commons. Se guardan en caché local y «Acerca de» muestra su tamaño y permite borrarla.
- **Hazlo tuyo.** Cinco formas de barra de acciones; temas Claro, Oscuro, Vidrio mate y Vidrio líquido; y una tipografía inglesa con texto de burbuja de 12–18 px.
- **Cinco idiomas.** La interfaz y las explicaciones ofrecen inglés, chino, chino tradicional, español y japonés. Las traducciones al chino tradicional, al español y al japonés están generadas por máquina y no han sido revisadas por un hablante nativo.

## Temas

<p align="center"><img src="docs/readme/themes.png" width="900" alt="Claro, Oscuro, Vidrio mate y Vidrio líquido"></p>

- **Claro y Oscuro** son sólidos.
- **Vidrio mate y Vidrio líquido** dibujan cada tarjeta sobre una copia desenfocada del escritorio y mantienen a Lens fuera de las capturas de pantalla y del uso compartido mientras están activos.
- **Las animaciones** siguen la configuración de animaciones de Windows.

## Descargar

Descarga `Lens-<version>-setup.exe` desde la [página de versiones](https://github.com/kakuyo1/lens/releases/latest), o `Lens-<version>-portable.zip` de la misma versión.

- **El instalador** es por usuario, así que Windows nunca pide permisos de administrador; **el zip portátil** se ejecuta desde cualquier carpeta.
- **Windows 10/11 x64.** Necesitarás una clave de API de un proveedor compatible.
- Lens no incluye un diccionario de definiciones; incluye una lista de unas 168.000 palabras inglesas solo para comprobar que un candidato es una palabra real.

<details>
<summary>Puede que Windows te avise antes de ejecutarlo</summary>

Lens no tiene firma de código y SmartScreen bloquea los programas que no reconoce. Elige **Más información** y luego **Ejecutar de todos modos**. El aviso se debe a la falta de firma, no a un archivo dañado. Para comprobar que la descarga llegó intacta, compara su SHA-256 con la línea correspondiente del archivo `SHA256SUMS` de la versión; eso demuestra que el archivo no se alteró durante la transferencia, no quién lo publicó.

</details>

## Privacidad

Lens no tiene cuenta ni servidor propio.

- **Lo que se envía.** El texto que seleccionas o capturas va al proveedor que configures, después de ocultar localmente URL, direcciones de correo y cadenas de seis o más dígitos. La interpretación semanal y el relato solo se envían cuando pulsas su botón, llevan solo recuentos y palabras, y estas se enmascaran igual.
- **OCR.** Se ejecuta localmente por defecto. El OCR en línea es opcional: cada captura muestra el recorte congelado y el destino exacto, se sube solo cuando lo confirmas, y el escaneo automático experimental nunca lo usa.
- **El altavoz.** Lee en local con la voz del sistema o un modelo local descargado; cuando recurre a un audio en línea, solo esa palabra sale del dispositivo, y la grabación se guarda en `%APPDATA%\Lens\audio`.
- **En este dispositivo.** Tu clave de API, las credenciales de OCR y tus datos de aprendizaje se quedan en `%APPDATA%\Lens\settings.json`.
- **Sincronización.** Si la activas, la copia en la nube se cifra con tu frase de contraseña y se guarda en tu propio gist de GitHub; la clave se queda en este dispositivo.

Consulta [SECURITY.md](SECURITY.md) para conocer todos los límites.

## Preguntas frecuentes

**¿Lens es un traductor?** Puede traducir, pero está pensado para explicar. Las selecciones en chino, japonés o español vuelven en inglés.

**¿Necesito una clave de API?** Sí. Usa la de un proveedor compatible. Lens muestra el coste de cada petición y puede limitar el gasto diario. Por ejemplo, con DeepSeek una consulta de palabra cuesta unos 0,001 CNY, así que con 1 CNY caben unas 1000; otros proveedores cobran de otra forma.

**¿Funciona sin conexión?** El OCR local, la lista de palabras y una voz local descargada funcionan sin conexión. Las explicaciones, el OCR en línea y los audios en línea, si los activas, necesitan el servicio que configures.

**¿Qué sale de mi PC?** Solo la selección enmascarada, una captura que confirmes expresamente si usas OCR en línea, la palabra suelta cuando el altavoz recurre a un audio en línea, y los recuentos y palabras que envía el informe semanal cuando pulsas su botón. Consulta [Privacidad](#privacidad).

**¿Por qué me avisa Windows?** El instalador no tiene firma de código; la nota de [Descargar](#descargar) explica cómo ejecutarlo y verificarlo.

**¿Hay versión para macOS o Linux?** Por ahora, solo Windows.

## Contribuir y licencia

Lens es código abierto bajo la [licencia MIT](LICENSE). Las issues y los pull requests son bienvenidos. Si Lens te ayuda a leer más inglés, una estrella ayuda a que otro lector lo encuentre.

<sub>Las imágenes de la interfaz se han renderizado a partir de los prototipos de diseño actuales de Lens; la compilación publicada sigue el mismo diseño y los mismos tokens.</sub>
