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

void R200Driver::setTxPower(uint8_t dbm)
{
    // Limita entre 0 e 30 dBm (0x1E)
    if (dbm > 30)
        dbm = 30;

    // Comando 0xB6: Set Transmit Power
    // Param 1: 0x00 (Open Loop)
    // Param 2: Valor em dBm
    uint8_t params[2];
    params[0] = 0x00;
    params[1] = dbm;

    Serial.print("[R200] Configurando Potencia para: ");
    Serial.print(dbm);
    Serial.println(" dBm");

    sendCommand(0x00, 0xB6, params, 2);
}

void R200Driver::setRegionUS()
{
    uint8_t region = 0x01; // US/Brasil (902-928MHz)
    Serial.println("[R200] Configurando Regiao para US/Brasil...");
    sendCommand(0x00, 0x07, &region, 1);
}

String R200Driver::getTID(String expectedEPC)
{
    // Comando 0x39: Ler Dados -> Banco 0x02 (TID)
    // O 0x06 no final significa ler 6 Words (12 bytes) para extrair o Número de Série Único!
    uint8_t params[9] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06};

    while (_serial.available())
        _serial.read();
    sendCommand(0x00, 0x39, params, 9);

    unsigned long start = millis();
    int bufIdx = 0;
    uint8_t buf[64];

    while (millis() - start < 150)
    {
        if (_serial.available())
        {
            uint8_t b = _serial.read();
            if (bufIdx == 0 && b != FRAME_HEAD)
                continue;
            buf[bufIdx++] = b;

            if (bufIdx >= 5)
            {
                int payloadLen = (buf[3] << 8) | buf[4];
                int expectedTotalLen = payloadLen + 7;

                if (expectedTotalLen > 64 || expectedTotalLen < 7)
                {
                    bufIdx = 0;
                    continue;
                }

                if (bufIdx == expectedTotalLen)
                {
                    if (b == FRAME_END)
                    {
                        uint8_t type = buf[1];
                        uint8_t cmd = buf[2];

                        if (type == 0x01 && cmd == 0x39)
                        {
                            int epcLen = buf[5];
                            int tidStart = 5 + 1 + epcLen;
                            int tidLen = payloadLen - 1 - epcLen;

                            if (tidLen > 0)
                            {
                                // 1. Extrai o EPC que o chip informou junto com o TID
                                String readEPC = "";
                                int epcBytes = epcLen - 2; // Subtrai o cabeçalho PC (2 bytes)
                                for (int i = 0; i < epcBytes; i++)
                                {
                                    if (buf[8 + i] < 0x10)
                                        readEPC += "0";
                                    readEPC += String(buf[8 + i], HEX);
                                }
                                readEPC.toUpperCase();

                                // 2. Extrai o TID (Agora os 12 bytes completos!)
                                String tid = "";
                                for (int i = 0; i < tidLen; i++)
                                {
                                    if (buf[tidStart + i] < 0x10)
                                        tid += "0";
                                    tid += String(buf[tidStart + i], HEX);
                                }
                                tid.toUpperCase();

                                // 3. O FILTRO DE MENTIRAS (Anti Cross-Talk)
                                // Impede que uma etiqueta vizinha "roube" a resposta
                                if (expectedEPC != "" && readEPC != expectedEPC)
                                {
                                    return "";
                                }

                                return tid;
                            }
                        }
                    }
                    bufIdx = 0;
                }
            }
        }
    }
    return "";
}

bool R200Driver::processIncomingData(R200Tag &outputTag)
{
    while (_serial.available())
    {
        uint8_t b = _serial.read();

        // 1. Sincronização (Ignora ruído inicial)
        if (_bufferIndex == 0 && b != FRAME_HEAD)
            continue;

        // 2. Armazena no buffer
        _buffer[_bufferIndex++] = b;

        // 3. Verificação Inteligente de Fim de Pacote baseada no tamanho real (PL)
        // Só avaliamos o pacote quando já recebemos o Cabeçalho que contém o tamanho
        if (_bufferIndex >= 5)
        {
            // O Payload Length (PL) fica nos bytes 3 (MSB) e 4 (LSB)
            int payloadLen = (_buffer[3] << 8) | _buffer[4];

            // O tamanho total esperado do pacote é:
            // Header(1) + Type(1) + Cmd(1) + PL(2) + Payload + Checksum(1) + End(1) = Payload + 7
            int expectedTotalLen = payloadLen + 7;

            // Prevenção contra lixo e estouro de memória (descarta pacotes impossíveis)
            if (expectedTotalLen > 255 || expectedTotalLen < 7)
            {
                _bufferIndex = 0;
                continue;
            }

            // Se o buffer chegou no tamanho exato que o pacote DEVE ter
            if (_bufferIndex == expectedTotalLen)
            {
                if (b == FRAME_END) // Agora sim, confirmamos se o último byte é o 0xDD
                {
                    uint8_t cmd = _buffer[2];
                    uint8_t type = _buffer[1];

                    // CASO 1: Leitura de Tag
                    if (cmd == 0x22 && type == 0x02)
                    {
                        parsePacket(_buffer, _bufferIndex, outputTag);
                        _bufferIndex = 0;
                        return true;
                    }
                    // CASO 2: Resposta de Escrita
                    else if (cmd == 0x49)
                    {
                        writeStatus = 1;
                        _bufferIndex = 0;
                    }
                    // CASO 3: Erro do R200
                    else if (cmd == 0xFF)
                    {
                        writeStatus = _buffer[5];
                        _bufferIndex = 0;
                    }
                    else
                    {
                        _bufferIndex = 0; // Ignora
                    }
                }
                else
                {
                    // Chegou no tamanho esperado, mas o último byte não era DD. Lixo de rádio!
                    _bufferIndex = 0;
                }
            }
        }
    }
    return false;
}

void R200Driver::parsePacket(uint8_t *pkt, int length, R200Tag &tag)
{
    tag.rssi = pkt[5];

    int paramLen = (pkt[3] << 8) | pkt[4];
    int epcLen = paramLen - 5;

    // --- Trava de Segurança ---
    if (epcLen < 0 || epcLen > 64)
    {
        tag.valid = false;
        return;
    }

    tag.epc = "";
    for (int i = 0; i < epcLen; i++)
    {
        if (pkt[8 + i] < 0x10)
            tag.epc += "0";
        tag.epc += String(pkt[8 + i], HEX);
    }
    tag.epc.toUpperCase();
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
    // 1. Tratamento de Padding (Preenchimento Automático)
    // O protocolo exige blocos de 16 bits (4 caracteres Hex).
    // Se não for múltiplo de 4, adicionamos '0' à ESQUERDA até corrigir.
    while (newEPC.length() % 4 != 0)
    {
        newEPC = "0" + newEPC;
    }

    Serial.print("[R200] EPC Ajustado para gravar: ");
    Serial.println(newEPC);

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