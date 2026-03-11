🌎 [English](README.md) | 🇧🇷 [Português](README.pt-br.md)

# Leitor UHF Inteligente (Projeto Smart Stock)

Firmware para uma "pistola" versátil de leitura e gravação RFID UHF, baseada no ESP32 e no módulo R200. Este dispositivo atua como a ponta de coleta de dados do projeto **Smart Stock** — um sistema inovador de controle de estoque criado no âmbito do **Programa EmbarcaTech**.

O projeto apresenta uma arquitetura robusta e multitarefa utilizando FreeRTOS, manipulação avançada de rádio UHF e um protocolo de comandos estruturados em JSON via **Bluetooth Low Energy (BLE)** para comunicação nativa com aplicativos móveis.

![PatroRFID Project Photo](PatroRFID.png)

## ✨ Principais Funcionalidades e Soluções de Engenharia

- **Modos Duplos (Leitura/Gravação):** Alterne perfeitamente entre a leitura de etiquetas existentes e a gravação de novos dados através da interface do aplicativo BLE.
- **Modo Contínuo ("Metralhadora"):** O gatilho (botão) pode ser mantido pressionado para ler ou gravar várias etiquetas em lote, ideal para varredura rápida de prateleiras.
- **Memória de Sessão (Filtro Anti-Spam):** Para evitar a "Cegueira por Captura" (onde a antena monopoliza o sinal da etiqueta mais próxima), o ESP32 memoriza temporariamente as tags já processadas no ciclo atual, ignorando-as fisicamente para conseguir ler as tags no fundo das caixas.
- **Uso da "Digital de Fábrica" (TID 96-bits):** Em vez de usar o EPC regravável como UID, o sistema extrai o TID de hardware (Imutável) da etiqueta enviando comandos complexos de extração (`0x39`). Isso garante integridade absoluta no banco de dados do Smart Stock, impossibilitando a duplicação de itens.
- **Escudo Anti Cross-Talk:** Em ambientes de alta densidade, o firmware cruza a informação do EPC com a resposta do TID para garantir que não está juntando respostas de etiquetas vizinhas (evitando pacotes "Frankenstein").
- **Limpeza de Pacotes (Sanity Check):** Proteção rigorosa do buffer UART contra pacotes corrompidos e ruído de rádio, prevenindo o envio de lixo ou "Produtos Desconhecidos" ao App.
- **Restauração de Tags (Zero Padding):** Reaproveitamento de etiquetas de estoque através de comandos de string vazia (`""`), que o ESP32 traduz em preenchimento nulo (zeros hexadecimais) para zerar a memória do chip.
- **Arquitetura Multitarefa:** Construído sobre FreeRTOS para lidar simultaneamente com as operações do RFID, comunicação BLE e feedback visual/sonoro sem travar a porta serial do rádio.

## ⚙️ Especificações de Hardware

- 1 x Placa de Desenvolvimento ESP32
- 1 x Módulo Leitor RFID UHF R200 (UART)
- 1 x Botão (Push Button)
- 1 x Buzzer Ativo
- 1 x LED 5mm (qualquer cor)
- 1 x Resistor 220-330 Ohm
- Protoboard e Jumpers

## 🔌 Diagrama de Ligação (Pinagem)

| Componente        | Pino ESP32 | Observação                                  |
| :---------------- | :--------- | :------------------------------------------ |
| **R200 TX**       | `GPIO 16`  | Liga ao RX do ESP32                         |
| **R200 RX**       | `GPIO 17`  | Liga ao TX do ESP32                         |
| **Buzzer (+)**    | `GPIO 22`  | Buzzer Ativo                                |
| **Botão**         | `GPIO 21`  | Configurado com pull-up interno             |
| **LED (+)**       | `GPIO 2`   | Com resistor limitador de corrente          |
| **VCC (3.3V/5V)** | `VCC`      | Verifique o requisito de tensão do seu R200 |
| **GND**           | `GND`      | Terra Comum                                 |

## 💻 Software e Instalação

Este projeto está configurado para o ecossistema **PlatformIO** dentro do Visual Studio Code.

1.  **Ambiente:** Certifique-se de que a extensão [PlatformIO IDE](https://platformio.org/platformio-ide) está instalada no VS Code.
2.  **Dependências:** As bibliotecas necessárias (ex: `ArduinoJson`) estão listadas no arquivo `platformio.ini` e serão instaladas automaticamente no primeiro build.
3.  **Build & Upload:**
    - Clone o repositório.
    - Abra a pasta do projeto no VS Code.
    - Conecte sua placa ESP32 via USB.
    - Use a barra inferior do PlatformIO para fazer o **Upload** do firmware.

## 📖 Guia Operacional

1.  **Ligando:** Conecte o ESP32 à energia. O LED de status piscará lentamente, indicando que está em modo de _advertising_ e pronto para receber uma conexão BLE.
2.  **Conectando via BLE:** Utilize o aplicativo móvel do Smart Stock para se conectar ao dispositivo nomeado `PatroRFID-Reader`. Uma vez conectado, o LED parará de piscar.

### Lendo uma Etiqueta (Padrão)

- Pressione e segure o botão. O leitor fará varreduras contínuas no campo.
- Uma leitura bem-sucedida aciona um bipe, e um payload JSON `readResult` (contendo o TID imutável e os dados decodificados) é enviado ao App.

### Gravando em uma Etiqueta

1.  **Modo de Escrita:** O app envia o comando `changeMode` (`"write"`).
2.  **Enviar Dados:** O app envia o comando `writeData` contendo o SKU/Produto a ser gravado.
3.  **Apresentar Tag:** Pressione o botão e aponte a pistola para as tags. O dispositivo descobre o TID de hardware, sobrescreve o EPC e gerencia a memória de sessão para pular rapidamente para a próxima tag virgem.
4.  **Confirmação:** O dispositivo envia um JSON `writeResult` confirmando o sucesso e ecoando o TID único gravado.

## 📶 Protocolo de Comunicação (BLE JSON)

Toda a comunicação ocorre através de uma única característica BLE que aceita comandos JSON e envia dados JSON via notificações.

### App Cliente → ESP32 (Comandos)

#### 1. Alterar Modo de Operação

```json
{
  "type": "changeMode",
  "content": "write"
}
```

_(Envie `"stop"` para voltar ao modo de leitura)_

#### 2. Enviar Dados para Gravação

```json
{
  "type": "writeData",
  "content": "SENYAR13021"
}
```

#### 3. Restaurar Tag (Limpar Memória)

```json
{
  "type": "writeData",
  "content": ""
}
```

#### 4. Alternar Som

```json
{
  "type": "toggleSound",
  "content": "off"
}
```

### ESP32 → App Cliente (Respostas e Dados)

#### 1. Resultado de Leitura

Enviado quando uma tag é lida e validada contra cross-talk.

```json
{
  "type": "readResult",
  "content": {
    "status": "ok",
    "uid": "E28069152000501D1A2B3C4D",
    "rssi": 194,
    "data": "SENYAR13021"
  }
}
```

#### 2. Resultado de Gravação

Enviado após um ciclo de escrita bem-sucedido.

```json
{
  "type": "writeResult",
  "content": {
    "status": "ok",
    "uid": "E28069152000501D1A2B3C4D",
    "data": "SENYAR13021",
    "message": "Gravado com Sucesso!"
  }
}
```

## 🏗️ Estrutura do Código

O firmware está organizado em uma arquitetura limpa e modular:

- `main.cpp`: Ponto de entrada, inicialização e criação das Tasks do FreeRTOS.
- `config.h`: Definições centralizadas de pinos de hardware e constantes da UART.
- `R200.cpp / .h`: Driver de baixo nível do módulo UHF (Roteamento de bytes, extração de TID de 96 bits, regras de protocolo).
- `rtos_comm.h`: Declaração de variáveis globais compartilhadas e primitivas do FreeRTOS.
- `ble_comm.cpp / .h`: Gerencia os serviços BLE e callbacks de escrita.
- `rfid_handler.cpp / .h`: A lógica principal das tarefas de leitura/gravação contínua e gerenciamento da memória de sessão.
- `ui_handler.cpp / .h`: Tarefas não-bloqueantes de feedback do LED e do Buzzer.

## 📄 Licença

Este projeto está licenciado sob a Licença MIT.
Veja o arquivo [LICENSE](https://www.google.com/search?q=./LICENSE) para mais detalhes.

## 👨‍💻 Autores

Este projeto foi criado e mantido por:

- **[Luis Felipe Patrocinio](https://github.com/luisfpatrocinio/)**
- **[João Pedro Mendes de Oliveira](https://github.com/Jpedro23)**

Desenvolvido para o projeto **Smart Stock** dentro do **Programa EmbarcaTech**.
