# AirCare – Monitor IoT de qualidade do ar e conforto térmico

Projeto da disciplina **Objetos Inteligentes Conectados** – Universidade Presbiteriana Mackenzie (Análise e Desenvolvimento de Sistemas).
Autor: Luiz Galdino de Sousa Xavier Reche.

O AirCare mede temperatura, umidade e a qualidade do ar de um ambiente interno, liga automaticamente um exaustor quando o ar ou o calor ficam inadequados e envia leituras e alertas pela internet usando o protocolo **MQTT**. O projeto está alinhado à **ODS 3 – Saúde e Bem-Estar**.

## Como funciona

1. O ESP32 conecta ao Wi-Fi, sincroniza o relógio por NTP e conecta ao broker MQTT público **HiveMQ** (`broker.hivemq.com`, porta 1883).
2. A cada 5 segundos lê o DHT22 (temperatura e umidade) e o MQ-135 (qualidade do ar, valor analógico de 0 a 4095) e publica as leituras em JSON.
3. Classifica o ambiente:
   - **NORMAL**: ar abaixo de 1500 e temperatura abaixo de 28 °C → exaustor desligado.
   - **ATENÇÃO**: ar ≥ 1500 ou temperatura ≥ 28 °C → exaustor ligado pelo relé.
   - **CRÍTICO**: ar ≥ 2500 → exaustor ligado e buzzer de alerta.
4. Histerese: em modo automático, o exaustor só desliga após 1 minuto seguido em NORMAL.
5. O painel (`dashboard.html`) recebe os dados via MQTT sobre WebSocket e permite ligar/desligar o exaustor e o buzzer ou voltar ao modo automático.

## Vídeo de demonstração

https://youtu.be/qDejsUkqbIA (não listado, 3 min 45 s): circuito no Wokwi, explicação do código, demonstração do MQTT e medição dos tempos de resposta.

## Tempos de resposta medidos (4 medidas, ida e volta pelo broker)

| Sensor/atuador | M1 | M2 | M3 | M4 | Média |
|---|---|---|---|---|---|
| Temperatura (DHT22) | 659 ms | 664 ms | 669 ms | 653 ms | 661,3 ms |
| Umidade (DHT22) | 704 ms | 743 ms | 753 ms | 765 ms | 741,3 ms |
| Qualidade do ar (MQ-135) | 706 ms | 720 ms | 666 ms | 663 ms | 688,8 ms |
| Exaustor (relé) | 672 ms | 670 ms | 669 ms | 671 ms | 670,5 ms |
| Buzzer | 670 ms | 659 ms | 680 ms | 671 ms | 670,0 ms |

## Hardware

| Componente | Função | Pino ESP32 |
|---|---|---|
| ESP32 DevKit V1 | Microcontrolador com Wi-Fi integrado | – |
| DHT22 (AM2302) | Temperatura e umidade | GPIO4 |
| MQ-135 | Qualidade do ar (no Wokwi, simulado por potenciômetro) | GPIO34 (via divisor 10 kΩ/20 kΩ na montagem física) |
| Módulo relé 5 V | Aciona o exaustor (no Wokwi, LED azul) | GPIO26 |
| Buzzer ativo | Alerta sonoro | GPIO27 |
| Ventilador/cooler 5 V | Exaustor | contato NO do relé |

Diagrama de montagem: `diagrama_montagem.png`. Simulação: `diagram.json` (Wokwi).

## Comunicação MQTT

| Tópico | Direção | Conteúdo (JSON) |
|---|---|---|
| `aircare/luizreche/sensores` | ESP32 → broker | `{"temp":24.0,"umid":55.0,"ar":800,"estado":"NORMAL","ts":<epoch ms>}` |
| `aircare/luizreche/status` | ESP32 → broker (retido) | `{"exaustor":"OFF","buzzer":"OFF","modo":"AUTO"}` |
| `aircare/luizreche/exaustor/cmd` | painel → ESP32 | `{"id":"abc123","valor":"ON"}` (ON, OFF ou AUTO) |
| `aircare/luizreche/buzzer/cmd` | painel → ESP32 | `{"id":"abc123","valor":"ON"}` (ON ou OFF) |
| `aircare/luizreche/sensores/ler` | painel → ESP32 | `{"id":"abc123","valor":"temp"}` (temp, umid ou ar: pede leitura imediata) |
| `aircare/luizreche/ack` | ESP32 → painel | `{"id":"abc123","alvo":"exaustor","valor":"ON","ts":<epoch ms>}` |

O ESP32 usa MQTT sobre TCP (porta 1883); o painel web usa MQTT sobre WebSocket seguro (porta 8884). O protocolo Blynk **não** é usado.

### Medição dos tempos de resposta

- **Sensores:** o painel publica um pedido de leitura com um `id` no tópico `sensores/ler`; o ESP32 lê o sensor e publica a leitura com o mesmo `id`; o painel mede o tempo entre o pedido e a chegada da leitura.

Os dois tempos usam só o relógio do painel (ida e volta pelo broker). Isso evita erro de sincronização: no Wokwi o relógio simulado do ESP32 anda mais devagar que o real quando a simulação roda abaixo de 100%, então comparar o `ts` do ESP32 com o relógio do painel dava valores falsos.
- **Atuadores:** o painel envia o comando com um `id`; o ESP32 aciona o pino e responde no tópico `ack` com o mesmo `id`; o painel mede o tempo entre o envio e a confirmação.

O painel guarda 4 medidas por sensor/atuador, calcula a média e desenha o gráfico.

## Como reproduzir

### Simulação no Wokwi
1. Acesse [wokwi.com](https://wokwi.com) → **New Project** → **ESP32**.
2. Substitua `sketch.ino` e `diagram.json` pelos deste repositório e crie o arquivo `libraries.txt` com as bibliotecas listadas.
3. Clique em **Play**. O Monitor Serial mostra a conexão e as leituras.
4. Clique no DHT22 para alterar temperatura/umidade e gire o potenciômetro para simular a poluição do ar.

### Painel
1. Baixe `dashboard.html` e abra no navegador (Chrome/Edge).
2. Aguarde "Conectado ao broker" e acompanhe as leituras.
3. Use os botões para comandar o exaustor e o buzzer e os botões "Ler" dos sensores; a tabela de tempos se preenche sozinha.

Alternativa: [HiveMQ Web Client](https://www.hivemq.com/demos/websocket-client/), assinando `aircare/luizreche/#`.

## Estrutura

```
sketch.ino             firmware do ESP32
diagram.json           circuito da simulação Wokwi
libraries.txt          bibliotecas (PubSubClient, ArduinoJson, DHT sensor library, Adafruit Unified Sensor)
dashboard.html         painel MQTT com comandos e medição de tempos
diagrama_montagem.png  modelo de montagem do protótipo
```

## Bibliotecas

- [PubSubClient](https://github.com/knolleary/pubsubclient) – cliente MQTT
- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library) e [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor) – leitura do DHT22 (a DHTesp dava TIMEOUT no Wokwi com o Wi-Fi ativo)
- [ArduinoJson](https://arduinojson.org/) – mensagens JSON
- [MQTT.js](https://github.com/mqttjs/MQTT.js) – MQTT no painel web
