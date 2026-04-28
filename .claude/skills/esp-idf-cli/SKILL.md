---
name: esp-idf-cli
description: Guia completo do CLI idf.py para projetos ESP32 no Linux e macOS. Use esta skill sempre que o usuário quiser criar, configurar, compilar, flashar ou monitorar um projeto ESP32 via linha de comando. Ative quando o usuário mencionar idf.py build, idf.py flash, idf.py monitor, idf.py menuconfig, porta serial (/dev/tty, /dev/cu), ativar ambiente ESP-IDF, EIM, set-target, erase-flash, ou perguntar como começar um projeto ESP-IDF do zero. Esta skill cobre o fluxo operacional de CLI — para ajuda com código de firmware, componentes ou APIs ESP-IDF, use a skill esp-idf-expert.
---

# ESP-IDF CLI — Guia idf.py (Linux e macOS)

> Referência: [ESP-IDF Programming Guide v6.0 — Start a Project on Linux and macOS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-macos-start-project.html)

Este guia cobre o fluxo completo: ativar ambiente → criar projeto → configurar → compilar → flashar → monitorar.

---

## 1. Ativar o Ambiente ESP-IDF

Execute **toda vez** que abrir um terminal novo — nenhum comando `idf.py` funciona sem isso.

### Via EIM CLI (recomendado para ESP-IDF v6.0+)

```bash
source "/Users/<username>/.espressif/tools/activate_idf_v5.4.2.sh"
```

> Substitua pelo caminho exato impresso pelo instalador EIM.

### Via EIM GUI (alternativa)

1. Abra o **ESP-IDF Installation Manager (eim)**.
2. Em **Manage Installations**, clique em **Open Dashboard**.
3. Selecione a versão desejada do ESP-IDF.
4. Clique em **Open IDF Terminal** — o terminal já abre com o ambiente ativo.

---

## 2. Criar um Novo Projeto

> **Importante:** ESP-IDF não suporta espaços no caminho do projeto ou do próprio ESP-IDF.

Copie um exemplo para seu diretório de trabalho:

```bash
cd ~/esp
cp -r $IDF_PATH/examples/get-started/hello_world .
```

Use qualquer exemplo de `$IDF_PATH/examples`.

---

## 3. Identificar a Porta Serial

Conecte o ESP32 via USB e identifique a porta:

| SO    | Padrão de nome         |
|-------|------------------------|
| Linux | `/dev/tty*`            |
| macOS | `/dev/cu.*`            |

Anote a porta — você vai precisar para flash e monitor.

---

## 4. Configurar o Projeto

```bash
cd ~/esp/hello_world
idf.py set-target esp32
idf.py menuconfig
```

- `set-target` — define o chip alvo (`esp32`, `esp32s2`, `esp32c3`, etc.). **Limpa** os arquivos de build existentes.
- `menuconfig` — abre o TUI interativo para configurar opções (credenciais Wi-Fi, velocidade da CPU, etc.).

Para o `hello_world`, a configuração padrão já funciona — `menuconfig` pode ser pulado.

---

## 5. Compilar

```bash
idf.py build
```

Gera bootloader, tabela de partições e o binário da aplicação (`.bin`). Sucesso termina com:

```
[527/527] Generating hello_world.bin
Project build complete.
```

---

## 6. Flashar no Dispositivo

```bash
idf.py -p PORT flash
```

Substitua `PORT` pela porta serial (ex: `/dev/ttyUSB0` ou `/dev/cu.usbserial-0001`).

- Omitir `-p PORT` faz o `idf.py` tentar detectar automaticamente.
- `flash` já compila antes de flashar — não é necessário rodar `build` separadamente.

### Comandos de Erase

| Comando | Descrição |
|---------|-----------|
| `idf.py -p PORT erase-flash` | Apaga toda a memória flash |
| `idf.py -p PORT erase-otadata` | Apaga apenas a partição OTA data |

> Não desconecte o dispositivo durante o erase.

---

## 7. Monitorar Saída Serial

```bash
idf.py -p PORT monitor
```

Inicia o **IDF Monitor** para exibir a saída serial. Saída de exemplo:

```
Hello world!
Restarting in 10 seconds...
This is esp32 chip with 2 CPU core(s), WiFi/BT/BLE, silicon revision 1, 2 MB external flash
```

| Atalho       | Ação                    |
|--------------|-------------------------|
| `Ctrl+]`     | Sair do IDF Monitor     |
| `Ctrl+T`     | Abrir menu do monitor   |
| `Ctrl+T H`   | Mostrar ajuda do monitor|

---

## 8. Comandos Combinados

```bash
idf.py -p PORT flash monitor
```

Compila, flasha e abre o monitor em sequência.

---

## 9. Adicionar Board Support Package (BSP)

```bash
idf.py add-dependency <nome-do-bsp>
```

**Exemplo:**
```bash
idf.py add-dependency esp_wrover_kit
```

BSPs são distribuídos pelo [IDF Component Manager](https://components.espressif.com/).

---

## Referência Rápida de Comandos

| Comando | Descrição |
|---------|-----------|
| `idf.py set-target <chip>` | Define o chip alvo |
| `idf.py menuconfig` | Abre o menu de configuração |
| `idf.py build` | Compila o projeto |
| `idf.py -p PORT flash` | Flasha no dispositivo |
| `idf.py -p PORT monitor` | Monitora saída serial |
| `idf.py -p PORT flash monitor` | Flasha e monitora |
| `idf.py -p PORT erase-flash` | Apaga toda a flash |
| `idf.py -p PORT erase-otadata` | Apaga partição OTA data |
| `idf.py add-dependency <pkg>` | Adiciona componente/BSP |

---

## Solução de Problemas

### Permission Denied no Linux

```
Could not open port <PORT>: Permission denied
```

```bash
sudo usermod -aG dialout $USER
# ou
sudo usermod -aG uucp $USER
```

Faça logout e login novamente para as mudanças surtirem efeito.

### Saída Garbled no Monitor (Frequência de Cristal Errada)

Se a saída serial parece caracteres aleatórios:

1. Saia do monitor (`Ctrl+]`).
2. Execute `idf.py menuconfig`.
3. Navegue para: **Component config → Hardware Settings → Main XTAL Config → Main XTAL frequency**.
4. Defina `CONFIG_XTAL_FREQ` como **26 MHz** (padrão é 40 MHz).
5. Recompile e reflashe.

### Versão do Python

ESP-IDF requer **Python 3.10 ou superior**. Use `pyenv` ou atualize o sistema se necessário.

---

## Links Úteis

- [IDF Monitor](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-monitor.html)
- [idf.py — referência completa](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-py.html)
- [Conexão serial com ESP32](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/establish-serial-connection.html)
- [ESP Component Registry](https://components.espressif.com/)
