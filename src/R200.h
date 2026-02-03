/**
 * @file R200.h
 * @version 1.0
 * @date 2026-01-12
 * @brief Interface da classe controladora do leitor RFID UHF R200.
 *
 * Este arquivo de cabeçalho declara a classe R200Driver e as estruturas de
 * dados associadas. Ele atua como uma camada de abstração (HAL), escondendo a
 * complexidade de bytes e checksums do programa principal.
 *
 * @see R200 user protocol V2.3.3.pdf
 */

#ifndef R200_H
#define R200_H

#include <Arduino.h>

/**
 * @struct R200Tag
 * @brief Estrutura de dados para representar uma Tag RFID lida.
 *
 * Armazena as informações processadas de uma leitura, convertendo
 * os bytes brutos do protocolo em dados legíveis.
 */
struct R200Tag
{
    /** @brief Código Eletrônico do Produto (EPC) em formato String Hexadecimal.
     */
    String epc;

    /**
     * @brief Indicador de Força do Sinal Recebido (RSSI).
     * @note No R200, valores mais altos (em Hex) indicam sinal mais forte.
     */
    int rssi;

    /** @brief Flag auxiliar para indicar se o objeto contém dados válidos. */
    bool valid;

    // --- CONSTRUTOR ---
    // Assim que você declarar "R200Tag tag;", isso roda automaticamente.
    R200Tag() : epc(""), rssi(0), valid(false) {}
};

/**
 * @class R200Driver
 * @brief Classe principal para gerenciamento do módulo R200 via UART.
 */
class R200Driver
{
public:
    /**
     * @brief Construtor da classe.
     * @param serial Referência para a HardwareSerial (ex: Serial1, Serial2) a ser
     * utilizada.
     */
    R200Driver(HardwareSerial &serial);

    /**
     * @brief Inicializa a porta serial e configurações do módulo.
     *
     * Configura o baudrate, bits de parada e pinos RX/TX definidos no Config.h.
     */
    void begin();

    /**
     * @brief Solicita a versão de hardware do módulo.
     *
     * Envia o comando 0x03. Útil para verificar se o módulo está respondendo
     * e se a fiação está correta (Health Check).
     */
    void getHardwareVersion();

    /**
     * @brief Executa uma leitura única de inventário (Single Polling).
     *
     * Envia o comando 0x22. O módulo liga a antena, busca tags brevemente,
     * responde e desliga a antena. Ideal para testes de baixo consumo.
     */
    void singlePoll();

    /**
     * @brief Processa os dados que chegam na Buffer Serial.
     *
     * Esta função deve ser chamada repetidamente no loop principal. Ela remonta
     * os pacotes fragmentados, verifica a integridade e extrai a tag.
     *
     * @param outputTag Referência para armazenar os dados da tag caso encontrada.
     * @return true Se um pacote de tag válido foi decodificado completamente.
     * @return false Se não há dados ou o pacote ainda está incompleto.
     */
    bool processIncomingData(R200Tag &outputTag);

    /**
     * @brief Escreve um novo código EPC na etiqueta.
     * @note A etiqueta deve estar próxima da antena. Cuidado para não ter várias tags perto!
     *
     * @param newEPC String hexadecimal com o novo ID (ex: "E2001122").
     * Deve ter numero par de caracteres (múltiplo de 4 é ideal).
     * @param password Senha de acesso (Padrão é "00000000").
     */
    void writeEPC(String newEPC, String password = "00000000");

    int writeStatus = 0; ///< Status da última operação de escrita (0=Nada, 1=Sucesso, >1=Erro).

private:
    HardwareSerial &_serial; ///< Referência para a instância da Serial física.
    uint8_t _buffer[256];    ///< Buffer circular para remontagem de pacotes.
    int _bufferIndex = 0;    ///< Índice atual de escrita no buffer.

    /**
     * @brief Calcula o Checksum do protocolo R200.
     *
     * O checksum é a soma de todos os bytes (Type até o último Parâmetro),
     * pegando apenas o byte menos significativo (LSB).
     *
     * @param data Ponteiro para o array de bytes.
     * @param length Quantidade de bytes a somar.
     * @return uint8_t O byte de checksum calculado.
     */
    uint8_t calculateChecksum(uint8_t *data, int length);

    /**
     * @brief Monta e envia um frame de comando binário via UART.
     *
     * Encapsula os dados adicionando Header (0xAA), calculando Checksum e
     * adicionando End (0xDD).
     *
     * @param type Tipo do frame (geralmente 0x00 para comandos).
     * @param cmd Código do comando (ex: 0x22 para Poll).
     * @param params Ponteiro para array de parâmetros (ou NULL).
     * @param paramLen Tamanho dos parâmetros.
     */
    void sendCommand(uint8_t type, uint8_t cmd, uint8_t *params, int paramLen);

    /**
     * @brief Decodifica um pacote bruto recebido e preenche a estrutura R200Tag.
     *
     * @param packet Ponteiro para o buffer contendo o frame completo.
     * @param length Tamanho total do frame.
     * @param tag Referência para a estrutura onde os dados serão salvos.
     */
    void parsePacket(uint8_t *packet, int length, R200Tag &tag);

    /**
     * @brief Converte um caractere Hex (0-9, A-F) para valor numérico (0-15).
     */
    uint8_t hexCharToByte(char c);
};

#endif // R200_H