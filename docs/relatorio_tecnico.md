# Relatório Técnico — Instrument Cluster Module sobre Trampoline RTOS

**Project #2 — Automotive Instrument Cluster Module Using Trampoline RTOS**\
Disciplina Sistemas Embarcados — PPGEE, UTFPR Ponta Grossa\
Prof. Max Mauro Dias Santos\
Equipe: Osvaldo Janeri Filho <janeri@gmail.com>, Norton Dezonet (@dezonet), Vinicius Antunes\
Apresentação: 27/10/2026\
Repositório: <https://github.com/ojaneri/ee-icm-trampoline>

---

## Resumo

Este trabalho implementa o protótipo de um painel de instrumentos automotivo
(*Instrument Cluster Module*, ICM) sobre o RTOS Trampoline, implementação
aberta do padrão OSEK/VDX com API alinhada ao AUTOSAR OS 4.2. O sistema recebe
sinais do veículo por CAN, valida, escala, filtra e exibe velocidade,
rotação, combustível, temperatura, setas, luzes de aviso e mensagens, sob
escalonamento de prioridade fixa e preemptivo, com seis tarefas, uma ISR, dois
eventos, sete alarmes e dois recursos sob o protocolo de teto de prioridade.

O objetivo da disciplina, *Deterministic Real-Time Control*, foi tratado como
uma afirmação a ser medida, e não apenas implementada. Os testes medem
período, jitter, tempo de resposta e perda de *deadline* de cada tarefa, e
também as latências de detecção de falha ponta a ponta. Para a ausência de
*race condition*, há um controle negativo: o mesmo teste, compilado sem o
recurso, precisa encontrar a corrida. Resultado principal: em operação nominal
e sob injeção de falhas de CAN, **100 % das ativações ocorreram exatamente no
tick previsto e nenhuma tarefa perdeu deadline**. Com o recurso, **0 de 617**
cópias da estrutura compartilhada saíram inconsistentes; sem ele, **199**.

Não há hardware: o Trampoline roda no alvo POSIX (dentro de um processo
Linux), o barramento CAN é uma interface SocketCAN virtual (`vcan0`) e o
painel é textual. A seção 10 delimita o que isso permite e o que não permite
concluir.

---

## 1. Introdução

Um painel de instrumentos é um caso típico de sistema de tempo real
*firm*: um ponteiro atrasado alguns milissegundos não causa acidente, mas um
velocímetro congelado mostrando um valor antigo como se fosse atual, ou uma
seta que continua piscando depois que o módulo de carroceria parou de
responder, é uma informação falsa ao motorista. Por isso a especificação
deste trabalho dá tanto peso ao tratamento de falhas (timeout, sinal fora de
faixa, integridade de quadro) quanto ao escalonamento.

O Trampoline foi escolhido pelo professor por ser OSEK/VDX: a configuração do
sistema operacional é estática, descrita num arquivo OIL e transformada em
código C pelo gerador GOIL antes da compilação. Não há criação dinâmica de
tarefas, alocação de memória nem filas ilimitadas, e todas as prioridades,
tetos e alarmes são conhecidos em tempo de compilação. É essa
característica que torna possível a análise de escalonabilidade da seção 8.

### 1.1 Entregáveis

| # | Entregável (plano do professor) | Onde está |
|---|---|---|
| 1 | Requisitos do ICM e dicionário de sinais | `requirements/icm_requirements.md`, `dbc/icm.dbc` |
| 2 | Projeto de tasks, eventos, alarmes, ISR e recursos | `requirements/architecture.md` |
| 3 | Configuração OIL e aplicação Trampoline | `src/icm.oil`, `src/icm_app.c` e módulos |
| 4 | Simulação de entradas CAN/sensores | `sim/can_sim.py`, `src/can_ctrl.c` |
| 5 | Dashboard automotivo funcional | `src/dashboard.c` |
| 6 | Testes de timing, deadline e falhas | `tests/`, `requirements/test_evidence.md`, `docs/evidence/` |
| 7 | Repositório, relatório e demonstração | este repositório e este relatório; roteiro de demo no README |

---

## 2. Requisitos e dicionário de sinais

Os requisitos estão em `requirements/icm_requirements.md`, em quatro
famílias com identificadores rastreáveis:

- **ICM-F** (funcionais, 8): o que o painel recebe e exibe;
- **ICM-T** (temporais, 9): períodos, deadlines, sobrecarga;
- **ICM-S** (segurança e falhas, 7): timeout, faixa, integridade, DTCs,
  exibição *fail-safe*, overrun;
- **ICM-D** (dados e sincronização, 4): a estrutura compartilhada.

Cada teste cita no nome ou na *docstring* os requisitos que cobre. O script
`tools/check_traceability.py` falha o CI se algum requisito ficar sem teste,
ou se algum teste citar um requisito inexistente. Resultado: **28 de 28
requisitos cobertos** (matriz completa em
`docs/evidence/02b_traceability_matrix.md`).

### 2.1 Mensagens CAN

O plano do professor fixa duas mensagens: **M1 Engine Speed a cada 500 ms**
e **M2 Vehicle Speed a cada 1000 ms**. Combustível, temperatura, setas e
luzes de aviso aparecem como entradas do veículo, mas sem mensagem
atribuída. Para elas criamos **M3** e **M4**. Isso foi decisão nossa e está
sinalizado assim em toda a documentação.

| Msg | ID | Conteúdo | Período | Timeout (3 períodos) | Origem |
|---|---|---|---|---|---|
| M1 | 0x100 | rotação do motor | 500 ms | 1500 ms | professor |
| M2 | 0x200 | velocidade do veículo | 1000 ms | 3000 ms | professor |
| M3 | 0x300 | combustível, temperatura do arrefecimento | 1000 ms | 3000 ms | nossa |
| M4 | 0x400 | setas, pisca-alerta, 8 luzes de aviso | 100 ms | 300 ms | nossa |

Todos os quadros têm DLC 8, ordem Intel, um **contador *alive*** de 4 bits no
byte 6 e um **CRC-8 SAE J1850** no byte 7, calculado sobre o identificador e
o payload. Esse é o esquema dos perfis 1/2 de E2E do AUTOSAR. Incluir o ID no
CRC faz com que um quadro entregue sob o identificador errado falhe a
verificação, em vez de ser decodificado como outra mensagem.

### 2.2 Sinais e faixas

| Sinal | Fator | Offset | Faixa válida | Bruto válido | SNA |
|---|---|---|---|---|---|
| EngineSpeed | 0,25 rpm/bit | 0 | 0–8000 rpm | 0–32000 | 0xFFFF |
| VehicleSpeed | 0,01 km/h/bit | 0 | 0–250 km/h | 0–25000 | 0xFFFF |
| FuelLevel | 0,5 %/bit | 0 | 0–100 % | 0–200 | 0xFF |
| CoolantTemp | 1 °C/bit | −40 | −40–150 °C | 0–190 | 0xFF |

As faixas de velocidade e rotação são as do plano. O código SNA (*signal not
available*) segue a convenção J1939: o emissor sabe que seu sensor falhou e
avisa. O painel o trata como valor fora de faixa. O mesmo dicionário existe em
formato DBC (`dbc/icm.dbc`), e um teste verifica, byte a byte, que o DBC, o
simulador e o código C concordam entre si.

### 2.3 Condições de falha

| Falha | Detecção | DTC | Confirmação |
|---|---|---|---|
| mensagem ausente | sem quadro há mais de 3 períodos | 0x01–0x04 | 1 ciclo |
| ainda não recebida no boot | timeout mínimo de 2 s (*first timeout*) | idem | — |
| valor fora de faixa / SNA | bruto acima do máximo | 0x11–0x14 | 3 ciclos (300 ms) |
| CRC, DLC, contador repetido | quadro descartado e contado | 0x21 | 3 ciclos |
| overrun de recepção | FIFO cheia, quadro perdido e contado | 0x22 | 1 ciclo |
| deadline perdido | `E_OS_LIMIT` ou resposta > deadline | 0x31 | 1 ciclo |

A escolha de **3 períodos** para o timeout é o compromisso usual do
*deadline monitoring* do AUTOSAR COM: com 1 período, qualquer quadro atrasado
viraria falha; com 5, o motorista olharia por 5 s um velocímetro congelado.

---

## 3. Arquitetura de tempo real

### 3.1 Tarefas

| Tarefa | Tipo | Ativação | Período | Prioridade | Etapa do pipeline |
|---|---|---|---|---|---|
| Task_Init | básica | autostart | uma vez | 10 | arma os alarmes |
| Task_CAN_RX | **estendida** | evento EV_CAN_RX | esporádica, D = 10 ms | 6 | recebe |
| Task_Acquisition | básica | ALARM_Acquisition | 10 ms | 5 | valida |
| Task_Processing | básica | ALARM_Processing | 20 ms | 4 | escala e filtra |
| Task_Display | básica | ALARM_Display | 50 ms | 3 | exibe |
| Task_Diagnostics | básica | ALARM_Diagnostics | 100 ms | 2 | DTCs, deadlines |
| Task_Logging | básica | ALARM_Logging | 1000 ms | 1 | registro |
| Task_Shutdown | básica | ALARM_Shutdown | uma vez | 10 | fim do teste |

O pipeline exigido (**recebe → valida → escala → filtra → exibe**) virou
uma etapa por tarefa. As etapas não se chamam entre si: cada uma lê o que a
anterior deixou em `ICM_Data` e escreve a sua própria seção. Assim o trabalho
de cada tarefa é limitado e independente do *timing* das outras.

### 3.2 Por que essas prioridades

**Rate-monotonic para as periódicas.** Quanto menor o período, maior a
prioridade. Para tarefas periódicas independentes com deadline igual ao
período, essa atribuição é ótima entre as de prioridade fixa (Liu & Layland,
1973): se alguma atribuição fixa cumpre todos os deadlines, a RM também
cumpre. O nosso conjunto tem exatamente esse formato, e uma atribuição que a
banca consegue conferir num relance é uma vantagem, não uma simplificação.

**Task_CAN_RX acima de todas as periódicas**, por três razões:

1. seu deadline (10 ms) é tão curto quanto o menor período, e seus jobs são
   minúsculos (≈ 5 µs medidos);
2. a fila do driver é finita (32 quadros); quadro não consumido é quadro que
   pode ser descartado, e descarte vira timeout três períodos depois;
3. a Task_Acquisition amostra a cada tick e deve enxergar os quadros que
   chegaram no tick anterior, não um tick depois.

Colocar uma tarefa esporádica acima das periódicas só é seguro se a carga
dela for limitada. Ela é: são no máximo 14 quadros/s em operação nominal, e
mesmo o cenário de 1000 quadros/s (ECU "*babbling idiot*") custa menos de
1 % da CPU (1000 jobs/s × 4,5 µs médios).

**Por que `ACTIVATION = 1`.** O OSEK permite enfileirar ativações. Não
queremos isso: se a tarefa ainda estiver rodando quando o alarme disparar de
novo, um segundo job enfileirado começaria atrasado e esconderia o
*overrun*. Com uma ativação só, o `ActivateTask` do alarme falha com
`E_OS_LIMIT`, o `ErrorHook` recebe o erro e o *overrun* é reportado. É o
monitor de deadline que o próprio OSEK oferece.

**Por que a Task_CAN_RX é estendida.** Ela fica bloqueada em `WaitEvent` e
mantém seu contexto entre os despertares. A ISR só precisa de `SetEvent`, que
é mais barato que uma ativação e não falha se duas interrupções chegarem
próximas: o evento simplesmente já estará setado.

### 3.3 Alarmes e o instante crítico

Todos os alarmes periódicos são armados por `SetRelAlarm` na Task_Init com
**o mesmo primeiro disparo** (tick 10). Todas as tarefas são liberadas juntas
no início e de novo a cada 1000 ms (o hiperperíodo). Esse é o *instante
crítico* de Liu & Layland, o padrão de liberação em que cada tarefa sofre seu
pior tempo de resposta. Escalonar os alarmes em fases diferentes deixaria os
números mais bonitos e o teste mais fraco.

Um sétimo alarme, `ALARM_TickProbe`, chama um *callback* a cada tick para
carimbar o instante do tick com `CLOCK_MONOTONIC`. É a base de tempo do
trace de medição e não faz parte da função do painel.

### 3.4 ISR, eventos e o caminho de recepção CAN

O alvo POSIX não tem periférico CAN. O módulo `src/can_ctrl.c` faz o papel
dele em três camadas, como num microcontrolador:

1. **barramento**: SocketCAN (`vcan0`). Uma interface física (`can0`, por
   exemplo um CANable) funciona sem mudar código;
2. **controlador**: uma thread POSIX lê o socket, aplica o filtro de
   aceitação (os 4 IDs) e coloca o quadro numa FIFO de 8 posições. Com a FIFO
   cheia, o quadro é descartado e um *overrun* é contado, como no bxCAN ou no
   FlexCAN. Em seguida a thread "levanta a linha de interrupção" com
   `kill(getpid(), SIGQUIT)`;
3. **driver**: a `ISR_CAN_RX` (categoria 2, fonte SIGQUIT) esvazia a FIFO
   para a fila do driver (32 posições) e faz `SetEvent(Task_CAN_RX,
   EV_CAN_RX)`, ou `EV_CAN_ERROR` se houve descarte.

A ISR não decodifica nada. Ela roda com interrupções mascaradas, e cada
microssegundo gasto ali é latência para todas as outras, inclusive o tick do
sistema.

Dois detalhes do alvo POSIX que moldaram o projeto:

- **Sinais POSIX não enfileiram.** Três quadros chegando enquanto a
  interrupção está mascarada geram uma única interrupção. Por isso a ISR
  esvazia a FIFO inteira, e a tarefa esvazia a fila inteira a cada evento:
  é exatamente como se trata uma interrupção de FIFO sensível a nível em
  hardware real;
- **`StartOS()` faz `fork()`.** No alvo POSIX, o processo pai vira o leitor
  de teclado da plataforma virtual, e o filho roda o SO. Uma thread criada em
  `main()` ficaria no pai. Por isso o controlador CAN é iniciado no
  `StartupHook`, que roda no filho. Descobrimos isso lendo
  `machines/posix/tpl_posixvp_irq_gen.c` antes de escrever o código.

A thread do controlador bloqueia todos os sinais, de modo que as interrupções
caem sempre na thread principal, onde o Trampoline roda. A FIFO entre a thread
e a ISR é um *ring* produtor-único/consumidor-único com *atomics* C11
(*acquire/release*), o equivalente em software aos registradores da FIFO de
um controlador real.

---

## 4. Configuração OIL e aplicação Trampoline

### 4.1 Do OIL ao executável

```
src/icm.oil ──GOIL──> tpl_app_config.c/.h, tpl_app_define.h, build.py
                         │
src/*.c + os/ + machines/posix/ ──gcc──> icm_exe
```

O GOIL (versão 3.1.16, compilado a partir do fonte do Trampoline no commit
fixado `09e2dcb`) lê o OIL, valida os objetos e gera os descritores estáticos
de tarefas, alarmes, recursos e ISRs, além do sistema de build. O resultado
para o nosso arquivo é `No warning, no error.` nas cinco variantes.

Objetos configurados (exigência do item 2 da arquitetura técnica):

| Objeto OIL | Instâncias |
|---|---|
| OS | `icm_os`: `STATUS = EXTENDED`, os 5 *hooks*, `USEGETSERVICEID`, `USEPARAMETERACCESS` |
| TASK | 8 (6 de aplicação + Init + Shutdown) |
| ALARM | 7 |
| COUNTER | `SystemCounter` (10 ms/tick no POSIX) |
| EVENT | `EV_CAN_RX`, `EV_CAN_ERROR` |
| RESOURCE | `ICM_Resource`, `RES_CONSOLE` |
| ISR | `ISR_CAN_RX` (categoria 2, SIGQUIT) |

**`STATUS = EXTENDED`**: todo serviço do SO verifica argumentos e
permissões. Por exemplo, um `GetResource` feito por uma tarefa que não
declarou o recurso retorna `E_OS_ACCESS`. É mais lento que `STANDARD`, e essa
é a troca certa para um protótipo cujo objetivo é provar o projeto. Os testes
exigem **zero** erros de API em tempo de execução.

### 4.2 Como o GOIL implementa o teto de prioridade

Lendo o `tpl_app_config.c` gerado, vê-se que o GOIL compacta as prioridades
do OIL em níveis internos e **insere um nível próprio para cada teto**, logo
acima do usuário mais prioritário do recurso:

| Nível | Objeto |
|---|---|
| 1 / 2 / 3 | Logging / Diagnostics / Display |
| **4** | **teto de RES_CONSOLE** |
| 5 / 6 / 7 | Processing / Acquisition / CAN_RX |
| **8** | **teto de ICM_Resource** |
| 9 | Init, Shutdown |
| 11 | ISR_CAN_RX |

Quem segura `ICM_Resource` roda no nível 8: nenhuma tarefa que usa os dados
pode preemptá-la, nem mesmo a Task_CAN_RX. A ISR (nível 11), no entanto,
continua podendo entrar, e não há problema nisso porque ela não toca em
`ICM_Data`.

### 4.3 Variantes de build

`tools/build.sh` gera, a partir do **mesmo** `icm.oil`, o produto e quatro
variantes de injeção de falha, diferentes só nos `-D` passados ao compilador:

| Variante | O que muda | Para quê |
|---|---|---|
| `nominal` | nada | o produto |
| `race_locked` | Display copia `ICM_Data` com uma janela de 12 ms no meio, segurando o recurso | teste de race |
| `race_unlocked` | a mesma janela, com `GetResource`/`ReleaseResource` removidos | controle negativo |
| `overload` | Diagnostics gasta 150 ms por job (período 100 ms) entre 5 e 10 s | overrun e isolamento |
| `irq_lock` | uma seção com todas as interrupções mascaradas por 200 ms em t = 3 s | perda de quadros |

---

## 5. Simulação das entradas

`sim/can_sim.py` faz o papel das outras ECUs: ECM, ABS e BCM. Ele usa só a
biblioteca padrão do Python (sockets `AF_CAN`), transmite cada mensagem no
seu período com agendamento por **tempo absoluto** (`próximo += período`,
sem acumular deriva) e com defasagem de 3 ms entre mensagens, como ECUs reais.
Os valores seguem um ciclo de condução de 30 s: arrancada até 120 km/h,
cruzeiro, frenagem, setas, pisca-alerta, *bulb check* na ignição e
aquecimento do motor.

Cenários:

| Cenário | Conteúdo |
|---|---|
| `nominal` | o ciclo de condução, sem falhas |
| `faults` | M2 ausente (5–10 s), 9000 rpm (11–14 s), 300 km/h (15–17 s), CRC errado na M3 (18–19,5 s), contador congelado na M4 (20–20,8 s), M4 ausente (22,5–24,5 s), rajada de 200 quadros (26 s) |
| `warnings` | combustível em 8 %, superaquecimento até 125 °C, 7000 rpm |
| `babble` | M1 a cada 1 ms entre 2 e 6 s (ECU tagarela) |

Cada quadro enviado é registrado com seu instante `CLOCK_MONOTONIC`. O cluster
registra cada mudança de DTC no mesmo relógio. Isso permite medir a latência
de detecção **ponta a ponta** com uma subtração simples: último quadro da M2 no
barramento → DTC confirmado no painel.

---

## 6. Dashboard

O painel é um quadro de texto de largura fixa (72 colunas), redesenhado a
cada 50 ms quando a saída é um terminal (modo `live`, com cores ANSI). Em
logs e no CI, um quadro por segundo é impresso (modo `plain`). Trecho real
de uma execução, durante a perda da M2:

```text
| SPEED [????????????????????????????????????????]    --- km/h         |
| RPM   [############............................]   2353 rpm          |
| FUEL  [#############.......]  64.7 %  TEMP [###.......]   63 C       |
| LAMPS  ...   ...   ...   ...   ...   ...   ....   ....               |
|        ....   ....  [FAULT]                                          |
| MSG   COMM LOST: VEHICLE SPEED (M2)                                  |
```

**Por que textual e não gráfico** (decisão documentada em
`src/dashboard.h`): o protótipo roda no alvo POSIX, dentro de um processo
Linux sem *framebuffer* próprio, e a disciplina trata de escalonamento
determinístico, não de desenho. Um front-end gráfico acrescentaria outro
processo, um canal de IPC e um renderizador cujo *timing* não controlamos,
e cada um deles borraria as medições que a Task_Display existe para produzir.
O quadro de texto de tamanho fixo mantém o trabalho da tarefa idêntico em
todo período. Em um painel real, este módulo seria trocado pelo driver dos
ponteiros, com a mesma interface (um *snapshot* entra, um quadro sai).

Comportamento *fail-safe* (ICM-S-05): sinal inválido aparece como `---`, nunca
como número, nem antigo nem zero. Com a M4 perdida, as setas e as luzes
pedidas pelo barramento se apagam, porque mostrar uma seta "esquerda" antiga
seria mentir. Qualquer DTC confirmado acende a luz FAULT e mostra uma
mensagem.

---

## 7. Dados compartilhados e ausência de race condition

Este é o ponto que a banca mais cobra, por isso o argumento está organizado na
ordem em que deve ser verificado. A versão completa está em
`requirements/architecture.md`, seção 5.

**1. Existe uma única porta.** A instância é `static ICM_Data icm_data` em
`icm_data.c`, e nenhum outro arquivo consegue nomeá-la. As funções de acesso
copiam para dentro ou para fora de uma variável do chamador e nunca devolvem
ponteiro para dentro da estrutura. Ninguém consegue tocar nos dados depois de
liberar o recurso.

**2. A porta tem uma tranca que a preempção não fura.** Toda função de acesso
é `GetResource(ICM_Resource); cópia; ReleaseResource(ICM_Resource)`. Pelo
protocolo de teto de prioridade do OSEK, quem chama roda no teto (nível 8)
durante a cópia. Nenhuma tarefa que usa os dados tem prioridade acima disso,
logo nenhuma pode ser despachada nessa janela. Num SO monoprocessador, isso
é exclusão mútua **sem espera**: se uma tarefa está rodando, ninguém mais
segura o recurso.

**3. Todo usuário está declarado.** O GOIL calcula o teto a partir das
linhas `RESOURCE` das tarefas no OIL. Se uma tarefa usasse os dados sem
declarar o recurso, o teto ficaria baixo demais. Com `STATUS = EXTENDED`, o SO
rejeitaria o `GetResource` dela.

**4. A ISR fica de fora.** A ISR não toca em `ICM_Data`. Ela compartilha com a
Task_CAN_RX apenas a fila do driver, que a tarefa lê entre
`SuspendOSInterrupts()` e `ResumeOSInterrupts()`.

**5. Escritor único por seção, e mesmo assim com tranca.** Cada seção da
estrutura tem uma só tarefa escritora. Isso **não basta**: um leitor de menor
prioridade preemptado no meio da cópia pelo escritor fica com metade dado
velho, metade dado novo (*torn read*). É isso que a tranca impede, e por isso
os leitores também a usam.

**6. Verificação estática.** `tools/check_shared_access.py` roda no CI e
falha se algum outro arquivo mencionar a instância ou chamar
`GetResource(ICM_Resource)`, se alguma função de acesso tiver *lock/unlock*
desbalanceado ou `return` com o recurso preso, ou se alguma tarefa que chama
uma função de acesso não declarar o recurso no OIL.

**7. Teste com controle negativo.** Toda cópia confere dois números de
sequência que o escritor grava iguais antes e depois do payload; uma cópia
rasgada os tem diferentes. Como "zero cópias rasgadas" prova pouco se a
verificação puder ser cega, o teste de race constrói o leitor com uma janela
de 12 ms no meio da cópia. A janela é maior que um tick, então o escritor fica
garantidamente pronto durante a cópia.

| Build | Cópias | Rasgadas |
|---|---|---|
| `race_locked` (com recurso) | 617 | **0** |
| `race_unlocked` (sem recurso) | 617 | **199** |

As 617 cópias incluem as de Diagnostics e Logging, que não têm janela. Cerca
de 400 são as cópias lentas do Display, e 199 é a metade delas, exatamente o
previsto: o Display é liberado a cada 5 ticks e o Processing a cada 2, então o
tick que cai dentro da janela ativa o Processing uma vez sim, outra não.

**8. O preço do teto.** O mesmo `race_locked` mostra o custo do
protocolo. Enquanto o Display segura o recurso por 12 ms, a Task_CAN_RX (cujo
teto é a própria prioridade) fica bloqueada até 11,9 ms e **perde seu
deadline de 10 ms 11 vezes**. A análise de tempo de resposta prevê isso
(termo de bloqueio B = 12,2 ms > D = 10 ms), e o monitor de deadline reporta
(DTC 0x31). A tranca está correta e não é de graça. É por isso que as seções
críticas do produto são um único `memcpy`, medido em microssegundos.

O protocolo também garante **ausência de *deadlock***: quem segura o teto não
pode ser preemptado por ninguém que queira o recurso, e nenhuma tarefa segura
dois recursos ao mesmo tempo (o código libera `ICM_Resource` antes de
formatar e só então pega `RES_CONSOLE`). E garante **inversão de prioridade
limitada**: cada job é bloqueado no máximo uma vez, por no máximo uma seção
crítica de menor prioridade, e somente antes de começar.

---

## 8. Testes e evidências

Toda a evidência é saída real de console, gerada por
`tools/collect_evidence.sh` e guardada sem edição em `docs/evidence/`. O
documento `requirements/test_evidence.md` cita cada trecho com o arquivo de
origem. Ambiente: Ubuntu 24.04, AMD EPYC 8 núcleos, GCC 13.3, servidor
compartilhado com **carga média ≈ 12** durante as medições.

### 8.1 Níveis de teste

| Nível | O quê | Quantidade | Resultado |
|---|---|---|---|
| Unitário (host, ASan+UBSan) | CRC, decodificação, faixas, escala, filtro, histerese, debounce, dashboard | 25 testes, 373 verificações | todos passam |
| Estático | porta única de `ICM_Data`; rastreabilidade | 2 verificações | passam |
| Integração (RTOS + barramento) | timing, sinais, falhas, avisos, race, sobrecarga, overrun, DBC | 46 testes | todos passam |
| CI (GitHub Actions) | tudo acima, a cada push e PR, em `vcan0` no runner | — | verde |

### 8.2 Timing nominal

31 s, ciclo de condução completo, `SCHED_FIFO`:

| Tarefa | T | Jobs | Gaps de liberação (ticks) | No tick de liberação | Resposta máx. | Perdas |
|---|---|---|---|---|---|---|
| Task_CAN_RX | D = 10 ms | 420 | — | — | 0,158 ms | 0 |
| Task_Acquisition | 10 ms | 3090 | todos = 1 | 100 % | 0,134 ms | 0 |
| Task_Processing | 20 ms | 1545 | todos = 2 | 100 % | 0,142 ms | 0 |
| Task_Display | 50 ms | 618 | todos = 5 | 100 % | 0,320 ms | 0 |
| Task_Diagnostics | 100 ms | 309 | todos = 10 | 100 % | 0,202 ms | 0 |
| Task_Logging | 1000 ms | 31 | todos = 100 | 100 % | 0,210 ms | 0 |

Utilização U = 2,9 %, contra o limite de Liu & Layland de 73,5 % para 6
tarefas. Pela análise de tempo de resposta com os piores tempos de execução
medidos, R ≤ D para todas as tarefas.

**Dois relógios, mantidos separados de propósito.** No relógio do SO (ticks),
o escalonamento é **exato**: todo intervalo entre liberações é exatamente o
período, e todo job começa no tick em que foi liberado. No relógio de parede
(`CLOCK_MONOTONIC`), todos os períodos aparecem 0,48 % mais longos. Isso não é
escalonamento; é a deriva do tick do ViPER (`usleep(10000)` em laço), igual
para todas as tarefas.

### 8.3 Com e sem escalonamento de tempo real no host

O mesmo teste, sem `SCHED_FIFO`:

| | `SCHED_FIFO` | `SCHED_OTHER` |
|---|---|---|
| tick: desvio-padrão / máximo | 0,043 / 10,9 ms | 0,675 / 23,2 ms |
| latência de IRQ CAN, p99 | 0,139 ms | 1,337 ms |
| jobs no tick de liberação | 100 % | 100 % |
| perdas de deadline | 0 | 0 |

No tempo do SO, o escalonamento é idêntico. O que muda é o host: um tick
chegou 23 ms depois do anterior e a cauda de latência cresceu dez vezes. É o
escalonador do Linux decidindo quando o processo do Trampoline roda, numa
máquina com carga 12. Esse é o enunciado mais claro do que o alvo POSIX pode
e não pode mostrar: **o determinismo do RTOS é mensurável aqui; o do
hardware, não**.

### 8.4 Falhas de CAN: latência de detecção ponta a ponta

| Falha | Esperado | Medido |
|---|---|---|
| M2 ausente (período 1 s) | > 3 s e ≤ 3,3 s | **3,102 s** |
| M2 de volta | cura após 10 ciclos bons (≈ 1 s) | 0,920 s |
| 9000 rpm | 3 ciclos de debounce (200–300 ms) | **0,224 s** |
| 300 km/h | idem | 0,240 s |
| CRC errado na M3 | todo quadro ruim contado | 2 enviados, 2 contados |
| contador congelado na M4 | quadros velhos descartados, depois timeout | 8 enviados, 8 descartados; timeout em 0,258 s |
| M4 ausente (período 100 ms) | > 0,3 s e ≤ 0,55 s | **0,369 s** |
| rajada de 200 quadros | nenhuma perda | 200 recebidos, 0 perdidos |

Em todo o cenário de falhas: **0 perdas de deadline**, 0 erros de API e 0
cópias rasgadas. Tratar falhas não custou tempo real.

### 8.5 Sobrecarga e isolamento

Com a Task_Diagnostics gastando 150 ms por job (período de 100 ms) por 5 s,
as **25 ativações perdidas** aparecem de três formas que concordam: 25
`E_OS_LIMIT` capturados pelo `ErrorHook`, 25 respostas acima de 100 ms
medidas, e 25 lacunas de 20 ticks no trace de liberações. O DTC 0x31 é
confirmado em t = 5,17 s e curado em 15,07 s. **Todas as tarefas de maior
prioridade ficam intactas** (0 perdas, 100 % no tick). A Task_Logging, única
abaixo, começa até 50 ms atrasada, como o escalonamento de prioridade fixa
permite, e termina muito antes do seu deadline de 1 s.

### 8.6 Perda de quadros (overrun)

Uma rajada de quadros sozinha não perde dados: a ISR esvazia a FIFO de 8
posições em dezenas de microssegundos, mais rápido do que qualquer emissor
consegue enchê-la (e num barramento real a 500 kbit/s um quadro leva
≈ 230 µs no fio). O que perde quadros numa ECU real é uma **seção com
interrupções mascaradas longa demais**. Com 200 ms de interrupções
mascaradas e a M1 chegando a cada 1 ms:

```text
SUMMARY can_pushed=3908 can_hw_dropped=196 can_drained=3908 can_sw_dropped=0 can_popped=3908
```

Foram 4104 quadros enviados = 3908 aceitos + 196 descartados e contados:
**nenhuma perda silenciosa**. O DTC 0x22 é confirmado. Dois efeitos colaterais
ficaram registrados como limitações. (a) Com ~192 quadros perdidos, o
contador *alive* de 4 bits **não** acusa salto: 192 mod 16 = 0. É uma
propriedade de qualquer contador E2E curto, e a razão de o flag de overrun do
controlador existir. (b) Com interrupções mascaradas, o tick também é
bloqueado: 20 ticks viram 1, e a Task_Acquisition vê um intervalo de 200 ms.
Num microcontrolador, o bit de pendência do timer se comporta da mesma forma.

---

## 9. Decisões de projeto e justificativas

| Decisão | Alternativa descartada | Por quê |
|---|---|---|
| Alvo POSIX do Trampoline | placa física | não havia hardware disponível no prazo; o POSIX permite CI automatizado e medição repetível (seção 10) |
| Rate-monotonic + CAN_RX no topo | prioridades "por importância" | ótima para D = T, verificável à mão; a carga do CAN_RX é limitada |
| `ACTIVATION = 1` | ativações enfileiradas | o overrun precisa aparecer como `E_OS_LIMIT`, não ser escondido |
| liberação síncrona (instante crítico) | alarmes defasados | testar o pior caso, não evitá-lo |
| `ICM_Data` acessível só por cópia | acesso por ponteiro | seção crítica de duração mensurável (é o termo B da RTA) |
| controle negativo de race | só o teste positivo | um teste que só passa pode estar cego |
| ISR mínima + tarefa estendida | decodificar na ISR | latência de interrupção independente da carga de CAN |
| FIFO de 8 + fila de 32 | fila única ilimitada | modela o hardware real; perda é contada, nunca silenciosa |
| timeout = 3 períodos, *first timeout* 2 s | 1 ou 5 períodos; sem *first timeout* | compromisso do AUTOSAR COM; o primeiro teste de falhas acusou falso timeout no boot, e a correção foi o *first timeout* |
| janela de 1 s para erros E2E | "erro novo neste ciclo" | uma mensagem de 1 Hz com CRC ruim gera um erro por segundo; o debounce de 3 ciclos nunca confirmaria (achado durante o projeto) |
| CRC inclui o ID | CRC só do payload | quadro sob ID errado é rejeitado (*Data ID* do E2E) |
| dashboard textual | gráfico | ver seção 6 |
| `STATUS = EXTENDED` | `STANDARD` | todo uso indevido de serviço do SO vira erro contado; os testes exigem zero |
| Trampoline fixado num commit | `master` | build reprodutível; toda a evidência é desse commit |
| `SCHED_FIFO` nos testes | escalonamento padrão | separa o jitter do host do comportamento do RTOS; os dois modos estão medidos |

---

## 10. Limitações: o que não pôde ser simulado sem hardware

Isto é dito explicitamente para que nenhum resultado seja lido além do que
ele mostra:

1. **Tempos de execução** são de um núcleo x86-64 de servidor, duas a três
   ordens de grandeza mais rápido que um MCU de painel. A margem de
   escalonabilidade (U ≈ 3 %) **encolheria** no alvo real; o método de
   cálculo (`tools/analyze_timing.py`) continuaria o mesmo.
2. **O tick** vem do ViPER via `usleep` e sinal Unix, com deriva de +0,5 %
   (FIFO) a +1,8 % (sem FIFO). Um SysTick de hardware não deriva assim.
3. **Interrupções são sinais**. A latência de interrupção medida (p99
   ≈ 0,14 ms) é do Linux entregando um sinal, não de um NVIC; num Cortex-M
   seria da ordem de microssegundos.
4. **O barramento CAN é virtual**: sem *bit timing*, arbitragem, *error
   frames*, *bus-off* ou ruído elétrico. Esses fenômenos não foram testados.
   O controlador (filtro, FIFO, overrun, interrupção) é emulado em software.
5. **Não houve medição com transceptor real.** A interface `can0` de um
   adaptador USB funciona sem mudar código, mas isso não foi executado.
6. **O painel é textual**; não há ponteiros físicos nem display gráfico.
7. **Execução monoprocessada do SO**, mas o host é multinúcleo: a thread do
   controlador CAN roda de fato em paralelo com o SO, o que é *mais* exigente
   que um periférico real para a sincronização FIFO/ISR, e não menos.

---

## 11. Fluxo de trabalho no GitHub

O repositório segue o fluxo do diagrama do professor: **Issues → Branches →
Commits → Pull Requests → Code Review**.

- **Issues**: #1 (CI), #2 (evidências e relatório), #3 (port para a Nucleo
  L476RG, trabalho futuro);
- **Branches**: uma por issue (`ci/github-actions`, `docs/evidence-report`);
- **Commits** incrementais, cada um com mensagem explicando o porquê;
- **Pull Requests**: #4 (CI) e o PR de documentação, ambos com descrição,
  vínculo à issue e CI verde antes do merge;
- **Code Review**: no PR #4, a revisão identificou um defeito real. O
  cluster roda como root sob `sudo chrt`, e um teste travado não poderia ser
  morto pelo runner, segurando o job pelo limite padrão de 6 horas. A correção
  (timeouts explícitos) foi feita em commit separado, referenciando a revisão.
- **CI** (`.github/workflows/ci.yml`): testes unitários com sanitizers,
  verificações estáticas e a suíte de integração completa numa `vcan0` dentro
  do runner do GitHub, com o cluster em `SCHED_FIFO`. As tabelas de timing de
  cada execução vão para o resumo do job, e os traces, para um artefato.

---

## 12. Trabalhos futuros

1. **Port para a STM32 Nucleo L476RG** (issue #3). O Trampoline já suporta o
   STM32L432 (Nucleo-32), da mesma família L4; o port exige *startup*, *linker
   script*, relógio e SysTick como `SystemCounter` (1 ms). Para CAN: o bxCAN
   interno com transceptor, ou os módulos MCP2515 + TJA1050 via SPI.
2. **Barramento físico**: um adaptador CANable 2.0 aparece no Linux como
   interface SocketCAN, então `sim/can_sim.py --iface can0` alimentaria o
   barramento real sem mudar código.
3. **Refazer a análise de tempo** com os tempos de execução do MCU, e medir a
   latência de interrupção com analisador lógico.

---

## 13. Como reproduzir

```bash
tools/setup_trampoline.sh                       # Trampoline fixado, GOIL, ViPER
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
for v in nominal race_locked race_unlocked overload irq_lock; do tools/build.sh $v; done
sudo tools/collect_evidence.sh                  # tudo, com a saída em docs/evidence/
```

Demonstração ao vivo: em um terminal,
`VIPER_PATH=third_party/trampoline/viper build/nominal/icm_exe`; em outro,
`python3 sim/can_sim.py --scenario faults`.

---

## Referências

1. OSEK/VDX. *Operating System Specification*, versão 2.2.3, 2005.
2. OSEK/VDX. *OIL: OSEK Implementation Language*, versão 2.5, 2004.
3. AUTOSAR. *Specification of Operating System*, R4.2.
4. AUTOSAR. *Specification of SW-C End-to-End Communication Protection Library* (perfis E2E 1 e 2).
5. Trampoline RTOS. <https://trampoline.rts-software.org/> e <https://github.com/TrampolineRTOS/trampoline> (commit `09e2dcb`).
6. Béchennec, J.-L. et al. *Trampoline: an open source implementation of the OSEK/VDX RTOS specification*. ETFA 2006.
7. Liu, C. L.; Layland, J. W. *Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment*. JACM 20(1), 1973.
8. Audsley, N. et al. *Applying new scheduling theory to static priority pre-emptive scheduling*. Software Engineering Journal, 1993 (análise de tempo de resposta).
9. Sha, L.; Rajkumar, R.; Lehoczky, J. *Priority Inheritance Protocols: An Approach to Real-Time Synchronization*. IEEE Trans. Computers 39(9), 1990.
10. UNECE Regulation No. 48 (frequência das setas: 90 ± 30 piscadas/min).
11. SAE J1850 (CRC-8); SAE J1939 (convenção de *signal not available*).
