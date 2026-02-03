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

        // 1. Sincronização (Ignora ruído inicial)
        if (_bufferIndex == 0 && b != FRAME_HEAD)
        {
            continue;
        }

        // 2. Armazenamento
        _buffer[_bufferIndex++] = b;

        // 3. Verificação de Fim de Pacote
        if (b == FRAME_END && _bufferIndex >= 7)
        {
            uint8_t cmd = _buffer[2];
            uint8_t type = _buffer[1];

            // --- ROTEAMENTO DE PACOTES ---

            // CASO 1: Leitura de Tag (Sucesso)
            if (cmd == 0x22 && type == 0x02)
            {
                parsePacket(_buffer, _bufferIndex, outputTag);
                _bufferIndex = 0; // Limpa para o próximo
                return true;      // Avisa o main que tem Tag nova!
            }
            // CASO 2: Confirmação de Escrita
            else if (cmd == 0x49)
            {
                writeStatus = 1; // SUCESSO!
                Serial.println("[R200] Escrita realizada com SUCESSO!");
            }
            // CASO 3: Erro (Leitura ou Escrita)
            else if (cmd == 0xFF)
            {
                uint8_t errCode = _buffer[5];
                writeStatus = errCode; // Guarda o código do erro (ex: 0x16)

                Serial.print("[R200] Erro: 0x");
                Serial.println(errCode, HEX);

                if (errCode == 0x16)
                    Serial.println("-> Acesso Negado (Senha incorreta ou Bloqueada)");
                if (errCode == 0x10)
                    Serial.println("-> Falha (Tag longe ou inexistente)");
                if (errCode == 0x15)
                    Serial.println("-> Nenhuma tag detectada no Poll");
            }
            // CASO 4: Info de Hardware
            else if (cmd == 0x03)
            {
                Serial.print("[DEBUG] Hardware Info RAW: ");
                for (int i = 0; i < _bufferIndex; i++)
                    Serial.print(_buffer[i], HEX);
                Serial.println();
            }

            // Reset final do buffer para qualquer pacote processado (que não retornou antes)
            _bufferIndex = 0;
        }

        // Proteção Overflow
        if (_bufferIndex >= 256)
            _bufferIndex = 0;
    }

    return false; // Nenhuma tag lida neste ciclo
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

uint8_t R200Driver::hexCharToByte(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

void R200Driver::writeEPC(String newEPC, String password)
{
    // 1. Validação Básica
    // O EPC deve ter tamanho par (bytes completos) e idealmente múltiplo de 4 (words)
    if (newEPC.length() % 4 != 0)
    {
        Serial.println("[Erro] O EPC deve ter comprimento multiplo de 4 caracteres (Ex: 1122, AABBCCDD)");
        return;
    }

    // 2. Preparação dos Parâmetros do Comando 0x49
    // Estrutura: [Pass(4)] + [MemBank(1)] + [StartAddr(2)] + [DataLen(2)] + [Data(N)]

    int dataBytes = newEPC.length() / 2;           // Quantos bytes de dados reais
    int wordCount = dataBytes / 2;                 // Quantas "Words" de 16 bits
    int totalParamLen = 4 + 1 + 2 + 2 + dataBytes; // Tamanho total do payload

    uint8_t params[64];
    int idx = 0;

    // A. Password (4 Bytes) - Padrão 00 00 00 00
    // (Aqui simplificado, convertendo a string de senha se necessario, ou fixo 0)
    uint32_t pwd = strtoul(password.c_str(), NULL, 16);
    params[idx++] = (pwd >> 24) & 0xFF;
    params[idx++] = (pwd >> 16) & 0xFF;
    params[idx++] = (pwd >> 8) & 0xFF;
    params[idx++] = pwd & 0xFF;

    // B. Memory Bank (1 Byte)
    // 0x01 = Banco EPC
    params[idx++] = 0x01;

    // C. Start Address (2 Bytes)
    // Começamos no endereço 2 (pulamos CRC e PC)
    params[idx++] = 0x00;
    params[idx++] = 0x02;

    // D. Data Length (2 Bytes) - Quantidade de WORDS
    params[idx++] = (wordCount >> 8) & 0xFF;
    params[idx++] = wordCount & 0xFF;

    // E. Dados (O novo EPC)
    // Converte a String Hex em Bytes Reais
    for (int i = 0; i < newEPC.length(); i += 2)
    {
        uint8_t high = hexCharToByte(newEPC[i]);
        uint8_t low = hexCharToByte(newEPC[i + 1]);
        params[idx++] = (high << 4) | low;
    }

    // 3. Envia o comando
    // Type=00, Cmd=0x49 (Write)
    sendCommand(0x00, 0x49, params, idx);
    Serial.println("Comando de Escrita Enviado...");
}