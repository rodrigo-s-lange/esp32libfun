# Firmware de pixels — recursos entregues e alinhamento com a EasyLED

**Data:** 22/09/2026

**Destinatário:** equipe EasyLED — backend, contratos e integração de dispositivos

**Origem:** frente de firmware esp32libfun / esp_pixels / esp_pixels_fx / esp_pixels_composer

**Objetivo de entrega do produto:** 15/10/2026

Equipe EasyLED,

Já temos uma base de controle de pixels funcionando em hardware, com presets,
composição 1D/2D, duas saídas, Mirror e parâmetros customizados. Iniciamos também
uma sessão de conectividade Wi-Fi + MQTT/WSS, ainda separada da aplicação de pixels.

Precisamos alinhar essa implementação ao contrato e à API que vocês já possuem.
Encontramos o contrato de controle v1.0.0 marcado como canônico e uma OpenAPI
com operações implementadas. **Podem confirmar quais revisões são a referência
para o firmware, quais fluxos já estão disponíveis em desenvolvimento e fornecer
exemplos completos para a primeira integração?**

Este documento descreve o estado entregue e as diferenças que precisamos resolver
em conjunto. Não propõe substituir os contratos da plataforma nem alterar seu
código. O documento será encaminhado por Rodrigo; não foi publicado como issue.

## 1. Base de implementação e evidências

| Parte | Estado de referência |
|---|---|
| Aplicação de pixels | Checkpoint local `4d8c1ac`; aplicação e controles validados na WROOM |
| Composer | `d575dbe`, versão local v0.2.3 |
| Efeitos | `6a93967`, versão local v0.2.2 |
| Transporte de pixels | `5d7db6b`, biblioteca esp_pixels |
| Sessão de rede independente | Checkpoint local `500209a`; build e validação pontual no simulador |
| SDK | ESP-IDF 6.0; builds desta entrega com 6.0.2 |
| Bancada | ESP32-WROOM-32; WS2812 RGB/ordem GRB; GPIO16 com 50 pixels e GPIO17 com 12 |

São checkpoints locais; não presumir que tags ou branches remotos já contenham
essas entregas. A aplicação de pixels passou pela execução das suítes em hardware,
com zero falhas no último ciclo, e pelas confirmações visuais de Rodrigo. Os 100
presets originais foram restaurados e verificados após a bancada.

O 2D foi observado como canvas virtual 5×10 em uma fita reta. Ainda não foi
validado em um painel físico montado em matriz. A nova conectividade não foi
validada contra o broker EasyLED nem junto ao renderizador em hardware.

## 2. Recursos de pixels implementados

| Recurso | Comportamento disponível | Limite ou detalhe relevante para integração |
|---|---|---|
| Renderização local | Efeitos executados pelo dispositivo; Composer controla a tarefa de renderização | A aplicação atual não depende de nuvem; perda de rede com firmware integrado ainda precisa de bancada |
| Segmentos | Até 32 segmentos; regiões 1D e 2D; efeitos e parâmetros por segmento | Em 1D, intervalo `[start, stop)`; em 2D, retângulo `(x,y,w,h)` no canvas |
| Composição | Sobreposição ordenada; Replace, Add e Alpha; brilho global e por segmento; opacidade | Brilho, opacidade, velocidade e intensidade usam valores 0–255; o significado dos controles depende do efeito |
| Estado e edição | Ativar, pausar, desabilitar e reiniciar segmentos; pausa global; edição preparada antes da publicação | O controle AT distingue configuração em edição da configuração confirmada |
| Cores e paletas | Até três cores RGB nos parâmetros; paletas e seleção da fonte de cor quando suportadas | A plataforma deve consultar metadados do efeito, não apresentar todos os controles indiscriminadamente |
| Mirror e reverse | Espelhamento simétrico 1D, independente da inversão de direção | Mirror não é suportado em segmento 2D; funciona em comprimentos pares e ímpares |
| Parâmetros customizados | Três valores por segmento, cada um de 0–255, com labels e flags de suporte | Já possuem controle AT, persistência e efeito consumidor real: Stripes |
| Saídas físicas | Até duas saídas no adaptador/aplicação atual; GPIO, quantidade e ordem RGB configuráveis | Os segmentos usam um espaço lógico global e podem atravessar a divisão entre saídas |
| Topologia persistente | Configuração de saídas preparada, validada e salva | Entra em vigor no próximo reboot; não há troca dinâmica de topologia nesta aplicação |
| Canvas / ledmap | Motor com mapeamento real e segmentos 2D; suporta células sem pixel no ledmap | A interface AT/persistência da aplicação atual gera um único retângulo progressivo ou serpentino; não oferece editor de mapa arbitrário |
| Presets | 100 posições nomeadas; salvar, listar, carregar, substituir e excluir | Nomes de 1–31 bytes ASCII imprimíveis; comparação sensível a maiúsculas; nomes não são UUIDs da plataforma |
| Recuperação | Cena confirmada e presets persistidos em NVS; recuperação após reboot | Restaura configuração; não persiste fase de animação nem framebuffer |

O limite de validação atual é de 2048 pixels por saída. **Esse valor não é uma
capacidade de desempenho comprovada com Wi-Fi/TLS:** a bancada possui 62 pixels,
e o orçamento depende de segmentos, buffers, efeitos e memória disponível.
O manifesto do produto deverá informar capacidades reais da configuração final.

O controle AT já permite operar esses recursos localmente, incluindo comandos
de segmentos/publicação, presets, saídas e mapa. Ele serve à bancada e ao
diagnóstico; não estamos propondo transportar strings AT como protocolo EasyLED.
A integração deve consumir as APIs C++ e preservar a semântica do contrato remoto.

### Catálogo de efeitos entregue

| ID estável | Efeito | Dimensão | Controles declarados pelo efeito |
|---|---|---|---|
| 0 | Solid | 1D e 2D | Cor principal |
| 1 | Sinelon | 1D | Velocidade, intensidade/rastro, paleta e três cores |
| 2 | Rainbow | 1D | Velocidade, intensidade/densidade e paleta |
| 3 | WipeOnce | 1D | Velocidade e três cores |
| 4 | XYGradient | 2D | Velocidade, intensidade/inclinação X/Y e paleta |
| 5 | Stripes | 1D | Duas cores; custom1 = largura da faixa, custom2 = largura do intervalo, custom3 = fase |

No Stripes, largura e intervalo são `valor + 1`; fase usa o valor bruto.
Brilho, blend e opacidade pertencem ao compositor e não aos controles do efeito.
A referência dos controles é `FxInfo` da biblioteca. IDs existentes não devem ser
renumerados. Uma superfície com altura 1 exige efeito compatível com 1D, inclusive
quando o segmento foi declarado pela API de geometria 2D.

**Próxima entrega solicitada à Claude:** Blink=6, Breathe=7 e Chase=8. Não fazem
parte do catálogo validado acima; aguardamos implementação, revisão e bancada.

### Persistência: o que existe e o que não deve ser confundido

A cena usa registros versionados, CRC32 e cópias A/B; o formato atual escreve
schema 4 e continua lendo schemas 1, 2 e 3. Presets passam por validação de
efeitos, geometria, topologia e mapa antes de aplicação. Um preset incompatível
é rejeitado sem substituir a cena ativa.

Cada registro de cena ocupa no máximo 1295 B, e cada registro de preset, 1348 B,
com os limites atuais. A partição de presets tem 320 KiB. Esses tamanhos são do
formato binário local, **não limites de tamanho de JSON da plataforma**.

A aplicação diferencia configuração aplicada de configuração salva: uma falha
na persistência posterior à aplicação é reportada como aplicada e não salva.
Esse comportamento local não equivale, por si só, à transação durável exigida
para `apply_config`. O firmware de produto precisa definir corretamente quando
emite `command_applied`, como trata falha de gravação e como recupera staging
interrompido. A geração interna do journal também não é o `config_version` remoto.

## 3. Conectividade preparada

Implementamos um componente pertencente à aplicação, em exemplo independente:

- Reutilização do Wi-Fi STA existente, com política de reconexão progressiva.
- MQTT 3.1.1 sobre WSS, com verificação do certificado e hostname; espera por
  relógio plausível antes de iniciar TLS, com SNTP no exemplo.
- Nova inscrição no tópico de comandos após reconexão; prontidão condicionada
  à confirmação de inscrição QoS1 pelo broker.
- Montagem de mensagens fragmentadas antes de entregá-las à aplicação.
- Limite atual de 1024 bytes por mensagem e fila de duas mensagens completas.
  Fila cheia descarta a nova mensagem e incrementa diagnóstico; não é fila durável.
- Publicação por fila QoS1, com outbox limitado; enqueue não significa aplicação
  do comando nem confirmação pelo backend.
- Rejeição de entregas retained no caminho de comandos. Deduplicação por
  `command_id`, validade temporal e recuperação de resultados ainda serão integradas.

O build independente passou em ESP-IDF 6.0.2, e a montagem de mensagens foi
executada no simulador. **Ainda não estão entregues:** bootstrap/claim do produto,
parser e execução dos comandos EasyLED, download HTTPS de configuração, staging
durável remoto, acknowledgements de negócio, publicação do estado no formato
EasyLED, OTA/rollback e integração de rede com renderização em um único firmware.

A aplicação de bancada usa partição de aplicação única, sem slots OTA. O exemplo
de rede possui outra tabela de partições. A integração deverá decidir o layout
de produto e a preservação/migração dos presets; não se deve sobrepor essas
tabelas como se fossem intercambiáveis.

## 4. O que encontramos na plataforma

Consulta somente leitura em 22/09/2026, `easyled-platform/main` no commit
`306fa44a4f4112c3518f5fa7dfa3ecb105c19c85`:

- O [contrato de controle v1.0.0](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/docs/contracts/easyled-control-contract.md)
  está marcado como canônico. Define WSS, identidade MQTT pelo UUID do dispositivo,
  tópicos `state/cmd/event/telemetry`, envelope com `command_id`, classes
  `apply_config`, `activate_preset`, `reboot` e `ota`, eventos e idempotência.
- A [OpenAPI](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/docs/openapi.yaml)
  declara documentar operações implementadas, incluindo autoria de ledmaps,
  segmentos, presets, versões de configuração e slots. O
  [router](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/api/go-api/router.go)
  registra essas rotas. O README contém uma descrição mais antiga; não usamos
  essa descrição para concluir que essas operações ainda não existem.
- A [issue #48](https://github.com/rodrigo-s-lange/easyled-platform/issues/48)
  acompanha firmware de produto, bootstrap, MQTT, staging durável e autonomia.
  Ela referencia credenciais em #47 e entrega HTTPS em #60. Estar descrito na
  issue não comprova disponibilidade em um ambiente implantado.
- O [SegmentConfig consultado](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/api/go-api/models/segment.go)
  contém paleta, cores, velocidade, intensidade, brilho, blend e opacidade.
  Ainda não declara mirror, reversed, custom[3] e color_source. Precisamos
  confirmar se há trabalho mais recente em outra branch/checkout para esses campos.

Essas referências orientam a integração, mas não demonstram que todos os endpoints
necessários ao dispositivo já estejam consolidados ou implantados. Por favor,
indiquem revisões posteriores e correções onde houver trabalho ainda não publicado.

## 5. Perguntas para fechar a primeira integração

| Tema | Resposta ou material solicitado |
|---|---|
| Referência oficial | Qual commit/tag do contrato e da OpenAPI devemos fixar? Quais operações estão implementadas, implantadas e ainda pendentes? Há JSON Schema/AsyncAPI ou fixtures MQTT canônicas? |
| Bootstrap e credenciais | Qual é o fluxo do dispositivo até obter tenant_id/device_id, credencial HTTPS e credencial MQTT? Endpoints, autenticação, expiração/rotação e exemplo de resposta. Não enviar segredos neste documento. |
| Ambiente de desenvolvimento | Como obter um dispositivo de teste e acesso autorizado? Qual WSS URI/path, CA e versão MQTT? Fornecer dados de acesso pelo canal apropriado. |
| Entrega de configuração | Endpoint HTTPS real, autenticação, formato completo do payload, limite de bytes, algoritmo/checksum e definição dos bytes exatos verificados. Quais são as regras de timeout, retry e URL expirada? |
| Comandos e resultados | Exemplos completos de `apply_config` e `activate_preset`, com received/accepted/applied/rejected/failed. Quais campos/códigos de erro são obrigatórios? Quando applied implica persistência concluída? |
| Duplicatas e reconexão | Política por command_id, duração/persistência da deduplicação, reenvio do resultado após reboot, comandos obsoletos e sequência de reconciliação após reconectar. requested_at sozinho não define expiração. |
| Presets | Como mapear UUID e revisão remotos para 100 posições locais? Quando baixar, atualizar, remover ou rejeitar por lotação? Qual comportamento para preset desconhecido e transição fade solicitada? A ativação local atual não implementa crossfade entre cenas. |
| Campos e catálogo | Como representar mirror/reversed/custom/color_source, estado dos segmentos e ordem de composição? Como alinhar IDs 0–5 e depois 6–8 sem renumeração? Precisamos de defaults explícitos e metadados de controles. |
| Topologia e 2D | Qual schema definitivo de outputs/ledmap, incluindo buracos? Como coordenar alterações que exigem reboot? Nosso descritor AT retangular não é equivalente ao ledmap completo da plataforma. |
| Estado e capacidades | Payloads exatos de state/event/telemetry, cadência, QoS/retain, boot_id/state_seq, versão desejada/reportada e canal do manifesto de capacidades. Como diferenciar aceito, ativo e persistido? |
| Escopo e ownership | Onde ficará o repositório de firmware de produto e quem mantém essa integração? Como tratar recursos de dimmer/relé mencionados no preset EasyLED, ainda não integrados à aplicação de pixels aqui descrita? |
| OTA e armazenamento | Qual contrato vigente de artefatos, assinatura, rollback e staging? O contrato de controle e a issue #48 descrevem aspectos distintos; precisamos fixar a exigência de MVP antes de fechar partições e boot. |

## 6. Material mínimo para destravar trabalho agora

Não precisamos esperar que toda a plataforma esteja concluída. Para começar,
pedimos este pacote pequeno:

1. Links e revisões oficiais, com uma lista curta do que já pode ser consumido.
2. Um `apply_config` completo, os bytes do payload HTTPS correspondente e seu
   checksum, mais os eventos e o estado esperados após aplicação.
3. Um `activate_preset` completo, incluindo como resolver o preset remoto.
4. Um exemplo de rejeição e a regra para repetição do mesmo command_id.
5. Instruções para obter credenciais de desenvolvimento e o responsável técnico
   para alinhar os campos que faltam.

Fixtures sem segredos já permitem implementar a adaptação no firmware enquanto
o ambiente real é preparado. Por favor, identifiquem expressamente qualquer
fixture provisória para não tratarmos um exemplo como contrato consolidado.

Propomos como primeiro fluxo conjunto: dispositivo recupera sua cena local,
conecta com identidade própria, recebe uma configuração válida, confirma o
resultado real e continua renderizando após perder a rede ou reiniciar. Essa é
uma proposta de sequência para alinhamento, não uma afirmação de integração pronta.

Enquanto vocês retornam, a frente de firmware pode avançar no build conjunto de
pixels + rede e no orçamento de memória; a Claude segue com Blink/Breathe/Chase.
Nenhuma alteração no repositório EasyLED é necessária para preparar essas frentes.

## Referências locais da entrega

- [Aplicação de pixels e evidências de bancada](../main/README.md)
- [Sessão independente de conectividade](../examples/basic/pixels_network_session/README.md)
- [Roadmap acordado](pixels-roadmap.md)
- [Briefing da próxima entrega de efeitos](pixels-effects-batch-task.md)

**Retorno solicitado:** confirmar a referência canônica, responder ao pacote
mínimo acima e apontar o responsável pela integração. Com isso conseguimos
implementar contra a plataforma existente e evitar divergências de protocolo.
