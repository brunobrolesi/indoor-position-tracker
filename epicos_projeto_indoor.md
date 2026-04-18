# Sistema de Localização Indoor via IoT e BLE
## Especificação de Épicos — Backlog do Projeto

> **TCC — Engenharia Eletrônica**
> Stack: ESP32 · Python · MQTT · FastAPI · InfluxDB · BLE 5.0
> Versão: v1.0 — Abril 2026

---

## Índice

| Épico | Módulo | Sprint |
|-------|--------|--------|
| [EPIC-01](#epic-01--módulo-tag-dispositivo-móvel) | Módulo Tag (Dispositivo Móvel) | Sprint 1 |
| [EPIC-02](#epic-02--módulo-anchor-ponto-fixo-de-referência) | Módulo Anchor (Ponto Fixo de Referência) | Sprint 1–2 |
| [EPIC-03](#epic-03--módulo-backend-processamento-e-api) | Módulo Backend (Processamento e API) | Sprint 2–3 |
| [EPIC-04](#epic-04--módulo-broker-mqtt--banco-de-dados) | Módulo Broker MQTT + Banco de Dados | Sprint 2 |
| [EPIC-05](#epic-05--módulo-interface-dashboard-web) | Módulo Interface (Dashboard Web) | Sprint 3–4 |
| [EPIC-06](#epic-06--módulo-validação-experimental) | Módulo Validação Experimental | Sprint 4 |

---



## EPIC-02 · Módulo Anchor (Ponto Fixo de Referência)

| Campo | Valor |
|-------|-------|
| **Módulo** | Hardware + Firmware |
| **Stack técnica** | ESP32 · ESP-IDF · BLE · MQTT · WiFi |
| **Responsável** | Equipe Hardware |
| **Sprint alvo** | Sprint 1–2 |
| **Status** | To Do |

### Descrição

Os anchors são os quatro nós fixos instalados em posições conhecidas na planta do ambiente. Cada anchor escaneia continuamente o sinal BLE do tag, mede o RSSI, aplica um pré-filtro EWA (Exponential Weighted Average) para reduzir ruído no chip e publica as leituras via MQTT ao broker. São a interface entre o mundo físico do rádio e o pipeline digital de processamento.

### Contexto e motivação

A posição dos anchors é determinante para a precisão geométrica da trilateração (GDOP). O firmware deve garantir leituras frequentes, com pré-filtro embarcado para reduzir o volume de dados transmitidos sem sacrificar qualidade, e publicação confiável mesmo em condições de rede instável.

---

### Requisitos Funcionais

| ID | Descrição | Prioridade |
|----|-----------|------------|
| RF-01 | Cada anchor deve escanear o canal BLE e registrar RSSI do tag identificado pelo UUID a cada 200 ms | Must Have |
| RF-02 | O firmware deve aplicar filtro EWA embarcado com fator alpha = 0.3 (configurável) antes de publicar o RSSI | Must Have |
| RF-03 | Os dados devem ser publicados no broker MQTT em formato JSON: `{"anchor_id":"A1","rssi":-67.3,"ts":1712345678}` | Must Have |
| RF-04 | O anchor deve usar tópico MQTT padronizado: `indoor/anchor/{anchor_id}/rssi` | Must Have |
| RF-05 | O firmware deve reconectar automaticamente ao broker MQTT em até 5 segundos após perda de conexão WiFi | Must Have |
| RF-06 | As coordenadas físicas (x, y) de cada anchor devem ser configuráveis via `config.h` sem recompilação do restante do firmware | Must Have |
| RF-07 | O anchor deve publicar heartbeat no tópico `indoor/anchor/{id}/status` a cada 10 segundos indicando uptime e qualidade do link WiFi | Should Have |
| RF-08 | O firmware deve suportar OTA via WiFi para atualização remota sem deslocamento físico | Should Have |
| RF-09 | Em caso de ausência de detecção do tag por mais de 2 segundos, publicar RSSI nulo (`null`) para indicar perda de sinal | Must Have |

---

### Garantias (Requisitos Não-Funcionais)

| Garantia | Critério de Aceitação |
|----------|-----------------------|
| Frequência de publicação | Mínimo de 4 publicações MQTT por segundo por anchor em condições normais de operação |
| Latência de publicação | Tempo entre scan do RSSI e publicação MQTT < 50 ms |
| Resiliência de rede | Reconexão automática ao broker em até 5 s após queda de WiFi, sem intervenção manual |
| Precisão do pré-filtro | EWA reduz desvio padrão do RSSI bruto em no mínimo 30% em ambiente estático |
| Disponibilidade | Operação contínua por no mínimo 72 horas sem reinicialização; watchdog ativo |
| Segurança MQTT | Conexão ao broker com autenticação usuário/senha; TLS opcional para ambiente de produção |
| Posicionamento físico | Anchors instalados em altura de 2–2,5 m, sem obstáculos metálicos a 30 cm de raio |

---

### Definition of Done (DoD)

- [ ] Firmware dos 3 anchors compilado, flashado e operando simultaneamente sem conflitos
- [ ] Publicações MQTT verificadas no broker com ferramenta MQTT Explorer (payload JSON válido)
- [ ] Frequência mínima de 4 msg/s confirmada por análise de log do broker por 5 minutos
- [ ] Teste de resiliência: queda e retorno de WiFi simulados; reconexão automática validada
- [ ] Pré-filtro EWA validado: desvio padrão do RSSI bruto vs. filtrado documentado em ambiente estático
- [ ] Coordenadas (x, y) de todos os anchors configuradas, documentadas e mapeadas na planta
- [ ] Heartbeat operando e visível no broker para todos os 3 anchors

---

## EPIC-03 · Módulo Backend (Processamento e API)

| Campo | Valor |
|-------|-------|
| **Módulo** | Software — Servidor |
| **Stack técnica** | Python 3.11 · FastAPI · paho-mqtt · NumPy · Mosquitto |
| **Responsável** | Equipe Software |
| **Sprint alvo** | Sprint 2–3 |
| **Status** | To Do |

### Descrição

O backend é o núcleo de processamento do sistema. Consome as mensagens MQTT dos anchors, executa o pipeline de estimação de posição (Filtro de Kalman → Path Loss Model → Trilateração WLS) e expõe os resultados via API REST e WebSocket para o dashboard e para o banco de dados. É onde toda a teoria do TCC se materializa em código.

### Contexto e motivação

O backend precisa processar até 15 mensagens MQTT por segundo (3 anchors × 5 msg/s) com latência total menor que 200 ms do recebimento à posição publicada. O pipeline de processamento deve ser modular, permitindo substituir ou ajustar cada etapa (filtro, modelo de propagação, algoritmo de posicionamento) de forma independente — facilitando os experimentos comparativos para o TCC.

---

### Requisitos Funcionais

| ID | Descrição | Prioridade |
|----|-----------|------------|
| RF-01 | O backend deve subscrever os tópicos MQTT `indoor/anchor/+/rssi` e processar mensagens em tempo real | Must Have |
| RF-02 | Deve manter uma instância do Filtro de Kalman 1D (Q=0.01, R=3.0, ajustáveis) por anchor por tag rastreado | Must Have |
| RF-03 | Deve converter RSSI filtrado em distância usando o modelo log-distance path loss com parâmetros TxPower e n configuráveis | Must Have |
| RF-04 | Deve estimar posição (x, y) via trilateração por Weighted Least Squares usando as 4 distâncias calculadas | Must Have |
| RF-05 | Deve expor endpoint REST `GET /position/{tag_id}` retornando última posição estimada em JSON | Must Have |
| RF-06 | Deve expor WebSocket `ws://.../ws/position/{tag_id}` com atualizações em tempo real a cada nova estimativa | Must Have |
| RF-07 | Deve persistir cada estimativa de posição no banco com timestamp UTC, posição (x, y), erro estimado e RSSI filtrado por anchor | Must Have |
| RF-08 | Deve expor endpoint `GET /history/{tag_id}?from=&to=` para recuperar histórico de posições por período | Should Have |
| RF-09 | Deve expor endpoint `GET /metrics/{tag_id}` retornando RMSE, MAE e erro médio do período configurado | Should Have |
| RF-10 | Os parâmetros Q, R, n e TxPower devem ser ajustáveis via `POST /config` sem reinicialização do serviço | Should Have |
| RF-11 | Deve implementar endpoint `POST /calibrate` para coletar amostras estáticas e estimar o parâmetro n empiricamente | Could Have |

---

### Garantias (Requisitos Não-Funcionais)

| Garantia | Critério de Aceitação |
|----------|-----------------------|
| Latência de processamento | Tempo de recebimento MQTT até posição disponível na API < 200 ms (p95) |
| Throughput | Processar no mínimo 15 mensagens MQTT/s sem perda ou atraso acumulado |
| Precisão de localização | Erro médio de posicionamento < 2,0 m em ambiente de teste controlado com 3 anchors |
| Disponibilidade da API | API REST disponível com uptime > 99% durante sessões de coleta de dados para o TCC |
| Modularidade | `KalmanRSSI`, `PathLossModel` e `Trilateration` implementados como classes independentes e testáveis com pytest |
| Rastreabilidade | Logs estruturados (JSON) para cada mensagem processada, com nível DEBUG/INFO/ERROR configurável |
| Persistência | Sem perda de estimativas em caso de reinicialização do serviço (buffer em disco ou banco persistente) |

---

### Definition of Done (DoD)

- [ ] Pipeline completo (MQTT → Kalman → PathLoss → WLS → API) operando de ponta a ponta com dados reais
- [ ] Testes unitários com pytest cobrindo `KalmanRSSI`, `PathLossModel` e `Trilateration` (cobertura > 80%)
- [ ] Endpoint `/position/{tag_id}` retornando JSON válido com latência p95 < 200 ms (medido com 100 requisições)
- [ ] WebSocket entregando atualizações em tempo real validadas no dashboard
- [ ] Dados de posição persistidos no banco e recuperáveis via `/history` sem perda
- [ ] Parâmetros Q, R, n e TxPower ajustáveis em runtime via `/config` e validados com teste de regressão
- [ ] Logs estruturados operando e permitindo diagnóstico de cada etapa do pipeline

---

## EPIC-04 · Módulo Broker MQTT + Banco de Dados

| Campo | Valor |
|-------|-------|
| **Módulo** | Infraestrutura |
| **Stack técnica** | Mosquitto 2.x · TimescaleDB · Docker Compose |
| **Responsável** | Equipe Software |
| **Sprint alvo** | Sprint 2 |
| **Status** | To Do |

### Descrição

Este módulo cobre a infraestrutura de mensageria e persistência. O broker Mosquitto recebe todas as publicações dos anchors e as repassa ao backend. O TimescaleDB armazena as séries temporais de RSSI e posições estimadas, permitindo análise histórica, geração de métricas de validação e visualização no Grafana (opcional).

---

### Requisitos Funcionais

| ID | Descrição | Prioridade |
|----|-----------|------------|
| RF-01 | O broker Mosquitto deve aceitar conexões dos anchors (porta 1883) e do backend simultaneamente | Must Have |
| RF-02 | O broker deve exigir autenticação com usuário e senha para todas as conexões (arquivo `passwd`) | Must Have |
| RF-03 | O broker deve reter (`retain`) a última mensagem de cada tópico `indoor/anchor/+/rssi` para novos subscribers | Should Have |
| RF-04 | O TimescaleDB deve armazenar measurements: `rssi_raw`, `rssi_filtered`, `position_estimated` com tags `anchor_id` e `tag_id` | Must Have |
| RF-05 | O banco deve permitir consultas por janela de tempo (ex: últimos 10 minutos) com resolução de 200 ms | Must Have |
| RF-06 | O ambiente deve ser provisionado via Docker Compose com um único comando (`docker compose up`) | Must Have |
| RF-07 | O TimescaleDB deve ter política de retenção de dados configurada para 30 dias | Should Have |

---

### Garantias (Requisitos Não-Funcionais)

| Garantia | Critério de Aceitação |
|----------|-----------------------|
| Throughput do broker | Mosquitto processa no mínimo 100 msg/s sem perda em hardware local (Raspberry Pi 4 ou superior) |
| Latência de persistência | Escrita no TimescaleDB concluída em < 50 ms para lotes de até 20 pontos |
| Segurança | Nenhuma porta exposta externamente sem autenticação; broker não aceita conexões anônimas |
| Reprodutibilidade | Ambiente completo sobe do zero em < 2 minutos via `docker compose up` em máquina limpa |

---

### Definition of Done (DoD)

- [ ] Mosquitto operando em container Docker com autenticação habilitada e testada
- [ ] TimescaleDB operando.
- [ ] `docker-compose.yml` versionado no repositório com variáveis sensíveis em `.env` (não versionado)
- [ ] Políticas de retenção de 30 dias configuradas e verificadas no InfluxDB
- [ ] Teste de carga: 100 msg/s publicadas no broker por 60 s sem perda (verificado por log)
- [ ] README de infraestrutura com diagrama de rede e instruções de setup publicado

---

## EPIC-05 · Módulo Interface (Dashboard Web)

| Campo | Valor |
|-------|-------|
| **Módulo** | Software — Frontend |
| **Stack técnica** | HTML5 · CSS3 · JavaScript · WebSocket · Canvas API |
| **Responsável** | Equipe Software |
| **Sprint alvo** | Sprint 3–4 |
| **Status** | To Do |

### Descrição

O dashboard web é a camada de visualização do sistema. Exibe a planta do ambiente em escala, a posição estimada do tag em tempo real via WebSocket, o histórico de trajetória, os valores de RSSI filtrado por anchor e as métricas de desempenho. É também a ferramenta usada durante a validação experimental para registrar posições de referência e calcular o erro.

### Contexto e motivação

Uma interface clara é essencial para a fase de validação experimental do TCC. O avaliador precisa ver em tempo real onde o sistema acredita que o tag está, comparar com a posição real conhecida e registrar o erro. O dashboard serve tanto como ferramenta de desenvolvimento quanto como demonstração durante a defesa.

---

### Requisitos Funcionais

| ID | Descrição | Prioridade |
|----|-----------|------------|
| RF-01 | Exibir planta do ambiente em escala com posição dos 4 anchors (marcadores fixos com ID e coordenadas) | Must Have |
| RF-02 | Exibir posição estimada do tag como ponto animado atualizado em tempo real via WebSocket (< 300 ms de latência visual) | Must Have |
| RF-03 | Exibir trajetória histórica do tag como linha sobre a planta (últimos 60 segundos, configurável) | Must Have |
| RF-04 | Painel lateral com RSSI filtrado de cada anchor atualizado em tempo real, com indicador visual de intensidade | Must Have |
| RF-05 | Exibir métricas ao vivo: erro estimado (m), estimativas por segundo e status de cada anchor (online/offline) | Must Have |
| RF-06 | Botão para registrar posição real do tag em um ponto de teste e calcular erro instantâneo (distância euclidiana) | Must Have |
| RF-07 | Exportar CSV com histórico de posições estimadas e reais para análise offline no Python | Must Have |
| RF-08 | Controles para ajustar parâmetros Q, R e n do backend em tempo real via chamada à API `/config` | Should Have |
| RF-09 | Modo de calibração: coletar 30 amostras em ponto fixo e enviar ao backend para estimar parâmetro n | Should Have |
| RF-10 | Interface responsiva funcionando em desktop e tablet (mínimo 768px de largura) | Could Have |

---

### Garantias (Requisitos Não-Funcionais)

| Garantia | Critério de Aceitação |
|----------|-----------------------|
| Latência visual | Posição do tag atualizada na tela em < 300 ms após nova estimativa disponível no backend |
| Estabilidade | Dashboard opera continuamente por 2 horas de sessão de validação sem travamento ou memory leak |
| Compatibilidade | Funciona nos navegadores Chrome 120+ e Firefox 120+ sem plugins adicionais |
| Clareza visual | Ponto do tag, trajetória e anchors distinguíveis para usuários com deficiência de visão de cores (formas além de cores) |
| Exportação | CSV com colunas: `timestamp`, `x_est`, `y_est`, `x_real`, `y_real`, `erro_m`, `rssi_A1`, `rssi_A2`, `rssi_A3`, `rssi_A4` |
| Sem dependências externas | Dashboard funciona sem CDN externo em rede local isolada (todas as libs bundladas ou inline) |

---

### Definition of Done (DoD)

- [ ] Planta do ambiente carregada com anchors nas posições corretas e escala calibrada em metros
- [ ] Tag aparece e se move na tela em tempo real ao se deslocar pelo ambiente, validado visualmente
- [ ] Trajetória histórica desenhada corretamente sobre a planta por no mínimo 60 segundos de movimento
- [ ] Painel de RSSI exibe valores de todos os 4 anchors atualizados; indicador offline quando anchor para de publicar
- [ ] Botão de registro de posição real funciona: calcula e exibe erro em metros imediatamente
- [ ] Exportação CSV gera arquivo com todas as colunas esperadas e dados corretos (validado em 5 pontos de teste)
- [ ] Sessão de 2h executada sem travamento ou crescimento de memória observável no DevTools
- [ ] Código-fonte versionado com README de execução; arquivo de planta em formato editável (SVG ou JSON)

---

## EPIC-06 · Módulo Validação Experimental

| Campo | Valor |
|-------|-------|
| **Módulo** | Pesquisa e Análise |
| **Stack técnica** | Python · NumPy · Matplotlib · Pandas · pytest |
| **Responsável** | Equipe TCC |
| **Sprint alvo** | Sprint 4 |
| **Status** | To Do |

### Descrição

Este épico cobre o protocolo experimental usado para validar e quantificar o desempenho do sistema. Inclui a definição de pontos de teste, o procedimento de coleta de dados, os scripts de análise estatística e a geração de tabelas e gráficos para a monografia. Os resultados deste módulo são a principal contribuição científica do TCC.

### Contexto e motivação

A validação precisa ser rigorosa o suficiente para sustentar as conclusões perante a banca. Isso requer cobertura espacial ampla (pontos LOS e NLOS), volume de amostras estatisticamente significativo e comparação justa entre configurações — mantendo todos os outros fatores constantes ao variar apenas o elemento sob análise.

---

### Requisitos Funcionais

| ID | Descrição | Prioridade |
|----|-----------|------------|
| RF-01 | Definir mínimo de 15 pontos de referência distribuídos pelo ambiente (incluindo LOS e NLOS em relação a cada anchor) | Must Have |
| RF-02 | Coletar mínimo de 50 amostras de posição estimada por ponto de referência para significância estatística | Must Have |
| RF-03 | Calcular RMSE, MAE e erro no percentil 90 (p90) para cada configuração testada | Must Have |
| RF-04 | Comparar pelo menos 3 configurações: sem filtro, com EWA, com Kalman — mantendo trilateração WLS igual | Must Have |
| RF-05 | Comparar trilateração WLS vs. fingerprinting KNN com Kalman aplicado em ambos | Should Have |
| RF-06 | Gerar mapa de calor de erro sobre a planta do ambiente (erro médio por região) | Should Have |
| RF-07 | Gerar gráficos CDF (Função de Distribuição Cumulativa) do erro para cada configuração testada | Must Have |
| RF-08 | Scripts de análise reprodutíveis: dado o CSV exportado do dashboard, gerar todos os gráficos com um comando | Must Have |

---

### Garantias (Requisitos Não-Funcionais)

| Garantia | Critério de Aceitação |
|----------|-----------------------|
| Reprodutibilidade | Scripts produzem resultados idênticos dado o mesmo CSV de entrada (sem aleatoriedade não semeada) |
| Abrangência espacial | Pontos de teste cobrem todas as regiões: sala, quartos, corredor e proximidade de paredes |
| Qualidade dos gráficos | Figuras exportadas em PNG 300 DPI prontas para inserção na monografia sem redimensionamento |
| Rastreabilidade | Cada gráfico e tabela referencia o arquivo CSV de origem e a data de coleta no próprio arquivo |

---

### Definition of Done (DoD)

- [ ] Protocolo de coleta documentado: planta com os 15+ pontos de referência marcados e numerados
- [ ] Coleta realizada: CSV com no mínimo 750 linhas de dados (15 pontos × 50 amostras) disponível no repositório
- [ ] Tabela comparativa RMSE/MAE/p90 gerada para todas as configurações testadas
- [ ] Gráficos CDF gerados para todas as configurações e exportados em 300 DPI
- [ ] Mapa de calor de erro sobre a planta gerado e interpretado no texto da monografia
- [ ] Scripts de análise testados: execução do zero a partir do CSV em < 30 segundos
- [ ] Seção de Resultados da monografia escrita com base nos dados coletados, com interpretação de cada métrica

---

## Resumo geral

| Épico | RFs | Must Have | Should Have | Could Have | Sprint |
|-------|-----|-----------|-------------|------------|--------|
| EPIC-01 Tag | 8 | 5 | 2 | 1 | 1 |
| EPIC-02 Anchor | 9 | 7 | 2 | 0 | 1–2 |
| EPIC-03 Backend | 11 | 7 | 3 | 1 | 2–3 |
| EPIC-04 Broker + DB | 7 | 5 | 2 | 0 | 2 |
| EPIC-05 Dashboard | 10 | 7 | 2 | 1 | 3–4 |
| EPIC-06 Validação | 8 | 6 | 2 | 0 | 4 |
| **Total** | **53** | **37** | **13** | **3** | — |

---

*Documento gerado em Abril 2026 · TCC Engenharia Eletrônica · v1.0*
