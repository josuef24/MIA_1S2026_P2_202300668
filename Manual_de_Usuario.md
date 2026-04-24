# Manual de Usuario - Sistema de Archivos EXT2/EXT3

## 1. Introducción
Bienvenido al Manual de Usuario de la plataforma de Simulación de Sistemas de Archivos (Golampi 2.0). Esta aplicación web te permite crear, gestionar y visualizar discos virtuales en la nube, operando como si estuvieras frente a la terminal de Linux. Podrás ejecutar sistemas de archivos EXT2 y EXT3 completamente funcionales a través de una consola interactiva.

## 2. Requisitos del Sistema
Para utilizar esta aplicación, solo necesitas:
* Una conexión estable a internet.
* Un navegador web moderno actualizado (Google Chrome, Mozilla Firefox, Microsoft Edge o Safari).
* No necesitas instalar absolutamente nada en tu computadora local.

## 3. Acceso a la Plataforma
Para comenzar, debes acceder al enlace público proporcionado por tu institución o por el administrador de AWS (el enlace suele tener el formato `http://mia-p2-frontend-...s3-website-us-east-1.amazonaws.com`).

Al ingresar, visualizarás la pantalla principal. Esta se divide en tres secciones clave:
1. **Consola Interactiva:** Donde escribirás los scripts y comandos.
2. **Explorador de Archivos (Visualizador):** Donde interactuarás gráficamente con las carpetas.
3. **Barra de Navegación (Login):** Donde gestionarás tu sesión activa.

> `![Captura de pantalla: Vista principal de la aplicación web](INSERTAR_IMAGEN_AQUI)`

## 4. Uso de la Consola (Línea de Comandos)
La plataforma utiliza un lenguaje de comandos estructurado. 

### Paso 4.1: Crear y Montar un Disco
Para que el entorno funcione, debes aprovisionar discos, particiones y montarlos. Escribe lo siguiente en el editor y presiona el botón **Ejecutar**:
```bash
# Crear un disco de 50MB
mkdisk -size=50 -unit=M -fit=FF -path=/home/ubuntu/Pruebas_MIA/Disco1.mia

# Crear una partición primaria de 20MB
fdisk -type=P -unit=M -name=Particion1 -size=20 -path=/home/ubuntu/Pruebas_MIA/Disco1.mia -fit=BF

# Montar la partición (Anote el ID generado, por ejemplo: 681A)
mount -path=/home/ubuntu/Pruebas_MIA/Disco1.mia -name=Particion1
```

> `![Captura de pantalla: Editor de comandos ejecutando mkdisk y mount](INSERTAR_IMAGEN_AQUI)`

### Paso 4.2: Formatear la Partición
Antes de poder crear carpetas, la partición debe ser inicializada (formateada) con un sistema de archivos. Utilice el comando `mkfs`:
```bash
# Para formatear con EXT3 (Incluye recuperación y Journaling)
mkfs -type=full -fs=3fs -id=681A
```

## 5. Iniciar Sesión (Login)
El sistema cuenta con un control estricto de usuarios (Permisos UGO).
1. Dirígete a la parte superior y haz clic en **Iniciar Sesión**.
2. Ingresa los datos predeterminados para el administrador principal:
   * **Usuario:** `root`
   * **Contraseña:** `123`
   * **ID de Partición:** `El ID que obtuviste al hacer mount (ej. 681A)`
3. Presiona Entrar. La barra de navegación ahora indicará tu usuario actual.

> `![Captura de pantalla: Modal o formulario de Iniciar Sesión](INSERTAR_IMAGEN_AQUI)`

## 6. Explorador de Archivos y Operaciones
Una vez iniciada la sesión, la pestaña del "Explorador de Archivos" se habilitará y te mostrará la raíz (`/`) de tu partición de forma gráfica.

* **Crear Carpetas y Archivos:** Puedes hacerlo ejecutando comandos `mkdir` y `mkfile` desde la consola, o usando los botones interactivos dentro de la interfaz gráfica del explorador.
* **Navegación:** Haz doble clic en una carpeta para abrirla. Puedes usar la barra de "Ruta actual" (breadcrumbs) para regresar a carpetas anteriores.
* **Lectura de Archivos:** Hacer clic sobre un archivo con extensión `.txt` abrirá un visor para que puedas leer el contenido sin tener que usar el comando `cat`.

> `![Captura de pantalla: Vista del Explorador de Archivos mostrando carpetas y archivos](INSERTAR_IMAGEN_AQUI)`

## 7. Simulacro de Desastres (Loss y Recovery)
Si formateaste la partición utilizando EXT3, posees características de recuperación ante caídas del sistema.

1. **Destrucción de datos:** Ejecuta el comando `loss -id=681A`. Si visitas el explorador de archivos y recargas, notarás que tu sistema de archivos está inaccesible o vacío.
2. **Recuperación:** Ejecuta `recovery -id=681A`. El sistema leerá el Journaling, recreará cada archivo en orden, y al volver al Explorador, todos tus archivos estarán tal como los dejaste antes de la falla.

Para visualizar el historial detallado de las operaciones recuperadas, puedes ejecutar el comando `journaling -id=681A`, o hacer clic en la opción "Ver Reporte de Journal" desde el Explorador (si tu UI lo implementa gráficamente).

> `![Captura de pantalla: Interfaz web mostrando el reporte de Journaling tras la recuperación](INSERTAR_IMAGEN_AQUI)`

## 8. Resolución de Problemas Comunes (Troubleshooting)

**Problema:** Mi comando `fdisk` me arroja el error "Ruta no encontrada".
**Solución:** Asegúrate de que escribiste la misma ruta (path) exactamente igual a como la usaste en `mkdisk`. Las rutas son sensibles a mayúsculas y minúsculas.

**Problema:** La interfaz web dice "Error de conexión con el Servidor".
**Solución:** Verifica que el servidor de AWS EC2 esté en ejecución. El servidor backend de C++ necesita ser arrancado con el comando `make start` mediante SSH.

**Problema:** Intento crear un archivo pero la consola dice "Permiso Denegado".
**Solución:** Estás intentando crear un archivo en un directorio del cual tu usuario activo no es dueño ni tiene permisos de Escritura (`W`). Inicia sesión como `root` o utiliza el comando `chmod` para modificar los privilegios.

**Problema:** La partición no me deja iniciar sesión.
**Solución:** Posiblemente olvidaste formatear la partición (`mkfs`). Solo las particiones formateadas generan el archivo `users.txt` que contiene a `root`.
