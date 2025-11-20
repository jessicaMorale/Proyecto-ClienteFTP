# Makefile para cliente FTP concurrente

CLT= clienteFTP
SRC= MoralesJ-clienteFTP.c connectsock.c connectTCP.c passivesock.c passiveTCP.c errexit.c
OBJ= $(SRC:.c=.o)

CFLAGS= -Wall -Wextra -O2
LDFLAGS=

all: $(CLT)

$(CLT): $(OBJ)
    $(CC) -o $(CLT) $(OBJ) $(LDFLAGS)

clean:
    rm -f $(OBJ) $(CLT)

