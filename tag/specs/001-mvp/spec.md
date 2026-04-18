## EPIC-01 · Módulo Tag (Dispositivo Móvel)

| Campo | Valor |
|-------|-------|
| **Módulo** | Hardware + Firmware |
| **Stack técnica** | ESP32 · ESP-IDF v6.0 · BLE 5.0 (Coded PHY) |
| **Responsável** | Equipe Hardware |
| **Sprint alvo** | Sprint 1 |
| **Status** | To Do |

### Descrição

O módulo Tag representa o dispositivo móvel rastreado — uma pessoa, ativo ou objeto. Deve transmitir continuamente um pacote BLE (beacon) com identificador único, permitindo que os anchors meçam a intensidade do sinal (RSSI) e o backend estime a posição. É o ponto de entrada de todo o pipeline de localização.

### Contexto e motivação

Sem um tag confiável e estável, todo o sistema de localização é comprometido. A frequência de advertising, a potência de transmissão e o formato do payload influenciam diretamente a qualidade das leituras RSSI nos anchors e, consequentemente, a precisão da posição estimada.

---

### Decisões Técnicas

| Decisão | Escolha | Justificativa |
|---------|---------|---------------|
| Formato do pacote BLE | Eddystone-UID (com desvio intencional) | Padrão aberto amplamente suportado; os 2 bytes `Reserved` do spec original são reutilizados como contador de sequência |
| Canais de advertising | Todos os 3 canais padrão (37, 38, 39) | Comportamento default do BLE stack; garante maior cobertura de detecção e simplicidade de configuração nos anchors |
| PHY BLE | Coded PHY S=8 (BLE 5.0 Long Range) | Permite alcance ampliado (~4× vs BLE 4.x), viabilizando ambientes maiores sem aumento de potência |
| UUID do tag (MVP) | `019d8ba3-5d5f-792b-8e60-906fbeca324a` | UUID fixo para MVP com uma única tag; anchors filtram por este valor |
| Versão do ESP-IDF | v6.0 | Suporte estável à API de BLE Extended Advertising (Coded PHY); versão em uso pela equipe |
| Configuração de parâmetros de RF | `#define` em tempo de compilação | Intervalo de advertising e potência de TX são constantes de compilação; não são alteráveis em runtime no MVP |
| Mecanismo de OTA | HTTP local (sem TLS) + Safe OTA (2 partições app) | Rede interna controlada dispensa TLS no MVP; Safe OTA garante rollback automático para firmware anterior em caso de falha |
| Credenciais WiFi para OTA | `#define` no firmware (SSID + senha) | Solução mínima para MVP em ambiente de lab; substituir por provisioning seguro em versão futura |
| Segurança e privacidade | Fora do escopo do MVP | Payload transmitido sem criptografia; rotating UUID/MAC endereçado em versão futura |
| Gerenciamento de bateria | Fora do escopo do firmware (MVP) | Carga realizada externamente; nenhuma lógica de BMS necessária no firmware |

---

### Requisitos Funcionais

| ID | Descrição | Prioridade |
|----|-----------|------------|
| RF-01 | O tag deve transmitir pacotes BLE Advertisement a cada 100 ms. O intervalo é definido pela constante de compilação `BLE_ADV_INTERVAL_MS` (padrão: 100 ms; valores válidos: 50–500 ms). Não é configurável em runtime no MVP. | Must Have |
| RF-02 | O payload deve seguir o formato Eddystone-UID com os seguintes campos: Ranging Data / TxPower@1m (1 byte, valor definido pela constante `BLE_TX_POWER_1M_DBM`; inteiro com sinal, encoding em dBm), Namespace (10 bytes, derivado dos primeiros 10 bytes do UUID do tag), Instance (6 bytes, bytes restantes do UUID), e os 2 bytes `Reserved` do padrão reutilizados como contador de sequência little-endian `uint16_t` com rollover natural (0xFFFF → 0x0000) | Must Have |
| RF-03 | O firmware deve operar em modo BLE Extended Advertising não-conectável e não-escaneável (`ADV_TYPE_NONCONN_IND`) com Coded PHY S=8 (BLE 5.0 Long Range) nos 3 canais de advertising (37, 38, 39) | Must Have |
| RF-04 | A potência de transmissão de saída do rádio é definida pela constante de compilação `BLE_TX_POWER_DBM` (padrão: 0 dBm; valores válidos: -12 dBm a +9 dBm). O campo TxPower no payload (`BLE_TX_POWER_1M_DBM`) representa o RSSI esperado a 1 metro — valor distinto, usado pelos anchors para estimar distância — e é definido separadamente. Nenhum dos valores é alterável em runtime no MVP. | Must Have |
| RF-05 | O tag deve piscar o LED de status uma vez por segundo (independente do intervalo de advertising) para facilitar diagnóstico em campo. O LED é controlado por timer de software. O pino GPIO é definido pela constante `LED_STATUS_GPIO` (placeholder: GPIO 2 — confirmar no esquemático). | Should Have |
| RF-06 | O firmware deve incluir rotina de watchdog com reset automático em caso de travamento por mais de 5 segundos | Must Have |
| RF-07 | O tag deve suportar atualização de firmware via OTA (Over-The-Air) por WiFi. Ver sub-requisitos RF-07a a RF-07e. | Should Have |
| RF-07a | O modo OTA é ativado ao detectar nível LOW no `OTA_TRIGGER_GPIO` durante o boot (comportamento idêntico ao botão BOOT da DevKit ESP32). A constante `OTA_TRIGGER_GPIO` define o pino (placeholder: GPIO 0 — confirmar no esquemático). Trigger por jumper externo mantido em LOW é igualmente suportado pelo mesmo mecanismo. | Should Have |
| RF-07b | As credenciais WiFi (SSID e senha) são definidas pelas constantes de compilação `OTA_WIFI_SSID` e `OTA_WIFI_PASSWORD`. O firmware conecta-se à rede ao entrar no modo OTA. | Should Have |
| RF-07c | O firmware busca a nova imagem via HTTP (sem TLS) em um servidor local. A URL do servidor é definida pela constante `OTA_SERVER_URL` (ex: `http://192.168.1.100:8070/tag-firmware.bin`). Timeout: 30 segundos para conexão WiFi + 60 segundos para download total. Sem retry — em caso de falha, o dispositivo reinicia imediatamente com o firmware anterior. | Should Have |
| RF-07d | O esquema de partições deve usar Safe OTA (2 partições app + 1 OTA data), permitindo rollback automático para o firmware anterior em caso de falha no download ou validação | Should Have |
| RF-07e | Durante o processo de OTA, o advertising BLE é suspenso. Após reboot bem-sucedido, o advertising retoma normalmente. Em caso de falha (timeout, download corrompido, erro HTTP), o dispositivo reinicia com o firmware anterior e retoma o advertising. | Should Have |
| RF-08 | O sistema deve registrar uptime e contador de resets na memória não-volátil (NVS) do ESP32. O contador de resets é incrementado a cada boot; o uptime é persistido a cada 60 segundos durante operação normal. | Could Have |

---

### Mapeamento do UUID para Eddystone-UID (MVP)

UUID fixo: `019d8ba3-5d5f-792b-8e60-906fbeca324a`

| Campo Eddystone | Bytes | Valor |
|-----------------|-------|-------|
| Namespace | 10 bytes | `01 9d 8b a3 5d 5f 79 2b 8e 60` |
| Instance | 6 bytes | `90 6f be ca 32 4a` |

> **Nota:** Em versões futuras, o UUID será único por dispositivo e provisionado em factory flash. No MVP, o valor é compilado como constante no firmware.

---

### Placeholders de Hardware

Os valores abaixo são utilizados no firmware durante o desenvolvimento. **Devem ser substituídos pelos valores definitivos do esquemático e de medição em bancada antes do primeiro flash em campo.**

| Constante | Placeholder | Ação necessária |
|-----------|-------------|-----------------|
| `LED_STATUS_GPIO` | GPIO 2 (LED embutido ESP32-WROOM-32) | Confirmar pino no esquemático |
| `OTA_TRIGGER_GPIO` | GPIO 0 (botão BOOT) | Confirmar pino no esquemático |
| `BLE_TX_POWER_1M_DBM` | -65 dBm (estimativa) | Medir em bancada a 1m antes do primeiro flash em campo |

---

### Dependências

| Dependência | Tipo | Notas |
|-------------|------|-------|
| Spec do módulo Anchor | Interna | Os anchors filtram advertising pelo UUID `019d8ba3-5d5f-792b-8e60-906fbeca324a` definido nesta spec |
| Esquemático elétrico | Hardware | Define os GPIOs definitivos para LED e trigger OTA. **Confirmação dos GPIOs e medição de `BLE_TX_POWER_1M_DBM` em bancada são pré-requisitos para flash em hardware definitivo** (não bloqueia desenvolvimento em DevKit com valores placeholder). |
| Servidor HTTP local para OTA | Infraestrutura | Necessário para testar RF-07; pode ser `python3 -m http.server` em rede local de lab |
| Spec do módulo Anchor | Interna | O módulo Anchor deve tratar o contador de sequência (bytes Reserved) como `uint16_t` circular — rollover de 0xFFFF para 0x0000 é comportamento esperado, não indica perda de pacote. |

---

### Garantias (Requisitos Não-Funcionais)

| Garantia | Critério de Aceitação |
|----------|-----------------------|
| Estabilidade de transmissão | Taxa de perda de pacotes < 2% em ambiente LOS (line-of-sight) a até 10 metros de distância |
| Consumo energético | Corrente média < 20 mA com advertising a 100 ms; autonomia mínima de 8h com bateria LiPo 500 mAh |
| Latência de inicialização | Tempo do boot até primeiro advertising < 2 segundos |
| Estabilidade do firmware | Operação contínua por no mínimo 24 horas sem reinicialização espontânea (validado em Sprint 2) |
| Compatibilidade de hardware | Firmware validado nos modelos ESP32-WROOM-32 e ESP32-WROVER |
| Documentação | Código-fonte com comentários em inglês, esquemático elétrico e README com instruções de flash |

---

### Definition of Done (DoD)

- [ ] Firmware compila sem erros no ESP-IDF v6.0
- [ ] Tag transmite beacons Eddystone-UID estáveis confirmados por scanner BLE externo (ex: nRF Connect)
- [ ] Namespace `01 9d 8b a3 5d 5f 79 2b 8e 60` e Instance `90 6f be ca 32 4a` verificados nos campos correspondentes do payload Eddystone-UID capturado pelo scanner
- [ ] Coded PHY S=8 confirmado no advertising (verificar com nRF Sniffer ou equivalente)
- [ ] Contador de sequência incrementando corretamente nos bytes Reserved do pacote
- [ ] LED de status piscando a 1 Hz confirmado visualmente em hardware
- [ ] Trigger GPIO para modo OTA testado: advertising suspende, firmware atualizado via HTTP local e advertising retoma após reboot
- [ ] Rollback OTA testado: download interrompido resulta em reboot com firmware anterior funcional
- [ ] Taxa de perda de pacotes medida e documentada (< 2% em condições LOS a 10 m)
- [ ] Consumo de corrente medido com multímetro e registrado em `tag/specs/001-mvp/validacao.md`
- [ ] README publicado em `tag/firmware/README.md` com instruções de compilação, flash, configuração de constantes e setup do servidor OTA
- [ ] Git tag `tag-firmware-v1.0` criada no commit de entrega

> **Nota:** O teste de estabilidade de 24h (operação contínua sem travamentos) está planejado para Sprint 2, após a entrega do firmware base.

---
