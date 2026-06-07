/*  xmodem
 *  autor pdsilva (aka pgordão)
 *  date 2026/05/26
 *
 *  System: PDS317
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
// Linux headers
#include <errno.h> // Error integer and strerror() function

/*
 * dd if=keyboard.bin of=teste1.bin bs=1 count=256 conv=notrunc
 * gcc -o xmodem xmodem.c
 */

unsigned char SERIAL_PORT[32];//= "/dev/ttyUSBx";
#define BAUDRATE    B115200       // Altere para B9600, B57600, etc., se necessário


#define SOH     0x01
#define EOT     0x04
#define ACK     0x06
#define NACK    0x15
#define CAN     0x18


#define DEBUG_NIVEL_0   0x00
#define DEBUG_NIVEL_1   0x01
#define DEBUG_NIVEL_2   0x02
#define DEBUG_NIVEL_3   0x03

unsigned char debug=DEBUG_NIVEL_0;
unsigned int filesize=0,total_bloco=0;

typedef struct{
    unsigned char soh;
    unsigned char pkt;
    unsigned char invpkt;
    unsigned char data[128];
    unsigned char chksum;
}SECTOR;

SECTOR  file[255];

typedef enum{
    NONE,
    CRC,
    SUM
} CRC_SUM;

CRC_SUM type=NONE;

int delay= 1000;

int configurar_serial(int fd){
  // Create new termios struct, we call it 'tty' for convention
  struct termios tty;

  // Read in existing settings, and handle any error
  if (tcgetattr(fd, &tty) != 0) {
      printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
      close(fd);
      return 1;
  }

  tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
  tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
  tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size
  tty.c_cflag |= CS8; // 8 bits per byte (most common)
  tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
  tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

  tty.c_lflag &= ~ICANON;
  tty.c_lflag &= ~ECHO; // Disable echo
  tty.c_lflag &= ~ECHOE; // Disable erasure
  tty.c_lflag &= ~ECHONL; // Disable new-line echo
  tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
  tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
  tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

  tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
  tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
  // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
  // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

  tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
  tty.c_cc[VMIN] = 0;

  // Set in/out baud rate to be 115200
  cfsetispeed(&tty, B115200);
  cfsetospeed(&tty, B115200);

  // Save tty settings, also checking for error
  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
      printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
      close(fd);
      return 1;
  }
  return 0;
}
long obter_tamanho_fseek(FILE *file) {

    // Vai até o final do arquivo
    fseek(file, 0, SEEK_END);

    // ftell diz a posição atual em bytes (ou seja, o tamanho)
    long tamanho = ftell(file);

    rewind(file);

    return tamanho;
}
void printBloco(unsigned char bloco, unsigned char *data){
    if( debug == DEBUG_NIVEL_2){
        unsigned char *d1=data;
        printf("******************************************************************\n");
        printf("SOH    %02X \n",file[bloco].soh);
        printf("PKT    %02X \n",file[bloco].pkt);
        printf("INVPKT %02X \n",file[bloco].invpkt);
        for(int i=0;i<128;i+=16){
            for(int j=0;j<16;j++){
                printf("%02X ",*(d1+i+j));
            }
            printf("\n");
        }
        if( debug >= DEBUG_NIVEL_3) printf("CHKSUM %02X \n",file[bloco].chksum);
    }
}
/*
char readBlock(FILE *arquivo ,unsigned char bloco){
    unsigned char byte=0;
    unsigned char dataIndex=0;
    volatile unsigned char chksum=0;
    chksum=0;
    if( debug >= DEBUG_NIVEL_2) printf("readBlock: Reading bloco [%d]\n",bloco);
    while (fread(&byte, 1, 1, arquivo) > 0) {
        chksum += byte;
        file[bloco].data[dataIndex++] = byte;
        if( dataIndex == 128 ){
            dataIndex = 0;
            file[bloco].chksum=chksum;
            if( debug >= DEBUG_NIVEL_2) printf("readBlock: Bloco[%02d]chksum[%02X]\n",bloco,chksum);
            printBloco(bloco,file[bloco].data);
            return 0;
        }
    }
    //printf("bloco[%02d]chksum[%02X] byte[%02X]\n ",bloco,chksum,byte);
    file[bloco].chksum=chksum;
    return -1;
}*/
unsigned char get_checksum(unsigned char buffer[]){
    unsigned char ck=0;
    for(int i=0; i < 128; i++){
        ck += buffer[i];
    }
    return ck;
}
int ler_bloco_128_bytes(FILE *arquivo, unsigned char *buffer) {
    if (arquivo == NULL || buffer == NULL) {
        return -1;
    }
    // Tenta ler 128 itens de 1 byte cada um de uma única vez
    size_t bytes_lidos = fread(buffer, 1, 128, arquivo);
    // Retorna a quantidade real que foi lida (será 128 se o arquivo tiver dados suficientes,
    // ou menos se bater no fim do arquivo antes de completar 128 bytes).
    return (int)bytes_lidos;
}

unsigned char readFile(const unsigned char * filename){
    unsigned char buffer[129];
    unsigned char bloco=0;
    unsigned char pkt=1;
     // 1. Abre o arquivo binário para leitura
    FILE *arquivo = fopen(filename, "rb");
    if (!arquivo) {
        perror("Erro ao abrir o arquivo binario");
        return EXIT_FAILURE;
    }

    filesize=obter_tamanho_fseek(arquivo);

    if( debug >= DEBUG_NIVEL_1) printf(" size %d \n",filesize);
    memset((void *)file,0,128*255);
    while(1){
        memset(buffer,0,128);
        if( ler_bloco_128_bytes(arquivo,buffer) > 0){
            if( debug >= DEBUG_NIVEL_2) printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
            file[bloco].soh = 0x01;
            file[bloco].pkt = pkt++;
            file[bloco].invpkt = 255-pkt;
            file[bloco].chksum = get_checksum(buffer);
            memcpy(file[bloco].data,buffer,128);
            printBloco(bloco,file[bloco].data);
            bloco++;
            if( bloco >= 32640 ){
                printf("Se o arquivo é maior que 32640 isso é um problema...\n");
                fclose(arquivo);
                exit(1);
            }
        }else{
            file[bloco].soh = 0;
            file[bloco].pkt = 0;
            file[bloco].invpkt = 0xFF;
            if( debug >= DEBUG_NIVEL_2) printf("readBlock retornou -1 no bloco[%d]\n",bloco);
            fclose(arquivo);
            break;
        }
    }
    return bloco;
}
char initTransmition(int fd_serial){
    unsigned char byte='3';
    if( debug >= DEBUG_NIVEL_1) printf("Enviando comando 3\n");
    write(fd_serial, &byte, 1);
    tcflush(fd_serial, TCIFLUSH);
    if( debug >= DEBUG_NIVEL_1) printf("Aguardando NACK\n");
    printf("Ponha no ar o programa de xmodem no Orion\n");
    printf("aguardando..\n");
    while( read(fd_serial, &byte, 1) < 1){
        usleep(delay);
        byte='3';
        write(fd_serial, &byte, 1);
    }
    if ( byte != 0x15){
        printf("Enviou cmd inicio errado...[%02X]\n",byte);
        return -1;
    }else{
        if( debug >= DEBUG_NIVEL_1) printf("ACK recebido...\n");
        type = SUM;
    }

    return 0;
}
unsigned char transmitHeader(int fd_serial,unsigned char bloco){
        unsigned char byte=0x01;
        if( debug >= DEBUG_NIVEL_1) printf("Enviando SOH 01\n");
        write(fd_serial, &byte, 1);
        tcflush(fd_serial, TCIFLUSH);
        usleep(delay);
        byte = file[bloco].pkt;
        if( debug >= DEBUG_NIVEL_1) printf("Enviando Numero do pacote [%02X]\n",byte);
        write(fd_serial, &byte, 1);
        tcflush(fd_serial, TCIFLUSH);
        usleep(delay);
        byte = file[bloco].invpkt;
        if( debug >= DEBUG_NIVEL_1) printf("Enviando Numero do pacote invertido[%02X]\n",byte);
        write(fd_serial, &byte, 1);
        tcflush(fd_serial, TCIFLUSH);
        usleep(delay);

        return 0;
}
unsigned char transmitData(int fd_serial,unsigned char bloco){
    if( debug >= DEBUG_NIVEL_1) printf("Enviando os dados do bloco[%02X]\n",bloco);
    for(int i=0;i<128;i++){
        write(fd_serial, &file[bloco].data[i], 1);
        tcflush(fd_serial, TCIFLUSH);
        if( debug >= DEBUG_NIVEL_2) printf("[%02X]",file[bloco].data[i]);
        fflush(stdout);
        usleep(delay+5000);
    }
    return 0;
}
unsigned char transmitChksum(int fd_serial,unsigned char bloco){
    if( debug >= DEBUG_NIVEL_1) printf("\nEnviando ocheck sum do bloco[%02X] ",bloco);
    write(fd_serial, &file[bloco].chksum, 1);
    tcflush(fd_serial, TCIFLUSH);
    if( debug >= DEBUG_NIVEL_1)printf("[%02X]\n",file[bloco].chksum);
    fflush(stdout);
    usleep(delay);
}
unsigned char get_answer(int fd_serial,unsigned char bloco){
        unsigned char byte=0;
        if( debug >= DEBUG_NIVEL_1) printf("Vou aguardar ACK ou NACK\n");
        ssize_t bytes_lidos = read(fd_serial, &byte, 1);
        if( debug >= DEBUG_NIVEL_1) printf("Total Lido[%02x] [%02X]\n",(unsigned int)bytes_lidos,byte);
        if( debug >= DEBUG_NIVEL_1)  if( byte == ACK) {printf("Recebido  ACK [%02X]\n",byte);}
        if( debug >= DEBUG_NIVEL_1)  if( byte == NACK){printf("Recebido NACK [%02X]\n",byte);}
        return byte;
}
unsigned char transmitEof(int fd_serial,unsigned char bloco){
        unsigned char byte=EOT;
        if( debug >= DEBUG_NIVEL_1) printf("Vou transmitir EOT\n");
        write(fd_serial, &byte, 1);
        tcflush(fd_serial, TCIFLUSH);
        tcflush(fd_serial, TCIFLUSH);
}
unsigned char transmitCan(int fd_serial,unsigned char bloco){
        unsigned char byte=CAN;
        if( debug >= DEBUG_NIVEL_1) printf("Vou transmitir CAN\n");
        write(fd_serial, &byte, 1);
        tcflush(fd_serial, TCIFLUSH);
        tcflush(fd_serial, TCIFLUSH);
}
unsigned char transmitFile(){
    unsigned char bloco=0;
    unsigned char retrans=0;
    unsigned char ret=0;
    int fd_serial = open(SERIAL_PORT, O_RDWR | O_NOCTTY);
    if (fd_serial < 0) {
        printf("Erro ao abrir a porta serial [%s]",SERIAL_PORT);
        return EXIT_FAILURE;
    }
    // 3. Configura os parâmetros da serial
    if (configurar_serial(fd_serial) < 0) {
        close(fd_serial);
        return EXIT_FAILURE;
    }
    if ( ! initTransmition(fd_serial) ){
        unsigned char retry=3;
        if( debug >= DEBUG_NIVEL_1) printf("Tentativa de transmissão pacote[%02X]\n",file[bloco].pkt);
        while( file[bloco].pkt > 0){
            printf("Transmiting block[%02X]\n",bloco);
            transmitHeader(fd_serial,bloco);
            transmitData(fd_serial,bloco);
            transmitChksum(fd_serial,bloco);
retry3:
            ret = get_answer(fd_serial,bloco);
            if( ret == NACK ){
                printf("Tentativa de RETRANSMISSÃO pacote[%02X] SEM SUCESSO FINALIZANDO O ENVIO DE ARQUIVO\n",file[bloco].pkt);
                transmitEof(fd_serial,bloco);
                transmitCan(fd_serial,bloco);
                transmitCan(fd_serial,bloco);
                close(fd_serial);
                exit(1);
                if(  retrans < 2 ){
                    printf("Tentativa de RETRANSMISSÃO pacote[%02X]\n",file[bloco].pkt);
                    retrans++;
                }else{
                    printf("Tentativa de RETRANSMISSÃO pacote[%02X] SEM SUCESSO FINALIZANDO O ENVIO DE ARQUIVO\n",file[bloco].pkt);
                    break;
                }
            }else if( ret == ACK ){
                if( debug >= DEBUG_NIVEL_1) printf("Recebido ACK\n");
                retrans=0;
                bloco++;
            }else{
                while(retry--){
                    goto retry3;
                }
                transmitEof(fd_serial,bloco);
                transmitEof(fd_serial,bloco);
                transmitEof(fd_serial,bloco);
                transmitEof(fd_serial,bloco);
                close(fd_serial);
                exit(1);
            }
        }
        transmitEof(fd_serial,bloco);
    }

    close(fd_serial);
}
int main(int argc, char *argv[]) {
    unsigned char total_blocos=0;
    printf("System PDS317 - xmodem app copyright (c)2026 pdsilva aka(pgordao)\n");
    printf("This xmodem sends a number 3 to start the xmodem protocol in PDS317\n");
    printf("Yes! I know this is not what xmodem protocol does, but it is as it is!\n");
    printf("Accept hurt a litle bit less!!!\n\n\n");
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <arquivo_binario> </dev/Serial port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    printf("File name %s ",argv[1]);
    total_blocos=readFile(argv[1]);
    printf("File loaded...[0x%02X] blocos to transmit\n",total_blocos);

    memcpy(SERIAL_PORT,argv[2],strlen(argv[2]));
    printf("Transminting [%s] to [%s]\n",argv[1],SERIAL_PORT);
    transmitFile();
    return EXIT_SUCCESS;
}

