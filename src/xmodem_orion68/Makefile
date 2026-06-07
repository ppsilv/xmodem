# Nome do executável final
TARGET = xmodemOrion

# Compilador e flags de compilação
CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.

# Links e bibliotecas (caso use alguma, ex: -lm para math.h)
LIBS =

# Descobre automaticamente todos os arquivos .c no diretório atual
SRCS = $(wildcard *.c)

# Cria a lista de arquivos .o baseada nos arquivos .c encontrados
OBJS = $(SRCS:.c=.o)

# Regra padrão (a primeira que roda ao digitar apenas 'make')
all: $(TARGET)

# Regra para linkar os objetos e gerar o executável
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

# Regra genérica para compilar cada arquivo .c em .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Regra para limpar os arquivos temporários de compilação
clean:
	rm -f $(OBJS) $(TARGET)

# Evita conflito caso existam arquivos com os nomes 'all' ou 'clean'
.PHONY: all clean
