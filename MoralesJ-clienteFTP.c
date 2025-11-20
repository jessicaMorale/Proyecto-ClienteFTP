/* MoralesJ-clienteFTP.c - Cliente FTP concurrente por procesos
 *
 * Características:
 * - Conexión de control persistente
 * - Comandos: USER, PASS, PASV, PORT, STOR, RETR
 * - Adicionales: PWD, MKD, DELE, CWD, LIST, QUIT
 * - Concurrencia: mget/mput (fork por archivo) para transferencias múltiples
 * - Integra auxiliares: connectTCP, connectsock, passiveTCP, passivesock, errexit
 *
 * Uso:
 *   ./clienteFTP [host [port]]
 *     host: default "localhost"
 *     port: default "ftp" (21) o numérico
 */

#include <unistd.h>	//Funciones POSIX: read, write, close, fork, ect.
#include <stdlib.h>	//funciones estándar: malloc, free, atoi, exxit, ect.
#include <string.h>	//manejo de cadenas: strlen, strcpy, strcmp, etc.
#include <stdio.h>	//entrada/salida estándar: printf, perror, etc.
#include <errno.h>	//variable global errno para errores del sistema.
#include <netdb.h>	//funciones de red: gethostbyname, etc.
#include <sys/types.h>	//tipos básicos para sockets.
#include <sys/socket.h>	//funciones de sockets: socket, connect, bind, etc.
#include <netinet/in.h>	//estructuras para direcciones IP (sockaddr_in).
#include <arpa/inet.h>	//funciones para convertir direcciones (inet_ntoa).
#include <sys/wait.h>	//manejo de procesos hijos (wait)
#include <fcntl.h>	//control de archivos (open, O_RDONLY, etc.)
#include <ctype.h>	//da acceso a las funciones de clasificación de caracteres y valida respuestas del servidor

extern int errno;	//usada para saber el código de error de llamadas al sistema.

int  errexit(const char *format, ...);	//función auxiliar para imprimir error y salir.
int  connectTCP(const char *host, const char *service);	//conecta a host/puerto TCP
int  passiveTCP(const char *service, int qlen);	// crea socket pasivo (servidor)

#define LINELEN 1024	//define un tamaño máximo de línea (buffer) de 1024 caracteres

/* Utilidades */  //función que elimina los caracteres de fin de línea (\r y \n) al final de una cadena.
//static void trim_crlf(char *s) {
//  size_t n = strlen(s);
//  while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n')) { s[n-1] = '\0'; n--; }
//}

/* Envía comandos FTP por el canal de control y muestra la respuesta */
static int sendCmd(int s, const char *cmd, char *res, size_t reslen) {
  char buf[LINELEN];
  int n;

  snprintf(buf, sizeof(buf), "%s\r\n", cmd);		//arma el comando con CRLF
  n = write(s, buf, strlen(buf));			//lo envía por el socket de control
  if (n < 0) { perror("write control"); return -1; }

  n = read(s, res, (int)reslen-1);			//lee la respuesta del servidor
  if (n < 0) { perror("read control"); return -1; }
  res[n] = '\0';					//termina la cadena con '\0'
  printf("%s", res); /* respuesta puede incluir \r\n */	//imprime la respuesta en pantalla
  return n;						//devuelve número de butes leídos
}

/* Extrae código numérico de respuesta (ej. "230 Logged in") */
static int replyCode(const char *res) {
  if (strlen(res) < 3) return -1;				//si la respuesta es muy corta , error
  if (!isdigit((unsigned char)res[0]) || !isdigit((unsigned char)res[1]) || !isdigit((unsigned char)res[2])) return -1;
  return (res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0');	//convierte los 3 dígitos en número
}

/* Modo PASV: retorna socket de datos conectado al servidor */
static int pasvConnect(int s) {
  char res[LINELEN];
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "PASV");				//arma comando PASV
  if (sendCmd(s, cmd, res, sizeof(res)) < 0) return -1;

  int code = replyCode(res);					//obtiene el código de respuesta
  if (code < 0 || (code != 227 && code != 229)) {		//debe ser 227 (PASV)
    fprintf(stderr, "PASV no aceptado: %s", res);
    return -1;
  }

  /* Buscar h1,h2,h3,h4,p1,p2 dentro de paréntesis */
  char *p = strchr(res, '(');					//busca parámetros en respuesta
  if (!p) { fprintf(stderr, "Respuesta PASV sin parámetros.\n"); return -1; }
  int h1,h2,h3,h4,p1,p2;
  if (sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2) != 6) {
    fprintf(stderr, "Formato PASV inválido.\n");
    return -1;
  }
  char host[64], port[16];
  snprintf(host, sizeof(host), "%d.%d.%d.%d", h1,h2,h3,h4);	//arma IP
  int nport = p1*256 + p2;					//calcula puerto
  snprintf(port, sizeof(port), "%d", nport);

  int sdata = connectTCP(host, port);				//conecta al socket de datos
  if (sdata < 0) { perror("connectTCP PASV"); return -1; }
  return sdata;							//devuelve socket de datos
}

/* Modo PORT: abre socket pasivo local y envía PORT con IP y puerto */
static int portPrepareAndSend(int s, const char *portStr, char *ipCommaOut, size_t ipCommaOutLen) {
  /* Determinar IP local en formato a,b,c,d */
  char lname[64];
  if (gethostname(lname, sizeof(lname)) < 0) { perror("gethostname"); return -1; }
  struct hostent *hent = gethostbyname(lname);
  if (!hent || !hent->h_addr_list || !hent->h_addr_list[0]) { fprintf(stderr, "No se pudo resolver IP local.\n"); return -1; }
  char ip[64];
  snprintf(ip, sizeof(ip), "%s", inet_ntoa(*((struct in_addr*) hent->h_addr_list[0])));
  for (size_t i = 0; i < strlen(ip); i++) if (ip[i] == '.') ip[i] = ','; /* a,b,c,d */

  /* portStr viene como decimal; convertir a p1,p2 */
  int pnum = atoi(portStr);
  int p1 = (pnum / 256) & 0xFF;
  int p2 = (pnum % 256) & 0xFF;
  char portComma[16];
  snprintf(portComma, sizeof(portComma), "%d,%d", p1, p2);

  char res[LINELEN], cmd[128];
  snprintf(ipCommaOut, ipCommaOutLen, "%s", ip);			//guarda IP en formato con comas
  snprintf(cmd, sizeof(cmd), "PORT %s,%s", ipCommaOut, portComma);	//arma comando PORT
  if (sendCmd(s, cmd, res, sizeof(res)) < 0) return -1;
  int code = replyCode(res);
  if (code < 0 || code >= 500) {					//error si codigo >=500
    fprintf(stderr, "PORT rechazado: %s", res);
    return -1;
  }
  return 0;
}

/* Ayuda */
static void ayuda() {
  printf("Cliente FTP concurrente. Comandos:\n"
         "  help            - muestra esta ayuda\n"
         "  dir             - LIST del directorio actual del servidor\n"
         "  get <archivo>   - RETR desde servidor (PASV)\n"
         "  put <archivo>   - STOR hacia servidor (PASV)\n"
         "  pput <archivo>  - STOR hacia servidor usando PORT local fijo\n"
         "  mget <a...>     - múltiples RETR en paralelo (PASV, fork por archivo)\n"
         "  mput <a...>     - múltiples STOR en paralelo (PASV, fork por archivo)\n"
         "  cd <dir>        - CWD en servidor\n"
         "  pwd             - PWD en servidor\n"
         "  mkd <dir>       - MKD en servidor\n"
         "  dele <archivo>  - DELE en servidor\n"
         "  quit            - QUIT y salir\n");
}

/* Transferencia RETR (bloqueante en el proceso actual) */
static int do_retr_pasv(int s, const char *remote, const char *local) {
  int sdata = pasvConnect(s);		//abre conexión de datos en modo PASV
  if (sdata < 0) return -1;

  char res[LINELEN], cmd[LINELEN];
  snprintf(cmd, sizeof(cmd), "RETR %s", remote);	//arma comando RETR con nombre remoto
  if (sendCmd(s, cmd, res, sizeof(res)) < 0) { close(sdata); return -1; }
  int code = replyCode(res);				//analiza código de respuesta
  if (code >= 500) { close(sdata); fprintf(stderr, "RETR error: %s", res); return -1; }

  FILE *fp = fopen(local, "wb");			//abre archivo local para escritura binaria
  if (!fp) { perror("fopen local"); close(sdata); return -1; }

  char buf[LINELEN];
  int n;
  while ((n = recv(sdata, buf, sizeof(buf), 0)) > 0) {	//recibe datos del servidor
    fwrite(buf, 1, n, fp);				//escribe en archivo local
  }
  fclose(fp);						//cierra archivo
  close(sdata);						//cierra socket de datos

  /* Leer respuesta final */
  int r = read(s, res, sizeof(res)-1);			//lee respuesta final del control
  if (r > 0) { res[r] = '\0'; printf("%s", res); }
  return 0;
}

/* Transferencia STOR (bloqueante en el proceso actual) */
static int do_stor_pasv(int s, const char *local, const char *remote) {
  FILE *fp = fopen(local, "rb");			//abre archivo local para lectura
  if (!fp) { perror("fopen local"); return -1; }

  int sdata = pasvConnect(s);				//abre conexión de datos PASV
  if (sdata < 0) { fclose(fp); return -1; }

  char res[LINELEN], cmd[LINELEN];
  snprintf(cmd, sizeof(cmd), "STOR %s", remote);	//arma comando STOR con nombre remoto
  if (sendCmd(s, cmd, res, sizeof(res)) < 0) { close(sdata); fclose(fp); return -1; }
  int code = replyCode(res);
  if (code >= 500) { close(sdata); fclose(fp); fprintf(stderr, "STOR error: %s", res); return -1; }

  char buf[LINELEN];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {	//lee bloques del archivo local
    if (send(sdata, buf, (int)n, 0) < 0) { perror("send data"); break; }//envía al servidor
  }
  fclose(fp);
  close(sdata);

  int r = read(s, res, sizeof(res)-1);			//lee respuesta final del control
  if (r > 0) { res[r] = '\0'; printf("%s", res); }
  return 0;
}

/* pput usando PORT: abre puerto local, espera accept y envía */
static int do_stor_port(int s, const char *local, const char *remote, const char *localPort) {
  int slisten = passiveTCP(localPort, 5);		//abre socket pasivo local en puerto fijo
  if (slisten < 0) { perror("passiveTCP"); return -1; }

  char ipComma[64];
  if (portPrepareAndSend(s, localPort, ipComma, sizeof(ipComma)) < 0) { close(slisten); return -1; }

  char res[LINELEN], cmd[LINELEN];
  snprintf(cmd, sizeof(cmd), "STOR %s", remote);	//arma comando STOR
  if (sendCmd(s, cmd, res, sizeof(res)) < 0) { close(slisten); return -1; }
  int code = replyCode(res);
  if (code >= 500) { fprintf(stderr, "STOR/PORT error: %s", res); close(slisten); return -1; }

  struct sockaddr_in addrSvr;
  socklen_t alen = sizeof(addrSvr);
  int sdata = accept(slisten, (struct sockaddr *)&addrSvr, &alen);	//espera conexión del servidor
  if (sdata < 0) { perror("accept"); close(slisten); return -1; }

  FILE *fp = fopen(local, "rb");		//abre archivo local
  if (!fp) { perror("fopen local"); close(sdata); close(slisten); return -1; }

  char buf[LINELEN];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
    if (send(sdata, buf, (int)n, 0) < 0) { perror("send data"); break; }	// envía al servidor
  }
  fclose(fp);
  close(sdata);
  close(slisten);

  int r = read(s, res, sizeof(res)-1);		//lee la respuesta final
  if (r > 0) { res[r] = '\0'; printf("%s", res); }
  return 0;
}

/* Login interactivo */
static int do_login(int s) {
  char res[LINELEN];
  int n = read(s, res, sizeof(res)-1); /* saludo del servidor */
  if (n < 0) { perror("read greeting"); return -1; }
  res[n] = '\0';
  printf("%s", res);

  char user[128];
  printf("Please enter your username: ");	//pide el usuario
  if (scanf("%127s", user) != 1) return -1;
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "USER %s", user);	//arma comando USER
  if (sendCmd(s, cmd, res, sizeof(res)) < 0) return -1;

  /* limpiar stdin */
  int c; while ((c = getchar()) != '\n' && c != EOF) {}	//limpia stdin

  char *pass = getpass("Enter your password: ");	//pide la contraseña sin eco
  snprintf(cmd, sizeof(cmd), "PASS %s", pass);		//arma comando PASS
  if (sendCmd(s, cmd, res, sizeof(res)) < 0) return -1;

  int code = replyCode(res);				//analiza respuesta
  if (code == 230) return 0;				//230 = login exitoso
  fprintf(stderr, "Login no aceptado: %s", res);
  return -1;
}

/* Muestra lista del directorio actual */
static int do_list(int s) {
  int sdata = pasvConnect(s);				//abre conexión de datos PASV
  if (sdata < 0) return -1;

  char res[LINELEN];
  if (sendCmd(s, "LIST", res, sizeof(res)) < 0) { close(sdata); return -1; }
  int code = replyCode(res);
  if (code >= 500) { close(sdata); fprintf(stderr, "LIST error: %s", res); return -1; }

  char buf[LINELEN];
  int n;
  while ((n = recv(sdata, buf, sizeof(buf), 0)) > 0) {	//recibe listado
    fwrite(buf, 1, n, stdout);				//imprime en pantalla
  }
  close(sdata);

  n = read(s, res, sizeof(res)-1);			//lee respuesta final
  if (n > 0) { res[n] = '\0'; printf("%s", res); }
  return 0;
}

/* Bucle principal */
int main(int argc, char *argv[]) {
  char *host = "localhost";		//host por defecto
  char *service = "ftp";		//servicio por defecto (puerto 21)

  switch (argc) {		//Procesa argumentos de línea de comandos
    case 1: break;		//sin argumentos -> usa localhost:ftp
    case 3: service = argv[2];	//si hay 2 argumentos, el segundo es el puerto
            /* FALL THROUGH */
    case 2: host = argv[1]; break;	//si hay 1 argumento, es el host
    default:
      fprintf(stderr, "Uso: clienteFTP [host [port]]\n");
      exit(1);
  }

  int s = connectTCP(host, service);	//conecta al servidor FTP
  if (s < 0) errexit("No se pudo conectar a %s:%s\n", host, service);

  if (do_login(s) < 0) { close(s); exit(1); }	//realiza login interactivo

  ayuda();		//muestra ayuda inicial

  char prompt[LINELEN];
  while (1) {		//bucle interactivo principal
    printf("ftp> ");	//muestra prompt
    if (!fgets(prompt, sizeof(prompt), stdin)) break;	//lee comando del usuario
    prompt[strcspn(prompt, "\n")] = 0;			//elimina saltos de línea
    if (strlen(prompt) == 0) continue;			//ignora líneas vacías

    char *ucmd = strtok(prompt, " ");			//separa comando principal
    if (!ucmd) continue;

    if (strcmp(ucmd, "help") == 0) {
      ayuda();						//muestra ayuda

    } else if (strcmp(ucmd, "dir") == 0) {
      do_list(s);					//lista de directorio remoto

    } else if (strcmp(ucmd, "get") == 0) {
      char *arg = strtok(NULL, " ");			//obtiene nombre de archivo
      if (!arg) { printf("Uso: get <archivo>\n"); continue; }
      /* local = mismo nombre */
      if (do_retr_pasv(s, arg, arg) < 0) printf("Fallo en get %s\n", arg);

    } else if (strcmp(ucmd, "put") == 0) {
      char *arg = strtok(NULL, " ");
      if (!arg) { printf("Uso: put <archivo>\n"); continue; }
      if (do_stor_pasv(s, arg, arg) < 0) printf("Fallo en put %s\n", arg);

    } else if (strcmp(ucmd, "pput") == 0) {
      char *arg = strtok(NULL, " ");
      if (!arg) { printf("Uso: pput <archivo>\n"); continue; }
      /* Puerto local fijo para PORT, ajusta si necesario */
      const char *localPort = "1030";			//puerto local fijo para PORT
      if (do_stor_port(s, arg, arg, localPort) < 0) printf("Fallo en pput %s\n", arg);

    } else if (strcmp(ucmd, "mget") == 0) {
      /* múltiples get concurrentes: fork por archivo */
      int status;
      pid_t pid;
      char *arg;
      int children = 0;
      while ((arg = strtok(NULL, " ")) != NULL) {
        pid = fork();
        if (pid == 0) {		//proceso hijo
          /* Proceso hijo: realiza RETR PASV usando el mismo control s */
          if (do_retr_pasv(s, arg, arg) < 0) _exit(2);
          _exit(0);
        } else if (pid > 0) {
          children++;		//proceso padre cuenta hijos
        } else {
          perror("fork");
        }
      }
      /* Esperar hijos */
      for (int i = 0; i < children; i++) wait(&status);	//espera hijos

    } else if (strcmp(ucmd, "mput") == 0) {
      /* múltiples put concurrentes: fork por archivo */
      int status;
      pid_t pid;
      char *arg;
      int children = 0;
      while ((arg = strtok(NULL, " ")) != NULL) {
        pid = fork();
        if (pid == 0) {
          if (do_stor_pasv(s, arg, arg) < 0) _exit(2);
          _exit(0);
        } else if (pid > 0) {
          children++;
        } else {
          perror("fork");
        }
      }
      for (int i = 0; i < children; i++) wait(&status);

    } else if (strcmp(ucmd, "cd") == 0) {
      char *arg = strtok(NULL, " ");
      if (!arg) { printf("Uso: cd <dir>\n"); continue; }
      char res[LINELEN], cmd[LINELEN];
      snprintf(cmd, sizeof(cmd), "CWD %s", arg);	//cambia directorio remoto
      sendCmd(s, cmd, res, sizeof(res));

    } else if (strcmp(ucmd, "pwd") == 0) {
      char res[LINELEN];
      sendCmd(s, "PWD", res, sizeof(res));		//muestra directorio actual

    } else if (strcmp(ucmd, "mkd") == 0) {
      char *arg = strtok(NULL, " ");
      if (!arg) { printf("Uso: mkd <dir>\n"); continue; }
      char res[LINELEN], cmd[LINELEN];
      snprintf(cmd, sizeof(cmd), "MKD %s", arg);	//crea directorio remoto
      sendCmd(s, cmd, res, sizeof(res));

    } else if (strcmp(ucmd, "dele") == 0) {
      char *arg = strtok(NULL, " ");
      if (!arg) { printf("Uso: dele <archivo>\n"); continue; }
      char res[LINELEN], cmd[LINELEN];
      snprintf(cmd, sizeof(cmd), "DELE %s", arg);	//borra archivo remoto
      sendCmd(s, cmd, res, sizeof(res));

    } else if (strcmp(ucmd, "quit") == 0) {
      char res[LINELEN];
      sendCmd(s, "QUIT", res, sizeof(res));		//cierra sesión FTP
      break;						// sale del bucle

    } else {
      printf("%s: comando no implementado.\n", ucmd);	//comando desconocido
    }
  }

  close(s);		//cierra socket de control
  return 0;		//fin del programa
}
