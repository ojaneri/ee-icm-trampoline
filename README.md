# ICM — Instrument Cluster Module on Trampoline RTOS

**Project #2 — Automotive Instrument Cluster Module Using Trampoline RTOS**  
Sistemas Embarcados, PPGEE UTFPR Ponta Grossa — Prof. Max Mauro Dias Santos  

**Equipe:** Osvaldo Janeri Filho (@ojaneri) · Norton Dezonet (@dezonet) · Vinicius Antunes

[![CI](https://github.com/ojaneri/ee-icm-trampoline/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/ojaneri/ee-icm-trampoline/actions/workflows/ci.yml)
[![GitHub Pages](https://img.shields.io/badge/🌐_Live_Demo-GitHub_Pages-blue?style=for-the-badge)](https://ojaneri.github.io/ee-icm-trampoline/)

Protótipo de painel de instrumentos automotivo com **controle determinístico de tempo real**: seis tarefas OSEK no [Trampoline RTOS](https://github.com/TrampolineRTOS/trampoline) (OSEK/VDX, API alinhada ao AUTOSAR OS 4.2), entrada CAN com interrupção e evento, estrutura de dados compartilhada sob protocolo de teto de prioridade, diagnóstico de falhas e **deploy em hardware real**.

### 🌐 [Acesse a Demonstração Interativa e Documentação Completa](https://ojaneri.github.io/ee-icm-trampoline/)
Simulação do display LCD, guia de conexão Wi-Fi e documentação técnica do projeto.

---

## Hardware Validado

O sistema foi validado em dois alvos físicos distintos:

| Alvo | Placa | Firmware / Framework | Função | Periféricos Chave |
|---|---|---|---|---|
| **ESP32-C6-LCD-1.47** | `esp32-c6-devkitc-1` | Arduino + PlatformIO (`espressif32`) | Dashboard gráfico & Wi-Fi AP | ST7789 1.47" TFT (SPI), WebServer (80), WebSocket (81) |
| **STM32 Nucleo L476RG** | `nucleo_l476rg` | CMSIS + PlatformIO (`ststm32`) | Validação RTOS em MCU real | ARM Cortex-M4, ST-Link V2-1, UART debug |

### ESP32-C6: Dashboard Gráfico e Wi-Fi

> ⚠️ **Known Issue (2026-10-27):** The ESP32-C6 firmware currently experiences a silent boot crash on some boards when using the Arduino framework with USB-JTAG CDC. The firmware compiles and uploads successfully, but execution halts before `setup()` on certain hardware revisions. This is under investigation — the STM32 Nucleo target and the GitHub Pages simulation remain fully functional for demonstration and grading purposes.

O firmware da ESP32-C6 implementa um dashboard completo que roda localmente no display TFT de 1.47" e simultaneamente hospeda uma interface web via Wi-Fi Access Point.

#### Telas do Display LCD (Auto-rotação a cada 5s)
1. **Gauges Principais**: Velocímetro (km/h), Tacômetro (rpm), Combustível (%) e Temperatura (°C) em fonte grande. Barra de status verde (normal) ou vermelha (falha).
2. **Tarefas RTOS**: Contadores de execução das 6 tarefas OSEK (CAN_RX, Acq, Proc, Disp, Diag) e utilização de CPU.
3. **Jitter Scope**: Gráfico de barras mostrando o desvio de período (jitter) da tarefa de aquisição em tempo real. Demonstra a determinística do escalonador.
4. **ISR Latency**: Mede e exibe a latência de resposta a interrupções de hardware (botão BOOT). Prova o tempo real da ISR.
5. **Debug & Falhas**: Métricas de confiabilidade (Deadline Misses, OS Overruns, Torn Copies) e estado dos DTCs.
6. **Rede**: Status do Wi-Fi AP (SSID, IP, Porta WebSocket).

#### Interface Web (Smartphone/PC)
Conecte-se ao Wi-Fi da placa e acesse a mesma interface visual do LCD, mas com gráficos Canvas interativos e abas navegáveis. Os dados são atualizados em tempo real via WebSocket.

### STM32 Nucleo L476RG: Validação RTOS
Executa o esqueleto das seis tarefas OSEK com proteção de recurso compartilhado (`ICM_Data`) sob protocolo de teto de prioridade, confirmando a arbitragem determinística e a ausência de race conditions em hardware ARM Cortex-M4 real.

---

## Como Reproduzir e Testar

### Pré-requisitos
- [PlatformIO Core](https://platformio.org/install/cli) ou VS Code com extensão PlatformIO
- Driver USB para ESP32-C6 e ST-Link para Nucleo (conforme aplicável)

### ESP32-C6 Dashboard
1. Conecte a placa via USB.
2. Compile e grave o firmware:
   ```bash
   platformio run -d esp32_dashboard --target upload
   ```
3. Abra o Monitor Serial (115200 baud) para ver os logs de boot e telemetria.
4. No seu celular ou PC, conecte na rede Wi-Fi:
   - **SSID:** `ICM-Dashboard-AP`
   - **Senha:** `12345678`
5. Acesse no navegador: **http://192.168.4.1**
6. Pressione o botão **BOOT** na placa para testar a latência da ISR e retornar à tela inicial.

### STM32 Nucleo L476RG
1. Conecte o ST-Link à placa e ao PC.
2. Compile e grave:
   ```bash
   platformio run -d nucleo_l476 --target upload
   ```
3. Use um terminal serial para verificar a saída de debug das tarefas RTOS.

---

## Estrutura do Repositório

```
esp32_dashboard/      Firmware ESP32-C6 (Arduino/PlatformIO)
  src/main.cpp        Lógica completa: Display, Wi-Fi, WebSocket, Simulação RTOS
  platformio.ini      Configuração da placa e dependências
nucleo_l476/          Firmware STM32 Nucleo (CMSIS/PlatformIO)
  src/main.c          Esqueleto das tarefas OSEK e proteção de recurso
src/                  Código-fonte original Trampoline (Simulação POSIX/CI)
docs/                 Relatório técnico e evidências de testes
requirements/         Requisitos, arquitetura e dicionário de sinais
tests/                Testes unitários e de integração (CI)
```

---

## Resultados de Tempo Real

Medições obtidas em hardware real e simulação CI:

| Métrica | Resultado |
|---|---|
| Releases periódicos no tick exato | 100% das ativações |
| Deadline misses (nominal e falha) | 0 |
| Latência ISR (GPIO → Handler) | < 10 µs (medido no scope) |
| Jitter de período (Task_Acq 10ms) | ± 50 µs típico |
| Utilização CPU (pior caso medido) | 2.9% (Limite Liu & Layland: 73.5%) |
| Cópias inconsistentes (com recurso) | 0 de 617 |
| Detecção de perda de mensagem CAN | ≤ 3 períodos (timeout configurado) |

---

## Contributors

- Osvaldo Janeri Filho ([@ojaneri](https://github.com/ojaneri))
- Norton Dezonet ([@dezonet](https://github.com/dezonet))
- Vinicius Antunes

---

## Licença

Este projeto é desenvolvido para fins acadêmicos no PPGEE/UTFPR.