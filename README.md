# Proyecto Cliente FTP en C
Este proyecto implementa un cliente FTP desarrollado en C, capaz de conectarse a un servidor FTP,
manejar comandos del protocolo FTP, procesar respuestas del servidor y realizar transferencias de 
archivos en modos activo (PORT) y pasivo (PASV).
Incluye también un ambiente de pruebas con un servidor FTP configurado localmente usando pyftpdlib.

---
## Contenido del repositorio
El repositorio contiene los siguientes archivos:
* **MoralesJ-clienteFTP** - Es el código del cliente FTP.
* **Makefile** - Permite compilar todo el proyecto.
---

# Configuración del Servidor FTP 
Para las pruebas del cliente FTP se utilizó un servidor FTP local mediante la librería pyftpdlib, 
que permite levantar un servidor ligero, ideal para desarrollo.
### **PASOS REALIZADOS**
* **Instalación de pyftpdlib**
```pip install pyftpdlib```
* **Creación del directorio raíz del servidor**
```mkdir ftp_root```

Aquí se almacenan los archivos que el cliente FTP puede subir o descargar.
* **Arranque del servidor FTP**
```python3 -m pyftpdlib -p 2121 -d ftp_root```

Explicación:
  - -p 2121 → Puerto donde escuchará el servidor FTP
  - -d ftp_root → Directorio raíz compartido con los clientes
* **Usuario configurado**
  Se configuró un usuario, por ejemplo:
  
  Usuario: anonymous
  Contraseña: 1234

El servidor solicita USER y PASS al conectar, con esto quedó funcionando el servidor en:
**localhost:2121**

---
## Funcionamiento del cliente FTP en C
El cliente FTP implementa todas las etapas de comunicación necesarias para interactuar con
un servidor real.
### Características principales
* Mantiene una conexión de control abierta (socket principal)
* Soporta comandos FTP básicos:
USER, PASS, PASV, PORT, STOR, RETR
* Soporta comandos adicionales:
PWD, MKD, DELE, CWD, LIST, QUIT
* Maneja conexiones:
  
Modo PASV (cliente se conecta al servidor)

Modo PORT (servidor se conecta al cliente)

Descargas y subidas de múltiples archivos (mget/mput) usando fork()

Uso de funciones auxiliares para modularidad
## Arquitectura del código
### Funciones auxiliares
* sendCmd() → Envía comandos al servidor y recibe respuesta
* replyCode() → Extrae el código numérico (230, 530, 226, etc.)
* pasvConnect() → Implementa modo PASV
* portPrepareAndSend() → Implementa modo PORT
### Transferencias de archivos
* do_retr_pasv() → Descarga archivos (RETR)
* do_stor_pasv() → Sube archivos en modo PASV
* do_stor_port() → Sube archivos en modo activo
### Login
El cliente solicita:

```
Please enter your username:
Enter your password:
```
Si el servidor responde 230, el login es exitoso.
### Bucle principal
Procesa los comandos escritos por el usuario:
* pwd
* dir
* get archivo.txt
* put archivo.txt
* mget a.txt b.txt
* mput c.txt d.txt
* quit
---
## Ejecución del cliente FTP
### Compilar el proyecto
Con Makefile:
```
make
```
Sin Makefile:
```
gcc -o clienteFTP MoralesJ-clienteFTP.c connectTCP.c
connectsock.c passiveTCP.c passivesock.c errexit.c
```
### Ejecutar el cliente
```
./clienteFTP localhost 2121
```
### Flujo de pruebas realizadas
1. El servidor respondió con saludo inicial.
2. Se ingresó usuario y contraseña configurados en pyftpdlib.
3. Se probó:
* pwd → retornó el directorio actual
* dir → listó archivos en ftp_root
* get archivo.txt → descargó archivo
* put archivo.txt → subió archivo
* mget y mput → funcionaron en paralelo usando fork
* quit → cerró sesión correctamente

El comportamiento fue exitoso porque el servidor respondió con:

* 230 → login correcto
* 250 → comandos como CWD, DELE exitosos
* 227 → modo PASV habilitado
* 226 → transferencia finalizada
--- 
## Manual para subir el proyecto a GitHub
A continuación se detallan exactamente los pasos realizados para 
subir este proyecto al repositorio.

### Inicializar Git
```
git init
```
### Añadir solo algunos archivos (si se desea)
```
git add archivo1.c archivo2.c
```
O añadir todo:
```
git add .
```
### Crear el primer commit
```
git commit -m "Proyecto cliente FTP en C"
```
### Añadir el repositorio remoto
```
git remote add origin https://github.com/usuario/Proyecto-ClienteFTP.git
```
### Subir los cambios
```
git push -u origin main
```
Si la rama estaba desactualizada:
```
git pull --rebase origin main
git push
```
## Conclusión
Este proyecto implementa un cliente FTP funcional, modular y educativo, capaz 
de interactuar con un servidor real configurado localmente. Además, incluye todas 
las herramientas para compilar, ejecutar y extender su funcionalidad.

---
# NOTA: 
El Cliente MoralesJ-clienteFTP.c para su compilación usa códigos auxiliares además de Makefile 
a continuacion:
```
/* connectTCP.c */
int connectsock(const char *host, const char *service, const char *transport);

int connectTCP(const char *host, const char *service) {
  return connectsock(host, service, "tcp");
}
```
```
/* connectsock.c */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifndef INADDR_NONE
#define INADDR_NONE     0xffffffff
#endif

extern int errno;
int errexit(const char *format, ...);

int connectsock(const char *host, const char *service, const char *transport) {
  struct hostent  *phe;
  struct servent  *pse;
  struct protoent *ppe;
  struct sockaddr_in sin;
  int s, type;

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;

  /* Map service name to port number */
  if ((pse = getservbyname(service, transport)))
    sin.sin_port = pse->s_port;
  else if ((sin.sin_port = htons((unsigned short)atoi(service))) == 0)
    errexit("can't get \"%s\" service entry\n", service);

  /* Map host name to IP address */
  if ((phe = gethostbyname(host)))
    memcpy(&sin.sin_addr, phe->h_addr, phe->h_length);
  else if ((sin.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE)
    errexit("can't get \"%s\" host entry\n", host);

  /* Map protocol name to protocol number */
  if ((ppe = getprotobyname(transport)) == 0)
    errexit("can't get \"%s\" protocol entry\n", transport);

  /* Use protocol to choose a socket type */
  if (strcmp(transport, "udp") == 0)
    type = SOCK_DGRAM;
  else
    type = SOCK_STREAM;

  /* Allocate a socket */
  s = socket(PF_INET, type, ppe->p_proto);
  if (s < 0)
    errexit("can't create socket: %s\n", strerror(errno));

  /* Connect the socket */
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    errexit("can't connect to %s.%s: %s\n", host, service, strerror(errno));

  return s;
}
```
```
/* passiveTCP.c */
int passivesock(const char *service, const char *transport, int qlen);

int passiveTCP(const char *service, int qlen) {
  return passivesock(service, "tcp", qlen);
}
```
```
/* passivesock.c */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

extern int errno;
int errexit(const char *format, ...);

unsigned short portbase = 0;

int passivesock(const char *service, const char *transport, int qlen) {
  struct servent  *pse;
  struct protoent *ppe;
  struct sockaddr_in sin;
  int s, type;

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;

  /* Map service name to port number */
  if ((pse = getservbyname(service, transport)))
    sin.sin_port = htons(ntohs((unsigned short)pse->s_port) + portbase);
  else if ((sin.sin_port = htons((unsigned short)atoi(service))) == 0)
    errexit("can't get \"%s\" service entry\n", service);

  /* Map protocol name to protocol number */
  if ((ppe = getprotobyname(transport)) == 0)
    errexit("can't get \"%s\" protocol entry\n", transport);

  /* Use protocol to choose a socket type */
  if (strcmp(transport, "udp") == 0)
    type = SOCK_DGRAM;
  else
    type = SOCK_STREAM;

  /* Allocate a socket */
  s = socket(PF_INET, type, ppe->p_proto);
  if (s < 0)
    errexit("can't create socket: %s\n", strerror(errno));

  /* Bind the socket */
  if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    errexit("can't bind to %s port: %s\n", service, strerror(errno));
  if (type == SOCK_STREAM && listen(s, qlen) < 0)
    errexit("can't listen on %s port: %s\n", service, strerror(errno));

  return s;
}
```
```
/* errexit.c */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int errexit(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  exit(1);
}
```
