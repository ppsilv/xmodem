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

/*
 * dd if=keyboard.bin of=teste1.bin bs=1 count=256 conv=notrunc
 * gcc -o xmodem xmodem.c
 */

unsigned char SERIAL_PORT[32];//= "/dev/ttyUSBx";
#define BAUDRATE    B115200       // Altere para B9600, B57600, etc., se necessário


#define NACK    0x15
#define ACK     0x06
#define EOT     0x04

#define DEBUG_NIVEL_0   0x00
#define DEBUG_NIVEL_1   0x01
#define DEBUG_NIVEL_2   0x02

unsigned char debug=DEBUG_NIVEL_0;

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
int configurar_serial(int fd) {
    struct termios tty;
    
    if (tcgetattr(fd, &tty) != 0) {
        perror("Erro ao obter atributos da serial");
        return -1;
    }

    // Configura a velocidade de entrada e saída
    cfsetospeed(&tty, BAUDRATE);
    cfsetispeed(&tty, BAUDRATE);

    // Modo RAW (Bruto): desativa processamento de eco, nova linha, sinais, etc.
    tty.c_cflag &= ~PARENB;        // Sem paridade
    tty.c_cflag &= ~CSTOPB;        // 1 Stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 bits de dados
    tty.c_cflag &= ~CRTSCTS;       // Sem controle de fluxo por hardware
    tty.c_cflag |= (CLOCAL | CREAD); // Ativa leitura e ignora linhas de controle

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Desativa modo canônico
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);         // Desativa controle de fluxo XON/XOFF
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;          // Desativa pós-processamento de saída

    // Configurações de timeout para leitura (não impactam a escrita pura)
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Erro ao aplicar atributos da serial");
        return -1;
    }
    return 0;
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
        printf("CHKSUM %02X \n",file[bloco].chksum);
    }
}
char readBlock(FILE *arquivo ,unsigned char bloco){
    unsigned char byte=0;
    unsigned char dataIndex=0;
    volatile unsigned char chksum=0;
    chksum=0;
    if( debug >= DEBUG_NIVEL_1) printf("Reading bloco [%d]\n",bloco);
    while (fread(&byte, 1, 1, arquivo) > 0) {
        chksum += byte;
        file[bloco].data[dataIndex++] = byte;
        if( dataIndex == 128 ){
            dataIndex = 0;
            file[bloco].chksum=chksum;
            //printf("bloco[%02d]chksum[%02X] byte[%02X]\n ",bloco,chksum,byte);
            return 0;
        }
    }
    //printf("bloco[%02d]chksum[%02X] byte[%02X]\n ",bloco,chksum,byte);
    file[bloco].chksum=chksum;
    return -1;
}

unsigned char readFile(const unsigned char * filename){
    unsigned char bloco=0;
    unsigned char pkt=1;
     // 1. Abre o arquivo binário para leitura
    FILE *arquivo = fopen(filename, "rb");
    if (!arquivo) {
        perror("Erro ao abrir o arquivo binario");
        return EXIT_FAILURE;
    }
    memset((void *)file,0,128*255);
    while(1){
        file[bloco].soh = 0x01;
        file[bloco].pkt = pkt++;
        file[bloco].invpkt = 255-pkt;
        if(! readBlock(arquivo,bloco) ){
            printBloco(bloco,file[bloco].data);
            //printf("Bloco[%02X] check sum [%02X]\n",bloco,file[bloco].chksum);
            bloco++;
            if( bloco == 32640 ){
                printf("Se o arquivo é maior que 32640 isso é um problema...\n");
                fclose(arquivo);
                exit(1);
            }
        }else{
           // printf("readBlock retornou -1 no bloco[%d]\n",bloco);
            printBloco(bloco,file[bloco].data);
            //printf("Bloco[%02X] check sum [%02X]\n",bloco,file[bloco].chksum);
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

    while( read(fd_serial, &byte, 1) < 1){}
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
        if( debug >= DEBUG_NIVEL_1) printf("[%02X]",file[bloco].data[i]);
        fflush(stdout);
        usleep(delay);
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
        return byte;
}
unsigned char transmitEof(int fd_serial,unsigned char bloco){
        unsigned char byte=EOT;
        if( debug >= DEBUG_NIVEL_1) printf("Vou transmitir EOT\n");
        write(fd_serial, &byte, 1);
}
unsigned char transmitFile(){
    unsigned char bloco=0;
    unsigned char retrans=0;
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
        if( debug >= DEBUG_NIVEL_1) printf("Tentativa de transmissão pacote[%02X]\n",file[bloco].pkt);
        while( file[bloco].pkt > 0){
            printf("Transmiting block[%02X]\n",bloco);
            transmitHeader(fd_serial,bloco);
            transmitData(fd_serial,bloco);
            transmitChksum(fd_serial,bloco);
            if( get_answer(fd_serial,bloco) == NACK ){
                if(  retrans < 2 ){
                    printf("Tentativa de RETRANSMISSÃO pacote[%02X]\n",file[bloco].pkt);
                    retrans++;
                }else{
                    printf("Tentativa de RETRANSMISSÃO pacote[%02X] SEM SUCESSO FINALIZANDO O ENVIO DE ARQUIVO\n",file[bloco].pkt);
                    break;
                }
            }else{
                if( debug >= DEBUG_NIVEL_1) printf("Recebido ACK\n");
                retrans=0;
                bloco++;
            }
        }
        transmitEof(fd_serial,bloco);
    }

    close(fd_serial);
}
int main(int argc, char *argv[]) {
    printf("System PDS317 - xmodem app copyright (c)2026 pdsilva aka(pgordao)\n");
    printf("This xmodem sends a number 3 to start the xmodem protocol in PDS317\n");
    printf("Yes! I know this is not what xmodem protocol does, but it is as it is!\n");
    printf("Accept hurt a litle bit less!!!\n\n\n");
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <arquivo_binario> </dev/Serial port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    readFile(argv[1]);
    printf("File loaded...[0x%02X] blocos to transmit\n",readFile(argv[1])+1);
    memcpy(SERIAL_PORT,argv[2],strlen(argv[2]));
    printf("Transminting [%s] to [%s]\n",argv[1],SERIAL_PORT);
    transmitFile();
    return EXIT_SUCCESS;
}

