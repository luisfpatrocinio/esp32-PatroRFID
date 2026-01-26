/**
 * @file R200.cpp
 * @version 1.0
 * @date 2026-01-12
 * @brief Implementação dos métodos da classe R200Driver.
 *
 * Este arquivo contém a lógica de baixo nível para manipulação de bytes,
 * controle de fluxo da UART e implementação das regras estritas do protocolo
 * definidas pelo fabricante do R200.
 */

#include "R200.h"
#include "Config.h"

R200Driver::R200Driver(HardwareSerial &serial) : _serial(serial)
{
    // Inicialização de variáveis membro se necessário
}

void R200Driver::begin()
{
    // Configura a UART com 8 bits de dados, sem paridade, 1 stop bit (SERIAL_8N1)
    _serial.begin(R200_BAUDRATE, SERIAL_8N1, R200_RX_PIN, R200_TX_PIN);

    // Pequeno delay para garantir que o hardware da UART estabilize
    delay(100);
}

uint8_t R200Driver::calculateChecksum(uint8_t *data, int length)
{
    long sum = 0;
    // Soma acumulativa de todos os bytes do payload
    for (int i = 0; i < length; i++)
    {
        sum += data[i];
    }
    // Retorna apenas os 8 bits menos significativos (LSB)
    return (uint8_t)(sum & 0xFF);
}

void R200Driver::sendCommand(uint8_t type, uint8_t cmd, uint8_t *params,
                             int paramLen)
{
    uint8_t packet[64]; // Buffer temporário para montagem do pacote de envio
    int idx = 0;

    packet[idx++] = FRAME_HEAD; // 0xAA
    packet[idx++] = type;       // Geralmente 0x00
    packet[idx++] = cmd;        // Código da instrução

    // O comprimento do parâmetro (PL) é de 2 bytes (Big Endian: MSB primeiro)
    packet[idx++] = (paramLen >> 8) & 0xFF;
    packet[idx++] = paramLen & 0xFF;

    // Copia os parâmetros opcionais para o pacote
    if (params != NULL && paramLen > 0)
    {
        for (int i = 0; i < paramLen; i++)
        {
            packet[idx++] = params[i];
        }
    }

    // Calcula o checksum.
    // Nota: O checksum é calculado a partir do 'Type' (índice 1), ignorando o
    // Header. O tamanho considerado é: Type(1) + Cmd(1) + PL_MSB(1) + PL_LSB(1) +
    // Params(paramLen)
    packet[idx] = calculateChecksum(&packet[1], 4 + paramLen);
    idx++;

    packet[idx++] = FRAME_END; // 0xDD

    // Escreve o pacote completo na porta serial
    _serial.write(packet, idx);
}

void R200Driver::getHardwareVersion()
{
    // Protocolo: Header | Type=00 | Cmd=03 | PL=0000 | Cks | End
    sendCommand(0x00, 0x03, NULL, 0);
}

void R200Driver::singlePoll()
{
    // Protocolo: Header | Type=00 | Cmd=22 | PL=0000 | Cks | End
    sendCommand(0x00, 0x22, NULL, 0);
}

bool R200Driver::processIncomingData(R200Tag &outputTag)
{
    while (_serial.available())
    {
        uint8_t b = _serial.read();

        // 1. Sincronização de Frame
        // Se estamos no início do buffer e o byte não é o Cabeçalho (0xAA),
        // descartamos (ruído).
        if (_bufferIndex == 0 && b != FRAME_HEAD)
        {
            continue;
        }

        // 2. Armazenamento
        _buffer[_bufferIndex++] = b;

        // 3. Verificação de Fim de Pacote
        // Um pacote mínimo tem 7 bytes (AA, Type, Cmd, PL_H, PL_L, CS, DD)
        if (b == FRAME_END && _bufferIndex >= 7)
        {

            // Verifica o comando recebido (Byte índice 2)
            uint8_t cmd = _buffer[2];
            uint8_t type = _buffer[1];

            // 0x22 = Resposta do Single Poll
            // Type 0x02 = Notification Frame (significa que encontrou dados)
            if (cmd == 0x22 && type == 0x02)
            {
                parsePacket(_buffer, _bufferIndex, outputTag);
                _bufferIndex = 0; // Reseta buffer para próximo pacote
                return true;      // Sucesso
            }

            // Debug: Resposta de Versão de Hardware (0x03)
            if (cmd == 0x03)
            {
                Serial.print("[DEBUG] Hardware Info RAW: ");
                for (int i = 0; i < _bufferIndex; i++)
                    Serial.print(_buffer[i], HEX);
                Serial.println();
            }

            // Se chegou um 0xDD mas não era o que queríamos, ou pacote processado,
            // limpa buffer.
            _bufferIndex = 0;
        }

        // Proteção contra Overflow do Buffer
        if (_bufferIndex >= 256)
            _bufferIndex = 0;
    }
    return false; // Nada pronto ainda
}

void R200Driver::parsePacket(uint8_t *pkt, int length, R200Tag &tag)
{
    /**
     * Estrutura do Frame de Resposta (Notification):
     * [0] AA (Header)
     * [1] Type (0x02)
     * [2] Cmd (0x22)
     * [3] PL MSB
     * [4] PL LSB
     * [5] RSSI  <-- Início dos dados úteis
     * [6] PC MSB
     * [7] PC LSB
     * [8...] EPC Bytes
     * [...] CRC MSB
     * [...] CRC LSB
     * [N-2] Checksum
     * [N-1] DD (End)
     */

    tag.rssi = pkt[5]; // Byte de RSSI

    // Cálculo do tamanho do EPC:
    // O Parameter Length (PL) inclui: RSSI(1) + PC(2) + EPC(x) + CRC(2)
    // Logo: Tamanho EPC = PL - 1 - 2 - 2 = PL - 5.
    int paramLen = (pkt[3] << 8) | pkt[4];
    int epcLen = paramLen - 5;

    tag.epc = "";
    // Extração dos bytes do EPC
    for (int i = 0; i < epcLen; i++)
    {
        // Formatação para garantir dois dígitos hexadecimais (ex: 0A ao invés de A)
        if (pkt[8 + i] < 0x10)
            tag.epc += "0";
        tag.epc += String(pkt[8 + i], HEX);
    }
    tag.epc.toUpperCase(); // Padronização visual
    tag.valid = true;
}